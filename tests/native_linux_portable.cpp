#include "app/desktop.hpp"
#include "app/sha256.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
using namespace srw64::app;
namespace {
unsigned checks{};
void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
struct Temp {
    fs::path root=fs::temp_directory_path()/("marchwind-linux-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){fs::create_directories(root);}
    ~Temp(){std::error_code error;fs::remove_all(root,error);}
};
void directory_policy() {
    Temp f;const auto game=f.root/"game";fs::create_directory(game);
    std::string xdg=(f.root/"xdg").string(),home=(f.root/"home").string();
    const EnvironmentLookup lookup=[&](const char* key){
        return std::string_view(key)=="XDG_DATA_HOME"?xdg:std::string_view(key)=="HOME"?home:std::string{};
    };
    auto options=prepare_linux_launch({},game,lookup);
    check(options.user_dir==f.root/"xdg/srw64-recomp" && !options.sync_system,"absolute XDG default changed");
    check(options.first_language==system_language(),"installed Linux first launch not in the system's language");
    xdg="relative-xdg";
    check(prepare_linux_launch({},game,lookup).user_dir==f.root/"home/.local/share/srw64-recomp","relative XDG not ignored");
    xdg.clear();
    check(prepare_linux_launch({},game,lookup).user_dir==f.root/"home/.local/share/srw64-recomp","HOME fallback changed");
    xdg=(f.root/"xdg").string();
    fs::create_directory(game/"user-data");
    options=prepare_linux_launch({},game,lookup);
    check(options.user_dir==game/"user-data" && options.sync_system && options.first_language==system_language(),"Linux local directory not preferred");
    check(options.sync_user_dir==f.root/"xdg/srw64-recomp","Linux sync target is not XDG");
    check(!fs::exists(options.sync_user_dir),"policy wrote system data before launch");
    Options explicit_options;explicit_options.user_dir=game/"custom";explicit_options.language="ja";
    const EnvironmentLookup forbidden=[](const char*)->std::string{throw std::runtime_error("explicit user dir consulted environment");};
    const auto custom=prepare_linux_launch(explicit_options,game,forbidden);
    check(custom.user_dir==explicit_options.user_dir && custom.language=="ja" && !custom.sync_system,"explicit Linux directory not isolated");
    const std::string_view args[]={"--export-save","card.srm"};
    options=prepare_linux_launch(parse_options(args,true),game,lookup);
    check(options.user_dir==f.root/"xdg/srw64-recomp" && !options.sync_system && options.language.empty(),"save-only Linux behavior changed");
    // Linux HOME/XDG ancestors may intentionally be symlinks (e.g. an immutable desktop).
    fs::create_directory(f.root/"real-data");fs::create_directory_symlink(f.root/"real-data",f.root/"data-alias");
    xdg=(f.root/"data-alias/xdg").string();
    check(prepare_linux_launch({},game,lookup).sync_user_dir==f.root/"real-data/xdg/srw64-recomp",
          "Linux system-data alias was not resolved");
    xdg=(f.root/"xdg").string();
    // A broken local storage selection must not switch to a different save library.
    fs::remove(game/"user-data");atomic_write(game/"user-data","not a directory");
    bool rejected=false;try{prepare_linux_launch({},game,lookup);}catch(const std::exception&){rejected=true;}
    check(rejected,"invalid Linux local directory silently fell back");
    fs::remove(game/"user-data");fs::create_directory_symlink(f.root/"missing",game/"user-data");
    rejected=false;try{prepare_linux_launch({},game,lookup);}catch(const std::exception&){rejected=true;}
    check(rejected,"broken local symlink silently fell back");
}
void move_and_sync() {
    Temp f;auto game=f.root/fs::path(u8"游戏 (portable)");
    auto rom=game/"roms"/fs::path(u8"日本語 dump.z64");
    fs::create_directories(rom.parent_path());fs::create_directory(game/"user-data");
    atomic_write(rom,"synthetic Linux ROM, not game data");const auto hash=sha256_file(rom);
    const EnvironmentLookup lookup=[&](const char* key){return key==std::string_view("XDG_DATA_HOME")?(f.root/"xdg").string():(f.root/"home").string();};
    const auto system=f.root/"xdg/srw64-recomp";
    fs::create_directories(system/"saves");
    const std::string settings="{\"schema\":\"srw64.presentation-settings.v1\",\"locale\":\"ja\",\"show_fps\":true}";
    atomic_write(system/"presentation.json",settings);atomic_write(system/"saves/progress.txt","system progress");
    unsigned picks=0,prompts=0;bool accept=false;
    DesktopUi ui{[&](){++picks;return std::optional(rom);},
        [](const std::string& message){throw std::runtime_error(message);},
        [&](const std::string& message,const std::string&,const std::string&){
            ++prompts;check(message.find(system.string())!=std::string::npos,"Linux conflict prompt missing target");return accept;
        }};
    const auto launch=[&](const Options& selected,const DesktopReady& ready){
        check(selected.language.empty(),"English default overrode imported Japanese");
        check(selected.rom==fs::canonical(rom),"relocated ROM still uses old path");
        Session session(selected);ready();
        check(read_text(selected.user_dir/"presentation.json",65536)==settings,"Linux settings not restored");
        atomic_write(selected.user_dir/"saves/progress.txt","local progress");return 0;
    };
    auto options=prepare_linux_launch({},game,lookup);
    check(run_desktop(options,hash,ui,launch)==0 && picks==1 && prompts==1,"Linux first portable launch failed");
    check(read_text(system/"saves/progress.txt",128)=="system progress","refused Linux sync replaced progress");
    check(read_text(options.user_dir/"last-rom.txt",16384).starts_with("srw64.rom-relative.v1\n"),"Linux ROM preference is absolute");
    accept=true;
    check(run_desktop(prepare_linux_launch({},game,lookup),hash,ui,launch)==0 && picks==1 && prompts==2,"Linux remembered ROM or confirmation failed");
    check(read_text(system/"saves/progress.txt",128)=="local progress","Linux exit did not mirror saves");
    const auto backup=fs::directory_iterator(system/"sync-backups")->path();
    check(read_text(backup/"saves/progress.txt",128)=="system progress","Linux overwritten save not backed up");
    const auto moved=f.root/"another mount point";fs::rename(game,moved);game=moved;rom=game/"roms"/rom.filename();
    const auto previous=fs::current_path();fs::current_path(f.root);
    try{check(run_desktop(prepare_linux_launch({},game,lookup),hash,ui,launch)==0,"moved Linux bundle failed");}
    catch(...){fs::current_path(previous);throw;}
    fs::current_path(previous);
    check(picks==1 && prompts==2,"moving bundle opened picker or repeated identical-save prompt");
    check(read_text(game/"user-data/presentation.json",65536)==settings,"Linux move lost language");
    auto preference=read_text(game/"user-data/last-rom.txt",16384);
    std::replace(preference.begin(),preference.end(),'/','\\');
    atomic_write(game/"user-data/last-rom.txt",preference);
    check(run_desktop(prepare_linux_launch({},game,lookup),hash,ui,launch)==0 && picks==1,
          "Linux could not read a Windows-relative preference");
}
}
int main(int argc,char** argv) {
    try {
        const std::string_view mode=argc>1?argv[1]:"";
        if(mode=="--play") {
            const auto* capture=std::getenv("MARCHWIND_TEST_ARGS");
            if(!capture)throw std::runtime_error("missing capture file");
            std::string text;for(int i=1;i<argc;++i)text+=std::string(argv[i])+"\n";
            const auto* keyboard=std::getenv("SDL_ENABLE_STEAM_SCREEN_KEYBOARD");
            text+="KEYBOARD="+std::string(keyboard?keyboard:"")+"\n";
            atomic_write(capture,text);return 37;
        }
        if(mode=="--ui-choose") {
            const auto selected=linux_desktop_ui().choose_rom();
            std::cout<<(selected?selected->string():"CANCEL")<<'\n';return 0;
        }
        if(mode=="--ui-confirm" && argc==3)return linux_desktop_ui().confirm(argv[2],"Back up and replace","Keep both")?0:1;
        if(mode=="--ui-error" && argc==3){linux_desktop_ui().show_error(argv[2]);return 0;}
        if(mode=="--named"){std::cout<<linux_named_rom().string()<<'\n';return 0;}
        directory_policy();move_and_sync();
        std::cout<<checks<<" Linux portable checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
