// Name MODs (src/host/mod_names.hpp, docs/design/mod-names-art-data.md §1): the file
// rules of dialogue/names.json, the length warnings and limits, the packages' order, and
// the names shown over the defaults while the packages change.
#include "mod_names.hpp"
#include <cstdio>
#include <cstdlib>
using namespace srw64;
using json=nlohmann::json;
namespace fs=std::filesystem;

static int failures=0;
static void check(bool ok,const std::string& what){if(!ok){std::fprintf(stderr,"FAIL %s\n",what.c_str());++failures;}}
static void write(const fs::path& path,const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path,std::ios::binary)<<text;
}
static const std::vector<std::string> locales{"en","ja","zh-Hans","zh-Hant"};
static std::string why(const std::string& text,mod_names::Parsed* out=nullptr) {
    mod_names::Parsed parsed;
    auto& target=out?*out:parsed;
    return mod_names::parse(json::parse(text),locales,{{names::Field::Nick,8}},target);
}
using Key=std::tuple<names::Field,unsigned,std::string>;

int main(int argc,char** argv) {
    if(argc!=2){std::fprintf(stderr,"usage: test <scratch folder>\n");return 2;}
    const fs::path dir=argv[1];
    fs::remove_all(dir);fs::create_directories(dir);

    // A route's hero and partner, a locale each; a missing nickname is the given name.
    mod_names::Parsed parsed;
    check(why(R"({"schema":"srw64.mod-names.v1","heroes":{"super-male":{"hero":{"zh-Hans":{"name":"龙","surname":"天野","nickname":"小龙"}},
        "partner":{"en":{"name":"Kai"}}}},"unit":{"zh-Hans":"疾风","ja":"ハヤテ"}})",&parsed).empty(),"a good file");
    check(parsed.names.at(Key{names::Field::Name,2,"zh-Hans"})=="龙" && parsed.names.at(Key{names::Field::Nick,2,"zh-Hans"})=="小龙","ブラッド is person 2");
    check(parsed.names.at(Key{names::Field::Nick,6,"en"})=="Kai" && !parsed.names.contains(Key{names::Field::Surname,6,"en"}),"カーツ: nickname from the name, no surname");
    check(parsed.names.at(Key{names::Field::Unit,0,"ja"})=="ハヤテ" && parsed.warnings.empty(),"the unit, no warnings");
    check(why(R"({"heroes":{"real-female":{"partner":{"ja":{"name":"リン"}}}}})",&parsed).empty() && parsed.names.contains(Key{names::Field::Name,5,"ja"}),"リッシュ is person 5");

    // Rules: all or nothing, each error naming where.
    check(why(R"({"heroes":{"super-male":{"hero":{"fr":{"name":"Ryu"}}}}})").find("未知语言")!=std::string::npos,"an unknown locale");
    check(why(R"({"heroes":{"ultra-male":{}}})").find("未知路线")!=std::string::npos,"an unknown route");
    check(why(R"({"heroes":{"super-male":{"rival":{}}}})").find("hero 或 partner")!=std::string::npos,"an unknown role");
    check(why(R"({"heroes":{"super-male":{"hero":{"en":{"surname":"Amano"}}}}})").find("缺少 name")!=std::string::npos,"a name is required");
    check(why(R"({"heroes":{"super-male":{"hero":{"en":{"name":"Ryu","title":"Sir"}}}}})").find("未知字段 title")!=std::string::npos,"an unknown field");
    check(why(R"({"heroes":{"super-male":{"hero":{"en":{"name":""}}}}})").find("不能为空")!=std::string::npos,"an empty name");
    check(why(R"({"heroes":{"super-male":{"hero":{"en":{"name":" Ryu"}}}}})").find("首尾")!=std::string::npos,"padding");
    check(why(R"({"heroes":{"super-male":{"hero":{"en":{"name":"Ryu{HeroNick}"}}}}})").find("< > { }")!=std::string::npos,"a token");
    check(why(R"({"heroes":{"super-male":{"hero":{"en":{"name":"Ryu\nAmano"}}}}})").find("控制字符")!=std::string::npos,"a new line");
    check(why(R"({"unit":{"en":"ABCDEFGHIJKLMNOPQRSTUVWXY"}})").find("最多 24")!=std::string::npos,"a unit name past 24");
    check(why(R"({"heroes":{"super-male":{"hero":{"en":{"name":"ABCDEFGHIJKLMNOPQ"}}}}})").find("最多 16")!=std::string::npos,"a name past 16");
    check(why(R"({"unit":"Gale"})").find("unit：要是对象")!=std::string::npos,"unit by locale");
    check(why(R"({"schema":"srw64.mod-names.v2"})").find("schema")!=std::string::npos,"another schema");
    check(why(R"({"parts":{}})").find("未知字段 parts")!=std::string::npos,"unknown sections");

    // Any other pilot, unit or weapon by number: their text records.
    parsed={};
    check(why(R"({"pilots":{"133":{"zh-Hans":{"name":"真吾","full":"北条真吾"},"en":{"name":"Shingo"}}},"units":{"184":{"zh-Hans":"豪将军改"}},"weapons":{"731":{"ja":"ビッグバズーカ"}}})",&parsed).empty(),"records");
    check(parsed.records.at(4382+133).at("zh-Hans")=="真吾" && parsed.records.at(4743+133).at("zh-Hans")=="北条真吾","a pilot's short and full name");
    check(parsed.records.at(4743+133).at("en")=="Shingo","a missing full name is the name");
    check(parsed.records.at(527+184).at("zh-Hans")=="豪将军改" && parsed.records.at(1370+731).at("ja")=="ビッグバズーカ","a unit's and a weapon's");
    check(why(R"({"pilots":{"27":{"en":{"name":"Brad"}}}})").find("heroes")!=std::string::npos,"the eight go in heroes");
    check(why(R"({"weapons":{"1329":{"en":"X"}}})").find("超出范围")!=std::string::npos,"weapon numbers in range");
    check(why(R"({"units":{"184":{"en":"ABCDEFGHIJKLMNOPQRSTUVWXY"}}})").find("最多 24")!=std::string::npos,"names past 24");
    check(why(R"({"pilots":{"133":{"en":"Shingo"}}})").find("name")!=std::string::npos,"a pilot's entry is an object");

    // Warnings only: past the original's cells, or wider than our widest default.
    parsed={};
    check(why(R"({"heroes":{"super-male":{"hero":{"zh-Hans":{"name":"一二三四五六七八","nickname":"一二三四五"}}}},"unit":{"en":"Twenty-One Characters"}})",&parsed).empty(),"long is not wrong");
    check(parsed.warnings.size()==3,"three warnings: the name's cells, the nickname's width, the unit's cells");
    check(mod_names::width("Ark")==3 && mod_names::width("阿克")==4 && mod_names::width("ｱｰｸ")==3,"half cells");

    // Packages: the upper one wins per name, a disabled one and a bad file count for nothing.
    const fs::path mods_dir=dir/"mods";
    const auto package=[&](const std::string& id,const std::string& names) {
        write(mods_dir/id/"mod.json",json({{"schema","srw64.mod.v1"},{"id",id},{"version","1.0.0"},{"name",id}}).dump());
        write(mods_dir/id/"dialogue"/"names.json",names);
    };
    package("test.lower",R"({"heroes":{"super-male":{"hero":{"zh-Hans":{"name":"龙","surname":"天野"}}}},"unit":{"zh-Hans":"疾风","en":"Gale"}})");
    package("test.upper",R"({"unit":{"zh-Hans":"苍穹"}})");
    package("test.off",R"({"unit":{"en":"Off"}})");
    package("test.broken",R"({"unit":{"en":""}})");
    write(mods_dir/"mods.json",json({{"schema",mods::state_schema},{"order",{"test.lower","test.upper","test.off","test.broken"}},{"disabled",{"test.off"}}}).dump());
    setenv("SRW64_MODS_DIR",mods_dir.string().c_str(),1);
    mods::set_game_version({0,5,0});
    const auto found=mod_names::collect(mods::installed(mods::game_version()),locales,{});
    check(found.names.at(Key{names::Field::Unit,0,"zh-Hans"})=="苍穹" && found.names.at(Key{names::Field::Unit,0,"en"})=="Gale","the upper package wins");
    check(!found.names.contains(Key{names::Field::Unit,0,"en"}) || found.names.at(Key{names::Field::Unit,0,"en"})!="Off","a disabled package");
    check(found.problems.size()==1 && found.problems[0].find("test.broken")==0,"a broken file is reported");

    // Shown over the defaults, again when the packages change.
    names::DefaultNames table;
    table.add({names::Field::Name,2,"ブラッド",{{"zh-Hans","布拉德"},{"en","Brad"}}});
    table.add({names::Field::Surname,2,"スカイウィンド",{{"zh-Hans","斯凯温德"},{"en","Skywind"}}});
    table.add({names::Field::Unit,0,"マーチウィンド",{{"zh-Hans","三月风"},{"en","March Wind"}}});
    mod_names::configure(locales,dir);
    mod_names::refresh(table);
    check(table.display(names::Field::Unit,"マーチウィンド","zh-Hans")=="苍穹" && table.display_full("ブラッド・スカイウィンド","zh-Hans")=="龙·天野","shown");
    check(table.display(names::Field::Unit,"マーチウィンド","ja")=="マーチウィンド","Japanese not named: as stored");
    check(fs::is_regular_file(dir/"mod-names.json"),"a report");
    mods::set_enabled("test.lower",false);
    mod_names::refresh(table);
    check(table.display(names::Field::Name,"ブラッド","zh-Hans")=="布拉德" && table.display(names::Field::Unit,"マーチウィンド","en")=="March Wind","again when the packages change");

    // Record texts after a refresh, Traditional Chinese falling back to the Simplified.
    package("test.records",R"({"units":{"184":{"zh-Hans":"豪将军改"}}})");
    write(mods_dir/"mods.json",json({{"schema",mods::state_schema},{"order",{"test.lower","test.upper","test.off","test.broken","test.records"}},{"disabled",{"test.off","test.lower"}}}).dump());
    mods::set_enabled("test.records",true);
    mod_names::refresh(table);
    check(mod_names::record("zh-Hant",527+184)==std::optional<std::string>("豪将军改") && !mod_names::record("en",527+184),"a record by locale");
    check(mod_names::any_record("zh-Hans") && !mod_names::any_record("ja"),"which locales are named");

    if(failures)std::fprintf(stderr,"%d failures\n",failures);
    else std::puts("mod names: ok");
    return failures?1:0;
}
