// http_get for the update check (update_check.hpp) through the system's own HTTP, so the
// program carries no TLS library or root certificates: WinHTTP on Windows, libcurl on
// Linux and the Steam Deck (opened at run time, so a system without it only fails the
// check). macOS has its own in macos/update_http_macos.mm.
#include "update_check.hpp"
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#include <iterator>
#include <vector>

namespace srw64::update {
namespace {
std::wstring wide(const std::string& text) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
    return out;
}
struct Handle {
    HINTERNET h{};
    ~Handle() { if (h) WinHttpCloseHandle(h); }
};
[[noreturn]] void fail(const char* step) {
    throw std::runtime_error(std::string(step) + " failed (" + std::to_string(GetLastError()) + ")");
}
}
std::string http_get(const std::string& url, int timeout_seconds) {
    const auto address = wide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256]{}, path[2048]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = DWORD(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = DWORD(std::size(path));
    if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts)) fail("WinHttpCrackUrl");
    Handle session{WinHttpOpen(L"Marchwind64", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) fail("WinHttpOpen");
    const int ms = timeout_seconds * 1000;
    WinHttpSetTimeouts(session.h, ms, ms, ms, ms);
    Handle connection{WinHttpConnect(session.h, host, parts.nPort, 0)};
    if (!connection.h) fail("WinHttpConnect");
    Handle request{WinHttpOpenRequest(connection.h, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.h) fail("WinHttpOpenRequest");
    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) fail("WinHttpSendRequest");
    if (!WinHttpReceiveResponse(request.h, nullptr)) fail("WinHttpReceiveResponse");
    DWORD code = 0, size = sizeof(code);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size,
                        WINHTTP_NO_HEADER_INDEX);
    if (code != 200) throw std::runtime_error("HTTP " + std::to_string(code));
    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available)) fail("WinHttpQueryDataAvailable");
        if (!available) break;
        if (body.size() + available > (1u << 20)) throw std::runtime_error("response too large");
        const size_t at = body.size();
        body.resize(at + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, body.data() + at, available, &read)) fail("WinHttpReadData");
        body.resize(at + read);
    }
    return body;
}
}

#elif defined(__APPLE__)
// macos/update_http_macos.mm

#elif defined(__ANDROID__)
namespace srw64::update {
std::string http_get(const std::string&, int) { throw std::runtime_error("not on Android"); }
}

#else
#include <dlfcn.h>

namespace srw64::update {
namespace {
// The few libcurl entry points used, by their documented C signatures and option numbers
// (curl.h), so no curl headers are needed to build.
using CURL = void;
constexpr int CURLOPT_URL = 10002, CURLOPT_WRITEFUNCTION = 20011, CURLOPT_WRITEDATA = 10001, CURLOPT_TIMEOUT = 13,
              CURLOPT_FOLLOWLOCATION = 52, CURLOPT_NOSIGNAL = 99, CURLOPT_USERAGENT = 10018;
constexpr int CURLINFO_RESPONSE_CODE = 0x200002;
struct Curl {
    void* library{};
    CURL* (*easy_init)(){};
    int (*easy_setopt)(CURL*, int, ...){};
    int (*easy_perform)(CURL*){};
    int (*easy_getinfo)(CURL*, int, ...){};
    const char* (*easy_strerror)(int){};
    void (*easy_cleanup)(CURL*){};
};
const Curl& curl() {
    static const Curl loaded = [] {
        Curl c;
        for (const char* name : {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"})
            if ((c.library = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
        if (!c.library) return c;
        c.easy_init = reinterpret_cast<decltype(c.easy_init)>(dlsym(c.library, "curl_easy_init"));
        c.easy_setopt = reinterpret_cast<decltype(c.easy_setopt)>(dlsym(c.library, "curl_easy_setopt"));
        c.easy_perform = reinterpret_cast<decltype(c.easy_perform)>(dlsym(c.library, "curl_easy_perform"));
        c.easy_getinfo = reinterpret_cast<decltype(c.easy_getinfo)>(dlsym(c.library, "curl_easy_getinfo"));
        c.easy_strerror = reinterpret_cast<decltype(c.easy_strerror)>(dlsym(c.library, "curl_easy_strerror"));
        c.easy_cleanup = reinterpret_cast<decltype(c.easy_cleanup)>(dlsym(c.library, "curl_easy_cleanup"));
        return c;
    }();
    return loaded;
}
size_t append(char* data, size_t size, size_t count, void* out) {
    auto& body = *static_cast<std::string*>(out);
    if (body.size() + size * count > (1u << 20)) return 0;
    body.append(data, size * count);
    return size * count;
}
}
std::string http_get(const std::string& url, int timeout_seconds) {
    const auto& c = curl();
    if (!c.easy_init || !c.easy_setopt || !c.easy_perform || !c.easy_getinfo || !c.easy_strerror || !c.easy_cleanup)
        throw std::runtime_error("libcurl is not installed");
    CURL* handle = c.easy_init();
    if (!handle) throw std::runtime_error("curl_easy_init failed");
    std::string body;
    c.easy_setopt(handle, CURLOPT_URL, url.c_str());
    c.easy_setopt(handle, CURLOPT_WRITEFUNCTION, &append);
    c.easy_setopt(handle, CURLOPT_WRITEDATA, &body);
    c.easy_setopt(handle, CURLOPT_TIMEOUT, long(timeout_seconds));
    c.easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    c.easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    c.easy_setopt(handle, CURLOPT_USERAGENT, "Marchwind64");
    const int result = c.easy_perform(handle);
    long code = 0;
    c.easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &code);
    c.easy_cleanup(handle);
    if (result != 0) throw std::runtime_error(c.easy_strerror(result));
    // A file:// URL (a test) reports no status.
    if (code != 200 && !(code == 0 && url.starts_with("file:"))) throw std::runtime_error("HTTP " + std::to_string(code));
    return body;
}
}
#endif
