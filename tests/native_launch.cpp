#include "app/launch.hpp"
#include "app/sha256.hpp"
#include "json/json.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace srw64::app;
using json=nlohmann::json;
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F f,const char* message){++checks;try{f();}catch(const std::exception&){return;}throw std::runtime_error(message);}
std::string env(const char* key){const auto* value=std::getenv(key);return value?value:"";}
json load(const fs::path& path){return json::parse(read_text(path,32*1024*1024));}
struct Fixture {
    fs::path root, previous_cwd;
    Options options;
    GameIdentity game;
    json data, manifest;
    Fixture() {
        previous_cwd=fs::current_path();
        for(unsigned i=0;i<100;++i) {
            root=fs::temp_directory_path()/("srw64-launch-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+'-'+std::to_string(i));
            if(fs::create_directory(root))break;
            if(i==99)throw std::runtime_error("No test directory");
        }
        options.rom=root/"a game.z64";options.content=root/"content";options.user_dir=root/"user data";
        atomic_write(options.rom,"synthetic ROM fixture, not original game data");
        game={"srw64-jp-rev0",sha256_file(options.rom),"test-game.bin",1,{{"fix-one",true},{"difficulty",false}}};
        fs::create_directories(options.content/"name-entry");
        atomic_write(options.content/"name-entry/face-27.png","synthetic portrait");
        json catalogs=json::object();
        for(const std::string locale:{"ja","en"})catalogs[locale]={
            {"config",{{"locale",locale},{"font","test-font"},{"font_size",14},{"mode","replace"}}},
            {"entries",json::object()},{"ui",json::object()},{"catalog_sha256",std::string(64,'b')}};
        data=catalogs["ja"];
        data["schema"]="srw64.native-dialogue-data.v2";data["rom_sha256"]=game.rom_sha256;
        data["source_entries"]=json::object();data["glyphs"]=json::object();data["locale_catalogs"]=catalogs;
        data["name_entry_assets"]={{"schema","srw64.name-entry-assets.v1"},{"portraits",{{"27",{
            {"original","name-entry/face-27.png"},{"original_sha256",sha256_file(options.content/"name-entry/face-27.png")}}}}}};
        manifest={{"schema","srw64.standalone-content.v1"},{"baseline",game.baseline},{"rom_sha256",game.rom_sha256},
                  {"dialogue","dialogue.json"},{"resolution_scale",2},{"files",json::object()}};
        publish_content();
    }
    void publish_content() {
        atomic_write(options.content/"dialogue.json",data.dump());
        for(const auto* relative:{"dialogue.json","name-entry/face-27.png"})manifest["files"][relative]=sha256_file(options.content/relative);
        atomic_write(options.content/"manifest.json",manifest.dump());
    }
    ~Fixture(){std::error_code ignored;fs::current_path(previous_cwd,ignored);fs::remove_all(root,ignored);}
};
void relocated_boot_and_resume() {
    Fixture f;
    const auto moved=f.root/"moved content";
    fs::rename(f.options.content,moved);f.options.content=moved;
    fs::create_directory(f.root/"unrelated working directory");fs::current_path(f.root/"unrelated working directory");
    set_environment("SRW64_DEBUG","1");set_environment("SRW64_SCRIPT_INJECT","1");
    set_environment("SRW64_ROM_VARIANT","model5600");
    f.options.language="en";f.options.resolution_scale=4;f.options.mute=true;
    const auto fake_host=[&](int argc,char** argv) {
        check(argc==5 && argv[argc]==nullptr,"unexpected fresh host ABI");
        check(fs::path(argv[1])==f.options.rom,"ROM path lost on relocation");
        check(std::string(argv[3])=="0" && std::string(argv[4])=="-","interactive ABI changed");
        check(env("SRW64_INTERACTIVE")=="1" && env("SRW64_AUDIO_OUTPUT")=="0","interactive/audio settings");
        check(env("SRW64_DEBUG").empty() && env("SRW64_SCRIPT_INJECT").empty(),"development environment leaked");
        check(env("SRW64_ROM_VARIANT")=="jp","ROM variant leaked");
        check(env("SRW64_RULE_FIXES")=="fix-one","first launch default rules");
        check(env("SRW64_RESOLUTION_SCALE")=="4","resolution override lost");
        const auto native=load(env("SRW64_DIALOGUE_DATA"));
        check(native.at("config").at("locale")=="en","locale override not applied");
        check(fs::path(native.at("name_entry_assets").at("portraits").at("27").at("original").get<std::string>())==
              fs::canonical(f.options.content/"name-entry/face-27.png"),"portrait still points at old source directory");
        const auto output=fs::path(argv[2]);
        check(!fs::exists(output),"probe output existed before host startup");
        fs::create_directories(output/"runtime-data/saves");
        atomic_write(output/"runtime-data/saves"/f.game.save_file,std::string(32768,'a'));
        // Simulate changing language and rules through the current native UI.
        atomic_write(env("SRW64_PRESENTATION_SETTINGS"),json({{"schema","srw64.presentation-settings.v1"},{"locale","ja"}}).dump());
        atomic_write(env("SRW64_RULE_SETTINGS"),json({{"schema","srw64.rule-settings.v1"},{"rules_version",1},{"fixes",json::array()}}).dump());
        return 0;
    };
    check(run_standalone(f.options,f.game,fake_host)==0,"first bootstrap failed");
    const auto pointer=read_text(f.options.user_dir/"last-session.txt",128);
    f.options.language.clear();
    check(run_standalone(f.options,f.game,[&](int argc,char** argv){
        check(argc==6,"resume did not stage SRAM");
        check(read_text(argv[5],32768)==std::string(32768,'a'),"resume SRAM changed");
        check(load(env("SRW64_DIALOGUE_DATA")).at("config").at("locale")=="ja","saved locale ignored");
        check(env("SRW64_RULE_FIXES").empty(),"explicit saved original rules became defaults");
        return 7; // A failed host must not publish an apparently successful session.
    })==7,"host error code lost");
    check(read_text(f.options.user_dir/"last-session.txt",128)==pointer,"failed host replaced latest save");
    check(load(f.options.content/"dialogue.json").at("config").at("locale")=="ja","bootstrap mutated source content");
}
void fail_closed() {
    Fixture f;
    bool unexpectedly_called=false;
    const auto no_host=[&](int,char**)->int{unexpectedly_called=true;return 0;};
    auto wrong=f.game;wrong.rom_sha256=std::string(64,'0');
    bool called=false;
    rejects([&]{run_standalone(f.options,wrong,[&](int,char**){called=true;return 0;});},"wrong ROM accepted");
    check(!called && !fs::exists(f.options.user_dir),"invalid ROM created a play session");
    atomic_write(f.options.content/"name-entry/face-27.png","changed");
    rejects([&]{run_standalone(f.options,f.game,no_host);},"modified portrait accepted");
    f.publish_content(); // Still disagrees with the portrait hash embedded in dialogue.
    rejects([&]{run_standalone(f.options,f.game,no_host);},"portrait/data inventory disagreement accepted");
    f.data["name_entry_assets"]["portraits"]["27"]["original_sha256"]=sha256_file(f.options.content/"name-entry/face-27.png");
    f.data["name_entry_assets"]["portraits"]["27"]["original"]="../outside.png";f.publish_content();
    rejects([&]{run_standalone(f.options,f.game,no_host);},"unlisted portrait path accepted");
    f.data["name_entry_assets"]["portraits"]["27"]["original"]="name-entry/face-27.png";
    f.data["name_entry_assets"]["portraits"]["27"]["hd"]="/private/hd.png";f.publish_content();
    rejects([&]{run_standalone(f.options,f.game,no_host);},"unsupported HD path accepted");
    f.data["name_entry_assets"]["portraits"]["27"].erase("hd");f.publish_content();
    f.options.language="unknown";
    rejects([&]{run_standalone(f.options,f.game,no_host);},"unregistered locale accepted");
    f.options.language="en";
    atomic_write(f.options.user_dir/"presentation.json","broken json");
    atomic_write(f.options.user_dir/"rules.json","broken json");
    rejects([&]{run_standalone(f.options,f.game,no_host);},"broken saved rules accepted");
    f.options.rules="original";
    check(run_standalone(f.options,f.game,[](int,char**){return 0;})==0,"explicit settings did not recover broken preferences");
    check(!fs::exists(f.options.user_dir/"last-session.txt"),"no-save host published a save pointer");
    f.options.rules.reset();
    atomic_write(f.options.user_dir/"rules.json",json({{"schema","srw64.rule-settings.v1"},{"rules_version",1},{"fixes",{"unknown"}}}).dump());
    rejects([&]{run_standalone(f.options,f.game,no_host);},"unknown saved rule accepted");
    check(!unexpectedly_called,"a rejected bootstrap entered the host");
}
}
int main(){try{relocated_boot_and_resume();fail_closed();std::cout<<checks<<" bootstrap checks passed\n";return 0;}
catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}}
