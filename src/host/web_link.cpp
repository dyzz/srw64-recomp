#include "web_link.hpp"
#include "debug_protocol.hpp"
#include "mod_data.hpp"
#include "mod_ids.hpp"
#include "mod_library.hpp"
#include "presentation_settings.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <regex>
#include <set>
#include <thread>
#include <vector>

#if defined(__ANDROID__) || defined(__SWITCH__) || defined(__vita__)
// A phone or a console has no browser beside the game to link: the switch does nothing.
namespace srw64::web_link {
void service(){}
bool listening(){return false;}
std::string failure(){return {};}
std::optional<Prompt> prompt(){return std::nullopt;}
void answer(unsigned,bool){}
void run_view(const std::function<std::string(const nlohmann::json&)>&){}
bool allowed_origin(const std::string&){return false;}
}
#else
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
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace srw64::web_link {
namespace {
namespace fs=std::filesystem;
using json=nlohmann::json;

#ifdef _WIN32
using Socket=SOCKET;
const Socket invalid=INVALID_SOCKET;
void close_socket(Socket s){::closesocket(s);}
int wait_readable(Socket s,int ms){WSAPOLLFD p{s,POLLRDNORM,0};return ::WSAPoll(&p,1,ms);}
#else
using Socket=int;
constexpr Socket invalid=-1;
void close_socket(Socket s){::close(s);}
int wait_readable(Socket s,int ms){pollfd p{s,POLLIN,0};return ::poll(&p,1,ms);}
#endif
bool send_all(Socket s,const std::string& data) {
    size_t sent=0;
    while(sent<data.size()) {
#if defined(_WIN32)
        const int n=::send(s,data.data()+sent,int(std::min<size_t>(data.size()-sent,1<<20)),0);
#elif defined(MSG_NOSIGNAL)
        const auto n=::send(s,data.data()+sent,data.size()-sent,MSG_NOSIGNAL);
#else
        const auto n=::send(s,data.data()+sent,data.size()-sent,0);
#endif
        if(n<=0)return false;
        sent+=size_t(n);
    }
    return true;
}

constexpr size_t header_limit=16*1024, body_limit=64u*1024*1024;

struct State {
    std::mutex mutex;
    Socket listener=invalid;
    std::thread thread;
    std::atomic_bool stopping{};
    bool on{};
    std::string failure;
    std::set<std::string> tokens;       // pages the player allowed, for this run
    struct Pair { std::string origin; int state=0; std::string token; };   // 0 waiting, 1 allowed, 2 refused
    std::map<unsigned,Pair> pairs;
    unsigned next_pair=1;
    struct View { json choice; std::promise<std::string> done; };
    std::shared_ptr<View> view;         // asked, for the window thread
};
State& state(){static State value;return value;}

std::string random_token() {
    std::random_device random;
    std::string out;
    for(int i=0;i<16;++i){char hex[3];std::snprintf(hex,sizeof hex,"%02x",unsigned(random()&0xFF));out+=hex;}
    return out;
}

struct Request {
    std::string method,path,query,origin,authorization;
    std::string body;
};
std::string lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}

// One request from the connection: its line, the headers this needs, its body.
std::optional<Request> read_request(Socket s) {
    std::string data;
    char chunk[8192];
    size_t end=std::string::npos;
    while((end=data.find("\r\n\r\n"))==std::string::npos) {
        if(data.size()>header_limit || wait_readable(s,5000)<=0)return std::nullopt;
        const auto n=::recv(s,chunk,int(sizeof chunk),0);
        if(n<=0)return std::nullopt;
        data.append(chunk,size_t(n));
    }
    Request r;
    const auto head=data.substr(0,end);
    std::string rest=data.substr(end+4);
    size_t line_end=head.find("\r\n");
    const auto first=head.substr(0,line_end);
    const auto a=first.find(' '),b=first.find(' ',a+1);
    if(a==std::string::npos || b==std::string::npos)return std::nullopt;
    r.method=first.substr(0,a);
    const auto target=first.substr(a+1,b-a-1);
    const auto q=target.find('?');
    r.path=target.substr(0,q);
    if(q!=std::string::npos)r.query=target.substr(q+1);
    size_t length=0;
    for(size_t at=line_end;at!=std::string::npos && at<head.size();) {
        const size_t from=at+2,next=head.find("\r\n",from);
        const auto line=head.substr(from,next==std::string::npos?std::string::npos:next-from);
        at=next;
        const auto colon=line.find(':');
        if(colon==std::string::npos)continue;
        const auto name=lower(line.substr(0,colon));
        auto value=line.substr(colon+1);
        while(!value.empty() && value.front()==' ')value.erase(value.begin());
        if(name=="origin")r.origin=value;
        else if(name=="authorization")r.authorization=value;
        else if(name=="content-length"){try{length=std::stoul(value);}catch(...){return std::nullopt;}}
    }
    if(length>body_limit)return std::nullopt;
    r.body=std::move(rest);
    while(r.body.size()<length) {
        if(wait_readable(s,10000)<=0)return std::nullopt;
        const auto n=::recv(s,chunk,int(sizeof chunk),0);
        if(n<=0)return std::nullopt;
        r.body.append(chunk,size_t(n));
    }
    r.body.resize(length);
    return r;
}

std::string response(int status,const std::string& origin,const std::string& type,const std::string& body) {
    const char* reason=status==200?"OK":status==204?"No Content":status==400?"Bad Request":status==401?"Unauthorized":
                       status==403?"Forbidden":status==404?"Not Found":status==409?"Conflict":"Error";
    std::string out="HTTP/1.1 "+std::to_string(status)+" "+reason+"\r\n";
    if(!origin.empty()) {
        out+="Access-Control-Allow-Origin: "+origin+"\r\nVary: Origin\r\n"
             "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Authorization, Content-Type\r\n"
             // Chrome's Private Network Access preflight: a public page may reach this computer.
             "Access-Control-Allow-Private-Network: true\r\nAccess-Control-Max-Age: 600\r\n";
    }
    if(!type.empty())out+="Content-Type: "+type+"\r\n";
    out+="Cache-Control: no-store\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n";
    return out+body;
}
std::string reply(int status,const std::string& origin,const json& value){return response(status,origin,"application/json",value.dump());}

std::string dotted(const mods::Version& v){return mods::version_text(v);}

bool paired(const Request& r) {
    if(!r.authorization.starts_with("Bearer "))return false;
    std::lock_guard lock(state().mutex);
    return state().tokens.count(r.authorization.substr(7))>0;
}

// The package a page sent: written to a file, put in the folder of the package to try, and
// its new numbers by name.
json install(const std::string& bytes) {
    std::error_code error;
    const auto file=fs::temp_directory_path(error)/"marchwind64-web-link.srw64mod";
    {
        std::ofstream out(file,std::ios::binary|std::ios::trunc);
        out.write(bytes.data(),std::streamsize(bytes.size()));
        if(!out)return {{"ok",false},{"problems",{"write "+file.string()}}};
    }
    const auto result=mods::install_trial(file);
    fs::remove(file,error);
    if(!result.ok)return {{"ok",false},{"problems",{result.problem.code+(result.problem.detail.empty()?"":" "+result.problem.detail)}}};
    // The game thread reads the packages again on the title (game_hooks.cpp); the viewer the
    // page starts next checks the new characters' quotes against what it read.
    for(int i=0;i<60 && !mod_data::current();++i)std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::printf("SRW64_WEB_LINK install %s read=%d\n",result.id.c_str(),int(mod_data::current()));std::fflush(stdout);
    // In, but not on: say why (a newer game wanted, a requirement missing).
    for(const auto& package:mods::installed(mods::game_version()))
        if(package.id()==result.id && !package.active)
            return {{"ok",false},{"problems",{package.problem.code.empty()?std::string("inactive"):package.problem.code+(package.problem.detail.empty()?"":" "+package.problem.detail)}}};
    const auto numbers=mod_ids::current();
    json out={{"units",json::object()},{"pilots",json::object()},{"weapons",json::object()}};
    const auto prefix=result.id+"@";
    for(const auto& [kind,table]:{std::pair<const char*,const std::map<std::string,unsigned>*>{"units",&numbers->units},{"pilots",&numbers->pilots},{"weapons",&numbers->weapons}})
        for(const auto& [key,n]:*table)if(key.starts_with(prefix))out[kind]["@"+key.substr(prefix.size())]=n;
    return {{"ok",true},{"id",result.id},{"version",dotted(result.version)},{"numbers",out}};
}

std::string screenshot() {
    std::error_code error;
    const auto file=fs::temp_directory_path(error)/"marchwind64-web-link-shot.png";
    fs::remove(file,error);
    const auto done=debug::screenshots().capture(file,std::chrono::milliseconds(3000));
    if(!done || done->contains("error"))return {};
    std::ifstream in(file,std::ios::binary);
    std::string png((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    in.close();
    fs::remove(file,error);
    return png;
}

std::string handle(const Request& r) {
    if(!allowed_origin(r.origin))return response(403,"","text/plain","origin");
    const auto& o=r.origin;
    if(r.method=="OPTIONS")return response(204,o,"","");
    if(r.method=="GET" && r.path=="/hello")
        return reply(200,o,{{"game","Marchwind64"},{"version",dotted(mods::game_version())},{"protocol",protocol},{"paired",paired(r)}});
    if(r.method=="POST" && r.path=="/pair") {
        std::lock_guard lock(state().mutex);
        // A page asking again while its question is up waits on the same one.
        for(const auto& [id,p]:state().pairs)if(p.state==0 && p.origin==o)return reply(200,o,{{"id",std::to_string(id)}});
        const unsigned id=state().next_pair++;
        state().pairs[id]={o,0,{}};
        return reply(200,o,{{"id",std::to_string(id)}});
    }
    if(r.method=="GET" && r.path=="/pair") {
        unsigned id=0;
        if(r.query.starts_with("id="))try{id=unsigned(std::stoul(r.query.substr(3)));}catch(...){}
        std::lock_guard lock(state().mutex);
        const auto found=state().pairs.find(id);
        if(found==state().pairs.end() || found->second.origin!=o)return reply(404,o,{{"error","no such request"}});
        if(found->second.state==1){const auto token=found->second.token;state().pairs.erase(found);return reply(200,o,{{"token",token}});}
        if(found->second.state==2){state().pairs.erase(found);return reply(200,o,{{"refused",true}});}
        return reply(200,o,{{"waiting",true}});
    }
    if(!paired(r))return reply(401,o,{{"error","not paired"}});
    if(r.method=="POST" && r.path=="/package")return reply(200,o,install(r.body));
    if(r.method=="POST" && r.path=="/viewer") {
        const auto choice=json::parse(r.body,nullptr,false);
        if(choice.is_discarded() || !choice.is_object())return reply(400,o,{{"ok",false},{"error","the choice is not JSON"}});
        auto view=std::make_shared<State::View>();
        view->choice=choice;
        auto answer=view->done.get_future();
        {
            std::lock_guard lock(state().mutex);
            if(state().view)return reply(409,o,{{"ok",false},{"error","busy"}});
            state().view=view;
        }
        if(answer.wait_for(std::chrono::seconds(5))!=std::future_status::ready) {
            std::lock_guard lock(state().mutex);
            if(state().view==view)state().view.reset();
            return reply(200,o,{{"ok",false},{"error","the game did not answer"}});
        }
        const auto why=answer.get();
        return reply(200,o,why.empty()?json{{"ok",true}}:json{{"ok",false},{"error",why}});
    }
    if(r.method=="GET" && r.path=="/screenshot") {
        const auto png=screenshot();
        if(png.empty())return reply(200,o,{{"error","no picture"}});
        return response(200,o,"image/png",png);
    }
    return reply(404,o,{{"error","no such request"}});
}

void serve(Socket client) {
    if(const auto r=read_request(client)) {
        std::string out;
        try{out=handle(*r);}catch(const std::exception& error){out=reply(500,allowed_origin(r->origin)?r->origin:"",{{"error",error.what()}});}
        send_all(client,out);
    }
    close_socket(client);
}

void accept_loop(Socket listener) {
    while(!state().stopping) {
        if(wait_readable(listener,200)<=0)continue;
        const Socket client=::accept(listener,nullptr,nullptr);
        if(client==invalid)continue;
        std::thread(serve,client).detach();
    }
}

void start() {
    auto& s=state();
#ifdef _WIN32
    WSADATA data;
    if(::WSAStartup(MAKEWORD(2,2),&data)!=0){s.failure="WSAStartup";return;}
#endif
    const Socket listener=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(listener==invalid){s.failure="socket";return;}
#ifdef _WIN32
    BOOL exclusive=TRUE;
    ::setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof exclusive);
#else
    int reuse=1;
    ::setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof reuse);
#endif
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=htons(port);
    if(::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof address)!=0 || ::listen(listener,8)!=0) {
        close_socket(listener);
        s.failure="port "+std::to_string(port)+" is taken";
        std::printf("SRW64_WEB_LINK failed %s\n",s.failure.c_str());std::fflush(stdout);
        return;
    }
    s.failure.clear();
    s.listener=listener;
    s.stopping=false;
    s.thread=std::thread(accept_loop,listener);
    std::printf("SRW64_WEB_LINK listening 127.0.0.1:%u\n",unsigned(port));std::fflush(stdout);
}
void stop() {
    auto& s=state();
    if(s.listener==invalid)return;
    s.stopping=true;
    if(s.thread.joinable())s.thread.join();
    close_socket(s.listener);
    s.listener=invalid;
    std::lock_guard lock(s.mutex);
    s.pairs.clear();
    s.tokens.clear();
    std::printf("SRW64_WEB_LINK stopped\n");std::fflush(stdout);
}
}

bool allowed_origin(const std::string& origin) {
    static const std::regex local(R"(^http://(localhost|127\.0\.0\.1)(:\d{1,5})?$)");
    return origin=="https://srw64.dreamquest.club" || std::regex_match(origin,local);
}

void service() {
    auto& s=state();
    const bool want=settings::web_link();
    if(want==s.on)return;
    s.on=want;
    if(want)start();else stop();
}
bool listening(){return state().listener!=invalid;}
std::string failure(){return state().failure;}

std::optional<Prompt> prompt() {
    std::lock_guard lock(state().mutex);
    for(const auto& [id,p]:state().pairs)if(p.state==0)return Prompt{id,p.origin};
    return std::nullopt;
}
void answer(unsigned id,bool allow) {
    std::lock_guard lock(state().mutex);
    const auto found=state().pairs.find(id);
    if(found==state().pairs.end())return;
    found->second.state=allow?1:2;
    if(allow){found->second.token=random_token();state().tokens.insert(found->second.token);}
}

void run_view(const std::function<std::string(const nlohmann::json&)>& start_view) {
    std::shared_ptr<State::View> view;
    {
        std::lock_guard lock(state().mutex);
        view=std::move(state().view);
        state().view.reset();
    }
    if(!view)return;
    std::string why;
    try{why=start_view(view->choice);}catch(const std::exception& error){why=error.what();}
    view->done.set_value(why);
}
}
#endif
