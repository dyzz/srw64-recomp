#include "app/runtime.hpp"
#include "app/sha256.hpp"
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
    reject({}); reject({"--rom","x"});
    reject({"--rom","x","--content","y","--resolution-scale","0"});
    reject({"--rom","x","--content","y","--resolution-scale","8x"});
    reject({"--rom","x","--content","y","--resolution-scale","9"});
    reject({"--rom","x","--content","y","--rules","surprise"});
    reject({"--rom","x","--content","y","--mute","--mute"});
    reject({"--rom","x","--content","y","--unknown","z"});
    reject({"--rom","x","--content","y","--new-game","--import-save","z"});
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
void sessions() {
    Temporary temp;
    Options options;options.user_dir=temp.path/"player data";
    fs::path completed;
    const auto old_save=std::string(32768,'a');
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
        atomic_write(temp.path/"host.bin",old_save);
        check(session.commit_save(temp.path/"host.bin"),"valid save not published");
        completed=session.session_dir();
        rejects([&]{session.commit_save(temp.path/"host.bin");},"duplicate commit accepted");
    }
    const auto pointer=read_text(options.user_dir/"last-session.txt",128);
    {
        Session session(options);
        check(session.initial_save().has_value(),"resume lost save");
        check(read_text(*session.initial_save(),32768)==old_save,"resume changed save");
        atomic_write(*session.initial_save(),std::string(32768,'b'));
        check(read_text(completed/"save.bin",32768)==old_save,"game edited immutable history");
        // Simulated failed/aborted host: do not commit.
    }
    check(read_text(options.user_dir/"last-session.txt",128)==pointer,"aborted launch replaced latest save");
    {
        auto fresh=options;fresh.new_game=true;Session session(fresh);
        check(!session.initial_save(),"--new-game restored an old save");
        check(!session.commit_save(session.output_dir()/"missing.bin"),"new game without a save replaced history");
    }
    check(read_text(options.user_dir/"last-session.txt",128)==pointer,"empty new game replaced latest pointer");
    atomic_write(completed/"save.bin",std::string(32768,'c'));
    rejects([&]{Session session(options);},"corrupted save resumed silently");
    {
        auto recovery=options;recovery.import_save=temp.path/"host.bin";Session session(recovery);
        check(read_text(*session.initial_save(),32768)==old_save,"explicit import failed");
        session.commit_save(*session.initial_save());
    }
    atomic_write(options.user_dir/"last-session.txt","../outside\n");
    rejects([&]{Session session(options);},"traversing session pointer accepted");
    // Even a stale lock FILE is safe after the owner closes the OS lock.
    options.new_game=true;Session unlocked(options);
    check(!unlocked.initial_save(),"stale file prevented new game");
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
int main() {
    try { hashing();parser_and_paths();sessions();environment();std::cout<<checks<<" checks passed\n";return 0; }
    catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}
}
