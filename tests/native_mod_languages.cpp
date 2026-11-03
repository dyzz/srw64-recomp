// Language packages (src/host/mod_languages.hpp, docs/design/mod-language-packs.md): the
// rules of language.json and terms.json, the packages' order, and the catalog registry's
// new locales with their fallback chain (localization/catalog.cpp, locale_fallbacks.hpp);
// and Korean, ours standing on English (docs/design/korean.md).
#include "mod_languages.hpp"
#include <cstdio>
using namespace srw64;
using json=nlohmann::json;
namespace fs=std::filesystem;

static int failures=0;
static void check(bool ok,const std::string& what){if(!ok){std::fprintf(stderr,"FAIL %s\n",what.c_str());++failures;}}
static void write(const fs::path& path,const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path,std::ios::binary)<<text;
}
static mods::Package package(const fs::path& folder,const std::string& id) {
    mods::Package p;p.folder=folder;p.active=true;
    mods::Manifest m;m.id=id;p.manifest=m;
    return p;
}
static bool has(const std::vector<std::string>& problems,const std::string& text) {
    for(const auto& p:problems)if(p.find(text)!=std::string::npos)return true;
    return false;
}

int main(int argc,char** argv) {
    if(argc!=2){std::fprintf(stderr,"usage: test <scratch folder>\n");return 2;}
    const fs::path dir=argv[1];
    fs::remove_all(dir);fs::create_directories(dir);

    // Two built-in catalogs over the same original records.
    const json data={{"schema","srw64.native-dialogue-data.v2"},{"config",{{"locale","en"},{"font","x"}}},
        {"source_entries",{{"base:t00_00000","平原<END>"},{"base:t00_00001","森<END>"},{"base:t00_05644","台詞<STOP>二<END>"}}},
        {"entries",json::object()},{"ui",json::object()},
        {"locale_catalogs",{{"ja",{{"config",{{"locale","ja"},{"font","x"}}},{"entries",json::object()},{"ui",{{"hello","こんにちは"},{"press","{A} を押す"}}}}},
                            {"en",{{"config",{{"locale","en"},{"font","x"}}},{"entries",{{"base:t00_00000","Plains<END>"}}},{"ui",{{"hello","Hello"},{"press","Press {A}"}}}}}}},
        {"locale_options",{{{"locale","ja"},{"label","日本語"}},{{"locale","en"},{"label","English"}}}}};
    localization::initialize(data);
    const auto english=localization::find("en");

    // A new language, a built-in one's layer, and the problems each folder can have.
    const auto a=dir/"a.lang",b=dir/"b.lang";
    write(a/"dialogue"/"fr"/"language.json",R"({"schema":"srw64.language.v1","locale":"fr","name":"Français","fonts":{"text":["fonts/f.ttf"]}})");
    write(a/"dialogue"/"fr"/"fonts"/"f.ttf","not a font, but a file");
    write(a/"dialogue"/"fr"/"terms.json",R"({"schema":"srw64.language-terms.v1","ui":{"hello":"Salut","press":"Appuyer sur {A}"},
        "entries":{"base:t00_00000":{"text":"Plaine<END>","ja":"平原<END>"},"base:t00_00001":"Forêt<STOP><END>","base:t00_05644":"Réplique<END>","base:t00_00009":"Rien<END>"}})");
    write(a/"dialogue"/"en"/"terms.json",R"({"schema":"srw64.language-terms.v1","ui":{"hello":"Hi"}})");
    write(a/"dialogue"/"nl"/"story.txt","");
    write(a/"dialogue"/"de"/"language.json",R"({"schema":"srw64.language.v1","locale":"de","name":"Deutsch","fallback":"fr"})");
    write(a/"dialogue"/"Pt"/"language.json",R"({"schema":"srw64.language.v1","locale":"Pt","name":"Português"})");
    write(a/"dialogue"/"ja"/"language.json",R"({"schema":"srw64.language.v1","locale":"ja","name":"日本語"})");
    write(a/"dialogue"/"es"/"language.json",R"({"schema":"srw64.language.v1","locale":"es","name":"Español","fonts":{"text":["../x.ttf"]}})");
    write(a/"dialogue"/"it"/"language.json",R"({"schema":"srw64.language.v1","locale":"it","name":"Italiano","colour":"red"})");
    write(a/"dialogue"/"eo"/"language.json",R"({"schema":"srw64.language.v1","locale":"eo","name":"Esperanto","fallback":"ko"})");
    // The upper package declares fr again: its declaration wins, both layers stay.
    write(b/"dialogue"/"fr"/"language.json",R"({"schema":"srw64.language.v1","locale":"fr","name":"Français canadien","fallback":"ja"})");
    write(b/"dialogue"/"fr"/"terms.json",R"({"schema":"srw64.language-terms.v1","ui":{"hello":"Allô"}})");
    write(b/"art"/"fr"/"scene-images"/"title-logo.png","png");
    // Korean is ours (docs/design/korean.md): a package's ko/ is a layer, and the 0.5.3 demo
    // package's declaration of it is left unread.
    write(b/"dialogue"/"ko"/"language.json",R"({"schema":"srw64.language.v1","locale":"ko","name":"한국어","fallback":"en"})");
    write(b/"dialogue"/"ko"/"terms.json",R"({"schema":"srw64.language-terms.v1","ui":{"hello":"안녕"}})");
    auto found=mod_languages::collect({package(a,"a.lang"),package(b,"b.lang")},*english);
    check(found.languages.size()==1 && found.languages[0].locale=="fr","one new language");
    check(found.languages[0].name=="Français canadien" && found.languages[0].fallback=="ja" && found.languages[0].package=="b.lang","the upper declaration wins");
    check(found.layers.at("fr").size()==2 && found.layers.at("fr")[0].package=="a.lang","both layers, bottom first");
    const auto& fr=found.layers.at("fr")[0];
    check(fr.ui.at("hello")=="Salut" && fr.entries.at("base:t00_00000")=="Plaine<END>","labels and an entry in the object form");
    check(!fr.entries.contains("base:t00_00001") && has(found.problems,"base:t00_00001：控制码和原文不一样"),"an entry whose controls differ is left out");
    check(!fr.entries.contains("base:t00_05644") && has(found.problems,"台词写进 .txt"),"dialogue records go in the text files");
    check(!fr.entries.contains("base:t00_00009") && has(found.problems,"base:t00_00009：没有这条记录"),"a record the ROM lacks");
    check(found.layers.at("ko").size()==1 && found.layers.at("ko")[0].ui.at("hello")=="안녕" && !has(found.problems,"dialogue/ko/"),
          "Korean's layer, its declaration unread");
    check(found.layers.at("en").size()==1 && found.layers.at("en")[0].ui.at("hello")=="Hi","a built-in language's layer");
    check(has(found.problems,"a.lang dialogue/nl/：不是内置语言，要有 language.json"),"a folder without a declaration");
    check(has(found.problems,"dialogue/de/language.json：fallback 要是"),"a package's language is no fallback");
    check(has(found.problems,"dialogue/eo/language.json：fallback 要是"),"nor is Korean, which stands on English");
    check(has(found.problems,"dialogue/Pt/language.json：语言代码"),"a code with a capital first letter");
    check(has(found.problems,"dialogue/ja/language.json：内置语言不用声明"),"a built-in language declared");
    check(has(found.problems,"dialogue/es/language.json：fonts.text：../x.ttf：只能是 fonts/ 里的文件"),"a font outside fonts/");
    check(has(found.problems,"dialogue/it/language.json：未知字段 colour"),"an unknown field");
    check(!found.layers.contains("nl") && !found.layers.contains("de") && !found.layers.contains("eo"),"no layers for refused folders");
    // A package turned off adds nothing.
    auto off=package(b,"b.lang");off.active=false;
    check(mod_languages::collect({off},*english).languages.empty(),"an inactive package");

    // The pictures: the topmost package's file for the locale.
    mod_languages::set(found);
    check(mod_languages::art("fr","scene-images/title-logo.png")==b/"art"/"fr"/"scene-images"/"title-logo.png","a scene image");
    check(mod_languages::art("fr","plates/argama.png").empty() && mod_languages::art("fr","../mod.json").empty(),"none, and no way out");

    // The registry: the new locale after the built-in ones, its style and chain its fallback's.
    localization::add_locale(english->as_locale("fr"),"Français","en");
    check(localization::find("fr") && localization::find("fr")->locale=="fr","registered");
    check(localization::next_locale("en")=="fr" && localization::next_locale("fr")=="ja","at the end of the cycle");
    check(localization::display_name("fr")=="Français" && localization::locale_options().size()==3,"in the language menu");
    check(localization::style("fr")=="en" && localization::style("en")=="en" && localization::style("zh-Hant")=="zh-Hant","typesetting follows the fallback");
    check(localization::fallbacks("fr")==std::vector<std::string>{"fr","en","ja"},"fr, en, ja");
    check(localization::fallbacks("zh-Hant")==std::vector<std::string>{"zh-Hant","zh-Hans","ja"} &&
          localization::fallbacks("ja")==std::vector<std::string>{"ja"},"the built-in chains");
    check(localization::pick(json{{"ja","外伝"},{"en","Gaiden"}},"fr")=="Gaiden" && localization::pick(json{{"fr","Récit"},{"en","Gaiden"}},"fr")=="Récit",
          "pick by the chain");
    check(localization::pick(json{{"de","Erzählung"}},"fr")=="Erzählung" && localization::pick(json("x"),"fr")=="x","then anything");
    bool refused=false;
    try{localization::add_locale(english->as_locale("fr"),"x","en");}catch(const std::exception&){refused=true;}
    check(refused,"a locale only once");
    refused=false;
    try{localization::add_locale(english->as_locale("eo"),"x","fr");}catch(const std::exception&){refused=true;}
    check(refused,"a package's locale is no fallback");
    // The text a new locale shows: its fallback's, under it.
    check(*localization::find("fr")->resolve(localization::TextKey::base(0,0))=="Plains<END>" && localization::find("fr")->ui("hello")=="Hello",
          "the fallback's text until the layers go on");
    // Initializing again forgets the packages' locales.
    localization::initialize(data);
    check(!localization::find("fr") && localization::style("fr")=="fr","initialize resets");

    // Korean, ours standing on English (docs/design/korean.md): a row of locale_catalogs with a
    // fallback, typeset and chained as a package's language, but not a package's.
    auto korean=data;
    korean["locale_catalogs"]["ko"]={{"config",{{"locale","ko"},{"font","x"},{"fallback","en"}}},
        {"entries",{{"base:t00_00000","평지<END>"}}},{"ui",{{"hello","안녕"},{"press","{A} 누르기"}}}};
    korean["locale_options"].push_back({{"locale","ko"},{"label","한국어"}});
    localization::initialize(korean);
    check(localization::find("ko") && localization::next_locale("en")=="ko","ours, in the cycle");
    check(localization::style("ko")=="en" && localization::fallbacks("ko")==std::vector<std::string>{"ko","en","ja"},"typeset and chained as English");
    check(!localization::is_package("ko") && !localization::is_package("en"),"not a package's");
    refused=false;
    try{localization::add_locale(english->as_locale("eo"),"x","ko");}catch(const std::exception&){refused=true;}
    check(refused,"no package's language stands on Korean");
    localization::add_locale(localization::find("en")->as_locale("fr"),"Français","en");
    check(localization::is_package("fr"),"a package's beside it");
    auto broken=korean;broken["locale_catalogs"]["ko"]["config"]["fallback"]="ko";
    refused=false;
    try{localization::initialize(broken);}catch(const std::exception&){refused=true;}
    check(refused,"a fallback of its own");
    localization::initialize(data);
    check(localization::style("ko")=="ko" && !localization::is_package("fr"),"initialize resets ours too");

    if(failures){std::fprintf(stderr,"%d failure(s)\n",failures);return 1;}
    std::puts("mod languages: ok");
    return 0;
}
