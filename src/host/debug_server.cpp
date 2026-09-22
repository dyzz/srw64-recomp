#include "debug_server.hpp"
#include "debug_protocol.hpp"
#include "debug_ui.hpp"
#include "graphics.hpp"
#include "native_dialogue.hpp"
#include "native_intro.hpp"
#include "native_name_entry.hpp"
#include "link_page.hpp"
#include "battle_page.hpp"
#include "mini_stage.hpp"
#include "presentation_settings.hpp"
#include "presentation/image_mode.hpp"
#include "rule_fixes.hpp"
#include "settings_window.hpp"
#include "notices.hpp"
#include "localization/catalog.hpp"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace srw64::debug {
namespace {
using json=nlohmann::json;
using namespace std::chrono_literals;

std::filesystem::path output;
json host_info;
bool running{};
std::filesystem::path socket_path;
uint64_t shots{};
std::mutex shot_mutex;

struct Task {std::function<json()> work;std::promise<json> result;};
std::mutex queue_mutex;
std::deque<std::shared_ptr<Task>> queue;

// Runs on the window thread, which services the queue every frame.
json on_window(std::function<json()> work) {
    auto task=std::make_shared<Task>();
    task->work=std::move(work);
    auto future=task->result.get_future();
    {std::lock_guard lock(queue_mutex);queue.push_back(task);}
    if(future.wait_for(5s)!=std::future_status::ready)throw RpcError(ServerError,"the window thread did not answer within 5 s");
    return future.get();
}

std::string utf8(const std::u16string& value){return dialogue::utf8(value);}
json name_page() {
    const auto request=names::request();
    json values=json::array();
    for(const auto& value:request.values)values.push_back(utf8(value));
    json page={{"visible",request.visible},{"active",request.active},{"pending",request.pending},
               {"person",request.person},{"serial",request.serial},{"values",values},{"error",request.error}};
    if(request.person==names::Selection) {   // the protagonist selection page
        page["route"]=request.route;page["choices"]=json::array();
        for(const auto& choice:request.choices)page["choices"].push_back({
            {"protagonist",utf8(choice.names[0][0]+u"・"+choice.names[0][1])},
            {"partner",utf8(choice.names[1][0]+u"・"+choice.names[1][1])}});
    }
    return page;
}
json dialogue_state(bool full_history) {
    auto state=dialogue::state();
    if(state.is_null() || full_history)return state;
    auto& history=state["history"];
    if(history.size()>3)history=json(std::vector<json>(history.end()-3,history.end()));
    return state;
}
json rules_state() {
    const unsigned fixes=rules::active_fixes();
    json ids=json::array();
    for(const auto& entry:rules::catalog)if(fixes&entry.fix)ids.push_back(entry.id);
    return ids;
}

uint16_t button_mask(const json& value) {
    static const std::pair<const char*,uint16_t> names[]={{"a",0x8000},{"b",0x4000},{"z",0x2000},{"start",0x1000},
        {"up",0x800},{"down",0x400},{"left",0x200},{"right",0x100},{"l",0x20},{"r",0x10},
        {"c_up",8},{"c_down",4},{"c_left",2},{"c_right",1}};
    if(value.is_number_unsigned())return uint16_t(value.get<unsigned>());
    if(!value.is_string())throw RpcError(InvalidParams,"buttons must be names like \"a+start\" or a mask");
    uint16_t mask=0;
    const auto text=value.get<std::string>();
    size_t start=0;
    while(start<=text.size()) {
        const auto plus=text.find('+',start);
        const auto name=text.substr(start,plus==std::string::npos?std::string::npos:plus-start);
        bool known=false;
        for(const auto& [key,bit]:names)if(name==key){mask|=bit;known=true;}
        if(!known)throw RpcError(InvalidParams,"unknown button '"+name+"'");
        if(plus==std::string::npos)break;
        start=plus+1;
    }
    return mask;
}

json status(const json& params) {
    json state={{"vi",srw64_current_vi()},{"pid",getpid()},{"run",output.string()},{"host",host_info},
        {"image_mode",{{"current",presentation::image_mode.current()},{"requested",presentation::image_mode.requested()},
                       {"hd_available",presentation::image_mode.enabled()}}},
        {"rules",rules_state()},{"keys_held",key_list(keyboard().held())},
        {"intro",intro::state()},{"dialogue",dialogue_state(params.value("history",false))},{"name_page",name_page()},
        {"mini_stage",mini_stage::snapshot()},{"battle_page",battle_page::state()},{"link_page",link_page::state()},{"notices",notices::recent()}};
    const auto window=on_window([] {
        return json{{"window",srw64_window_status()},{"locale",localization::catalog().locale},
                    {"settings_window",settings_window::visible()},{"ui",debug_ui::summary()}};
    });
    state.update(window);
    return state;
}

json keys(const json& params) {
    auto& board=keyboard();
    auto mask=[&](const char* field){
        try {return parse_keys(params[field].get<std::string>());}
        catch(const std::exception& error){throw RpcError(InvalidParams,error.what());}
    };
    if(params.value("release_all",false))board.release_all();
    if(params.contains("down"))board.down(mask("down"));
    if(params.contains("up"))board.up(mask("up"));
    if(params.contains("press")) {
        const auto keys=mask("press");
        const auto hold=params.value("hold_ms",120);
        if(hold<0 || hold>10000)throw RpcError(InvalidParams,"hold_ms must be 0..10000");
        board.down(keys);
        std::this_thread::sleep_for(std::chrono::milliseconds(hold));
        board.up(keys);
    }
    return {{"held",key_list(board.held())},{"vi",srw64_current_vi()}};
}

json screenshot(const json& params) {
    std::filesystem::path path;
    if(params.contains("path"))path=params["path"].get<std::string>();
    else {
        std::lock_guard lock(shot_mutex);
        std::filesystem::create_directories(output/"debug-shots");
        path=output/"debug-shots"/("shot-"+std::to_string(++shots)+".png");
    }
    if(params.contains("window") && params["window"]!="game")
        return on_window([&]{return debug_ui::capture(params,path);});
    auto result=screenshots().capture(path,std::chrono::milliseconds(params.value("timeout_ms",3000)));
    if(!result)throw RpcError(ServerError,"no frame was presented in time (is the game paused or minimised?)");
    if(result->contains("error"))throw RpcError(ServerError,(*result)["error"].get<std::string>());
    if(params.value("overlays",true))result->update(on_window([&]{return debug_ui::compose(path);}));
    return *result;
}

json settings(const json& params) {
    json done=json::object();
    if(params.contains("rules")) {
        const auto& value=params["rules"];
        unsigned fixes=0;
        if(value.is_string()) {
            const auto name=value.get<std::string>();
            if(name=="defaults")fixes=rules::default_fixes;
            else if(name=="original")fixes=0;
            else if(name=="all")fixes=rules::all_fixes;
            else try{fixes=rules::parse(name);}catch(const std::exception& error){throw RpcError(InvalidParams,error.what());}
        } else if(value.is_array()) {
            for(const auto& id:value)
                try{fixes|=rules::parse(id.get<std::string>());}catch(const std::exception& error){throw RpcError(InvalidParams,error.what());}
        } else throw RpcError(InvalidParams,"rules must be a preset name, an id list or a comma list");
        rules::set_fixes(fixes);
        done["rules"]=rules_state();
    }
    if(params.contains("images")) {
        const auto images=params["images"].get<std::string>();
        if(images!="original" && images!="hd")throw RpcError(InvalidParams,"images must be original or hd");
        presentation::image_mode.request(images=="hd");
        done["images"]=images;
    }
    if(params.contains("battle_ui")) {
        const auto ui=params["battle_ui"].get<std::string>();
        if(ui!="native" && ui!="original")throw RpcError(InvalidParams,"battle_ui must be native or original");
        on_window([&]{settings::set_native_battle_ui(ui=="native");return json(nullptr);});
        done["battle_ui"]=ui;
    }
    if(params.contains("locale")) {
        const auto locale=params["locale"].get<std::string>();
        on_window([&]{settings::request_locale(locale);return json(nullptr);});
        done["locale"]=locale;
    }
    return done;
}

json wait_vi(const json& params) {
    const auto target=params.value("vi",uint64_t{0});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(params.value("timeout_ms",10000));
    while(srw64_current_vi()<target) {
        if(std::chrono::steady_clock::now()>=deadline)throw RpcError(ServerError,"timed out at VI "+std::to_string(srw64_current_vi()));
        std::this_thread::sleep_for(5ms);
    }
    return {{"vi",srw64_current_vi()}};
}

json dispatch(const std::string& method,const json& params) {
    if(method=="status")return status(params);
    if(method=="keys")return keys(params);
    if(method=="buttons") {
        if(!params.contains("buttons"))throw RpcError(InvalidParams,"buttons needs buttons");
        const auto vis=params.value("vis",6);
        if(vis<1 || vis>600)throw RpcError(InvalidParams,"vis must be 1..600");
        srw64_debug_buttons(button_mask(params["buttons"]),vis);
        return {{"vi",srw64_current_vi()}};
    }
    if(method=="screenshot")return screenshot(params);
    if(method=="ui.tree")return on_window([&]{return debug_ui::tree(params);});
    if(method=="ui.click")return on_window([&]{return debug_ui::click(params);});
    if(method=="ui.key")return on_window([&]{return debug_ui::key(params);});
    if(method=="ui.type")return on_window([&]{return debug_ui::type(params);});
    if(method=="menu")return on_window([&]{return debug_ui::menu(params);});
    if(method=="settings")return settings(params);
    if(method=="window")return on_window([&] {
        try{return srw64_window_control(params);}
        catch(const std::invalid_argument& error){throw RpcError(InvalidParams,error.what());}
    });
    if(method=="wait_vi")return wait_vi(params);
    if(method=="mini_stage.load")return {{"name",mini_stage::load_file(params.at("path").get<std::string>())}};
    if(method=="quit"){srw64_debug_quit();return {{"quitting",true}};}
    if(method=="methods")return {"status","keys","buttons","screenshot","ui.tree","ui.click","ui.key","ui.type",
                                 "menu","settings","window","wait_vi","mini_stage.load","quit","methods"};
    throw RpcError(MethodNotFound,"unknown method '"+method+"'");
}

std::string handle(const std::string& line) {
    Call call;
    try{call=parse_call(line);}
    catch(const RpcError& error){return failure(nullptr,error.code,error.what());}
    try{return reply(call.id,dispatch(call.method,call.params));}
    catch(const RpcError& error){return failure(call.id,error.code,error.what());}
    catch(const std::exception& error){return failure(call.id,ServerError,error.what());}
}

void serve(int client) {
    std::string buffer;
    char chunk[4096];
    for(;;) {
        const auto count=::read(client,chunk,sizeof chunk);
        if(count<=0)break;
        buffer.append(chunk,size_t(count));
        size_t newline;
        while((newline=buffer.find('\n'))!=std::string::npos) {
            const auto line=buffer.substr(0,newline);
            buffer.erase(0,newline+1);
            if(line.find_first_not_of(" \t\r")==std::string::npos)continue;
            const auto response=handle(line);
            for(size_t sent=0;sent<response.size();) {
                const auto written=::write(client,response.data()+sent,response.size()-sent);
                if(written<=0){::close(client);return;}
                sent+=size_t(written);
            }
        }
    }
    ::close(client);
}

void remove_socket(){if(!socket_path.empty())::unlink(socket_path.c_str());}
}

bool enabled(){return running;}

void start(const std::filesystem::path& directory,json host) {
    const char* flag=std::getenv("SRW64_DEBUG");
    if(!flag || std::string(flag)!="1")return;
    output=directory;host_info=std::move(host);
    socket_path=directory/"debug.sock";
    sockaddr_un address{};
    address.sun_family=AF_UNIX;
    if(socket_path.string().size()>=sizeof address.sun_path)throw std::runtime_error("debug socket path is too long: "+socket_path.string());
    std::strncpy(address.sun_path,socket_path.c_str(),sizeof address.sun_path-1);
    const int listener=::socket(AF_UNIX,SOCK_STREAM,0);
    if(listener<0)throw std::runtime_error("debug socket: "+std::string(std::strerror(errno)));
    ::unlink(socket_path.c_str());
    if(::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof address)!=0 || ::listen(listener,4)!=0)
        throw std::runtime_error("debug socket: "+std::string(std::strerror(errno)));
    ::chmod(socket_path.c_str(),0600);
    std::signal(SIGPIPE,SIG_IGN);
    std::atexit(remove_socket);
    running=true;
    std::thread([listener] {
        for(;;) {
            const int client=::accept(listener,nullptr,nullptr);
            if(client<0){if(errno==EINTR)continue;break;}
            std::thread(serve,client).detach();
        }
    }).detach();
    std::ofstream(directory/"debug.json")<<json({{"schema","srw64.debug-endpoint.v1"},{"socket",socket_path.string()},
        {"pid",getpid()},{"protocol","JSON-RPC 2.0, one message per line"}}).dump(2)<<'\n';
    std::fprintf(stderr,"SRW64_DEBUG socket=%s\n",socket_path.c_str());
}

void service_main() {
    if(!running)return;
    std::deque<std::shared_ptr<Task>> work;
    {std::lock_guard lock(queue_mutex);work.swap(queue);}
    for(auto& task:work) {
        try{task->result.set_value(task->work());}
        catch(...){task->result.set_exception(std::current_exception());}
    }
}
}
