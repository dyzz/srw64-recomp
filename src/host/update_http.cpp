// http_get and http_download for the update check and the HD pack's setup step
// (update_check.hpp, hd_prepare.hpp) through the system's own HTTP, so the program carries
// no TLS library or root certificates: WinHTTP on Windows, libcurl on Linux and the Steam
// Deck (opened at run time, so a system without it only fails the request), Java's
// HttpURLConnection on Android, and devkitPro's libcurl over the system's SSL on the Switch.
// macOS has its own in macos/update_http_macos.mm.
#include "update_check.hpp"
#include <fstream>
#include <stdexcept>
#include <string>

#if !defined(__APPLE__)
namespace srw64::update {
namespace {
// Where a response's bytes go: a string with the update check's limit, or a file.
struct Sink {
    virtual ~Sink() = default;
    // False refuses the rest of the response (the body is too large).
    virtual bool take(const char* data, size_t size) = 0;
};
struct BodySink : Sink {
    std::string body;
    bool take(const char* data, size_t size) override {
        if (body.size() + size > (1u << 20)) return false;
        body.append(data, size);
        return true;
    }
};
struct FileSink : Sink {
    std::ofstream out;
    uint64_t received = 0;
    const std::function<void(uint64_t)>& report;
    FileSink(const std::filesystem::path& file, const std::function<void(uint64_t)>& report) : out(file, std::ios::binary | std::ios::trunc), report(report) {
        if (!out) throw std::runtime_error("cannot write " + file.string());
    }
    bool take(const char* data, size_t size) override {
        out.write(data, std::streamsize(size));
        if (!out) throw std::runtime_error("write failed");
        received += size;
        if (report) report(received);
        return true;
    }
    void finish() {
        out.close();
        if (!out) throw std::runtime_error("write failed");
    }
};
void request(const std::string& url, const std::string& user_agent, int timeout_seconds, Sink& sink);
}

std::string http_get(const std::string& url, const std::string& user_agent, int timeout_seconds) {
    BodySink sink;
    request(url, user_agent, timeout_seconds, sink);
    return std::move(sink.body);
}
void http_download(const std::string& url, const std::string& user_agent, const std::filesystem::path& file,
                   const std::function<void(uint64_t)>& received, int timeout_seconds) {
    FileSink sink(file, received);
    request(url, user_agent, timeout_seconds, sink);
    sink.finish();
}
}
#endif

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
void request(const std::string& url, const std::string& user_agent, int timeout_seconds, Sink& sink) {
    const auto address = wide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256]{}, path[2048]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = DWORD(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = DWORD(std::size(path));
    if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts)) fail("WinHttpCrackUrl");
    Handle session{WinHttpOpen(wide(user_agent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) fail("WinHttpOpen");
    // The receive timeout is per wait for data, so a long download is fine while it moves.
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
    std::vector<char> chunk;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available)) fail("WinHttpQueryDataAvailable");
        if (!available) break;
        chunk.resize(available);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, chunk.data(), available, &read)) fail("WinHttpReadData");
        if (!sink.take(chunk.data(), read)) throw std::runtime_error("response too large");
    }
}
}
}

#elif defined(__APPLE__)
// macos/update_http_macos.mm

#elif defined(__ANDROID__)
#include <SDL_system.h>
#include <jni.h>

namespace srw64::update {
namespace {
// SRW64Activity.httpGet (UpdateHttp.java): {error, body}. SDL attaches this thread to the VM
// and detaches it when the thread ends. The body comes whole and goes to the sink at once.
void request(const std::string& url, const std::string& user_agent, int timeout_seconds, Sink& sink) {
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) throw std::runtime_error("no Java activity");
    std::string error;
    std::string body;
    const jclass type = env->GetObjectClass(activity);
    const jmethodID method = env->GetMethodID(type, "httpGet", "(Ljava/lang/String;Ljava/lang/String;I)[Ljava/lang/Object;");
    jobjectArray result = nullptr;
    if (method) {
        const jstring address = env->NewStringUTF(url.c_str()), agent = env->NewStringUTF(user_agent.c_str());
        result = static_cast<jobjectArray>(env->CallObjectMethod(activity, method, address, agent, jint(timeout_seconds)));
        env->DeleteLocalRef(address);
        env->DeleteLocalRef(agent);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        result = nullptr;
    }
    if (!result) {
        error = "Java request failed";
    } else if (auto reason = static_cast<jstring>(env->GetObjectArrayElement(result, 0))) {
        if (const char* text = env->GetStringUTFChars(reason, nullptr)) {
            error = text;
            env->ReleaseStringUTFChars(reason, text);
        }
        if (error.empty()) error = "request failed";
        env->DeleteLocalRef(reason);
    } else if (auto bytes = static_cast<jbyteArray>(env->GetObjectArrayElement(result, 1))) {
        body.resize(size_t(env->GetArrayLength(bytes)));
        env->GetByteArrayRegion(bytes, 0, jsize(body.size()), reinterpret_cast<jbyte*>(body.data()));
        env->DeleteLocalRef(bytes);
    }
    if (result) env->DeleteLocalRef(result);
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
    if (!error.empty()) throw std::runtime_error(error);
    if (!sink.take(body.data(), body.size())) throw std::runtime_error("response too large");
}
}
}

#else
#if !defined(__SWITCH__)
#include <dlfcn.h>
#endif

namespace srw64::update {
namespace {
// The few libcurl entry points used, by their documented C signatures and option numbers
// (curl.h), so no curl headers are needed to build. The Switch links devkitPro's libcurl,
// whose TLS is the system's (libnx ssl, with the console's certificates); it starts the
// network, SSL and random services itself.
using CURL = void;
constexpr int CURLOPT_URL = 10002, CURLOPT_WRITEFUNCTION = 20011, CURLOPT_WRITEDATA = 10001,
              CURLOPT_LOW_SPEED_LIMIT = 19, CURLOPT_LOW_SPEED_TIME = 20,
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
#if defined(__SWITCH__)
extern "C" {
CURL* curl_easy_init();
int curl_easy_setopt(CURL*, int, ...);
int curl_easy_perform(CURL*);
int curl_easy_getinfo(CURL*, int, ...);
const char* curl_easy_strerror(int);
void curl_easy_cleanup(CURL*);
}
#endif
const Curl& curl() {
    static const Curl loaded = [] {
        Curl c;
#if defined(__SWITCH__)
        c.easy_init = &curl_easy_init;
        c.easy_setopt = &curl_easy_setopt;
        c.easy_perform = &curl_easy_perform;
        c.easy_getinfo = &curl_easy_getinfo;
        c.easy_strerror = &curl_easy_strerror;
        c.easy_cleanup = &curl_easy_cleanup;
#else
        for (const char* name : {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"})
            if ((c.library = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
        if (!c.library) return c;
        c.easy_init = reinterpret_cast<decltype(c.easy_init)>(dlsym(c.library, "curl_easy_init"));
        c.easy_setopt = reinterpret_cast<decltype(c.easy_setopt)>(dlsym(c.library, "curl_easy_setopt"));
        c.easy_perform = reinterpret_cast<decltype(c.easy_perform)>(dlsym(c.library, "curl_easy_perform"));
        c.easy_getinfo = reinterpret_cast<decltype(c.easy_getinfo)>(dlsym(c.library, "curl_easy_getinfo"));
        c.easy_strerror = reinterpret_cast<decltype(c.easy_strerror)>(dlsym(c.library, "curl_easy_strerror"));
        c.easy_cleanup = reinterpret_cast<decltype(c.easy_cleanup)>(dlsym(c.library, "curl_easy_cleanup"));
#endif
        return c;
    }();
    return loaded;
}
struct Writing {
    Sink* sink;
    std::string error;  // a sink's exception, carried out of the C callback
};
size_t append(char* data, size_t size, size_t count, void* out) {
    auto& writing = *static_cast<Writing*>(out);
    try {
        if (!writing.sink->take(data, size * count)) return 0;
    } catch (const std::exception& failure) {
        writing.error = failure.what();
        return 0;
    }
    return size * count;
}
void request(const std::string& url, const std::string& user_agent, int timeout_seconds, Sink& sink) {
    const auto& c = curl();
    if (!c.easy_init || !c.easy_setopt || !c.easy_perform || !c.easy_getinfo || !c.easy_strerror || !c.easy_cleanup)
        throw std::runtime_error("libcurl is not installed");
    CURL* handle = c.easy_init();
    if (!handle) throw std::runtime_error("curl_easy_init failed");
    Writing writing{&sink, {}};
    c.easy_setopt(handle, CURLOPT_URL, url.c_str());
    c.easy_setopt(handle, CURLOPT_WRITEFUNCTION, &append);
    c.easy_setopt(handle, CURLOPT_WRITEDATA, &writing);
    // Gives up when nothing arrives for `timeout_seconds`, however long the whole takes.
    c.easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
    c.easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, long(timeout_seconds));
    c.easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    c.easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    c.easy_setopt(handle, CURLOPT_USERAGENT, user_agent.c_str());
    const int result = c.easy_perform(handle);
    long code = 0;
    c.easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &code);
    c.easy_cleanup(handle);
    if (!writing.error.empty()) throw std::runtime_error(writing.error);
    if (result == 23 /* CURLE_WRITE_ERROR: the sink refused */) throw std::runtime_error("response too large");
    if (result != 0) throw std::runtime_error(c.easy_strerror(result));
    // A file:// URL (a test) reports no status.
    if (code != 200 && !(code == 0 && url.starts_with("file:"))) throw std::runtime_error("HTTP " + std::to_string(code));
}
}
}
#endif
