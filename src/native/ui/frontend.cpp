#include "frontend.hpp"
#include "app_menu.hpp"
#include "name_page.hpp"
#include "ui_renderer.h"
#include "RmlUi_Platform_SDL.h"
#include "native_dialogue.hpp"
#include "link_page.hpp"
#include "graphics.hpp"
#include "battle_page.hpp"
#include "mini_stage.hpp"
#include "settings_window.hpp"
#include "presentation_settings.hpp"
#include "rule_fixes.hpp"
#include "notices.hpp"
#include "debug_ui.hpp"
#include "debug_protocol.hpp"
#include "modal_input.hpp"
#include "presentation/image_mode.hpp"
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <set>

namespace srw64::ui {
namespace {
using json=nlohmann::json;
std::mutex mutex;
std::condition_variable completed;
bool in_flight{}, ready{}, initialized{};
SDL_Window* window{};
std::filesystem::path output;
std::unique_ptr<recompui::RmlRenderInterface_RT64> renderer;
// All access, including Render(), holds mutex. SDL callbacks are deferred to the
// window thread, including text deactivation during render-device teardown.
SystemInterface_SDL system;
TextInput input;
Rml::Context* context{};
std::unique_ptr<NamePage> name_page;
std::vector<Rml::byte> font;
std::map<std::string,std::string> images;
std::atomic_bool settings_open{}, physical_held{};
ModalInputRelease settings_release;
link_page::Request link_request;
std::array<bool,3> ticked{};
unsigned link_focus{};
bool link_waiting{};
Rml::ElementDocument *settings_doc{}, *link_doc{}, *notice_doc{};
std::string settings_stamp, link_stamp, notice_stamp;
json battle_request;
Rml::ElementDocument* battle_doc{},*original_doc{};
std::string battle_stamp,original_stamp;
Rml::ElementDocument* mini_doc{};
std::string mini_stamp;
std::string preedit;
std::mutex notice_mutex;
std::deque<json> notices_pending, notices_kept;
struct Banner {std::string text;double until;};
std::deque<Banner> banners;
int pixels_w{},pixels_h{};
float pixel_ratio=1;
std::unique_lock<std::mutex> lock_ui() {
    std::unique_lock lock(mutex);completed.wait(lock,[]{return !in_flight;});return lock;
}
std::string escape(const std::string& text){return Rml::StringUtilities::EncodeRml(text);}
std::string label(const std::string& key){return escape(localization::catalog().ui(key));}
const char* css=R"(
scrollbarvertical { width: 12dp; } scrollbarhorizontal { height: 12dp; }
scrollbarvertical slidertrack, scrollbarhorizontal slidertrack { background-color: #122131; }
scrollbarvertical sliderbar, scrollbarhorizontal sliderbar { background-color: #506d81; min-height: 16dp; min-width: 12dp; }
body { display: block; width: 100%; height: 100%; margin: 0; font-family: srw64-ui; font-size: 17dp; color: #d6e2ef; }
div,h1,h2,p { display: block; } h1 {font-size: 28dp; margin: 0 0 12dp;} h2 {font-size: 20dp; margin: 18dp 0 10dp;}
p {color: #9eafc3; margin: 10dp 0;} .modal {background-color: #0b1421;}
.page {width: 88%; max-width: 1080dp; margin: 24dp auto; height: 90%; overflow-y: auto;}
button {display: inline-block; background-color: #152436; color: #d6e2ef; border: 1dp #304859; border-radius: 6dp; padding: 10dp 14dp; margin: 4dp; cursor: pointer; tab-index: auto;}
button:hover,button:focus {border-color: #9be4f7;} button.on {background-color: #23506a; border-color: #9be4f7;}
button:disabled {opacity: 0.45;} .row {display: flex;} .column {width: 48%; margin-right: 2%;}
.rule {display: block; width: 92%; text-align: left; font-size: 15dp; padding: 7dp;}
.card {width: 28%;} img {width: 86dp; height: 86dp; margin: 8dp;}
#notices {width: 86%; margin: 8dp auto; text-align: center;} .banner {padding: 12dp; background-color: #122131ed; border: 1dp #9be4f7; margin-bottom: 6dp;}

.bp-dim {position:absolute; left:0; top:0; width:100%; height:100%; background-color:#070a1655;}
.bp-tint {position:absolute; top:0; width:35%; height:100%;}
.bp-tint.left {left:0; decorator:horizontal-gradient(#78144659 #080c1e00);}
.bp-tint.right {right:0; decorator:horizontal-gradient(#080c1e00 #0a3c6e59);}
.battle-page {position:relative; display:flex; flex-direction:column; width:100%; max-width:1500dp; height:100%; margin:0 auto; box-sizing:border-box; padding:12dp 18dp 8dp; gap:7dp; color:#e8eefc; font-size:11dp;}
.bp-row {display:flex; justify-content:space-between; align-items:stretch;}
.bp-mid {flex:1 1 0; min-height:0; align-items:center;}
.bp-side {width:31%; box-sizing:border-box;} .bp-center {width:35.5%; box-sizing:border-box;}
.bp-banner {height:124dp; overflow:hidden; padding:6dp 12dp; background-color:#0c122ceb; border:2dp #ff6fa8;}
.bp-banner.right {border-color:#3fd0ff;}
.bp-title {display:flex; align-items:center; gap:8dp; height:24dp;}
.bp-name {font-size:17dp; font-weight:bold; white-space:nowrap; overflow:hidden;}
.bp-tag {font-size:9dp; font-weight:bold; padding:1dp 5dp; white-space:nowrap; color:#ff6fa8; border:1dp #ff6fa8;}
.right .bp-tag {color:#3fd0ff; border-color:#3fd0ff;} .bp-tag.first {color:#0b1230; background-color:#ffd75e; border-color:#ffd75e;}
.bp-weapon {font-size:12dp; height:17dp; overflow:hidden; white-space:nowrap; color:#ff6fa8;} .right .bp-weapon {color:#3fd0ff;}
.battle-resource {display:flex; align-items:center; gap:7dp; margin:3dp 0; font-size:12dp;}
.battle-resource .resource-key {width:20dp; color:#ffd75e; font-weight:bold;}
.battle-resource .resource-value {width:84dp; text-align:right;}
.battle-track {height:6dp; background-color:#d03040; flex:1;}
.battle-track div {height:6dp; background-color:#34d05a;}
.weapon-cost {font-size:10dp; line-height:12dp; color:#a4b0d2; margin-top:3dp; height:24dp; overflow:hidden;}
.right .bp-title,.right .battle-resource,.right .bp-pilot-head,.right .bp-pilot-name,.right .bp-stat {flex-direction:row-reverse;}
.right .resource-value {text-align:left;} .right .resource-key {text-align:right;}
.right .battle-track {display:flex; justify-content:flex-end;}
.bp-banner.right,.bp-pilot.right {text-align:right;}
.battle-unit {display:flex; align-items:center; justify-content:center;}
.battle-unit img {margin:0; image-color:#fff;} .battle-unit img.face-right {transform:scaleX(-1);}
.bp-clash {display:flex; align-items:flex-end; background-color:#0c122cf0; border-top:3dp #3fd0ff; padding:7dp 10dp 9dp;}
.bp-clash-side {flex:1; min-width:0;} .bp-clash-side.left {text-align:right;}
.bp-damage {font-size:46dp; line-height:48dp; font-weight:bold; color:#ffd75e;}
.bp-caption {font-size:10dp; color:#a4b0d2;}
.bp-arrow {width:62dp; text-align:center; padding-bottom:16dp; font-size:8dp; line-height:10dp; color:#ffd75e;}
.bp-arrow div {font-size:13dp;}
.bp-versus {display:flex; align-items:center; margin-top:5dp; padding:3dp 14dp; background-color:#0c122ceb; font-size:20dp;}
.bp-versus span {flex:1; color:#3fd0ff;} .bp-versus span.left {text-align:right; color:#ff6fa8;}
.bp-versus b {width:100dp; margin:0 10dp; padding:2dp 0; text-align:center; font-size:11dp; color:#0b1230; background-color:#3fd0ff;}
.bp-detail {margin-top:5dp; padding:4dp 12dp; background-color:#0c122ce0; font-size:10dp; color:#d6ddf2;}
.bp-detail div {display:flex; align-items:center; margin:2dp 0;}
.bp-detail span {flex:1;} .bp-detail span.left {text-align:right;} .bp-detail span.good {color:#57e389;}
.bp-detail b {width:124dp; text-align:center; color:#a4b0d2; font-weight:normal;}
.battle-response {margin:6dp auto 0; text-align:center; font-size:11dp;}
.battle-response span {display:inline-block; padding:3dp 11dp; background-color:#3fd0ff29; border:1dp #3fd0ff;} .battle-response b {color:#3fd0ff;}
.bp-pilot {padding:7dp 9dp; background-color:#0c122ceb; border:2dp #ff6fa8;} .bp-pilot.right {border-color:#3fd0ff;}
.bp-pilot-head {display:flex; gap:8dp;}
.bp-pilot-head img {width:56dp; height:56dp; margin:0; border:2dp #ff6fa8;} .right .bp-pilot-head img {border-color:#3fd0ff;}
.bp-pilot-info {flex:1; min-width:0;}
.bp-pilot-name {display:flex; justify-content:space-between; align-items:flex-end; font-size:15dp; font-weight:bold; height:21dp;}
.bp-pilot-name .level {font-size:10dp; font-weight:normal; color:#a4b0d2;}
.bp-stats {display:flex; gap:4dp; margin-top:3dp;}
.bp-stat {flex:1; display:flex; justify-content:space-between; padding:2dp 6dp; font-size:11dp; background-color:#ff6fa81f;} .right .bp-stat {background-color:#3fd0ff1f;}
.bp-stat span {color:#a4b0d2;} .bp-stat b.spent {color:#ffd75e;}
.battle-spirits {display:flex; flex-wrap:wrap; margin-top:5dp; height:34dp; overflow-y:auto; font-size:10dp; gap:2dp;}
.right .battle-spirits {justify-content:flex-end;}
.battle-spirits span {display:inline-block; padding:1dp 4dp; height:13dp; color:#7e88a8; border:1dp #4a5578; background-color:#ffffff0a;}
.battle-spirits span.active {color:#ffd75e; border-color:#ffd75e; background-color:#ffd75e2e; font-weight:bold;}
.battle-spirits span.defensive {color:#57e389; border-color:#57e389; background-color:#57e3892e; font-weight:bold;}
.battle-spirits .muted {border:0; color:#7e88a8;}
.battle-defenses {border-top:1dp #ff6fa859; margin-top:6dp; padding-top:4dp; height:15dp; font-size:11dp; color:#57e389;} .right .battle-defenses {border-color:#3fd0ff59;}
.battle-defense-note {font-size:9dp; white-space:normal; color:#a4b0d2; margin-top:2dp;}
.battle-defense-conditions {height:12dp; overflow:hidden; white-space:nowrap;}
.battle-effects {height:58dp; overflow-y:auto; font-size:10dp; line-height:1.3;}
.battle-effects b {color:#a4b0d2; font-weight:normal;}
.battle-effect {margin:2dp 0; color:#d6ddf2;} .battle-effect strong {color:#57e389;} .battle-effect strong.off {color:#a4b0d2;}
.battle-actions {text-align:center; display:flex; flex-direction:column; justify-content:center;}
.battle-actions button {margin:3dp; padding:5dp 11dp; font-size:12dp; background-color:#0c122ceb; border:2dp #3fd0ff; border-radius:0; color:#e8eefc;}
.battle-actions button:hover,.battle-actions button:focus {background-color:#3fd0ff40; border-color:#ffd75e;}
.battle-actions #battle-confirm {display:block; width:220dp; margin:0 auto 5dp; padding:6dp 0; border:2dp #12a53c; background-color:#12a53c; color:#fff; font-size:19dp; font-weight:bold; letter-spacing:3dp;}
.battle-actions #battle-confirm:hover,.battle-actions #battle-confirm:focus {background-color:#16bf46; border-color:#ffd75e;}
.bp-segment {margin:2dp 0 4dp;} .bp-segment div {display:inline-block; border:2dp #3fd0ff; background-color:#0c122ceb;}
.battle-actions .bp-segment button {margin:0; border:0; padding:5dp 17dp; font-size:12dp; font-weight:bold; background-color:transparent;}
.battle-actions .bp-segment button.on {background-color:#3fd0ff; color:#0b1230;}
.battle-actions .bp-segment button:hover,.battle-actions .bp-segment button:focus {background-color:#ffd75e; color:#0b1230;}
.bp-original {position:absolute; bottom:14dp; left:0; width:100%; text-align:center; font-size:15dp; color:#a4b0d2;}
.bp-original div {display:inline-block; padding:5dp 16dp; background-color:#0c122ceb; border:1dp #3fd0ff;}
.bp-original .key {color:#ffd75e;} .bp-original b {color:#e8eefc;}
.bp-hints {text-align:center; font-size:10dp; color:#a4b0d2; height:14dp; white-space:nowrap; overflow:hidden;}
.bp-hints span {margin:0 9dp;} .bp-hints b {color:#ffd75e; font-weight:normal;}
.spirit-shade {position:absolute; left:0; top:0; width:100%; height:100%; background-color:#04071299;}
.spirit-overlay {position:absolute; left:22%; top:12%; width:56%; max-height:78%; box-sizing:border-box; padding:12dp 22dp 13dp; color:#e8eefc; background-color:#0c122cfa; border-top:3dp #3fd0ff; border-bottom:3dp #3fd0ff;}
.spirit-overlay h2 {margin:0; font-size:16dp;} .spirit-overlay p {margin:2dp 0 8dp; font-size:10dp; color:#a4b0d2;}
.spirit-options {max-height:340dp; overflow-y:auto;}
.spirit-options button {display:block; width:98%; box-sizing:border-box; margin:3dp 0; padding:6dp 8dp; font-size:14dp; text-align:left; border-radius:0; border:0; border-left:4dp #3fd0ff40; background-color:transparent; color:#e8eefc;}
.spirit-options button:hover,.spirit-options button:focus {background-color:#3fd0ff29; border-left:4dp #3fd0ff;}
.spirit-options button.on {border-left:4dp #ffd75e; background-color:#ffd75e1f; color:#ffd75e; opacity:1;}
.spirit-options .spirit-row {display:flex; align-items:center; gap:8dp;}
.spirit-options .name {width:96dp;} .spirit-options .cost {width:48dp; font-size:11dp; color:#ffd75e;}
.spirit-options .detail {flex:1; font-size:10dp; color:#d6ddf2;} .spirit-options .status {font-size:10dp; text-align:right;}
.spirit-footer {text-align:right; margin-top:8dp;}
.spirit-footer button {margin:0; padding:4dp 12dp; font-size:11dp; font-weight:bold; border-radius:0; color:#0b1230; background-color:#3fd0ff; border:2dp #3fd0ff;}
.spirit-footer button:hover,.spirit-footer button:focus {background-color:#8fe4ff; border-color:#ffd75e;}



)";
void document_close(Rml::ElementDocument*& doc) {
    if(doc){doc->Close();doc=nullptr;}
}
bool held();
void choose(const std::string& id);
void battle_buttons(uint32_t pressed);
struct Actions : Rml::EventListener {
    void ProcessEvent(Rml::Event& event) override {
        for(auto* el=event.GetTargetElement();el;el=el->GetParentNode())
            if(el->GetTagName()=="button"){choose(el->GetId());break;}
    }
} actions;
Rml::ElementDocument* document(const std::string& body,bool modal) {
    auto* doc=context->LoadDocumentFromMemory("<rml><head><style>"+std::string(css)+"</style></head><body style='pointer-events: "+std::string(modal?"auto":"none")+";' class='"+(modal?"modal":"")+"'>"+body+"</body></rml>");
    if(!doc)throw std::runtime_error("Cannot create shared UI document");
    doc->AddEventListener("click",&actions);doc->Show(Rml::ModalFlag::None,Rml::FocusFlag::None);return doc;
}
std::string button(const std::string& id,const std::string& text,bool on=false,bool disabled=false,const std::string& cls="") {
    return "<button id='"+escape(id)+"' class='"+cls+(on?" on":"")+"'"+(disabled?" disabled":"")+">"+text+"</button>";
}
std::string image(const std::string& path) {
    if(path.empty())return {};
    if(auto found=images.find(path);found!=images.end())return found->second;
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot load shared UI portrait: "+path);
    // RmlUi normalizes leading slashes in URLs. Stable relative resource keys
    // avoid platform-specific filesystem paths leaking into its URL resolver.
    const auto name="portrait-"+std::to_string(images.size());
    renderer->queue_image_from_bytes_file(name,{std::istreambuf_iterator<char>(file),{}});images[path]=name;return name;
}

void settings_sync() {
    if(!settings_open){document_close(settings_doc);settings_stamp.clear();return;}
    const auto stamp=localization::catalog().locale+std::to_string(rules::active_fixes())+std::to_string(presentation::image_mode.requested())+std::to_string(settings::native_battle_ui())+
        std::to_string(presentation::image_mode.enabled())+std::to_string(settings::owns_input())+std::to_string(settings::failed());
    if(settings_doc && stamp==settings_stamp){settings_doc->PullToFront();return;}
    document_close(settings_doc);settings_stamp=stamp;
    std::string body="<div class='page'><h1>"+label("settings_title")+"</h1><div class='row'><div class='column'>";
    for(auto group:{rules::Kind::correction,rules::Kind::difficulty}){
        body+="<h2>"+label(group==rules::Kind::correction?"rules_group_corrections":"rules_group_difficulty")+"</h2>";
        for(const auto& entry:rules::catalog)if(entry.kind==group)
            body+=button("rule:"+std::string(entry.id),label(rules::ui_key(entry.id)),rules::active_fixes()&entry.fix,false,"rule");
    }
    body+="</div><div class='column'><h2>"+label("settings_language")+"</h2>";
    for(const auto& [locale,catalog]:localization::registered())body+=button("locale:"+locale,escape(localization::display_name(locale)),locale==localization::catalog().locale,settings::owns_input());
    body+="<p>"+label("settings_language_note")+"</p><h2>"+label("settings_images")+"</h2>";
    for(auto mode:{"original","hd"})body+=button(std::string("images:")+mode,label(std::string("settings_images_")+mode),presentation::image_mode.requested()==(std::string(mode)=="hd"),!presentation::image_mode.enabled());
    body+="<p>"+label("settings_images_note")+"</p><h2>"+label("settings_battle_ui")+"</h2>";
    for(auto mode:{"native","original"})body+=button(std::string("battle-ui:")+mode,label(std::string("settings_battle_ui_")+mode),settings::native_battle_ui()==(std::string(mode)=="native"));
    body+="<p>"+label("settings_battle_ui_note")+"</p><h2>"+label("rules_menu")+"</h2>";
    for(const auto& preset:rules::presets)body+=button("preset:"+std::string(preset.key),label(std::string(preset.key)));
    body+="<p>"+label("rules_note")+"</p>";
    if(settings::failed())body+="<p>"+label("settings_error")+"</p>";
    body+=button("settings-close",label("link_back"))+"</div></div></div>";
    settings_doc=document(body,true);settings_doc->PullToFront();settings_doc->Focus();
}
void link_sync() {
    const auto next=link_page::request();
    if(!next.visible){link_request=next;document_close(link_doc);link_stamp.clear();return;}
    if(next.serial!=link_request.serial){
        ticked=next.scheduled;link_focus=0;link_waiting=false;
        for(unsigned i=0;i<3;++i)if(!next.joined[i] && !next.scheduled[i]){link_focus=i;break;}
    }
    link_request=next;
    std::string stamp=std::to_string(next.serial)+localization::catalog().locale+std::to_string(link_focus)+std::to_string(link_waiting);
    for(bool value:ticked)stamp+=value?'1':'0';
    if(link_doc && link_stamp==stamp)return;
    document_close(link_doc);link_stamp=stamp;
    std::string body="<div class='page'><h1>"+label("link_title")+"</h1><p>"+label("link_hint")+"</p><div class='row'>";
    const char* keys[]={"f91","goshogun","zambot"};
    for(unsigned i=0;i<3;++i){
        std::string content;
        for(const auto& path:next.portraits[i]){content+="<img src='"+image(path)+"'/>";}
        content+="<h2>"+label(std::string("link_series_")+keys[i])+"</h2><p>"+label(std::string("link_lead_")+keys[i])+"</p><p>"+label(std::string("link_units_")+keys[i])+"</p><p>"+label(std::string("link_crew_")+keys[i])+"</p>";
        if(next.joined[i] || next.scheduled[i] || ticked[i])content+="<p>"+label(next.joined[i]?"link_joined":next.scheduled[i]?"link_scheduled":"link_ticked")+"</p>";
        body+=button("link:"+std::to_string(i),content,ticked[i] || i==link_focus,link_waiting || next.joined[i] || next.scheduled[i],"card");
    }
    body+="</div><p>"+label("link_keyboard_hint")+"</p>"+button("link-back",label("link_back"),false,link_waiting)+button("link-next",label("link_confirm"),true,link_waiting)+"</div>";
    link_doc=document(body,true);
}
std::string battle_number(int value,bool sign=false){return (sign && value>=0?"+":"")+std::to_string(value);}
// Abilities the unit really has. A skill without its equipment or level is left
// out; dummies only count while some remain.
bool battle_has(const json& e) {
    const auto reason=e.value("reason",std::string{});
    return reason!="no_equipment" && reason!="no_skill";
}
std::string battle_effects(const json& c) {
    const auto pilots=c.value("pilot_effects",json::array()),defenses=c.value("defensive_effects",json::array());
    std::string out="<div class='battle-effects'>";
    for(const auto& e:pilots) {
        const auto id=e.at("id").get<std::string>();const bool active=e.at("active");
        out+="<div class='battle-effect'><strong class='"+std::string(active?"":"off")+"'>"+label("battle_skill_"+id)+" L"+battle_number(e.at("level"))+"</strong> · ";
        if(active) {
            out+=label("battle_effect_hit")+" "+battle_number(e.at("hit"),true)+" · "+label("battle_effect_evade")+" "+battle_number(e.at("evade"),true);
            if(id=="potential" && c.at("side").get<int>()==0)out+=" · "+label("battle_effect_crit")+" "+battle_number(e.at("critical"),true);
            if(e.value("critical_suppressed",false))out+=" · "+label("battle_effect_heat");
        } else {
            out+=label("battle_effect_inactive");
            if(id=="potential" && !e.value("full_hp",false))out+=" · HP &lt; "+battle_number(e.at("threshold"))+"%";
        }
        out+="</div>";
    }
    for(const auto& e:defenses) {
        const auto id=e.at("id").get<std::string>();
        // Parry, shield and clone abilities are on the summary line above the list.
        if(!battle_has(e) || e.contains("chance") || (id=="dummy" && e.at("remaining").get<int>()<=0))continue;
        out+="<div class='battle-effect'><strong class='"+std::string(e.value("chance",1)>0?"":"off")+"'>"+label("battle_skill_"+id);
        if(e.contains("level"))out+=" L"+battle_number(e.at("level"));
        if(id=="dummy")out+=" ×"+battle_number(e.at("remaining"));
        out+="</strong>";
        if(e.contains("chance"))out+=" · "+battle_number(e.at("chance"))+"%";
        if(e.contains("strength"))out+=" · "+battle_number(e.at("strength"));
        if(e.contains("damage"))out+=" · "+battle_number(e.at("damage"));
        if(e.contains("reason") && e.at("reason")!="ready") {
            const auto reason=e.at("reason").get<std::string>();
            out+=" · "+label((reason=="non_beam" || reason=="en_low" || reason=="broken" || reason=="absorbed" || reason=="reduced"?"battle_barrier_":"battle_effect_")+reason);
        }
        out+="</div>";
    }
    return out+"</div>";
}
// Top banner: unit, turn-order tag, weapon and mirrored HP/EN bars.
std::string battle_banner(const json& c,bool left,bool first,const std::string& response) {
    const std::string side=left?"left":"right";const bool armed=c.at("weapon").get<int>()>=0;
    std::string body="<div id='battle-card-"+side+"' class='bp-side bp-banner "+side+"'><div class='bp-title'><span class='bp-name'>"+escape(c.at("unit_name").get<std::string>())+"</span>";
    body+="<span class='bp-tag"+std::string(first?" first":"")+"'>"+(first?label("battle_first"):label("battle_second")+" · "+response)+"</span></div>";
    body+="<div class='bp-weapon'>"+(armed?escape(c.at("weapon_name").get<std::string>()):label("battle_none"))+"</div>";
    for(const auto* kind:{"hp","en"}) {
        const int value=c.at(kind),maximum=c.at(std::string("max_")+kind);
        body+="<div class='battle-resource'><span class='resource-key'>"+std::string(kind==std::string("hp")?"HP":"EN")+"</span><div class='battle-track'><div style='width:"+std::to_string(maximum?std::clamp(value*100/maximum,0,100):0)+"%;'></div></div><span class='resource-value'>"+battle_number(value)+" / "+battle_number(maximum)+"</span></div>";
    }
    body+="<div class='weapon-cost'>";
    if(armed)body+=label("battle_cost")+" "+battle_number(c.at("en_cost"))+" · "+label("battle_ammo")+" "+(c.at("ammo").get<int>()<0?"—":battle_number(c.at("ammo")))+" · "+label("battle_hit_mod")+" "+battle_number(c.at("hit_modifier"),true)+" · "+label("battle_crit_mod")+" "+battle_number(c.at("critical_modifier"),true);
    else body+="—";
    return body+"</div></div>";
}
std::string battle_unit(const json& c,bool left) {
    std::string body="<div class='bp-side battle-unit'>";
    if(const auto unit=c.value("unit_art",json::object());unit.contains("path")) {
        const float scale=std::min(230.f/unit.at("width").get<float>(),220.f/unit.at("height").get<float>());
        body+="<img class='"+std::string(left?"face-right":"face-left")+"' src='"+escape(image(unit.at("path")))+"' style='width:"+std::to_string(unit.at("width").get<float>()*scale)+"dp;height:"+std::to_string(unit.at("height").get<float>()*scale)+"dp;'/>";
    }
    return body+"</div>";
}
// Bottom panel: pilot identity, spirit state, special defenses and abilities.
std::string battle_pilot(const json& c,bool left) {
    const std::string side=left?"left":"right";
    std::string body="<div id='battle-pilot-"+side+"' class='bp-side bp-pilot "+side+"'><div class='bp-pilot-head'>";
    if(const auto face=c.value("portrait",json::object());face.contains("path"))body+="<img src='"+escape(image(face.at("path")))+"'/>";
    body+="<div class='bp-pilot-info'><div class='bp-pilot-name'><span>"+escape(c.at("pilot_name").get<std::string>())+"</span><span class='level'>Lv "+battle_number(c.at("level"))+"</span></div>";
    body+="<div class='bp-stats'><div class='bp-stat'><span>"+label("battle_morale")+"</span><b>"+battle_number(c.at("morale"))+"</b></div>";
    body+="<div class='bp-stat'><span>SP</span><b class='"+std::string(c.at("sp").get<int>()<c.at("max_sp").get<int>()?"spent":"")+"'>"+battle_number(c.at("sp"))+" / "+battle_number(c.at("max_sp"))+"</b></div></div><div class='battle-spirits'>";
    const auto spirits=c.value("spirit_grid",json::array());
    if(spirits.empty())body+="<span class='muted'>"+label("battle_spirits_none")+"</span>";
    for(const auto& spirit:spirits) {
        const bool active=spirit.at("active");const unsigned id=spirit.at("id");
        body+="<span class='"+std::string(!active?"":id==5 || id==9 || id==18?"defensive":"active")+"'>"+escape(spirit.at("name").get<std::string>())+"</span>";
    }
    const auto& d=c.at("defense");
    bool parry=false,shield=false,clone=false;std::string clone_name;
    for(const auto& e:c.value("defensive_effects",json::array())) {
        if(!e.contains("chance") || !battle_has(e))continue;
        const auto id=e.at("id").get<std::string>();
        (id=="parry"?parry:id=="shield"?shield:clone)=true;
        if(id!="parry" && id!="shield")clone_name=label("battle_skill_"+id); // the unit's own variant
    }
    std::string defenses,conditions;
    const auto add=[](std::string& line,const std::string& text){line+=(line.empty()?"":" · ")+text;};
    if(parry)add(defenses,label("battle_parry")+" "+battle_number(d.at("parry"))+"%");
    if(shield)add(defenses,label("battle_shield")+" "+battle_number(d.at("shield"))+"%");
    if(clone)add(defenses,clone_name+" "+battle_number(d.at("clone"))+"%");
    if(parry && d.at("parry_reason")=="uncuttable")add(conditions,label("battle_uncuttable"));
    if((parry && d.at("parry_reason")=="sure_hit") || (clone && d.at("clone_reason")=="sure_hit"))add(conditions,label("battle_sure_hit"));
    if(clone && d.at("clone_reason")=="morale_low")add(conditions,label("battle_clone_morale"));
    if(shield && d.at("shield_reason")=="barrier_first")add(conditions,label("battle_barrier_first"));
    body+="</div></div></div><div class='battle-defenses'>"+defenses+"</div><div class='battle-defense-note battle-defense-conditions'>"+conditions;
    return body+"</div>"+battle_effects(c)+"</div>";
}
// Centre column: both conditional damages face each other; "—" keeps the
// rows in place when a side does not attack.
std::string battle_clash(const json& enemy,const json& player,bool player_first,const std::string& response,bool responding) {
    const auto armed=[](const json& c){return c.at("weapon").get<int>()>=0;};
    const auto value=[&](const json& c,const char* key,const char* suffix=""){return armed(c)?battle_number(c.at(key))+suffix:std::string("—");};
    const auto reduced=[&](const json& c) {
        if(!armed(c) || c.at("barrier").at("kind")=="none")return false;
        const auto status=c.at("barrier").at("status").get<std::string>();return status=="absorbed" || status=="reduced";
    };
    const auto barrier=[&](const json& c) {
        if(!armed(c) || c.at("barrier").at("kind")=="none")return std::string("—");
        // Numbers only when the barrier changes this attack's damage.
        return label("battle_barrier_"+c.at("barrier").at("status").get<std::string>())+(reduced(c)?" · "+battle_number(c.at("damage_raw"))+" → "+battle_number(c.at("damage")):std::string{});
    };
    const auto critical=[&](const json& c){return armed(c) && c.at("critical").get<int>()>0?battle_number(c.at("critical_damage")):std::string("—");};
    std::string body="<div class='bp-center'><div class='bp-clash'><div class='bp-clash-side left'><div class='bp-damage'>"+value(enemy,"damage")+"</div><div class='bp-caption'>"+label("battle_damage_if_hit")+"</div></div>";
    body+="<div class='bp-arrow'><div>"+std::string(player_first?"◀━━":"━━▶")+"</div>"+label(player_first?"battle_first_player":"battle_first_enemy")+"</div>";
    body+="<div class='bp-clash-side right'><div class='bp-damage'>"+value(player,"damage")+"</div><div class='bp-caption'>"+label("battle_damage_if_hit")+"</div></div></div>";
    for(const auto* row:{"hit","critical"})
        body+="<div class='bp-versus'><span class='left'>"+value(enemy,row,"%")+"</span><b>"+label(std::string("battle_")+row)+"</b><span>"+value(player,row,"%")+"</span></div>";
    body+="<div class='bp-detail'><div><span class='left'>"+critical(enemy)+"</span><b>"+label("battle_critical_damage")+"</b><span>"+critical(player)+"</span></div>";
    const auto shielded=[&](const json& c){return armed(c) && c.value("target_shield",0)>0?battle_number(c.at("damage_if_shield")):std::string("—");};
    body+="<div><span class='left'>"+shielded(enemy)+"</span><b>"+label("battle_shield_damage")+"</b><span>"+shielded(player)+"</span></div>";
    body+="<div><span class='left"+std::string(reduced(enemy)?" good":"")+"'>"+barrier(enemy)+"</span><b>"+label("battle_barrier")+"</b><span class='"+std::string(reduced(player)?"good":"")+"'>"+barrier(player)+"</span></div></div>";
    if(responding)body+="<div class='battle-response'><span>"+label("battle_response")+" · <b>"+response+"</b></span></div>";
    return body+"</div>";
}
// The original's four panels in its own 320x240 coordinates (screen layouts 0x69 /
// 0x8D, swap window 0x86), scaled to the 4:3 game picture inside the window.
float text_units(const std::string& text) {
    float units=0;
    for(unsigned char c:text)if((c&0xC0)!=0x80)units+=c<0x80?0.58f:1.f;
    return units;
}
void mini_sync() {
    const auto state=mini_stage::snapshot();
    const bool entering=state.value("entering",false);
    const bool available=state.value("available",false) && !state.value("applied",0u) && intro::title_major()==3;
    if(!entering && !available){document_close(mini_doc);mini_stamp.clear();return;}
    const auto stamp=state.dump()+localization::catalog().locale;
    if(mini_doc && stamp==mini_stamp)return;
    document_close(mini_doc);mini_stamp=stamp;
    std::string body="<div style='position:absolute; bottom:32dp; left:25%; width:50%; text-align:center;'>";
    body+=entering?"<h2>"+label("mini_entering")+"</h2>":button("mini-enter",label("mini_enter"));
    body+="<p>"+escape(state.value("name",std::string{}))+"</p></div>";
    mini_doc=document(body,true);if(!entering)mini_doc->SetClass("modal",false);
}
void battle_sync() {
    const auto next=battle_page::state();battle_request=next;
    if(!next.value("visible",false)) {
        document_close(battle_doc);battle_stamp.clear();
        // Original HUD: only the animation state and its toggle key.
        const std::string stamp=next.value("original",false)?"original"+std::to_string(next.value("animation",true))+localization::catalog().locale:"";
        if(stamp!=original_stamp) {
            document_close(original_doc);original_stamp=stamp;
            if(!stamp.empty())original_doc=document("<div class='bp-original'><div><span class='key'>[K / C\xe2\x96\xbc]</span> "+label("battle_animation")+" \xc2\xb7 <b>"+label(next.value("animation",true)?"battle_on":"battle_off")+"</b></div></div>",false);
        }
        return;
    }
    document_close(original_doc);original_stamp.clear();
    const auto stamp=next.dump()+localization::catalog().locale;
    if(battle_doc && battle_stamp==stamp)return;
    document_close(battle_doc);battle_stamp=stamp;
    // Keep our unit on the right, matching the original battle HUD. Direction
    // follows screen position, not attacker/defender role or faction arithmetic.
    const bool player_attacks=next.at("attacker").at("side")==0,responding=next.value("mode",0)==2;
    const auto& enemy=next.at(player_attacks?"defender":"attacker");const auto& player=next.at(player_attacks?"attacker":"defender");
    const int response=next.value("response",0);
    const auto player_response=label(response==1?"battle_evade":response==2?"battle_defend":player.at("weapon").get<int>()<0?"battle_none":"battle_counter");
    const auto enemy_response=label(enemy.at("weapon").get<int>()<0?"battle_none":"battle_counter");
    const bool selecting_spirit=next.value("spirit_menu",false);
    std::string body="<div class='bp-dim'></div><div class='bp-tint left'></div><div class='bp-tint right'></div><div class='battle-page'><div class='bp-row'>";
    body+=battle_banner(enemy,true,!player_attacks,enemy_response)+"<div class='bp-center'></div>"+battle_banner(player,false,player_attacks,player_response)+"</div>";
    body+="<div class='bp-row bp-mid'>"+battle_unit(enemy,true)+battle_clash(enemy,player,player_attacks,player_response,responding)+battle_unit(player,false)+"</div>";
    body+="<div class='bp-row'>"+battle_pilot(enemy,true)+"<div class='bp-center battle-actions'><div>"+button("battle-confirm",label("battle_confirm"),false,selecting_spirit)+"</div>";
    if(responding) {
        body+="<div class='bp-segment'><div>"+button("battle-counter",label("battle_counter"),response==0,selecting_spirit);
        body+=button("battle-evade",label("battle_evade"),response==1,selecting_spirit);
        body+=button("battle-defend",label("battle_defend"),response==2,selecting_spirit)+"</div></div>";
    }
    body+="<div>"+button("battle-weapon",label("battle_change_weapon"),false,selecting_spirit);
    body+=button("battle-spirits",label("battle_spirits"),false,selecting_spirit);
    body+=button("battle-animation",label("battle_animation")+" · "+label(next.value("animation",true)?"battle_on":"battle_off"),false,selecting_spirit);
    if(next.value("can_cancel",false))body+=button("battle-back",label("battle_back"),false,selecting_spirit);
    body+="</div></div>"+battle_pilot(player,false)+"</div><div class='bp-hints'>";
    const auto hint=[&](const char* key,const std::string& text){body+="<span><b>["+std::string(key)+"]</b> "+text+"</span>";};
    hint("Z / A",label("battle_confirm"));hint("Q / L",label("battle_change_weapon"));hint("E / R",label("battle_spirits"));hint("K / C▼",label("battle_animation"));
    if(next.value("can_cancel",false))hint("X / B",label("battle_back"));
    body+="</div></div>";
    if(selecting_spirit) {
        body+="<div class='spirit-shade'></div><div class='spirit-overlay'><h2>"+label("battle_spirits")+" · "+escape(player.at("unit_name").get<std::string>())+"</h2><p>"+label("battle_spirit_hint")+"</p><div class='spirit-options'>";
        for(const auto& o:next.at("spirit_options")) {
            const bool used=(player.at("spirits").get<uint32_t>()>>o.at("id").get<unsigned>())&1;
            auto content="<div class='spirit-row'><span class='name'>"+escape(o.at("name").get<std::string>())+"</span><span class='cost'>SP "+battle_number(o.at("cost"))+"</span><span class='detail'>"+escape(o.at("pilot_name").get<std::string>())+" · SP "+battle_number(o.at("sp"))+" / "+battle_number(o.at("max_sp"))+"</span><span class='status'>"+label(used?"battle_effect_active":"battle_spirit_"+o.at("reason").get<std::string>())+"</span></div>";
            body+=button("battle-cast:"+battle_number(o.at("crew"))+":"+battle_number(o.at("slot")),content,used,!o.at("enabled").get<bool>());
        }
        body+="</div><div class='spirit-footer'>"+button("battle-spirit-back",label("battle_spirit_back"))+"</div></div>";
    }
    battle_doc=document(body,true);battle_doc->SetClass("modal",false);battle_doc->GetElementById(selecting_spirit?"battle-spirit-back":"battle-confirm")->Focus();

}

void choose(const std::string& id) {
    if(id=="mini-enter"){mini_stage::hotkey();return;}
    if(id.starts_with("battle-") && !id.starts_with("battle-ui:") && battle_request.value("visible",false) && !settings_open){battle_page::answer(battle_request.at("serial"),id.substr(7));return;}
    if(id=="settings-open"){settings_open=true;settings_release.hold();input.clear();return;}
    if(id=="settings-close"){physical_held=held();settings_open=false;return;}
    if(id.starts_with("link") && link_request.visible && !link_waiting && !settings_open){
        if(id=="link-back" || id=="link-next"){
            unsigned selected=0;for(unsigned i=0;i<3;++i)if(ticked[i] && !link_request.joined[i])selected|=1u<<i;
            link_page::answer(link_request.serial,selected,id=="link-next");link_waiting=true;
        } else if(id.size()==6 && id.starts_with("link:") && id[5]>='0' && id[5]<='2'){
            const unsigned i=id[5]-'0';link_focus=i;if(!link_request.joined[i] && !link_request.scheduled[i])ticked[i]=!ticked[i];
        }
        return;
    }
    if(!settings_open)return;
    try {
        if(id.starts_with("rule:"))for(const auto& entry:rules::catalog)if(entry.id==id.substr(5))rules::set_fixes(rules::active_fixes()^entry.fix);
        if(id.starts_with("preset:"))for(const auto& preset:rules::presets)if(preset.key==id.substr(7))rules::set_fixes(preset.fixes);
        if(id.starts_with("locale:") && !input.has_composition())settings::request_locale(id.substr(7));
        if(id.starts_with("images:") && presentation::image_mode.enabled())presentation::image_mode.request(id=="images:hd");
        if(id.starts_with("battle-ui:"))settings::set_native_battle_ui(id=="battle-ui:native");
    } catch(const std::exception& error){notices::post("settings-error",error.what());}
}
// Newly pressed N64 buttons on the battle page, from the keyboard table in
// dispatch() or from the controller. One binding for both:
// pad/stick move the focus, A or START activates it, B goes back, L opens the
// weapon list, R the spirits, C-down toggles the battle animation.
void battle_buttons(uint32_t pressed) {
    if(!battle_doc || !battle_request.value("visible",false) || settings_open)return;
    const bool spirits=battle_request.value("spirit_menu",false);
    if(pressed&0x4000){choose("battle-back");return;}
    if(!spirits) {
        if(pressed&0x0020){choose("battle-weapon");return;}
        if(pressed&0x0010){choose("battle-spirits");return;}
        if(pressed&0x0004){choose("battle-animation");return;}
    }
    if(pressed&(0x0F00|(0xFu<<16))) {
        std::vector<Rml::Element*> items;
        if(spirits) {
            for(const auto& o:battle_request.at("spirit_options"))if(o.at("enabled").get<bool>())
                if(auto* e=battle_doc->GetElementById("battle-cast:"+battle_number(o.at("crew"))+":"+battle_number(o.at("slot"))))items.push_back(e);
            items.push_back(battle_doc->GetElementById("battle-spirit-back"));
        } else for(const char* id:{"battle-confirm","battle-counter","battle-evade","battle-defend","battle-weapon","battle-spirits","battle-animation","battle-back"})
            if(auto* e=battle_doc->GetElementById(id))items.push_back(e);
        auto* focus=context->GetFocusElement();auto it=std::find(items.begin(),items.end(),focus);
        const int index=it==items.end()?0:int(it-items.begin());
        const bool reverse=pressed&(0x0800|0x0200|(1u<<16)|(1u<<18));
        items[(index+(reverse?int(items.size())-1:1))%items.size()]->Focus();return;
    }
    if(pressed&(0x8000|0x1000)) {
        auto* focus=context->GetFocusElement();
        choose(focus && focus->GetId().starts_with("battle-")?focus->GetId():"battle-confirm");
    }
}
void notices_sync() {
    const double now=system.GetElapsedTime();
    while(!banners.empty() && banners.front().until<=now)banners.pop_front();
    {std::lock_guard lock(notice_mutex);while(!notices_pending.empty() && banners.size()<3){banners.push_back({notices_pending.front().at("text"),now+6});notices_pending.pop_front();}}
    std::string stamp,body="<div id='notices'>";
    for(const auto& b:banners){stamp+=b.text+std::to_string(b.until);body+="<div class='banner'>"+escape(b.text)+"</div>";}
    body+="</div>";
    if(stamp!=notice_stamp){document_close(notice_doc);notice_stamp=stamp;if(!banners.empty())notice_doc=document(body,false);}
    if(notice_doc)notice_doc->PullToFront();
}
void initialize() {
    Rml::SetSystemInterface(&system);Rml::SetRenderInterface(renderer->get_rml_interface());
    if(!Rml::Initialise())throw std::runtime_error("Cannot initialize shared UI");
    initialized=true;
    std::filesystem::path path;
    if(const char* explicit_font=std::getenv("SRW64_UI_FONT"))path=explicit_font;
    else for(const auto* candidate:{"/System/Library/Fonts/Supplemental/Arial Unicode.ttf","C:/Windows/Fonts/msyh.ttc","/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"})
        if(std::filesystem::is_regular_file(candidate)){path=candidate;break;}
    std::ifstream file(path,std::ios::binary);font={std::istreambuf_iterator<char>(file),{}};
    if(font.empty() || !Rml::LoadFontFace(font,"srw64-ui",Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal,true))
        throw std::runtime_error("Shared UI needs a CJK font: set SRW64_UI_FONT to a local TTF/OTF/TTC file");
    context=Rml::CreateContext("game-ui",{pixels_w,pixels_h},nullptr,&input);
    if(!context)throw std::runtime_error("Cannot create shared UI context");input.bind(*context);
    name_page=std::make_unique<NamePage>(*context,input,NameActions{names::select,names::choose,names::submit,names::review,names::validate});
    std::ofstream(output/"shared-ui.json")<<json({{"schema","srw64.shared-ui.v1"},{"backend","SDL2/RmlUi/RT64"},{"font",path.string()}}).dump(2)<<'\n';
}
bool held() {
    int count=0;const auto* keys=SDL_GetKeyboardState(&count);
    for(int i=0;i<count;++i)if(keys[i])return true;
    return debug::keyboard().held()!=0;
}
void sync() {
    physical_held=held();
    context->SetDimensions({pixels_w,pixels_h});context->SetDensityIndependentPixelRatio(pixel_ratio*std::min({1.f,float(pixels_w)/pixel_ratio/960.f,float(pixels_h)/pixel_ratio/720.f}));input.set_scale(pixel_ratio);
    const auto language=localization::snapshot();localization::Scope scope(language);
    auto request=names::request();
    for(auto& choice:request.choices)for(auto& person:choice.portraits)for(auto& path:person)path=image(path);
    for(auto& person:request.portraits)for(auto& path:person)path=image(path);
    name_page->set_hd(presentation::image_mode.current()==1);
    // Catalog owns all labels. No duplicate translation table in the frontend.
    name_page->sync(request,language->ui_labels(),language->locale);
    {
        // Controller edges; a button already down when a page opens is not a press.
        static uint32_t pad_before=0;
        const uint32_t pad_now=srw64_pad_state(),pad_pressed=pad_now&~pad_before;pad_before=pad_now;
        if(pad_pressed)battle_buttons(pad_pressed);
    }
    link_sync();battle_sync();mini_sync();
    app_menu::update(language->ui("settings_open"));
    if(app_menu::take_settings_request())choose("settings-open");
    settings_sync();notices_sync();context->Update();input.update_rectangle();
    names::window_claim_input(request.visible || (names::owns_input() && held()));
    link_page::window_claim_input(link_request.visible || (link_page::owns_input() && held()));
    battle_page::window_claim_input(battle_request.value("visible",false) || (battle_page::owns_input() && held()));
}
bool dispatch(SDL_Event& event) {
    if(!context)return false;
    if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_CLOSE)return false;
    if(event.type==SDL_DROPFILE) {
        // Debug aid: a mini-stage file dropped on the title menu loads and enters.
        const std::string path=event.drop.file?event.drop.file:"";SDL_free(event.drop.file);
        try {notices::post("mini-stage",mini_stage::load_file(path));}
        catch(const std::exception& error){notices::post("mini-stage-error",error.what());}
        return true;
    }
    if(input.event(event))return true;
    if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_F7 && !input.has_composition() &&
       !(event.key.keysym.mod&(KMOD_GUI|KMOD_ALT|KMOD_CTRL|KMOD_SHIFT))){
        if(!event.key.repeat)settings::request_locale(localization::next_locale(localization::catalog().locale));return true;
    }
    if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_COMMA && (event.key.keysym.mod&(KMOD_CTRL|KMOD_GUI))){choose("settings-open");return true;}
    // After a modal closes, game keys must not activate stale UI focus.
    if(!settings_open && !names::request().visible && !link_request.visible && !battle_request.value("visible",false) &&
       (event.type==SDL_KEYDOWN || event.type==SDL_KEYUP))return false;
    if(settings_open){
        if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE){choose("settings-close");return true;}
    } else if(battle_request.value("visible",false)){
        // The game's own key map, so the page reads like the rest of the game:
        // Z/Enter = A, X/Esc = B, arrows and WASD = pad and stick, Q/E = L/R, K = C-down.
        if(event.type==SDL_KEYDOWN && !event.key.repeat){
            const auto k=event.key.keysym.sym;uint32_t pressed=0;
            if(k==SDLK_z || k==SDLK_RETURN)pressed=0x8000;
            else if(k==SDLK_x || k==SDLK_ESCAPE)pressed=0x4000;
            else if(k==SDLK_UP || k==SDLK_w)pressed=0x0800;
            else if(k==SDLK_DOWN || k==SDLK_s)pressed=0x0400;
            else if(k==SDLK_LEFT || k==SDLK_a)pressed=0x0200;
            else if(k==SDLK_RIGHT || k==SDLK_d)pressed=0x0100;
            else if(k==SDLK_TAB)pressed=event.key.keysym.mod&KMOD_SHIFT?0x0800:0x0400;
            else if(k==SDLK_q)pressed=0x0020;
            else if(k==SDLK_e)pressed=0x0010;
            else if(k==SDLK_k)pressed=0x0004;
            if(pressed){battle_buttons(pressed);return true;}
        }
    } else if(link_request.visible){
        if(event.type==SDL_KEYDOWN && !event.key.repeat){
            auto k=event.key.keysym.sym;
            if(k==SDLK_LEFT || k==SDLK_UP){link_focus=(link_focus+2)%3;return true;}
            if(k==SDLK_RIGHT || k==SDLK_DOWN){link_focus=(link_focus+1)%3;return true;}
            if(k==SDLK_SPACE || k==SDLK_z){choose("link:"+std::to_string(link_focus));return true;}
            if(k==SDLK_RETURN){choose("link-next");return true;}
            if(k==SDLK_ESCAPE || k==SDLK_x){choose("link-back");return true;}
        }
    } else if(name_page->event(event))return true;
    SDL_Event scaled=event;
    if(event.type==SDL_MOUSEMOTION){scaled.motion.x=int(event.motion.x*pixel_ratio);scaled.motion.y=int(event.motion.y*pixel_ratio);}
    if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP){scaled.button.x=int(event.button.x*pixel_ratio);scaled.button.y=int(event.button.y*pixel_ratio);}
    // InputEventHandler returns false when a UI element consumed the event.
    bool consumed=false;
    if(event.type==SDL_KEYDOWN || event.type==SDL_KEYUP){
        int modifiers=0;const auto mask=event.key.keysym.mod;
        if(mask&KMOD_CTRL)modifiers|=Rml::Input::KM_CTRL;
        if(mask&KMOD_SHIFT)modifiers|=Rml::Input::KM_SHIFT;
        if(mask&KMOD_ALT)modifiers|=Rml::Input::KM_ALT;
        if(mask&KMOD_GUI)modifiers|=Rml::Input::KM_META;
#ifdef __APPLE__
        // RmlUi's text widget uses CTRL for edit shortcuts. SDL reports the
        // platform-standard Command key as GUI on macOS.
        if(mask&KMOD_GUI)modifiers|=Rml::Input::KM_CTRL;
#endif
        const auto key=RmlSDL::ConvertKey(event.key.keysym.sym);
        consumed=!(event.type==SDL_KEYDOWN?context->ProcessKeyDown(key,modifiers):context->ProcessKeyUp(key,modifiers));
    } else if(event.type==SDL_WINDOWEVENT){
        // Window dimensions are in points; sync() supplies drawable pixels.
        if(event.window.event==SDL_WINDOWEVENT_LEAVE)context->ProcessMouseLeave();
    } else consumed=!RmlSDL::InputEventHandler(context,scaled);
    return consumed || settings_open || names::request().visible || link_request.visible || battle_request.value("visible",false);
}
json describe(Rml::Element* el,unsigned depth=0) {
    const auto offset=el->GetAbsoluteOffset();const auto size=el->GetBox().GetSize();
    json row={{"class",el->GetTagName()},{"id",el->GetId()},
        {"frame",{offset.x/pixel_ratio,offset.y/pixel_ratio,size.x/pixel_ratio,size.y/pixel_ratio}},
        {"enabled",!el->HasAttribute("disabled")},{"focused",el==context->GetFocusElement()}};
    if(el->GetTagName()=="input") {row["text"]=el->GetAttribute<Rml::String>("value","");row["editable"]=true;}
    else if(el->GetTagName()=="#text")row["text"]=static_cast<Rml::ElementText*>(el)->GetText();
    row["state"]=el->IsClassSet("on");
    if(depth<16){json children=json::array();for(int i=0;i<el->GetNumChildren();++i)if(el->GetChild(i)->IsVisible())children.push_back(describe(el->GetChild(i),depth+1));if(!children.empty())row["children"]=children;}
    return row;
}
Rml::Element* find(Rml::Element* root,const std::string& text,bool exact) {
    if(!root->IsVisible())return nullptr;
    if(!root->GetId().empty() && root->GetId()==text)return root;
    std::string value=root->GetId();if(auto* field=dynamic_cast<Rml::ElementFormControl*>(root))value=field->GetValue();
    if(auto* node=dynamic_cast<Rml::ElementText*>(root))value=node->GetText();
    if(!value.empty() && (exact?value==text:value.find(text)!=std::string::npos))return root;
    for(int i=0;i<root->GetNumChildren();++i)if(auto* found=find(root->GetChild(i),text,exact))return found;
    return nullptr;
}
void require(){if(!context)throw debug::RpcError(debug::ServerError,"shared UI is not ready");}
}
void window_init(SDL_Window* value,const std::filesystem::path& path){window=value;output=path;system.SetWindow(window);input.defer_sdl(true);}
void render_init(plume::RenderInterface* rhi,plume::RenderDevice* device){auto lock=lock_ui();renderer=std::make_unique<recompui::RmlRenderInterface_RT64>();renderer->init(rhi,device);ready=true;}
void update(){auto lock=lock_ui();if(!ready)return;SDL_GetWindowSizeInPixels(window,&pixels_w,&pixels_h);int w,h;SDL_GetWindowSize(window,&w,&h);pixel_ratio=w?float(pixels_w)/w:1;if(!initialized)initialize();sync();input.flush_sdl();}
bool event(SDL_Event& e){auto lock=lock_ui();bool consumed=dispatch(e);if(e.type==SDL_TEXTEDITING_EXT)SDL_free(e.editExt.text);if(context){context->Update();input.flush_sdl();}return consumed;}
bool draw(plume::RenderCommandList* list,plume::RenderFramebuffer* framebuffer,bool name_cover){
    auto lock=lock_ui();if(!context || int(framebuffer->getWidth())!=pixels_w || int(framebuffer->getHeight())!=pixels_h)return false;
    // The guest cover is workload keyed. Do not put a newly-opened page over an
    // older game workload that preceded interception of the guest name grid.
    if(names::request().visible && !name_cover)return false;
    renderer->start(list,pixels_w,pixels_h);list->setFramebuffer(framebuffer);
    list->setViewports(plume::RenderViewport{0,0,float(pixels_w),float(pixels_h)});context->Render();renderer->end(list,framebuffer);
    in_flight=true;return true;
}
void presented(){std::lock_guard lock(mutex);in_flight=false;completed.notify_all();}
void render_shutdown(){auto lock=lock_ui();ready=false;if(initialized){name_page.reset();Rml::Shutdown();initialized=false;context=nullptr;settings_doc=link_doc=notice_doc=battle_doc=mini_doc=nullptr;}renderer.reset();}
void shutdown(){app_menu::shutdown();input.flush_sdl();SDL_StopTextInput();window=nullptr;names::window_claim_input(false);link_page::window_claim_input(false);battle_page::window_claim_input(false);}
json tree(){auto lock=lock_ui();require();json docs=json::array();for(int i=0;i<context->GetNumDocuments();++i)if(context->GetDocument(i)->IsVisible())docs.push_back(describe(context->GetDocument(i)));return {{"backend","SDL2/RmlUi"},{"windows",json::array({{{"number",SDL_GetWindowID(window)},{"title",SDL_GetWindowTitle(window)},{"game",true},{"scale",pixel_ratio},{"views",{{"class","RmlContext"},{"children",docs}}}}})}};}
json click(const json& p){auto lock=lock_ui();require();float x=0,y=0;
    if(p.contains("text") || p.contains("id")){
        const std::string text=p.value("id",p.value("text",std::string{}));Rml::Element* found=nullptr;
        for(bool exact:{true,false}){for(int i=context->GetNumDocuments()-1;i>=0 && !found;--i)found=find(context->GetDocument(i),text,exact);if(found)break;}
        if(!found)throw debug::RpcError(debug::InvalidParams,"no visible control: "+text);
        for(auto* parent=found;parent;parent=parent->GetParentNode())if(parent->GetTagName()=="button" || parent->GetTagName()=="input"){found=parent;break;}
        if(found->HasAttribute("disabled"))throw debug::RpcError(debug::InvalidParams,"control is disabled: "+text);
        const auto a=found->GetAbsoluteOffset(),b=found->GetBox().GetSize();x=(a.x+b.x/2)/pixel_ratio;y=(a.y+b.y/2)/pixel_ratio;
    } else {x=p.at("x");y=p.at("y");}
    context->ProcessMouseMove(int(x*pixel_ratio),int(y*pixel_ratio),0);
    const int button=p.value("button","left")=="right"?1:0;
    for(int i=0;i<std::clamp(p.value("count",1),1,3);++i){context->ProcessMouseButtonDown(button,0);context->ProcessMouseButtonUp(button,0);}
    sync();input.flush_sdl();return {{"delivery","SDL/RmlUi"},{"point",{x,y}}};
}
json key(const json& p){
    std::string name=p.at("key");if(name=="return")name="Return";if(name=="delete")name="Backspace";if(name=="esc")name="Escape";
    SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDL_GetKeyFromName(name.c_str());if(e.key.keysym.sym==SDLK_UNKNOWN)throw debug::RpcError(debug::InvalidParams,"unknown SDL key");
    for(const auto& modifier:p.value("modifiers",json::array())){if(modifier=="cmd")e.key.keysym.mod|=KMOD_GUI;if(modifier=="control")e.key.keysym.mod|=KMOD_CTRL;if(modifier=="shift")e.key.keysym.mod|=KMOD_SHIFT;if(modifier=="option")e.key.keysym.mod|=KMOD_ALT;}
    if(!event(e))SDL_PushEvent(&e);e.type=SDL_KEYUP;event(e);return {{"delivery","SDL/RmlUi"},{"key",name}};
}
json type(const json& p){
    std::string text=p.value("text",std::string{});const bool marked=p.value("marked",false);
    if(p.value("unmark",false))text=preedit;
    if(marked){preedit=text;SDL_Event e{};e.type=SDL_TEXTEDITING_EXT;e.editExt.text=SDL_strdup(text.c_str());e.editExt.start=int(Rml::StringUtilities::LengthUTF8(text));event(e);}
    else { // SDL_TEXTINPUT has a fixed byte payload. Split only at UTF-8 boundaries.
        for(size_t start=0;start<text.size();){size_t end=std::min(start+31,text.size());while(end<text.size() && (static_cast<unsigned char>(text[end])&0xC0)==0x80)--end;
            SDL_Event e{};e.type=SDL_TEXTINPUT;text.copy(e.text.text,end-start,start);event(e);start=end;}
        preedit.clear();
    }
    return {{"delivery","SDL_TEXTEDITING/SDL_TEXTINPUT"},{"marked",marked}};
}
json menu(const json& p){
    auto lock=lock_ui();require();const auto path=p.value("path",std::vector<std::string>{});
    if(!path.empty()){
        const auto& wanted=path.back();
        if(wanted==localization::catalog().ui("settings_open") || wanted==localization::catalog().ui("settings_title")){
#ifdef __APPLE__
            if(!app_menu::activate_settings())throw debug::RpcError(debug::ServerError,"application menu is not ready");
#else
            choose("settings-open");
#endif
            sync();return {{"opened","settings"}};
        }
        for(const auto& entry:rules::catalog)if(wanted==localization::catalog().ui(rules::ui_key(entry.id))){choose("settings-open");choose("rule:"+std::string(entry.id));sync();return {{"pressed",wanted}};}
        for(const auto& preset:rules::presets)if(wanted==localization::catalog().ui(std::string(preset.key))){choose("settings-open");choose("preset:"+std::string(preset.key));sync();return {{"pressed",wanted}};}
    }
    return {{"backend","SDL2/RmlUi"},{"settings",localization::catalog().ui("settings_open")},{"shortcut","Ctrl/Cmd+,"},{"native_menu",app_menu::available()}};
}
}

namespace srw64::settings_window {
void open(){ui::settings_open=true;ui::settings_release.hold();}
void close(){ui::physical_held=ui::held();ui::settings_open=false;}
bool visible(){return ui::settings_open;}
bool owns_input(){return ui::settings_open || ui::settings_release.pending();}
uint16_t filter_input(uint16_t buttons,bool held){return uint16_t(ui::settings_release.filter(buttons,ui::settings_open,held || ui::physical_held.load()));}
void update(){}
void shutdown(){close();}
void control(const std::filesystem::path& directory){
    if(!std::getenv("SRW64_WINDOW_CONTROL"))return;static uint64_t sequence{};
    std::ifstream file(directory/"settings-control.json");const auto command=nlohmann::json::parse(file,nullptr,false);
    if(!command.is_object() || command.value("schema","")!="srw64.settings-control.v1" || command.value("sequence",uint64_t{})<=sequence)return;
    sequence=command.at("sequence");auto action=command.value("action",std::string{});
    if(action=="open")open();else if(action=="close")close();else if(action=="press")ui::click({{"id",command.at("id")}});
}
}
namespace srw64::notices {
void post(const std::string& kind,const std::string& text){std::lock_guard lock(ui::notice_mutex);nlohmann::json value={{"kind",kind},{"text",text},{"vi",srw64_current_vi()}};ui::notices_pending.push_back(value);ui::notices_kept.push_back(value);while(ui::notices_kept.size()>16)ui::notices_kept.pop_front();}
nlohmann::json recent(){std::lock_guard lock(ui::notice_mutex);return nlohmann::json(ui::notices_kept);}
}
namespace srw64::debug_ui {
void close_game_window(){SDL_Event event{};event.type=SDL_WINDOWEVENT;event.window.event=SDL_WINDOWEVENT_CLOSE;event.window.windowID=SDL_GetWindowID(ui::window);SDL_PushEvent(&event);}
nlohmann::json tree(const nlohmann::json&){return ui::tree();}
nlohmann::json summary(){
    auto lock=ui::lock_ui();
    nlohmann::json result={{"backend","SDL2/RmlUi"},{"ready",ui::context!=nullptr},{"focus",nullptr}};
    result["input_owners"]={{"battle",battle_page::owns_input()},{"names",names::owns_input()},{"link",link_page::owns_input()},{"settings",settings_window::owns_input()},{"locale",settings::owns_input()}};
    if(ui::window)result["active"]=bool(SDL_GetWindowFlags(ui::window)&SDL_WINDOW_INPUT_FOCUS);
    if(ui::context)if(auto* focused=ui::context->GetFocusElement())result["focus"]={{"id",focused->GetId()},{"class",focused->GetTagName()}};
    return result;
}
nlohmann::json click(const nlohmann::json& p){return ui::click(p);}
nlohmann::json key(const nlohmann::json& p){return ui::key(p);}
nlohmann::json type(const nlohmann::json& p){return ui::type(p);}
nlohmann::json menu(const nlohmann::json& p){return ui::menu(p);}
nlohmann::json compose(const std::filesystem::path&){return {{"backend","SDL2/RmlUi"},{"composited",false},{"reason","UI is already in the GPU frame"}};}
nlohmann::json capture(const nlohmann::json&,const std::filesystem::path&){throw debug::RpcError(debug::InvalidParams,"Shared settings use the game surface; request a game screenshot");}
}
