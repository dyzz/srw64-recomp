#include "localization/catalog.hpp"
#include "localization/dialogue_text.hpp"
#include <filesystem>
#include <fstream>
#include "game_adapter/dialogue_source.hpp"
#include "presentation/image_mode.hpp"
#include "presentation/display_list_snapshots.hpp"
#include "diagnostics.hpp"
#include "modal_input.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <unistd.h>

int main() {
    srw64::ModalInputRelease modal;
    modal.hold();assert(modal.filter(0x1010,true,false)==0);
    assert(modal.filter(0x1010,false,false)==0 && modal.pending()); // Held guest START/R after sheet close.
    assert(modal.filter(0,false,true)==0 && modal.pending()); // Physical F7 still held.
    assert(modal.filter(0,false,false)==0 && !modal.pending());
    assert(modal.filter(0x8000,false,false)==0x8000); // Only a fresh press returns to the game.
    unsetenv("SRW64_DIAGNOSTICS");assert(srw64_full_diagnostics());
    setenv("SRW64_DIAGNOSTICS","light",1);assert(!srw64_full_diagnostics());
    setenv("SRW64_DIAGNOSTICS","full",1);assert(srw64_full_diagnostics());
    unsetenv("SRW64_DIAGNOSTICS");
    srw64::presentation::DisplayListSnapshots<int> pending;
    // Both game buffers are ready before the render thread takes the first.
    pending.publish(120,180,std::make_shared<const int>(1));
    pending.publish(320,390,std::make_shared<const int>(2));
    assert(*pending.take(100,100)->frame==1);
    assert(*pending.take(300,100)->frame==2);
    assert(!pending.take(300,100)); // Each snapshot is consumed once.
    // A nonmatching task cannot discard another buffer's pending dialogue.
    pending.publish(120,180,std::make_shared<const int>(3));
    assert(!pending.take(100,50));
    assert(*pending.take(100,100)->frame==3);
    pending.publish(120,180,std::make_shared<const int>(4));
    pending.clear();assert(!pending.take(100,100)); // Scene invalidation.
    using namespace srw64::localization;
    Catalog c;
    nlohmann::json data={{"schema","srw64.native-dialogue-data.v2"},
        {"config",{{"locale","en"},{"font","Helvetica"}}},
        {"entries",{{"base:t00_00042","Translated<END>"}}},
        {"source_entries",{{"base:t00_00042","原文<END>"},{"base:t01_00042","別表<END>"}}},
        {"ui",{{"manual","Manual"}}}};
    c.load(data);
    assert(*c.resolve(TextKey::base(0,42))=="Translated<END>");
    assert(*c.resolve(TextKey::base(1,42))=="別表<END>");
    assert(!c.resolve(TextKey::base(2,42)));
    assert(c.ui("manual")=="Manual");
    auto japanese=data;japanese["config"]["locale"]="ja";japanese["entries"]=nlohmann::json::object();
    japanese["ui"]["manual"]="手動";
    data["locale_catalogs"]={{"en",{{"config",data["config"]},{"entries",data["entries"]},{"ui",data["ui"]}}},
                              {"ja",{{"config",japanese["config"]},{"entries",japanese["entries"]},{"ui",japanese["ui"]}}}};
    initialize(data);const auto old_frame=snapshot();activate(find("ja"));
    assert(catalog().locale=="ja" && *catalog().resolve(TextKey::base(0,42))=="原文<END>");
    {Scope frame(old_frame);assert(catalog().locale=="en" && catalog().ui("manual")=="Manual");}
    assert(catalog().locale=="ja");
    bool rejected=false;try{activate(find("missing"));}catch(const std::runtime_error&){rejected=true;}
    assert(rejected && catalog().locale=="ja");
    std::thread rendering([&]{Scope frame(old_frame);for(int i=0;i<10000;++i)assert(catalog().ui("manual")=="Manual");});
    for(int i=0;i<10000;++i)activate(find(i%2?"ja":"en"));rendering.join();
    auto chinese=japanese;chinese["config"]["locale"]="zh-Hans";chinese["ui"]["manual"]="手动";
    data["locale_catalogs"]["zh-Hans"]={{"config",chinese["config"]},{"entries",{{"base:t00_00042","译文<END>"}}},{"ui",chinese["ui"]}};
    data["locale_options"]={{{"locale","ja"},{"label","日本語"}},{{"locale","zh-Hans"},{"label","简体中文"}},{{"locale","en"},{"label","English"}}};
    initialize(data);
    assert(next_locale("ja")=="zh-Hans" && next_locale("zh-Hans")=="en" && next_locale("en")=="ja");
    assert(display_name("en")=="English" && language_choices()=="日本語 / 简体中文 / English");
    for(const auto* locale:{"ja","zh-Hans","en"}) {
        activate(find(locale));assert(*catalog().resolve(TextKey::base(1,42))=="別表<END>");
    }
    auto invalid=data;invalid["locale_options"].erase(1);
    rejected=false;try{initialize(invalid);}catch(const std::runtime_error&){rejected=true;}
    assert(rejected && catalog().locale=="en" && next_locale("ja")=="zh-Hans");
    // Older one-language probes remain usable without a cycle manifest.
    auto single=japanese;initialize(single);assert(next_locale("ja")=="ja");
    assert(srw64::game_adapter::standard_dialogue_key(42).value=="base:t00_00042");
    assert(!srw64::game_adapter::dialogue_font_image(0xFD4800FB,0x000501F8,0x00FC0000));
    assert(srw64::game_adapter::dialogue_font_image(0xFD4800FB,0x000501F8,0x01F80000));
    srw64::presentation::ImageMode mode;
    mode.toggle();assert(!mode.enabled());
    mode.configure(false);mode.acknowledge(false);
    mode.toggle();assert(mode.requested() && mode.current()==0);
    // A request cannot claim to be rendered before acknowledgement.
    mode.acknowledge(true);assert(mode.current()==1);
    mode.toggle();assert(!mode.requested());
    mode.original_only();mode.request(true);mode.toggle();
    assert(!mode.enabled() && !mode.requested() && mode.current()==0);
    assert(c.locale=="en" && *c.resolve(TextKey::base(0,42))=="Translated<END>");
    {
        // Dialogue text files: the same cases as tests/test_dialogue_text.py.
        namespace dt=dialogue_text;
        const std::string source="「お嬢様、<G:0124><G:0124><G:0124>です」<BR> 二行目<STOP>次のページ<END>";
        assert((dt::pages_of(source)==std::vector<std::vector<std::string>>{{"「お嬢様、{HeroNick}です」"," 二行目"},{"次のページ"}}));
        const auto one=[&](const std::string& text,const std::string& original)->std::optional<std::string> {
            std::vector<dt::Problem> problems;auto entries=dt::parse(text,"t.txt",problems);
            assert(problems.empty() && entries.size()==1);
            return dt::compile(entries[0],&original);
        };
        const auto fails=[&](const std::string& text,const std::string& original,const std::string& message) {
            try{one(text,original);}catch(const std::runtime_error& error){return std::string(error.what()).find(message)!=std::string::npos;}
            return false;
        };
        assert(one("@17412 ローレンス\n> 「お嬢様、{HeroNick}です」\n>  二行目\n「小姐，{HeroNick}」\n第二行\n---\n> 次のページ\n下一页\n",source)==
               "「小姐，<G:0124>」<BR>第二行<STOP>下一页<END>");
        assert(one("@17412\n{HeroNick}小姐，\n第二行\n第三行\n---\n下一页\n",source)=="<G:0124>小姐，<BR>第二行<BR>第三行<STOP>下一页<END>");
        assert(fails("@17412\n只有一页\n",source,"pages"));
        assert(fails("@17412\n没有名字\n---\n下一页\n",source,"missing {HeroNick}"));
        assert(fails("@17412\n{HeroNick}{HeroName}\n---\n下一页\n",source,"extra {HeroName}"));
        assert(fails("@17412\n{HeroNik}\n---\n下一页\n",source,"unknown placeholder"));
        assert(fails("@17412\n{HeroNick} <b>\n---\n下一页\n",source,"half-width"));
        assert(fails("@17412\n> 違う原文\n{HeroNick}\n---\n下一页\n",source,"original text"));
        assert(fails("@17412\n{HeroNick}\n---\n",source,"no translation"));
        assert(fails("@100\n名字\n","名前<END>","term tables"));
        assert(!one("@17412\n> 「お嬢様、{HeroNick}です」\n---\n> 次のページ\n",source));
        assert(one("@18020\n> * 協力する\n> * 断る\n* 合作\n* 拒绝\n","協力する<BR>断る<END>")=="合作<BR>拒绝<END>");
        assert(fails("@18020\n> * 協力する\n> * 断る\n* 合作\n","協力する<BR>断る<END>","options"));
        assert(fails("@18020\n* 合作\n拒绝\n","協力する<BR>断る<END>","mixes"));
        assert(one("\xEF\xBB\xBF# 注释\r\n@17412 \r\n\\> 箭头 {HeroNick}\r\n\\* 星号\r\n---\r\n\\---\r\n",source)==
               "> 箭头 <G:0124><BR>* 星号<STOP>---<END>");
        std::vector<dt::Problem> problems;
        assert(dt::parse("孤立的一行\n@abc\n","t.txt",problems).empty() && problems.size()==2 &&
               problems[0].message=="text outside an entry" && problems[1].message=="malformed entry header");
        // Overrides: a later root wins entry by entry; a broken override falls back.
        const auto root=std::filesystem::temp_directory_path()/("srw64-dialogue-text-"+std::to_string(::getpid()));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root/"bundled/story");std::filesystem::create_directories(root/"user");
        std::ofstream(root/"bundled/story/a.txt")<<"@17412\n{HeroNick}\n---\n附带\n";
        std::ofstream(root/"bundled/story/b.txt")<<"@17412\n{HeroNick}\n---\n重复\n@17413\n另一条\n";
        std::ofstream(root/"user/mine.txt")<<"@17412\n{HeroNick}\n---\n玩家改的\n@17413\n只有一页\n---\n多了一页\n";
        Catalog text;auto text_data=data;text_data.erase("locale_catalogs");
        text_data["source_entries"]={{"base:t00_17412",source},{"base:t00_17413","別の台詞<END>"}};
        text.load(text_data);
        const auto loaded=dt::load({root/"bundled",root/"user"},text);
        assert(loaded.files==3 && loaded.targets.at("base:t00_17412")=="<G:0124><STOP>玩家改的<END>");
        assert(loaded.targets.at("base:t00_17413")=="另一条<END>" && loaded.problems.size()==2);
        assert(loaded.problems[0].path=="story/b.txt" && loaded.problems[1].path=="mine.txt");
        const auto layered=text.with_translations(loaded.targets,"dialogue");
        assert(*layered->resolve(TextKey::base(0,17413))=="另一条<END>" && *layered->source_text(TextKey::base(0,17413))=="別の台詞<END>");
        assert(*text.resolve(TextKey::base(0,17413))=="別の台詞<END>");
        std::filesystem::remove_all(root);
    }
    {
        // A reload swaps the registered catalogs; the active one changes only on activate().
        auto before=find("ja");
        std::map<std::string,Snapshot> next{{"ja",before->with_translations({{"base:t00_00042","新<END>"}},"dialogue")}};
        replace(next);
        assert(snapshot()==before && *find("ja")->resolve(TextKey::base(0,42))=="新<END>");
        bool refused=false;try{replace({});}catch(const std::runtime_error&){refused=true;}
        assert(refused);
    }
    std::cout<<"native content: table identity, fallback, UI catalog, image requests and dialogue text passed\n";
}
