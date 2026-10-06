#include "launch.hpp"
#include "rom_order.hpp"
#include "sha256.hpp"
#include "rom_import.hpp"
#include "json/json.hpp"
#include <cstdio>
#include <cstdlib>
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

// The id a compiled campaign (srw64.campaign-image.v1) declares; it names the campaign's
// save library, so the session needs it before it opens one.
static std::string read_campaign_id(const fs::path& campaign) {
    // Stage images make a compiled campaign a few hundred KB; 64 MiB is only a sanity bound.
    const auto document=json::parse(read_text(campaign,64u<<20),nullptr,false);
    if(document.is_discarded() || document.value("schema","")!="srw64.campaign-image.v1")
        throw std::runtime_error("Not a compiled campaign (srw64.campaign-image.v1): "+campaign.string());
    auto id=document.value("id",std::string());
    if(!valid_campaign_id(id))throw std::runtime_error("A campaign id is 1-64 letters, digits, '.', '_' or '-': "+campaign.string());
    return id;
}

int run_standalone(const Options& requested,const GameIdentity& game,const HostMain& host,const ContentImporter& importer) {
    Options options=requested;
    if(!options.campaign.empty())options.campaign_id=read_campaign_id(options.campaign);
    if(game.save_file.empty() || game.save_file.find_first_of("/\\:")!=std::string::npos ||
       game.save_file=="." || game.save_file=="..")throw std::runtime_error("Invalid game save filename");
    // A .v64 or .n64 dump loads through a .z64 copy (rom_order.hpp).
    options.rom=z64_rom(options.rom,fs::absolute(options.user_dir.empty()?default_user_dir():options.user_dir),game.rom_sha256);
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
    // The HD pack (tools/release/prepare_hd_bundle.py): the compiled art, the page
    // portraits by (image, palette), and the model packs. A downloaded pack goes in
    // the user directory as hd/; a full-HD build bundles it as Contents/Resources/hd.
    const auto installed=fs::absolute(options.user_dir.empty()?default_user_dir():options.user_dir)/"hd";
    const auto hd=fs::exists(installed)?installed:bundled_resource("hd");
    if(!hd.empty() && fs::exists(hd)) {
        const auto about=hd/"hd.json";
        if(!fs::is_regular_file(about) || read_json(about,1024*1024).value("schema","")!="srw64.hd-bundle.v1"
           || !fs::is_regular_file(hd/"art"/"rt64.json"))
            throw std::runtime_error("The HD pack at "+hd.string()+" is incomplete or of another version: "
                                     "replace it with the pack for this release, or remove it to play in Original");
    }
    const bool hd_art=!hd.empty() && fs::exists(hd);
    if(hd_art) {
        const auto pages=read_json(hd/"art"/"srw64-page-portraits.json");
        if(pages.value("schema","")!="srw64.page-portraits.v1")throw std::runtime_error("Unsupported HD page portraits");
        const auto& index=pages.at("portraits");
        const auto attach=[&](json& row,unsigned image,unsigned palette) {
            const auto key=std::to_string(image)+":"+std::to_string(palette);
            if(index.contains(key))row["hd"]=(hd/"art"/index.at(key).get<std::string>()).string();
        };
        for(auto& [id,portrait]:portraits.items())
            attach(portrait,portrait.at("resource_id").get<unsigned>(),portrait.at("palette_id").get<unsigned>());
        if(data.contains("battle_assets"))for(auto& [id,art]:data["battle_assets"].at("portraits").items())
            attach(art,art.at("resources").at(0).get<unsigned>(),art.at("resources").at(1).get<unsigned>());
        // Whole HD unit poses by (scene, atlas, palette), as profile.py's unit_lookup.
        if(const auto units_index=hd/"art"/"srw64-units-hd.json";fs::is_regular_file(units_index) && data.contains("battle_assets")) {
            const auto poses=read_json(units_index);
            if(poses.value("schema","")!="srw64.unit-images.v1")throw std::runtime_error("Unsupported HD unit poses");
            std::map<std::string,std::string> by_triplet;
            for(const auto& row:poses.at("images"))
                by_triplet[std::to_string(row.at("scene").get<unsigned>())+":"+std::to_string(row.at("atlas").get<unsigned>())+":"+std::to_string(row.at("palette").get<unsigned>())]=row.at("file").get<std::string>();
            for(auto& [id,art]:data["battle_assets"].at("units").items()) {
                if(!art.contains("resources"))continue;
                const auto& r=art.at("resources");
                const auto key=std::to_string(r.at(0).get<unsigned>())+":"+std::to_string(r.at(1).get<unsigned>())+":"+std::to_string(r.at(2).get<unsigned>());
                if(const auto found=by_triplet.find(key);found!=by_triplet.end())art["hd"]=(hd/"art"/found->second).string();
            }
        }
    }
    unsigned scale=options.resolution_scale ? options.resolution_scale : manifest.at("resolution_scale").get<unsigned>();
    if(scale<1 || scale>8)throw std::runtime_error("Resolution scale must be in 1..8");

    if(!owned_session)owned_session=std::make_unique<Session>(options);
    auto& session=*owned_session;
    const auto language_file=session.user_dir()/"presentation.json";
    std::string locale=options.language,battle_ui="native",intermission_ui="native",name_entry_ui="native",title_ui="native",settings_page,ui_size,aspect="auto";
    if(fs::exists(language_file)) {
        // Unreadable settings count as invalid: an explicit --language replaces them.
        json saved=json::object();
        try{saved=read_json(language_file,65536);}catch(const std::exception&){}
        if(saved.value("schema","")!="srw64.presentation-settings.v1") {
            if(locale.empty())throw std::runtime_error("Invalid language settings; pass --language to replace them explicitly");
        } else {
            if(locale.empty())locale=saved.at("locale").get<std::string>();
            battle_ui=saved.value("battle_ui","native");
            intermission_ui=saved.value("intermission_ui","native");
            name_entry_ui=saved.value("name_entry_ui","native");
            title_ui=saved.value("title_ui","native");
            settings_page=saved.value("settings_page","");
            ui_size=saved.value("ui_size","");
            aspect=saved.value("aspect","auto");
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
    if(!options.language.empty()) {
        json settings={{"schema","srw64.presentation-settings.v1"},
            {"locale",locale},{"battle_ui",battle_ui},{"intermission_ui",intermission_ui},{"name_entry_ui",name_entry_ui},{"title_ui",title_ui},{"settings_page",settings_page},{"aspect",aspect}};
        if(!ui_size.empty())settings["ui_size"]=ui_size;  // absent until the player chooses one
        atomic_write(language_file,settings.dump(2)+"\n");
    }
    const auto dialogue=session.session_dir()/"dialogue.json";
    atomic_write(dialogue,data.dump()+"\n");

    // Do not inherit development probes, scripted inputs or another ROM variant. A test may
    // point the update check at its own server (update_check.hpp).
    const char* update_url=std::getenv("SRW64_UPDATE_URL");
    const std::string kept_update_url=update_url?update_url:"";
    clear_runtime_environment();
    if(!kept_update_url.empty())set_environment("SRW64_UPDATE_URL",kept_update_url);
    for(const auto& [key,value]:std::map<std::string,std::string>{
        {"SRW64_INTERACTIVE","1"},{"SRW64_ROM_VARIANT","jp"},
        {"SRW64_AUDIO_OUTPUT",options.mute?"0":"1"},{"SRW64_NATIVE_NAME_ENTRY","1"},
        {"SRW64_DIALOGUE_DATA",dialogue.string()},{"SRW64_PRESENTATION_SETTINGS",language_file.string()},
        // The player's key and controller bindings (input_bindings.hpp), beside the language.
        {"SRW64_INPUT_SETTINGS",(session.user_dir()/"input.json").string()},
        // The update check's switch and its last result (update_check.hpp).
        {"SRW64_UPDATE_STATE",(session.user_dir()/"update.json").string()},
        {"SRW64_RULE_SETTINGS",rules_file.string()},{"SRW64_RULE_FIXES",rule_names},
        // The save library: extended slots, and the card published as the game saves.
        {"SRW64_SAVE_LIBRARY",session.saves_dir().string()},
        // HD starts on when the bundle has it; F6 or the settings window switch to Original.
        {"SRW64_HD_AVAILABLE",hd_art?"1":"0"},{"SRW64_IMAGE_MODE",hd_art?"hd":"original"},{"SRW64_RESOLUTION_SCALE",std::to_string(scale)},
        // The dialogue text shipped with the program, and the player's own edits.
        {"SRW64_DIALOGUE_TEXT",bundled_resource("dialogue").string()},{"SRW64_DIALOGUE_OVERRIDES",(session.user_dir()/"dialogue").string()},
        // HarmonyOS Sans and the symbol font shipped in the bundle.
        {"SRW64_FONT_DIR",bundled_resource("fonts").string()}})
        set_environment(key,value);
    // A custom campaign: its stages replace the scenes they borrow (mini_stage.hpp).
    if(!options.campaign.empty())set_environment("SRW64_CAMPAIGN",options.campaign.string());
    // The MOD manager's extra scenarios (campaign_library.hpp): the player's campaigns,
    // then the bundled ones, and where each keeps its saves (campaign_switch.hpp).
    std::string campaign_dirs=(session.user_dir()/"campaigns").string();
    if(const auto bundled=bundled_resource("campaigns");!bundled.empty())campaign_dirs+=campaign_dir_separator+bundled.string();
    set_environment("SRW64_CAMPAIGN_DIRS",campaign_dirs);
    set_environment("SRW64_CAMPAIGN_SAVES",(session.user_dir()/"campaigns").string());
    if(hd_art) {
        set_environment("SRW64_ART_PACK",(hd/"art").string());
        // The golden beacon replaces the dashed ring only with both model packs loaded.
        for(const auto& [key,name]:{std::pair{"SRW64_NATIVE_MARKER","native-marker"},std::pair{"SRW64_NATIVE_MODELS","native-models"}})
            if(fs::is_regular_file(hd/name/"manifest.json"))set_environment(key,(hd/name).string());
    }
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
    session.release_run();
    report["status"]=result==0?"complete":"host-failed";report["exit_code"]=result;report["save_committed"]=committed;
    atomic_write(session.session_dir()/"launch.json",report.dump(2)+"\n");
    return result;
}
}
