#include "launch.hpp"
#include "sha256.hpp"
#include "rom_import.hpp"
#include "json/json.hpp"
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>

namespace srw64::app {
namespace {
using json=nlohmann::json;
json read_json(const fs::path& path, size_t limit=32*1024*1024) {
    const auto value=json::parse(read_text(path,limit));
    if(!value.is_object())throw std::runtime_error("Expected a JSON object: "+path.string());
    return value;
}
std::vector<std::string> selected_rules(const Options& options,const GameIdentity& game,const fs::path& path) {
    std::set<std::string> selected, known;
    for(const auto& rule:game.rules)known.insert(rule.id);
    if(options.rules) {
        if(*options.rules!="original" && *options.rules!="fixed" && *options.rules!="all")throw std::runtime_error("Unknown rule preset");
        for(const auto& rule:game.rules)
            if(*options.rules=="all" || (*options.rules=="fixed" && rule.enabled_by_default))selected.insert(rule.id);
    } else if(fs::exists(path)) {
        const auto data=read_json(path,65536);
        if(data.value("schema","")!="srw64.rule-settings.v1" || data.value("rules_version",0u)!=game.rules_version)
            throw std::runtime_error("Invalid rules settings; pass --rules to replace them explicitly");
        selected=data.at("fixes").get<std::set<std::string>>();
        for(const auto& id:selected)if(!known.contains(id))throw std::runtime_error("Unknown saved rule: "+id);
    } else {
        for(const auto& rule:game.rules)if(rule.enabled_by_default)selected.insert(rule.id);
    }
    std::vector<std::string> result;
    for(const auto& rule:game.rules)if(selected.contains(rule.id))result.push_back(rule.id);
    return result;
}
}

int run_standalone(const Options& options,const GameIdentity& game,const HostMain& host,const ContentImporter& importer) {
    if(game.save_file.empty() || game.save_file.find_first_of("/\\:")!=std::string::npos ||
       game.save_file=="." || game.save_file=="..")throw std::runtime_error("Invalid game save filename");
    if(sha256_file(options.rom)!=game.rom_sha256)throw std::runtime_error("ROM does not match the supported baseline");
    auto content=options.content;
    std::unique_ptr<Session> owned_session;
    if(content.empty()) {
        // Lock before importing. Two launches cannot create/consume a partial
        // cache, and an import failure cannot update the committed save pointer.
        owned_session=std::make_unique<Session>(options);
        const auto cache=owned_session->user_dir()/"content-cache";
        content=importer ? importer(options.rom,cache,game.rom_sha256)
                         : prepare_rom_content(options.rom,cache,embedded_import_spec(),game.rom_sha256);
    }
    const auto root=fs::canonical(content);
    const auto manifest_path=content_path(root,"manifest.json");
    const auto manifest=read_json(manifest_path,1024*1024);
    if(manifest.value("schema","")!="srw64.standalone-content.v1" ||
       manifest.at("baseline")!=game.baseline || manifest.at("rom_sha256")!=game.rom_sha256)
        throw std::runtime_error("Content does not match the supported baseline/schema");
    const auto& files=manifest.at("files");
    if(!files.is_object() || files.empty() || files.size()>2048)throw std::runtime_error("Invalid content inventory");
    std::map<std::string,fs::path> verified;
    for(const auto& [relative,digest]:files.items()) {
        const auto path=content_path(root,relative);
        if(sha256_file(path)!=digest.get<std::string>())throw std::runtime_error("Content integrity check failed: "+relative);
        verified.emplace(relative,path);
    }
    const auto dialogue_name=manifest.at("dialogue").get<std::string>();
    if(!verified.contains(dialogue_name))throw std::runtime_error("Dialogue is absent from the content inventory");
    auto data=read_json(verified.at(dialogue_name));
    if(data.value("schema","")!="srw64.native-dialogue-data.v2" || data.at("rom_sha256")!=game.rom_sha256)
        throw std::runtime_error("Unsupported native dialogue data");
    auto& portraits=data.at("name_entry_assets").at("portraits");
    for(auto& [id,portrait]:portraits.items()) {
        const auto relative=portrait.at("original").get<std::string>();
        if(!verified.contains(relative) || portrait.at("original_sha256")!=files.at(relative))
            throw std::runtime_error("Portrait is absent from the verified inventory: "+id);
        if(portrait.contains("hd"))throw std::runtime_error("This standalone milestone supports Original content only");
        portrait["original"]=verified.at(relative).string();
    }
    if(data.contains("battle_assets"))for(const char* group:{"units","portraits"})
        for(auto& [id,art]:data["battle_assets"].at(group).items()) {
            const auto relative=art.at("path").get<std::string>();
            if(!verified.contains(relative) || art.at("sha256")!=files.at(relative))
                throw std::runtime_error("Battle art is absent from the verified inventory: "+id);
            art["path"]=verified.at(relative).string();
        }
    unsigned scale=options.resolution_scale ? options.resolution_scale : manifest.at("resolution_scale").get<unsigned>();
    if(scale<1 || scale>8)throw std::runtime_error("Resolution scale must be in 1..8");

    if(!owned_session)owned_session=std::make_unique<Session>(options);
    auto& session=*owned_session;
    const auto language_file=session.user_dir()/"presentation.json";
    std::string locale=options.language,battle_ui="native",intermission_ui="native",name_entry_ui="native",title_ui="native";
    if(fs::exists(language_file)) {
        const auto saved=read_json(language_file,65536);
        if(saved.value("schema","")!="srw64.presentation-settings.v1") {
            if(locale.empty())throw std::runtime_error("Invalid language settings; pass --language to replace them explicitly");
        } else {
            if(locale.empty())locale=saved.at("locale").get<std::string>();
            battle_ui=saved.value("battle_ui","native");
            intermission_ui=saved.value("intermission_ui","native");
            name_entry_ui=saved.value("name_entry_ui","native");
            title_ui=saved.value("title_ui","native");
        }
    }
    if(locale.empty())locale=data.at("config").at("locale").get<std::string>();
    const auto catalogs=data.at("locale_catalogs");
    if(!catalogs.contains(locale))throw std::runtime_error("Locale is not available in this content: "+locale);
    for(const auto& [key,value]:catalogs.at(locale).items())data[key]=value;
    if(data.at("config").at("locale")!=locale)throw std::runtime_error("Locale registry identity mismatch");
    const auto rules_file=session.user_dir()/"rules.json";
    const auto rules=selected_rules(options,game,rules_file);
    std::string rule_names;
    for(const auto& id:rules){if(!rule_names.empty())rule_names+=',';rule_names+=id;}
    if(options.rules)atomic_write(rules_file,json({{"schema","srw64.rule-settings.v1"},
        {"rules_version",game.rules_version},{"fixes",rules}}).dump(2)+"\n");
    if(!options.language.empty())atomic_write(language_file,json({{"schema","srw64.presentation-settings.v1"},
        {"locale",locale},{"battle_ui",battle_ui},{"intermission_ui",intermission_ui},{"name_entry_ui",name_entry_ui},{"title_ui",title_ui}}).dump(2)+"\n");
    const auto dialogue=session.session_dir()/"dialogue.json";
    atomic_write(dialogue,data.dump()+"\n");

    // Do not inherit development probes, scripted inputs or another ROM variant.
    clear_runtime_environment();
    for(const auto& [key,value]:std::map<std::string,std::string>{
        {"SRW64_INTERACTIVE","1"},{"SRW64_DIAGNOSTICS","light"},{"SRW64_ROM_VARIANT","jp"},
        {"SRW64_AUDIO_OUTPUT",options.mute?"0":"1"},{"SRW64_NATIVE_NAME_ENTRY","1"},
        {"SRW64_DIALOGUE_DATA",dialogue.string()},{"SRW64_PRESENTATION_SETTINGS",language_file.string()},
        {"SRW64_RULE_SETTINGS",rules_file.string()},{"SRW64_RULE_FIXES",rule_names},
        {"SRW64_HD_AVAILABLE","0"},{"SRW64_IMAGE_MODE","original"},{"SRW64_RESOLUTION_SCALE",std::to_string(scale)},
        // The dialogue text shipped with the program, and the player's own edits.
        {"SRW64_DIALOGUE_TEXT",bundled_resource("dialogue").string()},{"SRW64_DIALOGUE_OVERRIDES",(session.user_dir()/"dialogue").string()},
        // HarmonyOS Sans and the symbol font shipped in the bundle.
        {"SRW64_FONT_DIR",bundled_resource("fonts").string()}})
        set_environment(key,value);
    std::vector<std::string> arguments={"srw64-gfx-host",options.rom.string(),session.output_dir().string(),"0","-"};
    if(session.initial_save())arguments.push_back(session.initial_save()->string());
    std::vector<char*> argv;
    for(auto& argument:arguments)argv.push_back(argument.data());
    argv.push_back(nullptr);
    json report={{"schema","srw64.standalone-session.v1"},{"status","running"},
        {"baseline",game.baseline},{"rom_sha256",game.rom_sha256},{"content_manifest_sha256",sha256_file(manifest_path)},
        {"locale",locale},{"rules",rules},{"output",session.output_dir().string()}};
    atomic_write(session.session_dir()/"launch.json",report.dump(2)+"\n");
    std::fprintf(stderr,"SRW64_PLAY_SESSION %s\n",session.session_dir().string().c_str());
    const int result=host(static_cast<int>(arguments.size()),argv.data());
    const bool committed=result==0 && session.commit_save(session.output_dir()/"runtime-data/saves"/game.save_file);
    report["status"]=result==0?"complete":"host-failed";report["exit_code"]=result;report["save_committed"]=committed;
    atomic_write(session.session_dir()/"launch.json",report.dump(2)+"\n");
    return result;
}
}
