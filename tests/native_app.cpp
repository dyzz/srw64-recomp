#include "app/rom_order.hpp"
#include "app/runtime.hpp"
#include "app/sha256.hpp"
#include "app/sram.hpp"
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
using namespace srw64::app;
namespace {
unsigned checks{};
void check(bool condition,const char* message) { ++checks;if(!condition)throw std::runtime_error(message); }
template<class F> void rejects(F f,const char* message) {
    ++checks;try{f();}catch(const std::exception&){return;}throw std::runtime_error(message);
}
struct Temporary {
    fs::path path;
    Temporary() {
        const auto base=fs::temp_directory_path();
        for(unsigned i=0;i<100;++i) {
            path=base/("srw64-app-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+'-'+std::to_string(i));
            if(fs::create_directory(path))return;
        }
        throw std::runtime_error("No temporary directory");
    }
    ~Temporary(){std::error_code ignored;fs::remove_all(path,ignored);}
};
std::string hash(std::string_view bytes,size_t chunk=65536) {
    Sha256 digest;
    for(size_t i=0;i<bytes.size();i+=chunk) {
        const auto n=std::min(chunk,bytes.size()-i);
        digest.update({reinterpret_cast<const uint8_t*>(bytes.data()+i),n});
    }
    return digest.finish();
}
void hashing() {
    check(hash("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","empty SHA-256");
    check(hash("abc",1)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","abc SHA-256");
    check(hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",7)==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1","two block SHA-256");
    check(hash(std::string(1000000,'a'),19)=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","million a SHA-256");
    Sha256 repeated;const uint8_t a='a';repeated.update({&a,1});
    check(repeated.finish()==repeated.finish(),"digest finish mutated state");
    Temporary temp;
    for(size_t count:{0,1,55,56,57,63,64,65,32768,65537}) {
        const auto bytes=std::string(count,'x');atomic_write(temp.path/"bytes",bytes);
        check(sha256_file(temp.path/"bytes")==hash(bytes,13),"file and incremental hash differ");
    }
    rejects([&]{sha256_file(temp.path/"missing");},"missing hash input accepted");
}
void parser_and_paths() {
    const std::vector<std::string_view> valid={"--rom","a rom.z64","--content","my content","--mute","--resolution-scale","4","--language","ja"};
    const auto options=parse_options(valid);
    check(options.mute && options.resolution_scale==4 && options.language=="ja","CLI flags");
    check(options.rom.is_absolute() && options.content.is_absolute(),"CLI depends on later cwd");
    const std::vector<std::string_view> unicode={"--rom","rom-测试.z64","--content","内容"};
    const auto names=parse_options(unicode);
    check(names.rom.filename()==fs::path(u8"rom-测试.z64") && names.content.filename()==fs::path(u8"内容"),"UTF-8 CLI paths");
    auto reject=[](std::vector<std::string_view> values){rejects([&]{parse_options(values);},"invalid CLI accepted");};
    reject({});
    const std::vector<std::string_view> first_boot={"--rom","x"};
    check(parse_options(first_boot).content.empty(),"first boot must request native import, not use cwd as content");
    reject({"--rom","x","--content","y","--resolution-scale","0"});
    reject({"--rom","x","--content","y","--resolution-scale","8x"});
    reject({"--rom","x","--content","y","--resolution-scale","9"});
    reject({"--rom","x","--content","y","--rules","surprise"});
    reject({"--rom","x","--content","y","--mute","--mute"});
    reject({"--rom","x","--content","y","--unknown","z"});
    reject({"--rom","x","--content","y","--new-game","--import-save","z"});
    const std::vector<std::string_view> campaign={"--rom","x","--campaign","sample.json"};
    check(parse_options(campaign).campaign.is_absolute(),"campaign path depends on later cwd");
    reject({"--rom","x","--campaign","c.json","--import-save","z"});
    reject({"--export-save","x.sra","--campaign","c.json"});
    const std::vector<std::string_view> exporting={"--export-save","card.srm","--user-dir","u"};
    const auto export_options=parse_options(exporting);
    check(export_options.export_save.is_absolute() && export_options.rom.empty(),"export needs no ROM");
    reject({"--export-format","ares"});
    reject({"--export-save","x.sra","--export-format","pj64"});
    reject({"--export-save","x.sra","--rom","y"});
    const std::vector<std::string_view> importing={"--import-save","card.srm"};
    check(parse_options(importing).import_save.is_absolute() && parse_options(importing).rom.empty(),"import alone needs no ROM");
    reject({"--import-save","x.sra","--mute"});
    reject({"--rom","--content","y"});
    std::map<std::string,std::string> env;
    const auto get=[&](const char* key){return env[key];};
    Temporary temp;
    env["HOME"]=temp.path.string();env["LOCALAPPDATA"]=temp.path.string();
    check(default_user_dir(Platform::Windows,get)==temp.path/"SRW64Recomp","Windows user directory");
    check(default_user_dir(Platform::MacOS,get)==temp.path/"Library/Application Support/SRW64Recomp","macOS user directory");
    check(default_user_dir(Platform::Linux,get)==temp.path/".local/share/srw64-recomp","Linux user directory");
    env["XDG_DATA_HOME"]="relative/ignored";
    check(default_user_dir(Platform::Linux,get)==temp.path/".local/share/srw64-recomp","relative XDG path accepted");
    env["XDG_DATA_HOME"]=(temp.path/"xdg").string();
    check(default_user_dir(Platform::Linux,get)==temp.path/"xdg/srw64-recomp","XDG override ignored");
    env.clear();rejects([&]{default_user_dir(Platform::MacOS,get);},"missing HOME accepted");
    rejects([&]{default_user_dir(Platform::Windows,get);},"missing LOCALAPPDATA accepted");
    fs::create_directory(temp.path/"content");
    atomic_write(temp.path/"content/a.txt","a");atomic_write(temp.path/"outside.txt","outside");
    check(content_path(temp.path/"content","a.txt")==fs::canonical(temp.path/"content/a.txt"),"content resolution");
    for(const std::string value:{"../outside.txt","./a.txt","C:/a.txt","\\\\server\\a.txt","/etc/passwd",""})
        rejects([&]{content_path(temp.path/"content",value);},"non-portable content path accepted");
    std::error_code symlink_error;
    fs::create_symlink(temp.path/"outside.txt",temp.path/"content/escape.txt",symlink_error);
    if(!symlink_error)rejects([&]{content_path(temp.path/"content","escape.txt");},"symlink escape accepted");
    atomic_write(temp.path/"replace.txt","before");atomic_write(temp.path/"replace.txt","after");
    check(read_text(temp.path/"replace.txt",5)=="after","atomic replacement");
    rejects([&]{read_text(temp.path/"replace.txt",4);},"oversized text accepted");
}
// A formatted card whose slot 1 holds `tag` bytes under a correct checksum.
std::string card(char tag) {
    std::string bytes(32768,'\0');
    std::copy(sram::magic.begin(),sram::magic.end(),bytes.begin());
    std::fill_n(bytes.begin()+0x12,0x100,tag);
    unsigned sum=0;for(size_t i=0x12;i<0x12+0x1F00;++i)sum+=uint8_t(bytes[i]);
    bytes[0x10]=char(0x80);bytes[0x11]=char(sum);
    return bytes;
}
void sessions() {
    Temporary temp;
    Options options;options.user_dir=temp.path/"player data";
    fs::path completed;
    const auto old_save=card('a');
    {
        Session session(options);
        check(!session.initial_save(),"fresh launch did not start empty");
        check(!fs::exists(session.output_dir()),"probe output directory already exists");
        rejects([&]{Session duplicate(options);},"second game acquired same data lock");
        Options other=options;other.user_dir=temp.path/"different player";Session independent(other);
        check(!independent.initial_save(),"independent data directory not isolated");
        check(!session.commit_save(temp.path/"absent.bin"),"absent save published");
        atomic_write(temp.path/"short.bin","bad");
        rejects([&]{session.commit_save(temp.path/"short.bin");},"short save published");
        atomic_write(temp.path/"blank.bin",std::string(32768,'a'));
        rejects([&]{session.commit_save(temp.path/"blank.bin");},"unformatted save published");
        atomic_write(temp.path/"host.bin",old_save);
        check(session.commit_save(temp.path/"host.bin"),"valid save not published");
        check(read_text(options.user_dir/"saves/cartridge.sram",32768)==old_save,"card not published");
        completed=session.session_dir();
        rejects([&]{session.commit_save(temp.path/"host.bin");},"duplicate commit accepted");
    }
    const auto pointer=read_text(options.user_dir/"last-session.txt",128);
    {
        Session session(options);
        check(session.initial_save().has_value(),"resume lost save");
        check(read_text(*session.initial_save(),32768)==old_save,"resume changed save");
        atomic_write(*session.initial_save(),card('b'));
        check(read_text(completed/"save.bin",32768)==old_save,"game edited immutable history");
        check(read_text(options.user_dir/"saves/cartridge.sram",32768)==old_save,"game edited the card before commit");
        // Simulated failed/aborted host: do not commit.
    }
    check(read_text(options.user_dir/"last-session.txt",128)==pointer,"aborted launch replaced latest save");
    {
        auto fresh=options;fresh.new_game=true;Session session(fresh);
        check(!session.initial_save(),"--new-game restored an old save");
        check(!session.commit_save(session.output_dir()/"missing.bin"),"new game without a save replaced history");
    }
    check(read_text(options.user_dir/"last-session.txt",128)==pointer,"empty new game replaced latest pointer");
    auto damaged=old_save;damaged[0x20]^=1;
    atomic_write(options.user_dir/"saves/cartridge.sram",damaged);
    rejects([&]{Session session(options);},"corrupted save resumed silently");
    {
        auto recovery=options;recovery.import_save=temp.path/"host.bin";Session session(recovery);
        check(read_text(*session.initial_save(),32768)==old_save,"explicit import failed");
        session.commit_save(*session.initial_save());
    }
    {
        // A new game replaces the card but keeps its slot as an extended slot.
        auto fresh=options;fresh.new_game=true;Session session(fresh);
        atomic_write(temp.path/"new.bin",card('n'));
        check(session.commit_save(temp.path/"new.bin"),"new game not published");
        check(read_text(options.user_dir/"saves/cartridge.sram",32768)==card('n'),"new game card not published");
        check(read_text(options.user_dir/"saves/slots/003.rec",0x1F00)==old_save.substr(0x10,0x1F00),"new game lost the old slot");
    }
    {
        // --import-save alone: the card comes in without a game, never while one runs.
        Options only;only.user_dir=temp.path/"importer";only.import_save=temp.path/"new.bin";
        atomic_write(temp.path/"new.bin",card('i'));
        check(import_save(only).starts_with("big-endian"),"import alone");
        check(read_text(only.user_dir/"saves/cartridge.sram",32768)==card('i'),"import alone did not publish");
        Options playing;playing.user_dir=only.user_dir;Session running(playing);
        rejects([&]{import_save(only);},"import alone beside a running game");
    }
    // Before saves/ the latest card was the last session's save.bin.
    Options legacy;legacy.user_dir=temp.path/"older player";
    {
        Session session(legacy);
        atomic_write(temp.path/"host.bin",old_save);session.commit_save(temp.path/"host.bin");
    }
    fs::remove_all(legacy.user_dir/"saves");
    {
        Session session(legacy);
        check(session.initial_save() && read_text(*session.initial_save(),32768)==old_save,"legacy session not migrated");
        check(read_text(legacy.user_dir/"saves/cartridge.sram",32768)==old_save,"migration did not publish the card");
    }
    fs::remove_all(legacy.user_dir/"saves");
    {
        // A new game on an unmigrated directory still keeps the old card's slot.
        auto fresh=legacy;fresh.new_game=true;Session session(fresh);
        atomic_write(temp.path/"new.bin",card('m'));session.commit_save(temp.path/"new.bin");
        check(read_text(legacy.user_dir/"saves/slots/003.rec",0x1F00)==old_save.substr(0x10,0x1F00),"unmigrated new game lost the slot");
    }
    {
        // ... and a damaged card does not stop a new game.
        auto damaged_card=card('m');damaged_card[0x40]^=1;
        atomic_write(legacy.user_dir/"saves/cartridge.sram",damaged_card);
        auto fresh=legacy;fresh.new_game=true;Session session(fresh);
        atomic_write(temp.path/"new.bin",card('k'));
        check(session.commit_save(temp.path/"new.bin"),"new game over a damaged card");
        check(read_text(legacy.user_dir/"saves/cartridge.sram",32768)==card('k'),"new game card after damage");
    }
    fs::remove_all(legacy.user_dir/"saves");
    atomic_write(legacy.user_dir/"last-session.txt","../outside\n");
    rejects([&]{Session session(legacy);},"traversing session pointer accepted");
    // Even a stale lock FILE is safe after the owner closes the OS lock.
    options.new_game=true;Session unlocked(options);
    check(!unlocked.initial_save(),"stale file prevented new game");
}
// Older sessions are pruned when one starts: the latest three and the one last-session.txt
// names stay, without the runtime's ROM copies; anything not named like a session stays.
void pruning() {
    Temporary temp;
    Options options;options.user_dir=temp.path/"player";
    const auto sessions=options.user_dir/"sessions";
    std::vector<std::string> names;
    for(int i=1;i<=6;++i) {
        char name[32];std::snprintf(name,sizeof name,"%x-%d",0x18f00000+i,7-i);  // creation order
        names.push_back(name);
        fs::create_directories(sessions/name/"run/runtime-data/saves");
        atomic_write(sessions/name/"run/runtime-data/srw64-jp-rev0.z64","rom");
        atomic_write(sessions/name/"run/runtime-data/saves/srw64-jp-rev0.bin","card");
    }
    fs::create_directories(sessions/"notes");
    fs::create_directories(options.user_dir/"saves");
    atomic_write(options.user_dir/"saves/cartridge.sram",card('p'));
    atomic_write(options.user_dir/"last-session.txt",names[0]+"\n");
    {
        Session session(options);
        for(const int i:{0,3,4,5}) {
            check(fs::is_directory(sessions/names[i]),"a kept session was removed");
            check(!fs::exists(sessions/names[i]/"run/runtime-data/srw64-jp-rev0.z64"),"a kept session kept its ROM copy");
            check(fs::exists(sessions/names[i]/"run/runtime-data/saves/srw64-jp-rev0.bin"),"a kept session lost its card");
        }
        for(const int i:{1,2})check(!fs::exists(sessions/names[i]),"an old session was not pruned");
        check(fs::is_directory(sessions/"notes"),"a directory not named like a session was removed");
        check(fs::is_directory(session.session_dir()),"the new session was pruned");
        fs::create_directories(session.output_dir()/"runtime-data");
        atomic_write(session.output_dir()/"runtime-data/srw64-jp-rev0.z64","rom");
        session.release_run();
        check(!fs::exists(session.output_dir()/"runtime-data/srw64-jp-rev0.z64"),"the run kept its ROM copy");
    }
}
void environment() {
    set_environment("SRW64_DEBUG","1");set_environment("SRW64_SCRIPT_INJECT","1");
    set_environment("SRW64_TEST_FUTURE_SWITCH","yes");set_environment("SRW_APP_TEST_UNRELATED","keep");
    clear_runtime_environment();
    check(std::getenv("SRW64_DEBUG")==nullptr,"debug environment leaked");
    check(std::getenv("SRW64_SCRIPT_INJECT")==nullptr,"script injection environment leaked");
    check(std::getenv("SRW64_TEST_FUTURE_SWITCH")==nullptr,"future SRW64 switch leaked");
    check(std::string(std::getenv("SRW_APP_TEST_UNRELATED"))=="keep","unrelated environment changed");
}
}
// .v64 and .n64 dumps load through a .z64 copy (rom_order.hpp).
void rom_byte_orders() {
    std::string z64="\x80\x37\x12\x40" "ABCDEFGHIJKL";
    std::string v64=z64,n64=z64;
    for(size_t i=0;i<v64.size();i+=2)std::swap(v64[i],v64[i+1]);
    for(size_t i=0;i<n64.size();i+=4){std::swap(n64[i],n64[i+3]);std::swap(n64[i+1],n64[i+2]);}
    check(rom_big_endian(z64)==z64,"z64 kept");
    check(rom_big_endian(v64)==z64,"v64 swapped back");
    check(rom_big_endian(n64)==z64,"n64 swapped back");
    check(rom_big_endian("not a rom!!!").empty(),"unknown header rejected");
    check(rom_big_endian(z64.substr(0,6)).empty(),"odd length rejected");
    const auto dir=fs::temp_directory_path()/("srw64-rom-order-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const auto expected=sha256_bytes(z64);
    atomic_write(dir/"game.n64",n64);atomic_write(dir/"game.z64",z64);
    check(rom_matches_any_order(dir/"game.n64",expected),"n64 matches");
    check(z64_rom(dir/"game.z64",dir/"user",expected)==dir/"game.z64","z64 used in place");
    const auto copy=z64_rom(dir/"game.n64",dir/"user",expected);
    check(copy==dir/"user"/"rom-z64.z64" && read_text(copy,64)==z64,"n64 converted to a z64 copy");
    bool threw=false;try{z64_rom(dir/"game.n64",dir/"user",sha256_bytes("other"));}catch(const std::runtime_error&){threw=true;}
    check(threw,"wrong game rejected");
    fs::remove_all(dir);
}

int main() {
    try { hashing();parser_and_paths();sessions();pruning();environment();rom_byte_orders();std::cout<<checks<<" checks passed\n";return 0; }
    catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}
}
