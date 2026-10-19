#include "app/desktop.hpp"
#include "app/sha256.hpp"
#include "app/user_sync.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace srw64::app;
namespace {
unsigned checks{};
void check(bool value,const char* label) { ++checks;if(!value)throw std::runtime_error(label); }
std::string utf8(const fs::path& path) {
    const auto bytes=path.u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
const std::string rom_bytes="synthetic portable ROM, not game data";
std::string rom_hash() { Sha256 hash;hash.update({reinterpret_cast<const uint8_t*>(rom_bytes.data()),rom_bytes.size()});return hash.finish(); }
const std::string settings="{\"schema\":\"srw64.presentation-settings.v1\",\"locale\":\"ja\",\"show_fps\":true}";
struct Temp {
    fs::path root=fs::temp_directory_path()/("marchwind-portable-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){fs::create_directories(root);}
    ~Temp(){std::error_code error;fs::remove_all(root,error);}
};
EnvironmentLookup no_appdata=[](const char*) -> std::string { throw std::runtime_error("portable accessed AppData"); };
void seed(const fs::path& root) {
    fs::create_directories(root/"roms");
    fs::create_directories(root/"user-data");
    const auto rom=root/"roms"/fs::path(u8"日本語 ROM.z64");
    atomic_write(rom,rom_bytes);
    auto options=prepare_windows_launch({},root,[&](const char*){return utf8(root.parent_path()/"AppData");});
    check(options.language.empty() && options.first_language=="en","portable first launch not English");
    options.rom=rom;
    bool launched=false;
    const DesktopUi ui{[]() -> std::optional<fs::path> {throw std::runtime_error("unexpected picker");},
                       [](const std::string& error){throw std::runtime_error(error);},{}};
    check(run_desktop(options,rom_hash(),ui,[&](const Options& chosen,const DesktopReady& ready){
        Session session(chosen);ready();launched=true;
        atomic_write(chosen.user_dir/"presentation.json",settings);
        atomic_write(chosen.user_dir/"saves/keep.txt","existing progress");
        return 0;
    })==0 && launched,"portable seed failed");
    check(read_text(options.user_dir/"last-rom.txt",16384).starts_with("srw64.rom-relative.v1\n"),"portable ROM persisted absolutely");
}
void resume(const fs::path& root) {
    auto options=prepare_windows_launch({},root,[&](const char*){return utf8(root.parent_path()/"AppData");});
    check(options.language.empty(),"saved language overwritten by first-run default");
    bool launched=false;
    const DesktopUi ui{[]() -> std::optional<fs::path> {throw std::runtime_error("moved ROM opened picker");},
                       [](const std::string& error){throw std::runtime_error(error);},{}};
    check(run_desktop(options,rom_hash(),ui,[&](const Options& chosen,const DesktopReady& ready){
        check(chosen.rom==fs::canonical(root/"roms"/fs::path(u8"日本語 ROM.z64")),"ROM still uses previous location");
        Session session(chosen);ready();launched=true;
        check(read_text(chosen.user_dir/"presentation.json",65536)==settings,"settings changed on relocation");
        check(read_text(chosen.user_dir/"saves/keep.txt",128)=="existing progress","progress lost on relocation");
        return 0;
    })==0 && launched,"portable resume failed");
}
void policy() {
    Temp f;
    const auto game=f.root/"game",appdata=f.root/"AppData";
    fs::create_directories(game);
    const EnvironmentLookup lookup=[&](const char* key){check(std::string_view(key)=="LOCALAPPDATA","unexpected environment key");return utf8(appdata);};
    auto options=prepare_windows_launch({},game,lookup);
    check(options.user_dir==appdata/"SRW64Recomp" && !options.sync_system,"installed directory changed");
    check(options.first_language=="zh-Hans","installed first language changed");
    fs::create_directories(options.user_dir/"saves");
    atomic_write(options.user_dir/"presentation.json",settings);
    atomic_write(options.user_dir/"saves/keep.txt","AppData progress");
    atomic_write(options.user_dir/"rom.z64",rom_bytes);atomic_write(game/"rom.z64",rom_bytes);
    check(prepare_windows_launch({},game,lookup).rom==options.user_dir/"rom.z64","installed ROM precedence changed");
    atomic_write(game/"user-data","wrong type");
    bool bad_directory=false;
    try{prepare_windows_launch({},game,lookup);}catch(const std::exception&){bad_directory=true;}
    check(bad_directory,"invalid local directory silently fell back to AppData");
    fs::remove(game/"user-data");fs::create_directory(game/"user-data");
    const auto portable=prepare_windows_launch({},game,lookup);
    check(portable.user_dir==game/"user-data" && portable.rom==game/"rom.z64","portable borrowed AppData ROM");
    check(portable.sync_system && portable.sync_user_dir==options.user_dir,"local mode did not select AppData synchronization");
    check(!fs::exists(portable.user_dir/"presentation.json"),"policy copied settings before ROM selection");
    Options explicit_options;explicit_options.user_dir=game/"custom data";
    fs::create_directories(explicit_options.user_dir);atomic_write(explicit_options.user_dir/"presentation.json",settings);
    explicit_options.language="ja";explicit_options.rom=f.root/"explicit.z64";
    auto explicit_result=prepare_windows_launch(explicit_options,game,no_appdata,game/"rom.z64");
    check(!explicit_result.sync_system && explicit_result.user_dir==explicit_options.user_dir &&
          explicit_result.language=="ja" && explicit_result.rom==explicit_options.rom,"explicit options not isolated");
    explicit_options.language.clear();
    check(prepare_windows_launch(explicit_options,game,no_appdata).language.empty(),"custom saved locale overridden");
    for(const auto key:{"--export-save","--import-save"}) {
        const std::string_view args[]={key,"card.srm"};
        const auto save=prepare_windows_launch(parse_options(args,true),game,lookup);
        check(save.language.empty() && save.rom.empty() && !save.sync_system && save.user_dir==options.user_dir,"save-only behavior changed");
    }
    const std::string_view args[]={"--user-dir","custom relative","--language","en","--mute"};
    const auto parsed=parse_options(args,true);
    check(parsed.rom.empty() && parsed.user_dir==fs::absolute("custom relative") && parsed.language=="en" && parsed.mute,"Windows argument parsing failed");
    bool rejected=false;try{parse_options(args);}catch(const std::exception&){rejected=true;}
    check(rejected,"other platforms no longer require --rom");
    check(read_text(options.user_dir/"presentation.json",65536)==settings &&
          read_text(options.user_dir/"saves/keep.txt",128)=="AppData progress","policy modified installed data");
}
void relocation_and_legacy() {
    Temp f;const auto before=f.root/fs::path(u8"游戏 (old)"),after=f.root/fs::path(u8"游戏 (new)");
    seed(before);fs::rename(before,after);
    const auto previous=fs::current_path();fs::current_path(f.root);
    try{resume(after);}catch(...){fs::current_path(previous);throw;}
    fs::current_path(previous);
    // Older absolute preferences still work, and an external ROM stays absolute.
    const auto external=f.root/"external.z64";atomic_write(external,rom_bytes);
    const auto data=after/"user-data";
    atomic_write(data/"last-rom.txt",utf8(external));
    Options custom;custom.user_dir=data;
    auto options=prepare_windows_launch(custom,after,no_appdata);
    const DesktopUi ui{[]() -> std::optional<fs::path>{throw std::runtime_error("legacy ROM opened picker");},
                       [](const std::string& error){throw std::runtime_error(error);},{}};
    check(run_desktop(options,rom_hash(),ui,[&](const Options& chosen,const DesktopReady& ready){
        Session session(chosen);ready();return 0;
    })==0,"legacy absolute preference failed");
    check(read_text(data/"last-rom.txt",16384)==utf8(external),"external ROM made portable");
    // Versioned malformed preferences fail before reaching the host.
    for(const auto bad:{"srw64.rom-relative.v1\n", "srw64.rom-relative.v1\nrom.z64\nextra", "plain-relative.z64"}) {
        atomic_write(data/"last-rom.txt",bad);unsigned errors=0;
        const DesktopUi cancel{[](){return std::optional<fs::path>{};},[&](const std::string&){++errors;},{}};
        check(run_desktop(options,rom_hash(),cancel,[](const Options&,const DesktopReady&)->int{throw std::runtime_error("invalid preference launched");})==0 && errors==1,"invalid preference did not offer safe cancellation");
        check(read_text(data/"presentation.json",65536)==settings,"cancel changed settings");
    }
}
void synchronization() {
    Temp f;const auto local=f.root/"local",appdata=f.root/"appdata";
    fs::create_directories(local);fs::create_directories(appdata/"saves/slots");
    atomic_write(appdata/"presentation.json",settings);
    atomic_write(appdata/"saves/cartridge.sram","old card");
    atomic_write(appdata/"saves/slots/003.rec","old extended slot");
    atomic_write(appdata/"last-rom.txt","machine-specific absolute ROM");
    fs::create_directories(appdata/"sessions");atomic_write(appdata/"sessions/do-not-copy","runtime cache");
    import_missing_user_data(local,appdata);
    check(read_text(local/"presentation.json",65536)==settings &&
          read_text(local/"saves/cartridge.sram",128)=="old card","first local import failed");
    check(!fs::exists(local/"last-rom.txt") && !fs::exists(local/"sessions"),"import copied machine-specific state");
    atomic_write(local/"presentation.json","local settings");atomic_write(local/"saves/cartridge.sram","local card");
    import_missing_user_data(local,appdata);
    check(read_text(local/"presentation.json",128)=="local settings" &&
          read_text(local/"saves/cartridge.sram",128)=="local card","AppData overrode local precedence");
    fs::remove(local/"saves/slots/003.rec"); // deletion is part of the local save snapshot
    fs::create_directories(local/"campaigns/sample/saves");
    atomic_write(local/"campaigns/sample/saves/slot.rec","campaign progress");
    unsigned questions=0;
    const auto decline=[&](const std::string& message,const std::string&,const std::string&){
        ++questions;check(message.find(utf8(appdata))!=std::string::npos,"conflict prompt missing destination");return false;
    };
    check(!sync_user_data(local,appdata,"en",decline) && questions==1,"different saves overwritten without consent");
    check(read_text(appdata/"saves/cartridge.sram",128)=="old card" &&
          read_text(appdata/"presentation.json",65536)==settings &&
          fs::exists(appdata/"saves/slots/003.rec"),"declining changed AppData");
    check(!fs::exists(appdata/"sync-backups"),"declining created replacement backup");
    const auto accept=[&](const std::string&,const std::string&,const std::string&){++questions;return true;};
    check(sync_user_data(local,appdata,"ja",accept) && questions==2,"confirmed sync failed");
    check(read_text(appdata/"saves/cartridge.sram",128)=="local card" &&
          read_text(appdata/"presentation.json",128)=="local settings","local data not mirrored");
    check(!fs::exists(appdata/"saves/slots/003.rec"),"deleted slot resurrected during sync");
    check(read_text(appdata/"campaigns/sample/saves/slot.rec",128)=="campaign progress","campaign progress missing");
    const auto backup=fs::directory_iterator(appdata/"sync-backups")->path();
    check(read_text(backup/"saves/cartridge.sram",128)=="old card" &&
          read_text(backup/"saves/slots/003.rec",128)=="old extended slot" &&
          read_text(backup/"presentation.json",65536)==settings,"replaced snapshot not backed up");
    check(sync_user_data(local,appdata,"en",decline) && questions==2,"identical saves prompted again");
    check(read_text(appdata/"last-rom.txt",128)=="machine-specific absolute ROM","sync changed AppData ROM preference");
    // A busy source or target cannot be copied while the other game writes it.
    {
        UserLock held(appdata);bool refused=false;
        try{sync_user_data(local,appdata,"en",accept);}catch(const PlayLockHeld&){refused=true;}
        check(refused && questions==2,"sync ignored AppData's live lock");
    }
    {
        UserLock held(local);bool refused=false;
        try{import_missing_user_data(local,appdata);}catch(const PlayLockHeld&){refused=true;}
        check(refused,"import ignored local live lock");
    }
    // A backup allocation failure must leave both snapshots intact.
    fs::remove_all(appdata/"sync-backups");atomic_write(appdata/"sync-backups","not a directory");
    atomic_write(local/"saves/cartridge.sram","next card");bool failed=false;
    try{sync_user_data(local,appdata,"en",accept);}catch(const std::exception&){failed=true;}
    check(failed && read_text(appdata/"saves/cartridge.sram",128)=="local card" &&
          read_text(local/"saves/cartridge.sram",128)=="next card","failed staging damaged saved progress");
    fs::remove(appdata/"sync-backups");
    // Symlinked save entries are refused without following or modifying the target.
    std::error_code error;fs::create_symlink(appdata/"saves/cartridge.sram",local/"saves/link",error);
    if(!error) {
        bool rejected=false;try{sync_user_data(local,appdata,"en",accept);}catch(const std::exception&){rejected=true;}
        check(rejected && read_text(appdata/"saves/cartridge.sram",128)=="local card","symlink followed during sync");
    }
}
void legacy_appdata_backup() {
    Temp f;const auto local=f.root/"local",appdata=f.root/"appdata";
    fs::create_directories(local/"saves");fs::create_directories(appdata/"sessions/abc-123");
    const auto old=appdata/"sessions/abc-123";
    atomic_write(old/"save.bin","legacy progress");atomic_write(old/"save.sha256",sha256_file(old/"save.bin")+"\n");
    atomic_write(appdata/"last-session.txt","abc-123\n");
    atomic_write(local/"saves/cartridge.sram","local progress");unsigned questions=0;
    check(!sync_user_data(local,appdata,"en",[&](const std::string&,const std::string&,const std::string&){++questions;return false;}),
          "legacy-only AppData was replaced without confirmation");
    check(questions==1 && !fs::exists(appdata/"saves"),"declining changed legacy-only AppData");
    check(sync_user_data(local,appdata,"en",[](const std::string&,const std::string&,const std::string&){return true;}),"legacy exit sync failed");
    const auto backup=fs::directory_iterator(appdata/"sync-backups")->path();
    check(read_text(backup/"last-session.txt",128)=="abc-123\n" &&
          read_text(backup/"sessions/abc-123/save.bin",128)=="legacy progress","legacy-only saves not backed up");
    check(read_text(old/"save.bin",128)=="legacy progress","sync removed original legacy session");
}
void exit_synchronization() {
    Temp f;const auto game=f.root/"game",appdata=f.root/"AppData/SRW64Recomp";
    fs::create_directories(game/"user-data");fs::create_directories(appdata/"saves");
    atomic_write(game/"rom.z64",rom_bytes);atomic_write(appdata/"saves/progress.txt","prior progress");
    const EnvironmentLookup lookup=[&](const char*){return utf8(appdata.parent_path());};
    auto options=prepare_windows_launch({},game,lookup);
    unsigned prompts=0,errors=0;bool allow=false;
    const DesktopUi ui{[]() -> std::optional<fs::path>{throw std::runtime_error("unexpected picker");},
        [&](const std::string&){++errors;},[&](const std::string&,const std::string&,const std::string&){++prompts;return allow;}};
    const auto launch=[&](const Options& selected,const DesktopReady& ready) {
        Session session(selected);ready();
        check(selected.language=="en","new portable language missing");
        atomic_write(selected.user_dir/"saves/progress.txt","portable progress");return 0;
    };
    check(run_desktop(options,rom_hash(),ui,launch)==0 && prompts==1 && errors==0,"exit did not offer conflict choice");
    check(read_text(appdata/"saves/progress.txt",128)=="prior progress","refused exit sync overwrote progress");
    allow=true;
    check(run_desktop(options,rom_hash(),ui,launch)==0 && prompts==2,"exit confirmation failed");
    check(read_text(appdata/"saves/progress.txt",128)=="portable progress","confirmed exit did not sync");
    // Host failure skips synchronization even if there are changed local files.
    check(run_desktop(options,rom_hash(),ui,[&](const Options& selected,const DesktopReady& ready) {
        Session session(selected);ready();atomic_write(selected.user_dir/"saves/progress.txt","failed host");return 7;
    })==7 && prompts==2 && errors==1,"failed host synchronized or lost its exit status");
    check(read_text(appdata/"saves/progress.txt",128)=="portable progress","failed host replaced AppData");
    std::unique_ptr<UserLock> busy;
    check(run_desktop(options,rom_hash(),ui,[&](const Options& selected,const DesktopReady& ready) {
        Session session(selected);ready();atomic_write(selected.user_dir/"saves/progress.txt","committed local progress");
        busy=std::make_unique<UserLock>(appdata);return 0;
    })==0 && errors==2,"sync failure changed the successful host result");
    check(read_text(game/"user-data/saves/progress.txt",128)=="committed local progress" &&
          read_text(appdata/"saves/progress.txt",128)=="portable progress","busy AppData lost committed local progress");
    busy.reset();
    const auto offline=prepare_windows_launch({},game,no_appdata);
    check(offline.sync_system && offline.sync_user_dir.empty(),"missing AppData disabled local mode");
    check(run_desktop(offline,rom_hash(),ui,launch)==0 && errors==3,"offline portable play failed instead of reporting skipped sync");
}

}
int main(int argc,char** argv) {
    try {
        // Invoked by the Windows-only CMD forwarding test, as its fake game EXE.
        if(argc>1 && std::string_view(argv[1])=="--play") {
            const char* output=std::getenv("MARCHWIND_TEST_ARGS");
            if(!output)throw std::runtime_error("missing argument capture path");
            std::string bytes;for(int i=1;i<argc;++i)bytes+=std::string(argv[i])+"\n";
            atomic_write(fs::path(output),bytes);return 37;
        }
        if(argc==3 && std::string_view(argv[1])=="--seed")seed(fs::absolute(argv[2]));
        else if(argc==3 && std::string_view(argv[1])=="--resume")resume(fs::absolute(argv[2]));
        else{policy();relocation_and_legacy();synchronization();legacy_appdata_backup();exit_synchronization();}
        std::cout<<checks<<" Windows portable policy/controller checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
