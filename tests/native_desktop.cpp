#include "app/desktop.hpp"
#include "app/sha256.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
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
    std::vector<std::string> errors;
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
        },[this](const std::string& message){errors.push_back(message);}};
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
void relocated_working_directory_and_lock() {
    Fixture f;f.options.rom=f.rom;
    const auto previous=fs::current_path();fs::create_directories(f.root/"unrelated");fs::current_path(f.root/"unrelated");
    try {check(f.run()==0,"unrelated cwd failed");}catch(...){fs::current_path(previous);throw;}
    fs::current_path(previous);
    Session held(f.options);
    check(f.run()==2,"second instance bypassed session lock");
}
}
int main() {
    try {cancel_and_resume();missing_and_wrong_rom();errors_never_retry_host_or_replace_progress();relocated_working_directory_and_lock();
        std::cout<<checks<<" desktop checks passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
