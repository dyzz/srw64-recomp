#include "debug_transport.hpp"
#include "debug_protocol.hpp"
#include "json/json.hpp"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <random>
#include <set>
#include <stdexcept>
#include <system_error>
#include <thread>
// Platform headers last: Winsock brings windows.h and its macros.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace srw64::debug::transport {
namespace {
namespace fs=std::filesystem;
using json=nlohmann::json;

#ifdef _WIN32
using Socket=SOCKET;
const Socket invalid=INVALID_SOCKET;
void close_socket(Socket s){::closesocket(s);}
void end_connection(Socket s){::shutdown(s,SD_BOTH);}
int wait_readable(Socket s,int ms){WSAPOLLFD p{s,POLLRDNORM,0};return ::WSAPoll(&p,1,ms);}
std::string last_error(){return "Winsock error "+std::to_string(::WSAGetLastError());}
#else
using Socket=int;
constexpr Socket invalid=-1;
void close_socket(Socket s){::close(s);}
void end_connection(Socket s){::shutdown(s,SHUT_RDWR);}
int wait_readable(Socket s,int ms){pollfd p{s,POLLIN,0};return ::poll(&p,1,ms);}
std::string last_error(){return std::strerror(errno);}
#endif

Handler handler;
std::string token;  // empty: no handshake (Android)
fs::path endpoint_file;
std::mutex clients_mutex;
std::set<Socket> clients;       // open connections, ended by stop()
// The accept loop runs detached (a joinable std::thread left at exit would terminate the
// process); stop() waits for it through `looping`.
std::atomic_bool stopping{false},accepting{false},looping{false};

// 256 random bits as hex. std::random_device reads the system's CSPRNG with every
// toolchain we build with (MSVC STL rand_s, libc++ arc4random/getentropy, libstdc++ getrandom).
std::string make_token() {
    static constexpr char digits[]="0123456789abcdef";
    std::random_device device;
    std::string out;
    for(int i=0;i<8;++i) {
        const uint32_t word=device();
        for(int shift=28;shift>=0;shift-=4)out.push_back(digits[(word>>shift)&15]);
    }
    return out;
}

bool send_all(Socket client,const std::string& data) {
    for(size_t sent=0;sent<data.size();) {
        const size_t part=std::min<size_t>(data.size()-sent,1u<<20);
#if defined(MSG_NOSIGNAL)
        const auto count=::send(client,data.data()+sent,part,MSG_NOSIGNAL);
#elif defined(_WIN32)
        const auto count=::send(client,data.data()+sent,int(part),0);
#else
        const auto count=::send(client,data.data()+sent,part,0);  // macOS: SIGPIPE is ignored
#endif
        if(count<=0)return false;
        sent+=size_t(count);
    }
    return true;
}

// The first line of a connection must present the token.
bool admit(const std::string& line,const std::string& expected,std::string& response) {
    try {
        const auto call=parse_call(line);
        const auto& given=call.params.contains("token")?call.params["token"]:json();
        if(call.method=="auth" && given.is_string() && same_token(given.get<std::string>(),expected)) {
            response=reply(call.id,{{"authenticated",true}});
            return true;
        }
        response=failure(call.id,InvalidRequest,"authenticate first: method auth with the token from debug.json");
    } catch(const RpcError& error) {
        response=failure(nullptr,error.code,error.what());
    }
    return false;
}

void finish(Socket client) {
    {std::lock_guard lock(clients_mutex);clients.erase(client);}
    close_socket(client);
}

// `expected`: the token when the connection came in; empty, no handshake.
void serve(Socket client,std::string expected) {
    std::string buffer;
    char chunk[4096];
    bool admitted=expected.empty();
    for(;;) {
        const auto count=::recv(client,chunk,int(sizeof chunk),0);
        if(count<=0)break;
        buffer.append(chunk,size_t(count));
        size_t newline;
        while((newline=buffer.find('\n'))!=std::string::npos) {
            const auto line=buffer.substr(0,newline);
            buffer.erase(0,newline+1);
            if(line.find_first_not_of(" \t\r")==std::string::npos)continue;
            if(!admitted) {
                std::string response;
                admitted=admit(line,expected,response);
                if(!send_all(client,response) || !admitted){finish(client);return;}
                continue;
            }
            if(!send_all(client,handler(line))){finish(client);return;}
        }
        if(!admitted && buffer.size()>4096)break;  // no unbounded buffering before the handshake
    }
    finish(client);
}

#ifdef __ANDROID__
// The app's files are out of adb's reach: an abstract socket, which
// `adb forward tcp:0 localabstract:srw64-debug` reaches.
Socket listen_abstract() {
    sockaddr_un address{};
    address.sun_family=AF_UNIX;
    constexpr char name[]="srw64-debug";
    std::memcpy(address.sun_path+1,name,sizeof name-1);
    const socklen_t length=socklen_t(offsetof(sockaddr_un,sun_path)+1+sizeof name-1);
    const Socket listener=::socket(AF_UNIX,SOCK_STREAM,0);
    if(listener==invalid)throw std::runtime_error("debug socket: "+last_error());
    if(::bind(listener,reinterpret_cast<sockaddr*>(&address),length)!=0 || ::listen(listener,4)!=0) {
        const auto error=last_error();close_socket(listener);
        throw std::runtime_error("debug socket: "+error);
    }
    return listener;
}
#else
Socket listen_loopback(int& port) {
#ifdef _WIN32
    WSADATA data;
    if(::WSAStartup(MAKEWORD(2,2),&data)!=0)throw std::runtime_error("debug socket: WSAStartup failed");
#endif
    const Socket listener=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(listener==invalid)throw std::runtime_error("debug socket: "+last_error());
#ifdef _WIN32
    // No other process may bind the same address while the game holds it.
    BOOL exclusive=TRUE;
    ::setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof exclusive);
#endif
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=0;  // the system picks a free port
    socklen_t length=sizeof address;
    if(::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof address)!=0 || ::listen(listener,4)!=0 ||
       ::getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)!=0) {
        const auto error=last_error();close_socket(listener);
        throw std::runtime_error("debug socket: "+error);
    }
    port=ntohs(address.sin_port);
    return listener;
}
#endif

// Owner-only before the token goes in, written whole under another name and renamed,
// so a client never reads half of it. (On Windows the user's own folders already are.)
void write_endpoint(const fs::path& run,const json& endpoint) {
    const auto temporary=run/"debug.json.tmp";
    {std::ofstream create(temporary,std::ios::trunc);}
    std::error_code ignored;
    fs::permissions(temporary,fs::perms::owner_read|fs::perms::owner_write,fs::perm_options::replace,ignored);
    {
        std::ofstream out(temporary,std::ios::trunc);
        out<<endpoint.dump(2)<<'\n';
        if(!out)throw std::runtime_error("debug socket: cannot write "+temporary.string());
    }
    fs::rename(temporary,run/"debug.json");
}

void remove_endpoint() {
    std::error_code ignored;
    if(!endpoint_file.empty())fs::remove(endpoint_file,ignored);
}

// Accepts until stop(): polls so it can see the request, then lets go of the socket.
void accept_loop(Socket listener) {
    while(!stopping) {
        const int ready=wait_readable(listener,200);
        if(ready<0) {
#ifndef _WIN32
            if(errno==EINTR)continue;
#endif
            break;
        }
        if(ready==0)continue;
        const Socket client=::accept(listener,nullptr,nullptr);
        if(client==invalid)continue;
        {std::lock_guard lock(clients_mutex);clients.insert(client);}
        std::thread(serve,client,token).detach();
    }
    close_socket(listener);
    looping=false;
}
}

int process_id() {
#ifdef _WIN32
    return ::_getpid();
#else
    return int(::getpid());
#endif
}

bool same_token(const std::string& a,const std::string& b) {
    if(a.size()!=b.size())return false;
    unsigned char difference=0;
    for(size_t i=0;i<a.size();++i)difference|=static_cast<unsigned char>(a[i]^b[i]);
    return difference==0;
}

Endpoint listen(const fs::path& run,Handler handle) {
    if(accepting)stop();
    handler=std::move(handle);
    json endpoint={{"schema","srw64.debug-endpoint.v2"},{"pid",process_id()},
                   {"protocol","JSON-RPC 2.0, one message per line"}};
#ifdef __ANDROID__
    const Socket listener=listen_abstract();
    endpoint["transport"]="abstract";
    endpoint["socket"]="@srw64-debug";
    const std::string address="@srw64-debug";
#else
    int port=0;
    const Socket listener=listen_loopback(port);
    token=make_token();
    endpoint["transport"]="tcp";
    endpoint["host"]="127.0.0.1";
    endpoint["port"]=port;
    endpoint["token"]=token;
    endpoint["auth"]="first line {\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"auth\",\"params\":{\"token\":<token>}}";
    const std::string address="127.0.0.1:"+std::to_string(port);
#endif
    static const bool once=[]{
#ifndef _WIN32
        std::signal(SIGPIPE,SIG_IGN);
#endif
        std::atexit(remove_endpoint);
        return true;
    }();
    (void)once;
    try{write_endpoint(run,endpoint);}
    catch(...){close_socket(listener);throw;}
    endpoint_file=run/"debug.json";
    stopping=false;accepting=true;looping=true;
    std::thread(accept_loop,listener).detach();
    return {address,endpoint_file};
}

void stop() {
    if(!accepting)return;
    stopping=true;
    while(looping)std::this_thread::sleep_for(std::chrono::milliseconds(10));  // one poll, at most 200 ms
    {std::lock_guard lock(clients_mutex);for(const auto client:clients)end_connection(client);}
    remove_endpoint();
    endpoint_file.clear();
    accepting=false;
}

bool listening(){return accepting.load();}
}
