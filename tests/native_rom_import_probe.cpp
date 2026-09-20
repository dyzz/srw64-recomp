// Test-only command line. Never install this executable in a player package.
#include "app/launch.hpp"
#include "app/rom_import.hpp"
#include "app/rom_import_codec.hpp"
#include "app/portrait_png.hpp"
#include "app/sha256.hpp"
#include "json/json.hpp"
#include <iostream>
using namespace srw64::app;
using json=nlohmann::json;
static fs::path path(const char* p){return fs::path(std::u8string(reinterpret_cast<const char8_t*>(p)));}
int main(int argc,char** argv){try {
    if(argc==3 && std::string(argv[1])=="metadata") {atomic_write(path(argv[2]),embedded_import_spec());return 0;}
    if(argc==5 && std::string(argv[1])=="lz") {
        const auto input=read_text(path(argv[2]),16*1024*1024);
        const auto data=rom_import::lz_decode({reinterpret_cast<const uint8_t*>(input.data()),input.size()},std::stoul(argv[3]));
        atomic_write(path(argv[4]),{reinterpret_cast<const char*>(data.bytes.data()),data.bytes.size()});
        std::cout<<data.consumed<<'\n';return 0;
    }
    if(argc!=5)throw std::runtime_error("Usage: test-probe import|bootstrap ROM SPEC CACHE_OR_USERDIR");
    const auto raw=read_text(path(argv[3]),8*1024*1024);
    const auto spec=json::parse(raw);
    const auto rom=fs::absolute(path(argv[2])),root=fs::absolute(path(argv[4]));
    const auto expected=spec.at("rom_sha256").get<std::string>();
    if(std::string(argv[1])=="import") {
        // Own a lock just like --play, with a separate test-only session root.
        Options o;o.user_dir=root/"test-lock";o.new_game=true;Session lock(o);
        const auto content=prepare_rom_content(rom,root/"cache",raw,expected);
        const auto u8=content.u8string();std::cout<<std::string(reinterpret_cast<const char*>(u8.data()),u8.size())<<'\n';return 0;
    }
    if(std::string(argv[1])!="bootstrap")throw std::runtime_error("Unknown mode");
    const std::string r=argv[2],u=argv[4];
    const std::vector<std::string_view> args{"--rom",r,"--user-dir",u,"--mute"};
    const auto options=parse_options(args); // --content must be optional.
    GameIdentity game{"srw64-jp-rev0",expected,"test.bin",1,{}};
    json result;
    const auto code=run_standalone(options,game,[&](int n,char** a){
        if(n!=5 && n!=6)throw std::runtime_error("Invalid host ABI");
        const auto data=json::parse(read_text(std::getenv("SRW64_DIALOGUE_DATA"),32*1024*1024));
        result={{"argc",n},{"texts",data.at("source_entries").size()},{"locale",data.at("config").at("locale")},
                {"portraits",data.at("name_entry_assets").at("portraits").size()}};
        const auto save=path(a[2])/"runtime-data/saves/test.bin";fs::create_directories(save.parent_path());
        if(n==6 && read_text(path(a[5]),32768)!=std::string(32768,'s'))throw std::runtime_error("Resume did not preserve SRAM");
        atomic_write(save,std::string(32768,'s'));return 0;
    },[&](const fs::path& input,const fs::path& cache,const std::string& digest){
        bool rejected=false;try{Session second(options);}catch(const std::exception&){rejected=true;}
        if(!rejected)throw std::runtime_error("Importer ran without holding the play lock");
        return prepare_rom_content(input,cache,raw,digest);
    });
    std::cout<<result.dump()<<'\n';return code;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
