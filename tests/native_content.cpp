#include "localization/catalog.hpp"
#include "game_adapter/dialogue_source.hpp"
#include "presentation/image_mode.hpp"
#include "presentation/display_list_snapshots.hpp"
#include "diagnostics.hpp"
#include "modal_input.hpp"
#include <cassert>
#include <iostream>
#include <thread>

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
    std::cout<<"native content: table identity, fallback, UI catalog and image requests passed\n";
}
