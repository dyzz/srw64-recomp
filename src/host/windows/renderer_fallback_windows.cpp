// Marchwind64.exe draws with its GL renderer over ANGLE on a PC RT64 cannot use
// (renderer_fallback.hpp). The DLLs are loaded here rather than linked, as in gpu_report.
#include "../renderer_fallback.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>   // _wputenv_s: the C runtime's copy too, which getenv reads (not SetEnvironmentVariable's alone)
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

namespace srw64::renderer_fallback {
namespace {
std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

// An adapter plume's D3D12 device takes (plume_d3d12.cpp, gpu_report_windows.cpp): feature
// level 11_0, shader model 6.0, ID3D12Device8. The Basic Render Driver is passed over there too.
bool d3d12_usable() {
    const HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    const HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
    using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
    const auto create_factory = dxgi ? reinterpret_cast<CreateFactory>(GetProcAddress(dxgi, "CreateDXGIFactory1")) : nullptr;
    const auto create_device = d3d12 ? reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
    IDXGIFactory1* factory = nullptr;
    if (!create_factory || !create_device || FAILED(create_factory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
        return false;
    bool usable = false;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT index = 0; !usable && factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        ID3D12Device* device = nullptr;
        // Microsoft's own adapters are WARP, the CPU renderer: on a PC without a GPU driver
        // (a cloud machine, a fresh Windows) the Basic Render Driver comes first, not marked
        // software, with D3D12 12_1 and shader model 6.0, and RT64 crashed on it in
        // D3D12Core.dll (2026-11-20, an EC2 instance with no GPU).
        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && desc.VendorId != 0x1414 &&
            SUCCEEDED(create_device(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&device)))) {
            D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_0};
            const bool sm6 = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof model)) &&
                             model.HighestShaderModel >= D3D_SHADER_MODEL_6_0;
            ID3D12Device8* device8 = nullptr;
            const bool has8 = SUCCEEDED(device->QueryInterface(__uuidof(ID3D12Device8), reinterpret_cast<void**>(&device8)));
            if (device8) device8->Release();
            device->Release();
            usable = sm6 && has8;
        }
        adapter->Release();
    }
    factory->Release();
    return usable;
}

// A GPU (not a CPU implementation) with Vulkan 1.2, behind a loader that offers the window
// surface: RT64's other API on Windows. The few Vulkan structures needed, as gpu_report does.
bool vulkan_usable() {
    const HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
    if (!loader) return false;
    struct Extension { char name[256]; uint32_t spec; };
    struct AppInfo { int32_t type; const void* next; const char* name; uint32_t version; const char* engine; uint32_t engine_version; uint32_t api; };
    struct CreateInfo { int32_t type; const void* next; uint32_t flags; const AppInfo* app; uint32_t layers; const char* const* layer_names;
                        uint32_t extensions; const char* const* extension_names; };
    using Enumerate = int32_t(__stdcall*)(const char*, uint32_t*, Extension*);
    using Create = int32_t(__stdcall*)(const CreateInfo*, const void*, void**);
    using Destroy = void(__stdcall*)(void*, const void*);
    using Devices = int32_t(__stdcall*)(void*, uint32_t*, void**);
    using Properties = void(__stdcall*)(void*, void*);
    const auto enumerate = reinterpret_cast<Enumerate>(GetProcAddress(loader, "vkEnumerateInstanceExtensionProperties"));
    const auto create = reinterpret_cast<Create>(GetProcAddress(loader, "vkCreateInstance"));
    const auto destroy = reinterpret_cast<Destroy>(GetProcAddress(loader, "vkDestroyInstance"));
    const auto devices = reinterpret_cast<Devices>(GetProcAddress(loader, "vkEnumeratePhysicalDevices"));
    const auto properties = reinterpret_cast<Properties>(GetProcAddress(loader, "vkGetPhysicalDeviceProperties"));
    bool usable = false;
    uint32_t count = 0;
    if (enumerate && create && destroy && devices && properties && enumerate(nullptr, &count, nullptr) == 0) {
        std::vector<Extension> extensions(count);
        if (enumerate(nullptr, &count, extensions.data()) < 0) count = 0;
        bool surface = false, win32 = false;
        for (uint32_t i = 0; i < count; ++i) {
            surface = surface || !std::strcmp(extensions[i].name, "VK_KHR_surface");
            win32 = win32 || !std::strcmp(extensions[i].name, "VK_KHR_win32_surface");
        }
        constexpr uint32_t api12 = (1u << 22) | (2u << 12);
        const AppInfo app{0, nullptr, "Marchwind64", 1, "Marchwind64", 1, api12};
        const CreateInfo info{1, nullptr, 0, &app, 0, nullptr, 0, nullptr};
        void* instance = nullptr;
        if (surface && win32 && create(&info, nullptr, &instance) == 0 && instance) {
            uint32_t found = 0;
            if (devices(instance, &found, nullptr) == 0 && found) {
                std::vector<void*> list(found);
                if (devices(instance, &found, list.data()) >= 0)
                    for (uint32_t i = 0; i < found && !usable; ++i) {
                        // VkPhysicalDeviceProperties: apiVersion, driverVersion, vendorID, deviceID,
                        // deviceType (4 is a CPU), ... 824 bytes in all.
                        alignas(8) unsigned char bytes[1024] = {};
                        properties(list[i], bytes);
                        uint32_t api = 0;
                        int32_t type = 0;
                        std::memcpy(&api, bytes, 4);
                        std::memcpy(&type, bytes + 16, 4);
                        usable = api >= api12 && type != 4;
                    }
            }
            destroy(instance, nullptr);
        }
    }
    FreeLibrary(loader);
    return usable;
}

std::wstring wide(const std::string& text) {
    std::wstring out(size_t(MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), int(out.size()));
    if (!out.empty()) out.pop_back();
    return out;
}

// This program's own path.
std::wstring program_path() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        if (length == 0) return {};
        if (length < path.size()) { path.resize(length); break; }
        path.resize(path.size() * 2);
    }
    return path;
}

// ANGLE's two DLLs beside the program, which the GL renderer needs (libGLESv2.dll is
// delay-loaded, libEGL.dll loaded by SDL): without them (a development build) RT64 stays.
bool angle_beside() {
    const std::wstring path = program_path();
    const auto slash = path.find_last_of(L"\\/");
    const std::wstring folder = path.substr(0, slash == std::wstring::npos ? 0 : slash + 1);
    for (const wchar_t* dll : {L"libEGL.dll", L"libGLESv2.dll"}) {
        const DWORD attributes = GetFileAttributesW((folder + dll).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    }
    return true;
}

bool gl = false;

// This process's command line with the GL host in place of the program's own name.
std::wstring command_line(const std::wstring& program) {
    std::wstring_view line = GetCommandLineW();
    size_t end = 0;
    if (!line.empty() && line[0] == L'"') {
        end = line.find(L'"', 1);
        end = end == std::wstring_view::npos ? line.size() : end + 1;
    } else {
        end = line.find_first_of(L" \t");
        if (end == std::wstring_view::npos) end = line.size();
    }
    return L"\"" + program + L"\"" + std::wstring(line.substr(end));
}

struct Streams { HANDLE in = nullptr, out = nullptr, err = nullptr; };
Streams streams() { return {GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE)}; }

// Starts the GL host with `given` as its standard handles when there are any (a --play run's
// redirected console.log); otherwise it joins this process's console as it would its own.
HANDLE start(const std::wstring& program, const std::string& reason, const Streams& given) {
    _wputenv_s(L"SRW64_RENDERER_FALLBACK", wide(reason).c_str());
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    const auto [in, out, err] = given;
    const auto valid = [](HANDLE h) { return h && h != INVALID_HANDLE_VALUE; };
    if (valid(out) || valid(err)) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = in; startup.hStdOutput = out; startup.hStdError = err;
    }
    PROCESS_INFORMATION process{};
    std::wstring line = command_line(program);
    if (!CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, startup.dwFlags ? TRUE : FALSE, 0, nullptr, nullptr, &startup, &process)) {
        std::fprintf(stderr, "SRW64_RENDERER_FALLBACK_FAILED CreateProcess error %lu\n", GetLastError());
        return nullptr;
    }
    CloseHandle(process.hThread);
    return process.hProcess;
}
}  // namespace

void decide(int argc, char** argv) {
    const std::string_view first = argc > 1 ? std::string_view(argv[1]) : std::string_view();
    const bool game = argc == 1 || (argc == 2 && first == "--choose-rom") ||
        (first == "--play" && !(argc >= 3 && (std::string_view(argv[2]) == "--help" || std::string_view(argv[2]) == "--prepare")));
    if (!game) return;
    const std::string api = env("SRW64_GRAPHICS_API");
    if (api == "d3d12" || api == "vulkan") return;   // the player's choice: RT64
    std::string reason;
    if (api == "gl") reason = "chosen (SRW64_GRAPHICS_API=gl)";
    else {
        // SRW64_RENDERER_PROBE=no-d3d12 answers the D3D12 question no, to try the Vulkan and
        // GL branches on a PC that has D3D12 (with a fake vulkan-1.dll for the last).
        const bool skip_d3d12 = env("SRW64_RENDERER_PROBE") == "no-d3d12";
        if ((!skip_d3d12 && d3d12_usable()) || vulkan_usable()) return;
        reason = "no GPU for RT64 (D3D12 shader model 6.0 or Vulkan 1.2)";
    }
    if (!angle_beside()) return;   // a development build without ANGLE: RT64 says what it can
    gl = true;
    // For console.log once it starts (host.cpp prints it after the release line); a restart
    // after RT64 failed (hand_over) keeps the reason it was given.
    if (env("SRW64_RENDERER_FALLBACK").empty()) _wputenv_s(L"SRW64_RENDERER_FALLBACK", wide(reason).c_str());
}

bool gl_chosen() { return gl; }

bool hand_over(const std::string& reason) {
    if (gl || !angle_beside()) return false;
    // This program again with the GL renderer chosen; launch.cpp keeps SRW64_GRAPHICS_API.
    _wputenv_s(L"SRW64_GRAPHICS_API", L"gl");
    const HANDLE child = start(program_path(), "RT64 failed: " + reason, streams());
    if (!child) return false;
    CloseHandle(child);
    std::fprintf(stderr, "SRW64_RENDERER_FALLBACK restarting with the GL renderer\n");
    return true;
}
}  // namespace srw64::renderer_fallback
