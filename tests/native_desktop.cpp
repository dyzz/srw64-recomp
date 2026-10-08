#include "app/desktop.hpp"
#include "app/sha256.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif
using namespace srw64::app;
namespace {
unsigned checks{};
void check(bool value, const char* label) { ++checks; if (!value) throw std::runtime_error(label); }
std::string utf8(const fs::path& path) {
    const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};
}
struct Fixture {
    fs::path root, rom;
    Options options;
    std::string hash;
    std::vector<std::optional<fs::path>> selections;
    std::vector<std::string> errors, questions;
    bool end_holder{};
    size_t picked{};
    unsigned launches{};
    Fixture() {
        root=fs::temp_directory_path()/("srw64-desktop-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
        rom=root/fs::path(u8"原始 ROM.z64");
        atomic_write(rom,"synthetic ROM data");hash=sha256_file(rom);options.user_dir=root/"user";
    }
    ~Fixture(){std::error_code e;fs::remove_all(root,e);}
    DesktopUi ui() {
        return {[this]() -> std::optional<fs::path> {
            check(picked<selections.size(),"unexpected picker");return selections.at(picked++);
        },[this](const std::string& message){errors.push_back(message);},
        [this](const std::string& message,const std::string&,const std::string&){questions.push_back(message);return end_holder;}};
    }
    int run(bool force=false, bool failure=false) {
        return run_desktop(options,hash,ui(),[&](const Options& selected,const DesktopReady& ready){
            ++launches;
            check(selected.rom==fs::canonical(rom),"wrong selected ROM");
            check(!selected.new_game,"desktop silently requested new game");
            check(selected.language==options.language && selected.mute==options.mute,"options not preserved");
            // Exercise the REAL Session lock and the same ready ordering as the
            // production run_standalone callback, without executing guest code.
            Session session(selected);
            if(failure)throw std::runtime_error("synthetic import failure");
            ready();
            check(read_text(selected.user_dir/"last-rom.txt",16384)==utf8(selected.rom),"ROM path not remembered");
            return 0;
        },force);
    }
};
void cancel_and_resume() {
    Fixture f;f.selections={std::nullopt};
    check(f.run()==0 && f.launches==0,"cancel launched game");
    check(!fs::exists(f.options.user_dir),"cancel wrote user directory");
    f.selections.push_back(f.rom);f.options.language="zh-Hans";f.options.mute=true;
    check(f.run()==0 && f.launches==1,"first selection failed");
    check(f.run()==0 && f.picked==2 && f.launches==2,"remembered ROM reopened picker");
    const auto saved=read_text(f.options.user_dir/"last-rom.txt",16384);
    f.selections.push_back(std::nullopt);
    check(f.run(true)==0 && f.launches==2,"forced picker cancel launched game");
    check(read_text(f.options.user_dir/"last-rom.txt",16384)==saved,"cancel changed preference");
}
void missing_and_wrong_rom() {
    Fixture f;
    fs::create_directories(f.options.user_dir);
    atomic_write(f.options.user_dir/"last-rom.txt",utf8(f.root/"moved.z64"));
    const auto wrong=f.root/"wrong.z64";atomic_write(wrong,"wrong ROM");
    f.selections={wrong,f.rom};
    check(f.run()==0 && f.launches==1,"ROM retry failed");
    check(f.errors.size()==2 && f.picked==2,"missing/bad ROM not reported");
    // Corrupt preferences do not get treated as a path relative to the app.
    atomic_write(f.options.user_dir/"last-rom.txt","relative.z64");
    f.selections.push_back(std::nullopt);
    check(f.run()==0 && f.launches==1,"relative preference launched game");
    atomic_write(f.options.user_dir/"last-rom.txt",std::string("bad\0path",8));
    f.selections.push_back(std::nullopt);
    check(f.run()==0 && f.launches==1,"NUL preference launched game");
}
void errors_never_retry_host_or_replace_progress() {
    Fixture f;f.selections={f.rom};
    check(f.run(false,true)==2,"import error hidden");
    check(f.launches==1 && f.picked==1,"import error retried bootstrap");
    check(!fs::exists(f.options.user_dir/"last-rom.txt"),"failed import remembered ROM");
    check(!fs::exists(f.options.user_dir/"last-session.txt"),"failed import committed progress");
    f.options.rom=f.rom;
    check(run_desktop(f.options,f.hash,f.ui(),[&](const Options& o,const DesktopReady& ready){
        Session session(o);ready();return 7;
    })==7,"host error code changed");
    check(!fs::exists(f.options.user_dir/"last-session.txt"),"failed host committed progress");
    const auto pointer="prior-save-pointer";
    atomic_write(f.options.user_dir/"last-session.txt",pointer);
    check(f.run()==2,"corrupt save pointer ignored");
    check(read_text(f.options.user_dir/"last-session.txt",128)==pointer,"corrupt pointer was reset");
}
void launcher_rom_order() {
    // Marchwind64.cmd's order: SRW64_ROM, then each name in the user directory before the
    // game's folder; a directory with a ROM's name is not one.
    Fixture f;
    const auto user=f.root/"data",game=f.root/fs::path(u8"游戏");
    fs::create_directories(user);fs::create_directories(game);
    check(launcher_rom({},user,game).empty(),"ROM found where there is none");
    fs::create_directories(user/"rom.z64");
    atomic_write(game/"rom.v64","v");
    check(launcher_rom({},user,game)==game/"rom.v64","directory taken for a ROM");
    atomic_write(user/"rom.n64","n");
    check(launcher_rom({},user,game)==user/"rom.n64","user directory not before the game's");
    atomic_write(game/"rom.z64","z");
    check(launcher_rom({},user,game)==game/"rom.z64",".z64 not before .n64");
    fs::remove(user/"rom.z64");atomic_write(user/"rom.z64","z");
    check(launcher_rom(f.root/"missing.z64",user,game)==user/"rom.z64","missing SRW64_ROM used");
    check(launcher_rom(f.rom,user,game)==f.rom,"SRW64_ROM not first");
}
void relocated_working_directory_and_lock() {
    Fixture f;f.options.rom=f.rom;
    const auto previous=fs::current_path();fs::create_directories(f.root/"unrelated");fs::current_path(f.root/"unrelated");
    try {check(f.run()==0,"unrelated cwd failed");}catch(...){fs::current_path(previous);throw;}
    fs::current_path(previous);
    Session held(f.options);
    atomic_write(f.options.user_dir/"presentation.json","{\n  \"locale\": \"en\"\n}\n");
    const auto shown=f.errors.size();
    check(f.run()==2,"second instance bypassed session lock");
    check(f.errors.size()==shown+1 && f.errors.back()==play_lock_message("en"),"lock message not in the saved language");
    check(f.questions.empty(),"offered to end this very process");
    check(play_lock_message("ja").starts_with("Marchwind64 を") && play_lock_message("fr")==play_lock_message("zh-Hans"),
          "lock message languages");
}
// Another process holding the user directory: Cancel leaves it, the other button ends it
// and the game starts. The holder is this program again (--hold), as another game would be.
void ending_the_holder(const char* self) {
    Fixture f;f.options.rom=f.rom;
    fs::create_directories(f.options.user_dir);
#ifdef _WIN32
    std::wstring command=L"\""+fs::path(self).wstring()+L"\" --hold \""+f.options.user_dir.wstring()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof startup;PROCESS_INFORMATION child{};
    check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child)!=0,"holder not started");
    CloseHandle(child.hThread);
    const unsigned long child_id=child.dwProcessId;
    const auto alive=[&]{return WaitForSingleObject(child.hProcess,0)==WAIT_TIMEOUT;};
#else
    (void)self;
    const pid_t child=fork();
    if (child==0) { try { UserLock held(f.options.user_dir); for (;;) pause(); } catch (...) {} _exit(1); }
    const unsigned long child_id=(unsigned long)child;
    const auto alive=[&]{return kill(child,0)==0;};
#endif
    std::optional<PlayLockHolder> holder;
    for (int i=0;i<200 && !(holder=play_lock_holder(f.options.user_dir));++i) std::this_thread::sleep_for(std::chrono::milliseconds(25));
    check(holder && holder->pid==child_id,"lock holder not recorded");
    check(f.run()==0 && f.launches==0 && f.questions.size()==1,"declined question started the game");
    check(f.questions.back()==play_lock_question("zh-Hans").message && alive(),"declining ended the holder");
    f.end_holder=true;
    check(f.run()==0 && f.launches==1 && f.questions.size()==2,"ending the holder did not start the game");
#ifdef _WIN32
    check(WaitForSingleObject(child.hProcess,5000)==WAIT_OBJECT_0,"holder not ended");
    CloseHandle(child.hProcess);
#else
    int status=0;
    check(waitpid(child,&status,0)==child && WIFSIGNALED(status),"holder not ended");
#endif
}
}
int main(int argc, char** argv) {
    if (argc==3 && std::string_view(argv[1])=="--hold") {   // the other game in ending_the_holder
        UserLock held{fs::path(argv[2])};
        for (;;) std::this_thread::sleep_for(std::chrono::seconds(60));
    }
    try {cancel_and_resume();missing_and_wrong_rom();errors_never_retry_host_or_replace_progress();launcher_rom_order();relocated_working_directory_and_lock();ending_the_holder(argv[0]);
        std::cout<<checks<<" desktop checks passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
