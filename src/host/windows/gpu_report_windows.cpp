// What each display adapter offers D3D12, and what the Vulkan loader offers (gpu_report.hpp).
// The DLLs are loaded here rather than linked, so a PC without them still gets its lines.
#include "../gpu_report.hpp"
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

namespace srw64::gpu_report {
namespace {
std::string utf8(const wchar_t* text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(size_t(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}

const char* level_name(D3D_FEATURE_LEVEL level) {
    switch (level) {
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    default: return "?";
    }
}

// One line per adapter, with what plume's D3D12Device asks of it: a device at feature
// level 11_0, shader model 6.0 and ID3D12Device8 (plume_d3d12.cpp). rt64=no on every
// adapter means RT64 falls back to Vulkan.
void log_d3d12(std::vector<std::string>& names) {
    const HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
    const auto create_factory = dxgi ? reinterpret_cast<CreateFactory>(GetProcAddress(dxgi, "CreateDXGIFactory1")) : nullptr;
    IDXGIFactory1* factory = nullptr;
    if (!create_factory || FAILED(create_factory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        std::fprintf(stderr, "SRW64_GPU dxgi=unavailable\n");
        return;
    }
    const HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
    const auto create_device = d3d12 ? reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
    if (!create_device) std::fprintf(stderr, "SRW64_GPU d3d12=unavailable\n");
    IDXGIAdapter1* adapter = nullptr;
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        const std::string name = utf8(desc.Description);
        // The Microsoft Basic Render Driver, which plume passes over too.
        const bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        if (!software) names.push_back(name);
        char driver[48] = "?";
        LARGE_INTEGER version{};
        if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version)))
            std::snprintf(driver, sizeof driver, "%u.%u.%u.%u", unsigned(HIWORD(version.HighPart)), unsigned(LOWORD(version.HighPart)),
                          unsigned(HIWORD(version.LowPart)), unsigned(LOWORD(version.LowPart)));
        char d3d[160] = "unavailable";
        if (create_device && !software) {
            ID3D12Device* device = nullptr;
            const HRESULT created = create_device(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
            if (FAILED(created)) {
                std::snprintf(d3d, sizeof d3d, "failed(0x%08lX) rt64=no", static_cast<unsigned long>(created));
            } else {
                const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
                D3D12_FEATURE_DATA_FEATURE_LEVELS level{UINT(std::size(levels)), levels, D3D_FEATURE_LEVEL_11_0};
                device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &level, sizeof level);
                // A runtime older than shader model 6.0 refuses the question (E_INVALIDARG).
                D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_0};
                const bool sm6 = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof model)) &&
                                 model.HighestShaderModel >= D3D_SHADER_MODEL_6_0;
                char sm[16] = "<6.0";
                if (sm6) std::snprintf(sm, sizeof sm, "%d.%d", int(model.HighestShaderModel) >> 4, int(model.HighestShaderModel) & 0xF);
                // Windows 10 2004 and later.
                ID3D12Device8* device8 = nullptr;
                const bool has8 = SUCCEEDED(device->QueryInterface(__uuidof(ID3D12Device8), reinterpret_cast<void**>(&device8)));
                if (device8) device8->Release();
                device->Release();
                std::snprintf(d3d, sizeof d3d, "fl=%s sm=%s device8=%s rt64=%s", level_name(level.MaxSupportedFeatureLevel), sm,
                              has8 ? "yes" : "no", sm6 && has8 ? "yes" : "no");
            }
        }
        std::fprintf(stderr, "SRW64_GPU adapter=%u name=\"%s\" vendor=%04X device=%04X driver=%s vram_mb=%llu%s d3d12 %s\n",
                     index, name.c_str(), unsigned(desc.VendorId), unsigned(desc.DeviceId), driver,
                     static_cast<unsigned long long>(desc.DedicatedVideoMemory >> 20), software ? " software" : "", d3d);
        adapter->Release();
    }
    factory->Release();
}

// The loader and the instance extensions its drivers give. A loader without
// VK_KHR_surface has no working driver behind it: 0.4.7's window failed on that.
void log_vulkan() {
    const HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
    if (!loader) {
        std::fprintf(stderr, "SRW64_GPU vulkan=no-loader\n");
        return;
    }
    struct Extension { char name[256]; uint32_t spec; };  // VkExtensionProperties
    using Enumerate = int32_t(__stdcall*)(const char*, uint32_t*, Extension*);
    using InstanceVersion = int32_t(__stdcall*)(uint32_t*);
    const auto enumerate = reinterpret_cast<Enumerate>(GetProcAddress(loader, "vkEnumerateInstanceExtensionProperties"));
    const auto instance_version = reinterpret_cast<InstanceVersion>(GetProcAddress(loader, "vkEnumerateInstanceVersion"));
    uint32_t version = 0;
    if (!instance_version || instance_version(&version) != 0) version = 0;
    uint32_t count = 0;
    std::vector<Extension> extensions;
    if (enumerate && enumerate(nullptr, &count, nullptr) == 0) {
        extensions.resize(count);
        if (enumerate(nullptr, &count, extensions.data()) < 0) count = 0;
        extensions.resize(count);
    }
    bool surface = false, win32 = false;
    for (const auto& extension : extensions) {
        surface = surface || std::string(extension.name) == "VK_KHR_surface";
        win32 = win32 || std::string(extension.name) == "VK_KHR_win32_surface";
    }
    std::fprintf(stderr, "SRW64_GPU vulkan=loader version=%u.%u.%u extensions=%u surface=%s win32_surface=%s\n",
                 (version >> 22) & 0x7F, (version >> 12) & 0x3FF, version & 0xFFF, unsigned(extensions.size()),
                 surface ? "yes" : "no", win32 ? "yes" : "no");
    FreeLibrary(loader);
}
}

std::vector<std::string> log() {
    std::vector<std::string> names;
    log_d3d12(names);
    log_vulkan();
    return names;
}
}
