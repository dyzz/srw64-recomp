#include "desktop.hpp"
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
namespace srw64::app {
namespace {
struct Result {int code;std::string output;};
struct Fd {
    int value;
    explicit Fd(int value):value(value){}
    ~Fd(){if(value>=0)close(value);}
    Fd(const Fd&)=delete;
    void reset(){if(value>=0)close(value);value=-1;}
};
void checked(int code) {if(code)throw std::runtime_error(std::string("Cannot prepare desktop dialog: ")+std::strerror(code));}
struct Actions {
    posix_spawn_file_actions_t value;
    Actions(){checked(posix_spawn_file_actions_init(&value));}
    ~Actions(){posix_spawn_file_actions_destroy(&value);}
};
Result dialog(std::vector<std::string> arguments) {
    int pipes[2];
    if(pipe2(pipes,O_CLOEXEC)!=0)throw std::runtime_error("Cannot open desktop dialog output");
    Fd input(pipes[0]),output(pipes[1]);
    Actions actions;
    checked(posix_spawn_file_actions_adddup2(&actions.value,output.value,STDOUT_FILENO));
    checked(posix_spawn_file_actions_addclose(&actions.value,input.value));
    checked(posix_spawn_file_actions_addclose(&actions.value,output.value));
    std::vector<char*> argv;for(auto& value:arguments)argv.push_back(value.data());argv.push_back(nullptr);
    pid_t child{};
    const int error=posix_spawnp(&child,argv[0],&actions.value,nullptr,argv.data(),environ);
    if(error==ENOENT)return {127,{}};
    if(error)throw std::runtime_error(std::string("Cannot start desktop dialog: ")+std::strerror(error));
    output.reset();
    std::string bytes;std::array<char,4096> buffer{};
    bool oversized=false,read_error=false;
    for(;;) {
        const auto count=read(input.value,buffer.data(),buffer.size());
        if(count<0 && errno==EINTR)continue;
        if(count<0){read_error=true;break;}
        if(!count)break;
        if(bytes.size()+size_t(count)>65536)oversized=true;
        else if(!oversized)bytes.append(buffer.data(),size_t(count));
    }
    input.reset();
    int status{};pid_t reaped{};
    do{reaped=waitpid(child,&status,0);}while(reaped<0 && errno==EINTR);
    if(reaped!=child || read_error || oversized)throw std::runtime_error("Invalid desktop dialog output");
    return {WIFEXITED(status)?WEXITSTATUS(status):128,std::move(bytes)};
}
bool finished(const Result& result){return result.code==0 || result.code==1 || result.code==2;}
constexpr const char* missing="Install kdialog or zenity for file selection and confirmation dialogs. "
    "You can also select a ROM with --rom. No save replacement was authorized.\n";
}
fs::path linux_named_rom() {
    const auto* value=std::getenv("SRW64_ROM");return value && *value?fs::path(value):fs::path{};
}
DesktopUi linux_desktop_ui(std::function<void(const std::string&)> fallback) {
    return {
        []() -> std::optional<fs::path> {
            auto result=dialog({"kdialog","--title","Marchwind64 — Select ROM (Japan, Rev 0)",
                "--getopenfilename",fs::current_path().string(),"*.z64 *.v64 *.n64|N64 ROM"});
            if(!finished(result))result=dialog({"zenity","--file-selection","--title=Marchwind64 — Select ROM (Japan, Rev 0)",
                "--file-filter=N64 ROM | *.z64 *.v64 *.n64"});
            if(!finished(result)){std::fputs(missing,stderr);throw DesktopPickerUnavailable();}
            if(result.code!=0)return std::nullopt;
            if(!result.output.empty() && result.output.back()=='\n')result.output.pop_back();
            if(!result.output.empty() && result.output.back()=='\r')result.output.pop_back();
            if(result.output.empty() || result.output.find_first_of(std::string("\0\r\n",3))!=std::string::npos ||
               !fs::path(result.output).is_absolute())throw std::runtime_error("Invalid ROM picker result");
            return fs::path(result.output);
        },
        [fallback](const std::string& message) {
            std::fprintf(stderr,"Marchwind64: %s\n",message.c_str());
            bool shown=false;
            try {
                auto result=dialog({"kdialog","--title","Marchwind64","--error",message});
                if(!finished(result))result=dialog({"zenity","--error","--no-markup","--title=Marchwind64","--text="+message});
                shown=finished(result);
            }catch(const std::exception&){} // The error is already available on stderr/console.log.
            if(!shown && fallback)fallback(message);
        },
        [](const std::string& message,const std::string& accept,const std::string& cancel) {
            auto result=dialog({"kdialog","--title","Marchwind64","--warningcontinuecancel",message,
                "--continue-label",accept,"--cancel-label",cancel});
            if(!finished(result))result=dialog({"zenity","--question","--default-cancel","--no-markup",
                "--title=Marchwind64","--text="+message,"--ok-label="+accept,"--cancel-label="+cancel});
            if(!finished(result)){std::fputs(missing,stderr);return false;}
            return result.code==0;
        }
    };
}
}
