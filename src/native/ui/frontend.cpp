#include "frontend.hpp"
#include "app_menu.hpp"
#include "name_page.hpp"
#include "ui_renderer.h"
#include "RmlUi_Platform_SDL.h"
#include "native_dialogue.hpp"
#include "link_page.hpp"
#include "graphics.hpp"
#include "battle_page.hpp"
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include "intermission_page.hpp"
#include "upgrade_page.hpp"
#include "parts_page.hpp"
#include "ability_page.hpp"
#include "swap_page.hpp"
#include "save_page.hpp"
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
json intermission_request,upgrade_request,parts_request,ability_request,swap_request,save_request;
// "intermission" or "upgrade" while the player types a new 資金 figure into that page.
std::string funds_editing;
Rml::ElementDocument* upgrade_doc{},* parts_doc{},* ability_doc{},* swap_doc{},* save_doc{};
std::string upgrade_stamp,parts_stamp,ability_stamp,swap_stamp,save_stamp;
Rml::ElementDocument* intermission_doc{};
std::string intermission_stamp;
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
// The upgrade pages: which gauge cells are the original cap and which the 上限突破 rule
// added; empty when the rule is off or the machine's own cap already is the cap.
std::string cap_legend(unsigned original,unsigned cap) {
    if(!original || cap<=original)return {};
    auto text=localization::catalog().ui("upgrade_cap_original");
    if(const auto at=text.find("{n}");at!=std::string::npos)text.replace(at,3,std::to_string(original));
    return escape(text);
}
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

.im-root {position:absolute; color:#ffffff; font-weight:bold;}
.im-panel {position:absolute; box-sizing:border-box; overflow:hidden; background-color:#0a0e3cc8; border-color:#3a78e0; white-space:nowrap;}
.im-panel button {display:block; width:100%; box-sizing:border-box; margin:0; border:0; border-radius:0; background-color:transparent; color:#ffffff; font-weight:bold; text-align:left; white-space:nowrap; overflow:hidden;}
.im-panel button:hover {background-color:#00c80055;} .im-panel button:focus {border:0;}
.im-panel button.on,.im-panel button.on:hover {background-color:#00c800;} .im-panel button:disabled {opacity:1;}
.im-line {display:flex; justify-content:space-between;} .im-line span {display:inline-block;}
.im-hint {position:absolute; text-align:center; font-weight:normal; color:#d6e2efb0; white-space:nowrap;}
.im-refused {color:#ffd75e;}
.im-funds {display:inline-block; text-align:right; color:#ffffff; font-weight:bold; background-color:transparent; border:0; border-radius:0; margin:0; padding:0;}
.im-funds:hover,.im-funds:focus {background-color:#00c80055; border:0;}
.im-funds-input {display:inline-block; text-align:right; color:#ffffff; font-weight:bold; background-color:#122131; border:0; border-radius:0; margin:0; padding:0 2dp; tab-index:auto;}
.im-funds-input selection {color:#0b1421; background-color:#9be4f7;}
.im-row {display:flex; align-items:center;} .im-row span {display:inline-block; white-space:nowrap; overflow:hidden;}
.im-right {text-align:right;} .im-center {text-align:center;} .im-dim {color:#9eafc3;}
.im-gauge {font-family: srw64-ui; letter-spacing:0;} .im-gauge b {font-weight:normal; color:#ff6fa8;} .im-gauge i {font-style:normal; color:#ffd75e;}
.im-up {color:#7dff8a;} .im-down {color:#ff8d8d;}
.im-badge {display:inline-block; text-align:center; vertical-align:middle; border-radius:6dp; border-width:1dp; border-color:#e8f0ff; color:#ffffff; font-weight:bold; margin-right:1dp; overflow:hidden;}
.im-badge.melee {background-color:#c8501e;} .im-badge.ranged {background-color:#2d6fd8;} .im-badge.post {background-color:#2a9a4a;} .im-badge.beam {background-color:#a04fd0;} .im-badge.map {background-color:#c09a1a; border-radius:3dp;} .im-bar {position:absolute; height:2dp; background-color:#00c800;} .im-bar-back {position:absolute; height:2dp; background-color:#123a2a;} .im-panel button.dim {background-color:#00c80055;}
.im-shade {position:absolute; left:0; top:0; width:100%; height:100%; background-color:#04071266;}
.im-panel img {display:block;}


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
    const auto stamp=localization::catalog().locale+std::to_string(rules::active_fixes())+std::to_string(presentation::image_mode.requested())+std::to_string(settings::native_battle_ui())+std::to_string(settings::native_intermission_ui())+
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
    body+="<p>"+label("settings_battle_ui_note")+"</p><h2>"+label("settings_intermission_ui")+"</h2>";
    for(auto mode:{"native","original"})body+=button(std::string("intermission-ui:")+mode,label(std::string("settings_intermission_ui_")+mode),settings::native_intermission_ui()==(std::string(mode)=="native"));
    body+="<p>"+label("settings_intermission_ui_note")+"</p><h2>"+label("settings_name_entry_ui")+"</h2>";
    for(auto mode:{"native","original"})body+=button(std::string("name-entry-ui:")+mode,label(std::string("settings_name_entry_ui_")+mode),settings::native_name_entry_ui()==(std::string(mode)=="native"));
    body+="<p>"+label("settings_name_entry_ui_note")+"</p><h2>"+label("rules_menu")+"</h2>";
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
// The 資金 figure: a button that opens an edit box in place (funds_editing names the page).
std::string funds_field(const std::string& page,unsigned value,const std::string& width,const std::string& font,const std::string& height) {
    const std::string id=page+"-funds";
    if(funds_editing==page)
        return "<input type='text' id='"+id+"-input' class='im-funds-input' maxlength='8' value='"+std::to_string(value)+"' style='width:"+width+"; font-size:"+font+"; height:"+height+"; line-height:"+height+";'/>";
    return "<button id='"+id+"' class='im-funds' style='width:"+width+"; font-size:"+font+"; height:"+height+"; line-height:"+height+";'>"+std::to_string(value)+"</button>";
}
void focus_funds(Rml::ElementDocument* doc,const std::string& page) {
    if(funds_editing!=page || !doc)return;
    // The old figure is selected, so typing replaces it.
    if(auto* field=dynamic_cast<Rml::ElementFormControlInput*>(doc->GetElementById(page+"-funds-input"))){context->Update();field->Focus();field->SetSelectionRange(0,int(field->GetValue().size()));}
}
float text_units(const std::string& text) {
    float units=0;
    for(unsigned char c:text)if((c&0xC0)!=0x80)units+=c<0x80?0.58f:1.f;
    return units;
}
void intermission_sync() {
    const auto next=intermission_page::state();intermission_request=next;
    if(!next.value("visible",false)){document_close(intermission_doc);intermission_stamp.clear();return;}
    const auto stamp=next.dump()+localization::catalog().locale+std::to_string(pixels_w)+"x"+std::to_string(pixels_h)+funds_editing;
    if(intermission_doc && intermission_stamp==stamp)return;
    document_close(intermission_doc);intermission_stamp=stamp;
    const float u=std::min(pixels_w/320.f,pixels_h/240.f),ox=(pixels_w-320*u)/2,oy=(pixels_h-240*u)/2;
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    const float line=std::max(1.f,float(int(u+0.5f)))/u;   // the border, one original pixel
    const auto panel=[&](float x0,float y0,float x1,float y1,const std::string& content,float font,const std::string& id="") {
        return "<div class='im-panel'"+(id.empty()?"":" id='"+id+"'")+" style='left:"+px(x0-line)+"; top:"+px(y0-line)+"; width:"+px(x1-x0+1+2*line)+"; height:"+px(y1-y0+1+2*line)+
            "; border-width:"+px(line)+"; font-size:"+px(font)+"; line-height:"+px(16)+";'>"+content+"</div>";
    };
    const auto fit=[&](const std::string& text,float width){return std::min(12.5f,width/std::max(1.f,text_units(text)));};
    const auto& items=next.at("items");const bool restricted=next.value("restricted",false),submenu=next.value("submenu",false);
    const unsigned cursor=next.value("cursor",0u);
    float item_font=12.5f;for(const auto& item:items)item_font=std::min(item_font,fit(item.get<std::string>(),67));
    const float row=(restricted?31.f:143.f)/items.size();
    std::string menu;
    for(unsigned n=0;n<items.size();++n)
        menu+="<button id='intermission:"+std::to_string(n)+"' class='"+(n==cursor?"on":"")+"'"+(submenu?" disabled":"")+" style='height:"+px(row)+"; line-height:"+px(row)+"; padding:0 "+px(1)+";'>"+escape(items[n].get<std::string>())+"</button>";
    const auto title=next.value("title",std::string());
    const auto turns_label=next.value("turns_label",std::string()),funds_label=next.value("funds_label",std::string());
    const float info_font=std::min(fit(turns_label,78),fit(funds_label,78));
    const auto info="<div class='im-line' style='padding:0 "+px(1)+"; line-height:"+px(15.5f)+";'><span>"+escape(turns_label)+"</span><span id='intermission-turns'>"+std::to_string(next.value("turns",0u))+"</span></div>"
        "<div class='im-line' style='padding:0 "+px(1)+"; line-height:"+px(15.5f)+";'><span>"+escape(funds_label)+"</span>"+funds_field("intermission",next.value("funds",0u),px(60),px(info_font),px(15.5f))+"</div>";
    auto episode=localization::catalog().ui("intermission_episode");
    if(const auto at=episode.find("{n}");at!=std::string::npos)episode.replace(at,3,std::to_string(next.value("episode",0u)));
    const auto footer=episode+" "+next.value("scene_title",std::string())+" "+next.value("clear_label",std::string());
    std::string body="<div class='im-root' id='intermission' style='left:"+px(ox/u)+"; top:"+px(oy/u)+"; width:"+px(320)+"; height:"+px(240)+";'>"+
        panel(120,25,199,39,"<div style='text-align:center;'>"+escape(title)+"</div>",fit(title,76),"intermission-title")+
        panel(33,41,103,restricted?71:183,menu,item_font,"intermission-menu")+
        panel(145,49,287,79,info,info_font,"intermission-info")+
        panel(34,193,287,207,"<div id='intermission-scene' style='padding:0 "+px(1)+";'>"+escape(footer)+"</div>",fit(footer,250),"intermission-footer");
    if(submenu) {
        const auto& swap=next.at("swap_items");const unsigned chosen=next.value("swap_cursor",0u);
        float swap_font=12.5f;std::string options;
        for(const auto& item:swap)swap_font=std::min(swap_font,fit(item.get<std::string>(),43));
        for(unsigned n=0;n<swap.size();++n)
            options+="<button id='intermission-swap:"+std::to_string(n)+"' class='"+(n==chosen?"on":"")+"' style='height:"+px(20)+"; line-height:"+px(20)+"; padding:0 "+px(2)+";'>"+escape(swap[n].get<std::string>())+"</button>";
        body+=panel(109,121,155,160,options,swap_font,"intermission-swap");
    }
    const auto hint=label(next.value("swap_refused",false)?"intermission_swap_refused":submenu?"intermission_swap_hint":funds_editing=="intermission"?"funds_edit_hint":"intermission_hint");
    body+="<div class='im-hint"+std::string(next.value("swap_refused",false)?" im-refused":"")+"' style='left:0; top:"+px(222)+"; width:"+px(320)+"; font-size:"+px(6.5f)+";'>"+hint+"</div></div>";
    intermission_doc=document(body,true);intermission_doc->SetClass("modal",false);focus_funds(intermission_doc,"intermission");
}
// ユニット改造 / 武器改造: the machine list (layout 0x6B) and the five-stat screen
// (0x75), in the original's 320x240 coordinates like the intermission menu.
// The weapon table the original draws on the 武器改造 list (layout 0x77) and the
// 武器一覧 (0x7A): a boxed header row (page / 武器名 / 攻撃力 / 射程 / 命中), rows of
// 16 with the 格／射 marker as a round icon before the name and P／B／MAP after it,
// then the selected weapon's 弾数, terrain letters, 必要気力, 消費EN, 必要技能 and
// クリティカル補正 in the boxed bands at y 160 and 180. Returns the panel body.
std::string weapon_table(const json& next,const std::string& id_prefix,float u,float line,const std::string& page_text) {
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    const auto span=[&](const std::string& text,float width,const std::string& cls="",float font=0,float gap=0){return "<span class='"+cls+"' style='width:"+px(width)+";"+(font?" font-size:"+px(font)+";":"")+(gap?" margin-left:"+px(gap)+";":"")+"'>"+escape(text)+"</span>";};
    const auto cell=[&](float x0,float x1,float y0,float y1,const std::string& content,const std::string& extra=""){
        return "<div style='position:absolute; left:"+px(x0-21)+"; top:"+px(y0-21)+"; width:"+px(x1-x0)+"; height:"+px(y1-y0)+"; border-width:"+px(line)+"; border-color:#3a78e0; box-sizing:border-box; line-height:"+px(y1-y0-2*line)+"; white-space:nowrap; overflow:hidden;"+extra+"'>"+content+"</div>";
    };
    const auto& W=next.at("weapon_labels");const auto wl=[&](const char* key){return W.value(key,std::string());};
    const auto number=[](const json& v){return v.is_number()?std::to_string(v.get<long long>()):std::string("---");};
    const auto signed_number=[](const json& v){const int n=v.is_number()?v.get<int>():0;return n>0?"+"+std::to_string(n):n<0?std::to_string(n):std::string("\u00b1 0");};
    const auto range=[](const json& r){const unsigned a=r.value("range_min",0u),b=r.value("range_max",0u);return a==b?std::to_string(a):std::to_string(a)+"\uff5e"+std::to_string(b);};
    const auto badge=[&](const std::string& token){
        const char* cls=token=="格"?"melee":token=="射"?"ranged":token=="P"?"post":token=="B"?"beam":"map";
        const float w=token=="MAP"?22.f:11.f;
        return "<span class='im-badge "+std::string(cls)+"' style='width:"+px(w)+"; height:"+px(11)+"; line-height:"+px(11)+"; font-size:"+px(token=="MAP"?6.f:7.f)+";'>"+escape(token)+"</span>";
    };
    const auto& rows=next.at("rows");const unsigned cursor=next.value("cursor",0u);
    std::string body=cell(21,68,21,44,"<div style='text-align:right; padding-right:"+px(4)+";'>"+escape(page_text)+"</div>")+
        cell(68,164,21,44,"<div style='text-align:center;' class='im-dim'>"+escape(wl("weapon"))+"</div>")+
        cell(164,220,21,44,"<div style='text-align:center;' class='im-dim'>"+escape(wl("power"))+"</div>")+
        cell(220,260,21,44,"<div style='text-align:center;' class='im-dim'>"+escape(wl("range"))+"</div>")+
        cell(260,299,21,44,"<div style='text-align:center;' class='im-dim'>"+escape(wl("hit"))+"</div>");
    std::string list;
    for(unsigned n=0;n<rows.size();++n) {
        const auto& r=rows[n];
        std::string name;
        const auto markers=r.value("markers",json::array());
        for(const auto& m:markers)if(m=="格" || m=="射")name+=badge(m.get<std::string>());
        name+=escape(r.value("display_name",r.value("name",std::string())));
        for(const auto& m:markers)if(m!="格" && m!="射")name+=badge(m.get<std::string>());
        list+="<button id='"+id_prefix+std::to_string(n)+"' class='im-row "+(n==cursor?"on":"")+"' style='height:"+px(16)+"; line-height:"+px(16)+"; padding:0 "+px(3)+";'>"
            "<span style='width:"+px(146)+"; white-space:nowrap; overflow:hidden;'>"+name+"</span>"+span(number(r.at("power")),40,"im-right")+span(range(r),44,"im-right")+span(signed_number(r.value("hit",json())),40,"im-right")+"</button>";
    }
    body+="<div id='"+id_prefix.substr(0,id_prefix.size()-1)+"-list' style='position:absolute; left:0; top:"+px(24)+"; width:100%;'>"+list+"</div>";
    const auto& sel=rows.empty()?json::object():rows[std::min<unsigned>(cursor,rows.size()-1)];
    const auto& unit=next.at("unit");
    const auto need=[](const json& v,int have){return (v.is_number()&&v.get<int>()?std::to_string(v.get<int>()):std::string("---"))+"("+(have>=0?std::to_string(have):std::string("---"))+")";};
    const std::string ammo=sel.contains("ammo")?std::to_string(sel.value("ammo",0u))+"/"+std::to_string(sel.value("ammo_max",0u)):std::string("--/--");
    const std::string terrain=sel.value("terrain",std::string("----"));
    const bool low_morale=sel.value("morale",0)>0 && unit.value("morale",-1)>=0 && sel.value("morale",0)>unit.value("morale",-1),low_en=sel.value("en",0)>0 && sel.value("en",0)>unit.value("en",0);
    const auto letter=[&](float x0,float x1,const char* key,const std::string& v){return cell(x0,x1,160,180,"<div class='im-row' style='padding:0 "+px(3)+";'>"+span(wl(key),16,"im-dim")+span(v,x1-x0-24,"im-right")+"</div>");};
    body+=cell(21,100,160,180,"<div class='im-row' style='padding:0 "+px(3)+";'>"+span(wl("ammo"),30,"im-dim")+span(ammo,42,"im-right")+"</div>")+
        cell(100,140,160,180,"<div style='text-align:center;' class='im-dim'>"+escape(wl("terrain"))+"</div>")+
        letter(140,180,"air",terrain.substr(0,1))+letter(180,220,"land",terrain.substr(1,1))+letter(220,260,"sea",terrain.substr(2,1))+letter(260,299,"space",terrain.substr(3,1))+
        cell(21,164,180,220,"<div class='im-row' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(3)+";'>"+span(wl("morale"),60,"im-dim")+span(need(sel.value("morale",json()),unit.value("morale",-1)),76,low_morale?"im-right im-down":"im-right")+"</div>"
            "<div class='im-row' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(3)+";'>"+span(wl("en"),60,"im-dim")+span(need(sel.value("en",json()),unit.value("en",-1)),76,low_en?"im-right im-down":"im-right")+"</div>","line-height:"+px(19)+";")+
        cell(164,299,180,220,"<div class='im-row' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(3)+";'>"+span(wl("skill"),56,"im-dim")+span(sel.value("skill_name",std::string()),72)+"</div>"
            "<div class='im-row' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(3)+";'>"+span(wl("critical"),80,"im-dim",std::min(10.5f,78/std::max(1.f,text_units(wl("critical")))))+span(signed_number(sel.value("critical",json()))+"%",48,"im-right")+"</div>","line-height:"+px(19)+";");
    return body;
}

void upgrade_sync() {
    const auto next=upgrade_page::state();upgrade_request=next;
    if(!next.value("visible",false)){document_close(upgrade_doc);upgrade_stamp.clear();return;}
    const auto stamp=next.dump()+localization::catalog().locale+std::to_string(pixels_w)+"x"+std::to_string(pixels_h)+funds_editing;
    if(upgrade_doc && upgrade_stamp==stamp)return;
    document_close(upgrade_doc);upgrade_stamp=stamp;
    const float u=std::min(pixels_w/320.f,pixels_h/240.f),ox=(pixels_w-320*u)/2,oy=(pixels_h-240*u)/2;
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    const float line=std::max(1.f,float(int(u+0.5f)))/u;
    const auto box=[&](float x0,float y0,float x1,float y1,const std::string& content,float font,const std::string& id="",const std::string& extra="") {
        return "<div class='im-panel'"+(id.empty()?"":" id='"+id+"'")+" style='left:"+px(x0-line)+"; top:"+px(y0-line)+"; width:"+px(x1-x0+1+2*line)+"; height:"+px(y1-y0+1+2*line)+
            "; border-width:"+px(line)+"; font-size:"+px(font)+"; line-height:"+px(16)+";"+extra+"'>"+content+"</div>";
    };
    const auto fit=[&](const std::string& text,float width){return std::min(12.5f,width/std::max(1.f,text_units(text)));};
    const auto span=[&](const std::string& text,float width,const std::string& cls="",float font=0,float gap=0){return "<span class='"+cls+"' style='width:"+px(width)+";"+(font?" font-size:"+px(font)+";":"")+(gap?" margin-left:"+px(gap)+";":"")+"'>"+escape(text)+"</span>";};
    const auto& L=next.at("labels");const auto label_of=[&](const char* key){return L.value(key,std::string());};
    const auto number=[](const json& v){return v.is_number()?std::to_string(v.get<long long>()):std::string("-----");};
    const std::string screen=next.value("screen",std::string());
    std::string body="<div class='im-root' id='upgrade' style='left:"+px(ox/u)+"; top:"+px(oy/u)+"; width:"+px(320)+"; height:"+px(240)+";'>";
    if(screen=="list") {
        const auto& rows=next.at("rows");const unsigned cursor=next.value("cursor",0u);
        std::string list;
        for(unsigned n=0;n<rows.size();++n) {
            const auto& r=rows[n];
            list+="<button id='upgrade:"+std::to_string(n)+"' class='im-row "+(n==cursor?"on":"")+"' style='height:"+px(18)+"; line-height:"+px(18)+"; padding:0 "+px(2)+";'>"+
                span(r.value("name",std::string()),148)+span("HP",22,"im-dim")+span(number(r.at("hp")),34,"im-right")+span("EN",22,"im-dim",0,8)+span(number(r.at("en")),30,"im-right")+"</button>";
        }
        const auto& sel=rows.empty()?json::object():rows[std::min<unsigned>(cursor,rows.size()-1)];
        const auto page=std::to_string(next.value("page",0u)+1)+"/"+std::to_string(next.value("pages",1u));
        const std::string pilot=sel.value("pilot",std::string());
        body+=box(21,21,299,219,
            "<div class='im-row' style='height:"+px(20)+"; line-height:"+px(20)+";'>"+span(page,60,"im-right")+"<span style='width:"+px(210)+"; text-align:center;'>"+escape(next.value("title",std::string()))+"</span></div>"
            "<div id='upgrade-list' style='margin-top:"+px(2)+";'>"+list+"</div>"
            "<div class='im-row' style='position:absolute; left:0; top:"+px(153)+"; width:100%; height:"+px(22)+"; line-height:"+px(22)+"; padding:0 "+px(3)+"; border-top-width:"+px(line)+"; border-top-color:#3a78e0;'>"+
                span(label_of("mobility"),44,"im-dim")+span(number(sel.value("mobility",json())),30,"im-right")+span(label_of("armor"),40,"im-dim",0,22)+span(number(sel.value("armor",json())),40,"im-right")+span(label_of("limit"),40,"im-dim",0,22)+span(number(sel.value("limit",json())),30,"im-right")+"</div>"
            "<div class='im-row' style='position:absolute; left:0; top:"+px(176)+"; width:100%; height:"+px(22)+"; line-height:"+px(22)+"; padding:0 "+px(3)+"; border-top-width:"+px(line)+"; border-top-color:#3a78e0;'>"+
                span(label_of("pilot"),52,"im-dim")+span(pilot.empty()?"--------":pilot,100,"",0,6)+span(label_of("funds"),40,"im-dim",0,10)+funds_field("upgrade",next.value("funds",0u),px(60),px(11.5f),px(22))+"</div>",
            11.5f,"upgrade-panel");
        const std::string hint=next.value("pages",1u)>1?"upgrade_list_hint_pages":"upgrade_list_hint";
        body+="<div class='im-hint' style='left:0; top:"+px(224)+"; width:"+px(320)+"; font-size:"+px(6.5f)+";'>"+label(hint)+"</div>";
    } else if(screen=="weapons" || screen=="weapon") {
        // Weapon list (layout 0x77): name / power / range / hit rows, the selected
        // weapon's details below; the confirm screen (0x78) on top of the list frame.
        const auto& W=next.at("weapon_labels");const auto wl=[&](const char* key){return W.value(key,std::string());};
        const auto signed_number=[](const json& v){const int n=v.is_number()?v.get<int>():0;return n>0?"+"+std::to_string(n):n<0?std::to_string(n):std::string("0");};
        const auto range=[](const json& r){const unsigned a=r.value("range_min",0u),b=r.value("range_max",0u);return a==b?std::to_string(a):std::to_string(a)+"-"+std::to_string(b);};
        const bool confirm=screen=="weapon";
        const json rows=confirm?json::array({next.at("weapon")}):next.at("rows");
        const unsigned cursor=confirm?0:next.value("cursor",0u);
        {
            // The list frame stays under the confirm screen; the confirm shows the
            // one weapon in its own row set.
            json table=next;table["rows"]=rows;table["cursor"]=cursor;
            const auto page=std::to_string(next.value("page",0u)+1)+"/ "+std::to_string(next.value("pages",1u));
            body+=box(21,21,299,219,weapon_table(table,"upgrade:",u,line,page),10.5f,"upgrade-panel","padding:0;");
        }
        const auto window=next.value("window",std::string());
        if(confirm) {
            const auto& w=next.at("weapon");
            std::string gauge;
            for(char c:w.value("gauge",std::string()))gauge+=c=='>'?"▶":c=='.'?"▷":c=='*'?"<i>●</i>":"<i>☆</i>";
            // Layout 0x78: name and gauge at y 64, 攻撃力 ▶ preview and 資金 at 88, 費用 in its own box.
            body+="<div class='im-shade'></div>"+box(21,61,299,107,
                "<div class='im-row' style='height:"+px(24)+"; line-height:"+px(24)+"; padding:0 "+px(3)+";'>"+span(w.value("display_name",w.value("name",std::string())),150)+"<span class='im-gauge' style='width:"+px(120)+"; font-size:"+px(9)+";'>"+gauge+"</span></div>"
                "<div class='im-row' style='height:"+px(20)+"; line-height:"+px(20)+"; padding:0 "+px(3)+";'>"+span(wl("power"),44,"im-dim")+span(number(w.at("power")),40,"im-right")+span("▶",14,"im-dim")+span(number(w.value("preview",json())),40,"im-right")+
                    span(label_of("funds"),32,"im-dim",0,10)+funds_field("upgrade",next.value("funds",0u),px(56),px(11),px(20))+"</div>",11.f,"upgrade-weapon")+
                box(181,107,299,123,"<div class='im-row' style='height:"+px(15)+"; line-height:"+px(15)+"; padding:0 "+px(3)+";'>"+span(label_of("price"),32,"im-dim")+span(number(w.value("price",json())),76,"im-right")+"</div>",11.f,"upgrade-weapon-price");
            if(const std::string raised=cap_legend(w.value("original_cap",0u),w.value("cap",0u));!raised.empty())
                body+="<div class='im-dim' style='position:absolute; left:"+px(24)+"; top:"+px(110)+"; width:"+px(150)+"; font-size:"+px(7)+"; line-height:"+px(12)+";'>"+raised+"</div>";
            if(window=="confirm")
                body+=box(261,133,291,171,"<button id='upgrade-confirm' class='"+std::string(cursor==0?"on":"")+"' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(2)+";'>"+escape(label_of("yes"))+"</button><button id='upgrade-cancel' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(2)+";'>"+escape(label_of("no"))+"</button>",8.f,"upgrade-choice");
            else body+=box(85,61,235,83,"<button id='upgrade-dismiss' style='height:"+px(22)+"; line-height:"+px(22)+"; text-align:center;'>"+escape(label_of(window=="poor"?"poor":"maxed"))+"</button>",fit(label_of(window=="poor"?"poor":"maxed"),146),"upgrade-message");
        } else if(window=="bonus") {
            // Layout 0x8C: 「X を最大まで改造したので、特別ボーナスとして Y が追加されます」.
            const auto& b=next.at("bonus");const auto& bl=next.at("bonus_labels");
            body+="<div class='im-shade'></div>"+box(29,77,291,179,"<button id='upgrade-dismiss' style='height:"+px(100)+"; padding:"+px(6)+" "+px(8)+"; line-height:"+px(18)+";'>"+
                escape(b.value("upgraded",std::string()))+"<br/>"+escape(bl[0].get<std::string>())+"<br/>"+escape(bl[1].get<std::string>())+"<br/>"+escape(b.value("unlocked",std::string()))+"<br/>"+escape(bl[2].get<std::string>())+"</button>",11.f,"upgrade-bonus");
        }
        const char* hint=confirm?(window=="confirm"?"upgrade_confirm_hint":"upgrade_message_hint"):window=="bonus"?"upgrade_message_hint":next.value("pages",1u)>1?"upgrade_weapons_hint_pages":"upgrade_weapons_hint";
        body+="<div class='im-hint' style='left:0; top:"+px(224)+"; width:"+px(320)+"; font-size:"+px(6.5f)+";'>"+label(hint)+"</div>";
    } else if(screen=="stats") {
        const auto& rows=next.at("rows");const unsigned cursor=next.value("cursor",0u);const auto& unit=next.at("unit");
        std::string cap=label_of("cap");
        if(const auto at=cap.find("  ");at!=std::string::npos)cap.replace(at,2,std::to_string(next.value("cap",0u)));
        const auto& row_at=rows[std::min<unsigned>(cursor,rows.size()-1)];
        const std::string raised=cap_legend(row_at.value("original_cap",0u),next.value("cap",0u));
        std::string lines;
        for(unsigned n=0;n<rows.size();++n) {
            const auto& r=rows[n];
            std::string gauge;
            for(char c:r.value("gauge",std::string()))gauge+=c=='>'?"▶":c=='.'?"▷":c=='*'?"<i>●</i>":"<i>☆</i>";
            lines+="<button id='upgrade:"+std::to_string(n)+"' class='im-row "+(n==cursor?"on":"")+"' style='height:"+px(17)+"; line-height:"+px(17)+"; padding:0 "+px(3)+";'>"+
                span(r.value("name",std::string()),44)+span(number(r.at("value")),40,"im-right")+span("▶",16,"im-dim")+span(number(r.value("preview",json())),40,"im-right")+
                "<span class='im-gauge' style='width:"+px(120)+"; margin-left:"+px(8)+"; font-size:"+px(9)+";'>"+gauge+"</span></button>";
        }
        body+=box(21,10,175,42,"<div style='padding:0 "+px(4)+"; line-height:"+px(31)+";'>"+escape(unit.value("name",std::string()))+"</div>",fit(unit.value("name",std::string()),146),"upgrade-name")+
            box(21,43,175,89,"<div style='padding:"+px(3)+" "+px(4)+" 0;'>"+escape(label_of("question"))+"<br/>"+escape(cap)+"</div>",fit(label_of("question"),146),"upgrade-question")+
            box(21,90,175,131,"<div class='im-row' style='padding:0 "+px(4)+";'>"+span(label_of("funds"),50,"im-dim")+funds_field("upgrade",next.value("funds",0u),px(90),px(12),px(16))+"</div>"
                "<div class='im-row' style='padding:0 "+px(4)+";'>"+span(label_of("price"),50,"im-dim")+span(number(row_at.value("price",json())),90,"im-right")+"</div>"+
                // The 上限突破 legend fills the money box's spare third row; the question box has no room for it.
                (raised.empty()?std::string():"<div class='im-dim' style='padding:0 "+px(4)+"; font-size:"+px(7)+"; line-height:"+px(12)+";'>"+raised+"</div>"),12.f,"upgrade-money");
        std::string art;
        if(unit.contains("art") && unit.at("art").contains("path")) {
            const float w=unit.at("art").value("width",96.f),h=unit.at("art").value("height",96.f),scale=std::min(118.f/w,118.f/h);
            art="<img src='"+escape(image(unit.at("art").at("path").get<std::string>()))+"' style='width:"+px(w*scale)+"; height:"+px(h*scale)+"; margin:auto;'/>";
        }
        body+=box(176,8,302,132,art,12.f,"upgrade-art","display:flex; align-items:center; justify-content:center;")+
            box(21,133,302,219,lines,11.5f,"upgrade-rows");
        const auto window=next.value("window",std::string());
        if(!window.empty()) {
            body+="<div class='im-shade'></div>";
            if(window=="confirm")
                body+=box(53,101,267,139,"<div style='padding:"+px(3)+" "+px(4)+";'>"+escape(label_of("ask"))+"</div>",12.f,"upgrade-window")+
                    box(221,139,251,163,"<button id='upgrade-confirm' class='on' style='height:"+px(12)+"; line-height:"+px(12)+"; padding:0 "+px(2)+";'>"+escape(label_of("yes"))+"</button><button id='upgrade-cancel' style='height:"+px(12)+"; line-height:"+px(12)+"; padding:0 "+px(2)+";'>"+escape(label_of("no"))+"</button>",7.5f,"upgrade-choice");
            else
                body+=box(85,61,235,83,"<button id='upgrade-dismiss' style='height:"+px(22)+"; line-height:"+px(22)+"; text-align:center;'>"+escape(label_of(window=="poor"?"poor":"maxed"))+"</button>",fit(label_of(window=="poor"?"poor":"maxed"),146),"upgrade-message");
        }
        body+="<div class='im-hint' style='left:0; top:"+px(224)+"; width:"+px(320)+"; font-size:"+px(6.5f)+";'>"+label(window.empty()?"upgrade_stats_hint":window=="confirm"?"upgrade_confirm_hint":"upgrade_message_hint")+"</div>";
    }
    body+="</div>";
    upgrade_doc=document(body,true);upgrade_doc->SetClass("modal",false);focus_funds(upgrade_doc,"upgrade");
}

// 強化パーツ (parts_page.cpp): the machine list (layout 0x70), the slots with the
// inventory and stat preview (0x7E) and the owned copies of one part (0x7F), each
// on the original panel rectangles.
void parts_sync() {
    const auto next=parts_page::state();parts_request=next;
    if(!next.value("visible",false)){document_close(parts_doc);parts_stamp.clear();return;}
    const auto stamp=next.dump()+localization::catalog().locale+std::to_string(pixels_w)+"x"+std::to_string(pixels_h);
    if(parts_doc && parts_stamp==stamp)return;
    document_close(parts_doc);parts_stamp=stamp;
    const float u=std::min(pixels_w/320.f,pixels_h/240.f),ox=(pixels_w-320*u)/2,oy=(pixels_h-240*u)/2;
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    const float line=std::max(1.f,float(int(u+0.5f)))/u;
    const auto box=[&](float x0,float y0,float x1,float y1,const std::string& content,float font,const std::string& id="",const std::string& extra="") {
        return "<div class='im-panel'"+(id.empty()?"":" id='"+id+"'")+" style='left:"+px(x0-line)+"; top:"+px(y0-line)+"; width:"+px(x1-x0+1+2*line)+"; height:"+px(y1-y0+1+2*line)+
            "; border-width:"+px(line)+"; font-size:"+px(font)+"; line-height:"+px(16)+";"+extra+"'>"+content+"</div>";
    };
    const auto fit=[&](const std::string& text,float width){return std::min(12.5f,width/std::max(1.f,text_units(text)));};
    const auto span=[&](const std::string& text,float width,const std::string& cls="",float font=0,float gap=0){return "<span class='"+cls+"' style='width:"+px(width)+";"+(font?" font-size:"+px(font)+";":"")+(gap?" margin-left:"+px(gap)+";":"")+"'>"+escape(text)+"</span>";};
    const auto& L=next.at("labels");const auto label_of=[&](const char* key){return L.value(key,std::string());};
    const auto number=[](const json& v){return v.is_number()?std::to_string(v.get<long long>()):std::string("-----");};
    const auto dashes=[](const std::string& s){return s.empty()?std::string("--------"):s;};
    // The unit's slots as the original prints them: two columns, two rows.
    const auto slot_grid=[&](const json& unit,float top) {
        std::string out;
        const auto& parts=unit.value("parts",json::array());
        for(unsigned n=0;n<parts.size() && n<4;++n)
            out+="<span style='position:absolute; left:"+px(n%2?188:91)+"; top:"+px(top+(n/2)*18)+"; width:"+px(96)+";'>"+escape(dashes(parts[n].value("name",std::string())))+"</span>";
        return out;
    };
    const std::string screen=next.value("screen",std::string());
    std::string body="<div class='im-root' id='parts' style='left:"+px(ox/u)+"; top:"+px(oy/u)+"; width:"+px(320)+"; height:"+px(240)+";'>";
    if(screen=="list") {
        const auto& rows=next.at("rows");const unsigned cursor=next.value("cursor",0u);
        std::string list;
        for(unsigned n=0;n<rows.size();++n) {
            const auto& r=rows[n];
            list+="<button id='parts:"+std::to_string(n)+"' class='im-row "+(n==cursor?"on":"")+"' style='height:"+px(16)+"; line-height:"+px(16)+"; padding:0 "+px(3)+";'>"+
                span(r.value("name",std::string()),126)+span(dashes(r.value("pilot",std::string())),90)+span(label_of("level"),36,"im-dim",fit(label_of("level"),34))+span(r.contains("level")?number(r.at("level")):std::string(),18,"im-right")+"</button>";
        }
        const auto& sel=rows.empty()?json::object():rows[std::min<unsigned>(cursor,rows.size()-1)];
        const auto page=std::to_string(next.value("page",0u)+1)+"/"+std::to_string(next.value("pages",1u));
        body+=box(21,21,299,219,
            "<div class='im-row' style='height:"+px(22)+"; line-height:"+px(22)+"; border-bottom-width:"+px(line)+"; border-bottom-color:#3a78e0;'>"+span(page,44,"im-right")+"<span style='width:"+px(2)+"; height:100%; border-left-width:"+px(line)+"; border-left-color:#3a78e0; margin-left:"+px(2)+";'></span><span style='width:"+px(228)+"; text-align:center;'>"+escape(label_of("title"))+"</span></div>"
            "<div id='parts-list' style='margin-top:"+px(3)+";'>"+list+"</div>"
            "<div style='position:absolute; left:0; top:"+px(157)+"; width:100%; height:"+px(42)+"; border-top-width:"+px(line)+"; border-top-color:#3a78e0;'>"
                "<span class='im-dim' style='position:absolute; left:"+px(3)+"; top:"+px(4)+"; width:"+px(88)+"; font-size:"+px(fit(label_of("equipped"),86))+";'>"+escape(label_of("equipped"))+"</span>"+slot_grid(sel,4)+"</div>",
            11.5f,"parts-panel");
        body+="<div class='im-hint' style='left:0; top:"+px(224)+"; width:"+px(320)+"; font-size:"+px(6.5f)+";'>"+label(next.value("pages",1u)>1?"parts_list_hint_pages":"parts_list_hint")+"</div>";
    } else if(screen=="slots") {
        const auto& unit=next.at("unit");const auto& inv=next.at("inventory");const unsigned mode=next.value("mode",0u),cursor=next.value("cursor",0u);
        const auto& parts=unit.value("parts",json::array());
        std::string slots;
        for(unsigned n=0;n<parts.size();++n)
            slots+="<button id='parts-slot:"+std::to_string(n)+"' class='im-row "+(n==cursor?(mode?"dim":"on"):"")+"' style='height:"+px(17)+"; line-height:"+px(17)+"; padding:0 "+px(3)+";'>"+span(dashes(parts[n].value("name",std::string())),136)+"</button>";
        std::string stats;
        for(const auto& s:next.at("stats")) {
            const long long cur=s.value("current",0ll),pre=s.value("preview",0ll);
            stats+="<div class='im-row' style='height:"+px(16)+"; line-height:"+px(16)+"; padding:0 "+px(3)+";'>"+span(label_of(s.value("key",std::string("hp")).c_str()),44,"im-dim")+span(std::to_string(cur),40,"im-right")+span(label_of("arrow"),14,"im-dim")+
                span(std::to_string(pre),40,std::string("im-right ")+(pre>cur?"im-up":pre<cur?"im-down":""))+"</div>";
        }
        std::string list;
        const auto& rows=inv.at("rows");const unsigned at=inv.value("cursor",0u),page=inv.value("page",0u),pages=inv.value("pages",1u);
        for(unsigned n=0;n<rows.size();++n) {
            const auto& r=rows[n];
            std::string count;
            if(r.contains("owned"))count=std::to_string(r.value("equipped",0u))+"("+std::to_string(r.value("owned",0u))+")";
            list+="<button id='parts:"+std::to_string(n)+"' class='im-row "+(mode && n==at?"on":"")+"' style='height:"+px(16)+"; line-height:"+px(16)+"; padding:0 "+px(3)+";'>"+span(r.value("name",std::string()),92,"",0,8)+span(count,30,"im-right")+"</button>";
        }
        const auto arrows="<div class='im-row im-dim' style='height:"+px(16)+"; line-height:"+px(16)+"; padding:0 "+px(3)+";'>"+span(page>0?label_of("prev"):std::string(),16)+span(std::to_string(page+1)+"/"+std::to_string(pages),96,"",0,0)+span(page+1<pages?label_of("next"):std::string(),16,"im-right")+"</div>";
        std::string description;
        for(const auto& d:next.value("description",json::array()))description+="<div style='height:"+px(16)+"; line-height:"+px(16)+"; padding:0 "+px(4)+";'>"+escape(d.get<std::string>())+"</div>";
        const auto page_text=std::to_string(page+1)+"/"+std::to_string(pages);
        body+=box(21,21,163,43,"<div class='im-row' style='height:"+px(22)+"; line-height:"+px(22)+";'>"+span(page_text,44,"im-right")+"<span style='width:"+px(2)+"; height:100%; border-left-width:"+px(line)+"; border-left-color:#3a78e0; margin-left:"+px(2)+";'></span><span style='width:"+px(90)+"; text-align:center; font-size:"+px(fit(label_of("select_title"),88))+";'>"+escape(label_of("select_title"))+"</span></div>",11.f,"parts-title")+
            box(21,43,163,113,"<div id='parts-slots' style='margin-top:"+px(1)+";'>"+slots+"</div>",11.5f,"parts-slot-panel")+
            box(21,113,163,219,"<div style='margin-top:"+px(5)+";'>"+stats+"</div>",11.f,"parts-stats")+
            box(163,21,299,43,"<div style='padding:0 "+px(4)+"; line-height:"+px(22)+"; font-size:"+px(fit(unit.value("name",std::string()),128))+";'>"+escape(unit.value("name",std::string()))+"</div>",11.5f,"parts-unit")+
            box(163,43,299,160,arrows+"<div id='parts-inventory'>"+list+"</div>",11.f,"parts-inventory-panel")+
            box(163,160,299,219,"<div style='margin-top:"+px(4)+";'>"+description+"</div>",9.5f,"parts-description");
        body+="<div class='im-hint' style='left:0; top:"+px(224)+"; width:"+px(320)+"; font-size:"+px(6.5f)+";'>"+label(mode?"parts_inventory_hint":"parts_slots_hint")+"</div>";
    } else if(screen=="holders") {
        const auto& rows=next.at("rows");const unsigned cursor=next.value("cursor",0u);
        std::string list;
        for(unsigned n=0;n<rows.size();++n) {
            const auto& r=rows[n];
            const bool free=r.value("free",false);
            list+="<button id='parts:"+std::to_string(n)+"' class='im-row "+(n==cursor?"on":"")+"' style='height:"+px(17)+"; line-height:"+px(17)+"; padding:0 "+px(3)+";'>"+
                span(next.at("part").value("name",std::string()),96)+span(free?label("parts_free"):r.value("name",std::string()),110,free?"im-dim":"")+span(free?"--------":dashes(r.value("pilot",std::string())),64)+"</button>";
        }
        body+=box(21,21,299,219,
            "<div style='position:absolute; left:0; top:0; width:100%; height:"+px(42)+"; border-bottom-width:"+px(line)+"; border-bottom-color:#3a78e0;'>"
                "<span class='im-dim' style='position:absolute; left:"+px(3)+"; top:"+px(3)+"; width:"+px(88)+"; font-size:"+px(fit(label_of("equipped"),86))+";'>"+escape(label_of("equipped"))+"</span>"+slot_grid(next.at("unit"),3)+"</div>"
            "<div id='parts-holders' style='margin-top:"+px(43)+";'>"+list+"</div>",11.5f,"parts-panel");
        body+="<div class='im-hint' style='left:0; top:"+px(224)+"; width:"+px(320)+"; font-size:"+px(6.5f)+";'>"+label("parts_holders_hint")+"</div>";
    }
    body+="</div>";
    parts_doc=document(body,true);parts_doc->SetClass("modal",false);
}

// のりかえ (swap_page.cpp): the pilot / fairy lists (layouts 0x6F / 0x88), the target
// lists with the pilot header (0x7C / 0x89, with the 乗せますか window 0x8A) and the
// confirm page (0x7D), on the original rectangles.
void swap_sync() {
    const auto next=swap_page::state();swap_request=next;
    if(!next.value("visible",false)){document_close(swap_doc);swap_stamp.clear();return;}
    const auto stamp=next.dump()+localization::catalog().locale+std::to_string(pixels_w)+"x"+std::to_string(pixels_h);
    if(swap_doc && swap_stamp==stamp)return;
    document_close(swap_doc);swap_stamp=stamp;
    const float u=std::min(pixels_w/320.f,pixels_h/240.f),ox=(pixels_w-320*u)/2,oy=(pixels_h-240*u)/2;
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    const float line=std::max(1.f,float(int(u+0.5f)))/u;
    const auto box=[&](float x0,float y0,float x1,float y1,const std::string& content,float font,const std::string& id="",const std::string& extra="") {
        return "<div class='im-panel'"+(id.empty()?"":" id='"+id+"'")+" style='left:"+px(x0-line)+"; top:"+px(y0-line)+"; width:"+px(x1-x0+1+2*line)+"; height:"+px(y1-y0+1+2*line)+
            "; border-width:"+px(line)+"; font-size:"+px(font)+"; line-height:"+px(16)+";"+extra+"'>"+content+"</div>";
    };
    const auto fit=[&](const std::string& text,float width){return std::min(12.5f,width/std::max(1.f,text_units(text)));};
    const auto span=[&](const std::string& text,float width,const std::string& cls="",float font=0,float gap=0){return "<span class='"+cls+"' style='width:"+px(width)+";"+(font?" font-size:"+px(font)+";":"")+(gap?" margin-left:"+px(gap)+";":"")+"'>"+escape(text)+"</span>";};
    const auto at=[&](float x,float y,const std::string& content,const std::string& cls="",float font=0,float width=0){return "<div class='"+cls+"' style='position:absolute; left:"+px(x)+"; top:"+px(y)+";"+(width?" width:"+px(width)+";":"")+(font?" font-size:"+px(font)+";":"")+" line-height:"+px(16)+"; white-space:nowrap;'>"+content+"</div>";};
    const auto& L=next.at("labels");const auto label_of=[&](const char* key){return L.value(key,std::string());};
    const auto dim=[&](const char* key){return "<span class='im-dim'>"+escape(label_of(key))+"</span>";};
    const auto number=[](const json& v){return v.is_number()?std::to_string(v.get<long long>()):std::string("--");};
    const auto dashes=[](const std::string& s){return s.empty()?std::string("--------"):s;};
    const auto hint=[&](const char* key){return "<div class='im-hint' style='left:0; top:"+px(224)+"; width:"+px(320)+"; font-size:"+px(6.5f)+";'>"+label(key)+"</div>";};
    const auto art_img=[&](const json& owner,float size){
        if(!owner.contains("art") || !owner.at("art").contains("path"))return std::string();
        const float w=owner.at("art").value("width",96.f),h=owner.at("art").value("height",96.f),scale=std::min(size/w,size/h);
        return "<img src='"+escape(image(owner.at("art").at("path").get<std::string>()))+"' style='width:"+px(w*scale)+"; height:"+px(h*scale)+"; margin:auto;'/>";
    };
    const auto yes_no=[&](const std::string& prefix,unsigned cursor,float x0,float y0,float x1,float y1,float row){
        return box(x0,y0,x1,y1,"<button id='"+prefix+"-yes' class='"+(cursor==0?"on":"")+"' style='height:"+px(row)+"; line-height:"+px(row)+"; padding:0 "+px(2)+";'>"+escape(label_of("yes"))+"</button><button id='"+prefix+"-no' class='"+(cursor==1?"on":"")+"' style='height:"+px(row)+"; line-height:"+px(row)+"; padding:0 "+px(2)+";'>"+escape(label_of("no"))+"</button>",8.f,prefix+"-choice");
    };
    const std::string screen=next.value("screen",std::string());
    std::string body="<div class='im-root' id='swap' style='left:"+px(ox/u)+"; top:"+px(oy/u)+"; width:"+px(320)+"; height:"+px(240)+";'>";
    if(screen=="pilots" || screen=="fairies") {
        const auto& rows=next.at("rows");const unsigned cursor=next.value("cursor",0u);
        std::string list;
        for(unsigned n=0;n<rows.size();++n) {
            const auto& r=rows[n];
            list+="<button id='swap:"+std::to_string(n)+"' class='im-row "+(n==cursor?"on":"")+"' style='height:"+px(16)+"; line-height:"+px(16)+"; padding:0 "+px(3)+";'>"+
                span(r.value("name",std::string()),70)+span(dashes(r.value("unit",std::string())),150)+span(label_of("level"),34,"im-dim",fit(label_of("level"),32))+span(number(r.value("level",json())),16,"im-right")+"</button>";
        }
        const auto page=std::to_string(next.value("page",0u)+1)+"/"+std::to_string(next.value("pages",1u));
        const auto& sub=next.value("sub",json::object());
        const char* sub_key=screen=="pilots"?"sub":"fairy";
        body+=box(21,21,299,219,
            "<div class='im-row' style='height:"+px(22)+"; line-height:"+px(22)+"; border-bottom-width:"+px(line)+"; border-bottom-color:#3a78e0;'>"+span(page,44,"im-right")+"<span style='width:"+px(2)+"; height:100%; border-left-width:"+px(line)+"; border-left-color:#3a78e0; margin-left:"+px(2)+";'></span><span style='width:"+px(228)+"; text-align:center;'>"+escape(label_of("title"))+"</span></div>"
            "<div id='swap-list' style='margin-top:"+px(3)+";'>"+list+"</div>"
            "<div class='im-row' style='position:absolute; left:0; top:"+px(175)+"; width:100%; height:"+px(22)+"; line-height:"+px(22)+"; padding:0 "+px(3)+"; border-top-width:"+px(line)+"; border-top-color:#3a78e0;'>"+
                span(label_of(sub_key),40,"im-dim",fit(label_of(sub_key),38))+span(dashes(sub.value("name",std::string())),150)+span(label_of("level"),34,"im-dim",fit(label_of("level"),32))+span(sub.contains("level")?number(sub.at("level")):std::string("--"),16,"im-right")+"</div>",
            11.5f,"swap-panel");
        body+=hint("swap_list_hint");
    } else if(screen=="targets" || screen=="fairy_targets") {
        const bool fairy=screen=="fairy_targets";
        const auto& rows=next.at("rows");const unsigned cursor=next.value("cursor",0u);const auto& p=next.at("pilot");const auto& sub=next.value("sub",json::object());
        std::string list;
        for(unsigned n=0;n<rows.size();++n) {
            const auto& r=rows[n];
            list+="<button id='swap:"+std::to_string(n)+"' class='im-row "+(n==cursor?"on":"")+"' style='height:"+px(16)+"; line-height:"+px(16)+"; padding:0 "+px(3)+";'>"+
                (fairy?span(r.value("name",std::string()),128)+span(dashes(r.value("unit",std::string())),96)+span(label_of("level"),34,"im-dim",fit(label_of("level"),32))+span(number(r.value("level",json())),16,"im-right")
                      :span(r.value("name",std::string()),128)+span(dashes(r.value("pilot",std::string())),80)+span(label_of("hp"),24,"im-dim")+span(number(r.value("hp",json())),40,"im-right"))+"</button>";
        }
        const auto page=std::to_string(next.value("page",0u)+1)+"/"+std::to_string(next.value("pages",1u));
        body+=box(18,10,299,219,
            "<div style='position:absolute; left:0; top:0; width:"+px(96)+"; height:"+px(94)+"; display:flex; align-items:center; justify-content:center;'>"+art_img(p,88)+"</div>"+
            at(94,4,escape(page),"im-right",10,40)+at(190,4,"<span class='im-dim'>"+escape(label_of("title"))+"</span>","",11)+
            at(98,32,escape(p.value("full_name",std::string())),"",fit(p.value("full_name",std::string()),120),120)+at(230,32,dim("level"))+at(262,32,number(p.value("level",json())),"im-right",0,20)+
            at(98,52,dim(fairy?"fairy":"sub"))+at(134,52,dashes(sub.value("name",std::string())),"",0,96)+at(230,52,dim("level"))+at(262,52,sub.contains("level")?number(sub.at("level")):std::string("--"),"im-right",0,20)+
            "<div style='position:absolute; left:0; top:"+px(94)+"; width:100%; border-top-width:"+px(line)+"; border-top-color:#3a78e0;'></div>"
            "<div id='swap-list' style='position:absolute; left:0; top:"+px(95)+"; width:100%;'>"+list+"</div>",11.5f,"swap-panel");
        if(fairy && next.value("mode",0u)) {
            const auto& target_row=rows.empty()?json::object():rows[std::min<unsigned>(cursor,rows.size()-1)];
            body+="<div class='im-shade'></div>"+box(53,101,267,139,"<div style='padding:"+px(3)+" "+px(4)+";'>"+escape(target_row.value("name",std::string()))+escape(label_of("board"))+"<br/>"+escape(label_of("ask"))+"</div>",11.f,"swap-window")+
                yes_no("swap",next.value("window_cursor",0u),221,122,251,163,20);
            body+=hint("swap_confirm_hint");
        } else body+=hint("swap_list_hint");
    } else if(screen=="confirm") {
        // Layout 0x7D as the original boxes it: portrait with the name, レベル and HP,
        // the fairy line, the two notes, the 限界／回避／命中 rows, the machine with its
        // name, the question with はい／いいえ, and the combined terrain ranks.
        const auto& p=next.at("pilot");const auto& from=next.value("from",json::object());const auto& to=next.at("to");const unsigned cursor=next.value("cursor",0u);
        const auto& evade=next.at("evade");const auto& hit=next.at("hit");
        const auto plus=[&](const json& s){return number(s.value("value",json()))+"+"+std::to_string(s.value("after",0)-s.value("value",0));};
        const std::string terrain=next.value("terrain",std::string("----"));
        const auto& sub=from.value("sub",json::object());
        body+=box(18,10,110,101,"<div style='position:absolute; left:0; top:0; width:100%; height:"+px(72)+"; display:flex; align-items:center; justify-content:center;'>"+art_img(p,70)+"</div>"+at(0,74,escape(p.value("name",std::string())),"im-center",fit(p.value("name",std::string()),86),90),11.f,"swap-portrait")+
            box(111,10,175,35,"<div class='im-row' style='height:"+px(24)+"; line-height:"+px(24)+"; padding:0 "+px(3)+";'>"+span(label_of("level"),34,"im-dim")+span(number(p.value("level",json())),22,"im-right")+"</div>",11.f,"swap-level")+
            box(111,36,175,58,"<div class='im-row' style='height:"+px(21)+"; line-height:"+px(21)+"; padding:0 "+px(3)+";'>"+span(label_of("hp"),20,"im-dim")+span(number(to.value("hp",json())),36,"im-right")+"</div>",11.f,"swap-hp")+
            box(18,102,175,122,"<div class='im-row' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(3)+";'>"+span(label_of("sub"),34,"im-dim")+span(dashes(sub.value("name",std::string())),112)+"</div>",11.f,"swap-fairy")+
            box(18,123,175,162,"<div style='padding:"+px(3)+" "+px(3)+"; line-height:"+px(16)+";' class='im-dim'>"+escape(label_of("current"))+"<br/>"+escape(label_of("after"))+"</div>",fit(label_of("after"),150),"swap-notes")+
            box(18,163,175,219,"<div class='im-row' style='height:"+px(17)+"; line-height:"+px(17)+"; padding:0 "+px(3)+";'>"+span(label_of("limit"),34,"im-dim")+span(number(to.value("limit",json())),40,"im-right")+"</div>"
                "<div class='im-row' style='height:"+px(17)+"; line-height:"+px(17)+"; padding:0 "+px(3)+";'>"+span(label_of("evade"),34,"im-dim")+span(plus(evade),60,evade.value("over",false)?"im-right im-down":"im-right")+span("("+std::to_string(evade.value("now",0))+")",50,"im-right im-dim")+"</div>"
                "<div class='im-row' style='height:"+px(17)+"; line-height:"+px(17)+"; padding:0 "+px(3)+";'>"+span(label_of("hit"),34,"im-dim")+span(plus(hit),60,hit.value("over",false)?"im-right im-down":"im-right")+span("("+std::to_string(hit.value("now",0))+")",50,"im-right im-dim")+"</div>",11.f,"swap-stats")+
            box(176,8,302,132,"<div style='position:absolute; left:0; top:0; width:100%; height:"+px(108)+"; display:flex; align-items:center; justify-content:center;'>"+art_img(to,104)+"</div>"+at(0,108,escape(to.value("name",std::string())),"im-center",fit(to.value("name",std::string()),120),126),11.f,"swap-art")+
            box(176,133,258,219,"<div style='padding:"+px(2)+" "+px(3)+"; line-height:"+px(15)+";'>"+escape(label_of("board"))+"<br/>"+escape(label_of("ask"))+"</div>",fit(label_of("board"),78),"swap-question")+
            yes_no("swap",cursor,215,168,250,206,18)+
            box(259,133,302,219,at(3,3,"<span class='im-dim'>"+escape(label_of("terrain"))+"</span>","",fit(label_of("terrain"),38))+at(3,20,dim("air"))+at(26,20,terrain.substr(0,1))+at(3,36,dim("land"))+at(26,36,terrain.substr(1,1))+
                at(3,52,dim("sea"))+at(26,52,terrain.substr(2,1))+at(3,68,dim("space"))+at(26,68,terrain.substr(3,1)),11.f,"swap-terrain");
        body+=hint("swap_confirm_hint");
    }
    body+="</div>";
    swap_doc=document(body,true);swap_doc->SetClass("modal",false);
}

// データセーブ (save_page.cpp): the medium choice (layout 0x6A with the 0x72 pause box)
// and the two-slot page (0x73) with the overwrite window (0x74) and the Controller Pak
// message box (0x8C), at the original positions.
void save_sync() {
    const auto next=save_page::state();save_request=next;
    if(!next.value("visible",false)){document_close(save_doc);save_stamp.clear();return;}
    const auto stamp=next.dump()+localization::catalog().locale+std::to_string(pixels_w)+"x"+std::to_string(pixels_h);
    if(save_doc && save_stamp==stamp)return;
    document_close(save_doc);save_stamp=stamp;
    const float u=std::min(pixels_w/320.f,pixels_h/240.f),ox=(pixels_w-320*u)/2,oy=(pixels_h-240*u)/2;
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    const float line=std::max(1.f,float(int(u+0.5f)))/u;
    const auto box=[&](float x0,float y0,float x1,float y1,const std::string& content,float font,const std::string& id="",const std::string& extra="") {
        return "<div class='im-panel'"+(id.empty()?"":" id='"+id+"'")+" style='left:"+px(x0-line)+"; top:"+px(y0-line)+"; width:"+px(x1-x0+1+2*line)+"; height:"+px(y1-y0+1+2*line)+
            "; border-width:"+px(line)+"; font-size:"+px(font)+"; line-height:"+px(16)+";"+extra+"'>"+content+"</div>";
    };
    const auto fit=[&](const std::string& text,float width){return std::min(12.5f,width/std::max(1.f,text_units(text)));};
    const auto span=[&](const std::string& text,float width,const std::string& cls="",float font=0,float gap=0){return "<span class='"+cls+"' style='width:"+px(width)+";"+(font?" font-size:"+px(font)+";":"")+(gap?" margin-left:"+px(gap)+";":"")+"'>"+escape(text)+"</span>";};
    const auto at=[&](float x,float y,const std::string& content,const std::string& cls="",float font=0,float width=0){return "<div class='"+cls+"' style='position:absolute; left:"+px(x)+"; top:"+px(y)+";"+(width?" width:"+px(width)+";":"")+(font?" font-size:"+px(font)+";":"")+" line-height:"+px(16)+"; white-space:nowrap;'>"+content+"</div>";};
    const auto& L=next.at("labels");const auto label_of=[&](const char* key){return L.value(key,std::string());};
    const auto number=[](const json& v){return v.is_number()?std::to_string(v.get<long long>()):std::string("--");};
    const auto hint=[&](const char* key){return "<div class='im-hint' style='left:0; top:"+px(224)+"; width:"+px(320)+"; font-size:"+px(6.5f)+";'>"+label(key)+"</div>";};
    const auto art_img=[&](const json& owner,float size){
        if(!owner.contains("art") || !owner.at("art").contains("path"))return std::string();
        const float w=owner.at("art").value("width",96.f),h=owner.at("art").value("height",96.f),scale=std::min(size/w,size/h);
        return "<img src='"+escape(image(owner.at("art").at("path").get<std::string>()))+"' style='width:"+px(w*scale)+"; height:"+px(h*scale)+"; margin:auto;'/>";
    };
    const auto yes_no=[&](unsigned cursor,float x0,float y0,float x1,float y1,float row){
        return box(x0,y0,x1,y1,std::string("<button id='save-yes' class='")+(cursor==0?"on":"")+"' style='height:"+px(row)+"; line-height:"+px(row)+"; padding:0 "+px(2)+";'>"+escape(label_of("yes"))+"</button><button id='save-no' class='"+(cursor==1?"on":"")+"' style='height:"+px(row)+"; line-height:"+px(row)+"; padding:0 "+px(2)+";'>"+escape(label_of("no"))+"</button>",8.f,"save-choice");
    };
    const auto multiline=[&](const std::string& text){std::string out;for(const char c:text)out+=c=='\n'?std::string("<br/>"):escape(std::string(1,c));return out;};
    const std::string screen=next.value("screen",std::string());
    const std::string media[2]={label_of("rom"),label_of("pak")};
    std::string body="<div class='im-root' id='save' style='left:"+px(ox/u)+"; top:"+px(oy/u)+"; width:"+px(320)+"; height:"+px(240)+";'>";
    if(screen=="choice") {
        const unsigned cursor=next.value("cursor",0u);
        std::string items;
        for(unsigned n=0;n<2;++n)
            items+="<button id='save:"+std::to_string(n)+"' class='im-center "+(n==cursor?"on":"")+"' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(2)+"; text-align:center;'>"+escape(media[n])+"</button>";
        body+=box(117,61,203,99,items,std::min(fit(media[0],80),fit(media[1],80)),"save-media");
        const std::string message=media[cursor]+" "+label_of("save_to");
        body+=box(85,125,235,147,at(0,3,escape(message),"im-center",fit(message,146),150),11.f,"save-message");
        if(next.value("waiting",false))
            body+=box(101,109,219,131,at(0,3,escape(label_of("checking")),"im-center",fit(label_of("checking"),114),118),11.f,"save-checking");
        body+=hint(next.value("waiting",false)?"save_message_hint":"save_choice_hint");
    } else if(screen=="slots") {
        const unsigned cursor=next.value("cursor",0u),mode=next.value("mode",0u),medium=next.value("medium",0u);
        body+=box(117,21,203,43,at(0,3,escape(media[medium]),"im-center",fit(media[medium],80),86),11.f,"save-title");
        const auto& slots=next.at("slots");
        for(unsigned n=0;n<slots.size();++n) {
            const auto& s=slots[n];const float y0=69+80*n;const bool used=s.value("used",false);
            body+=box(21,y0,87,y0+70,"<div style='position:absolute; left:0; top:0; width:100%; height:"+px(70)+"; display:flex; align-items:center; justify-content:center;'>"+(used?art_img(s,64):std::string())+"</div>",11.f,"save-face:"+std::to_string(n));
            std::string rows="<button id='save:"+std::to_string(n)+"' class='im-row "+(n==cursor?"on":"")+"' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(2)+";'>"+
                span(label_of("slot")+std::to_string(n+1),68,"",fit(label_of("slot")+std::to_string(n+1),66))+
                (used?span(s.value("name",std::string()),90,"",fit(s.value("name",std::string()),86))+span(label_of("level"),34,"im-dim",fit(label_of("level"),32))+span(number(s.value("level",json())),14,"im-right"):std::string())+"</button>";
            if(used) {
                // 第  話 carries the number in its blanks, as the original's %2d at x=104.
                std::string episode=label_of("episode");const std::string count=number(s.value("episode",json()));
                if(const auto blank=episode.find("  ");blank!=std::string::npos)episode.replace(blank,2,count.size()>1?count:" "+count);
                else if(const auto one=episode.find(' ');one!=std::string::npos)episode.replace(one,1,count);
                else episode+=count;
                const std::string title=s.value("title",std::string())+" "+label_of("clear");
                rows+=at(3,20,"<span class='im-dim'>"+escape(episode.substr(0,episode.find(count)))+"</span>"+escape(count)+"<span class='im-dim'>"+escape(episode.substr(episode.find(count)+count.size()))+"</span>","",fit(episode,60))+
                    at(2,37,escape(title),"",fit(title,204),204)+
                    at(3,54,escape(label_of("turns")),"im-dim",fit(label_of("turns"),54))+at(58,54,number(s.value("turns",json())),"im-right",0,24)+
                    at(104,54,escape(label_of("funds")),"im-dim",fit(label_of("funds"),38))+at(144,54,number(s.value("funds",json())),"im-right",0,62);
            }
            body+=box(87,y0,299,y0+70,rows,11.f,"save-slot:"+std::to_string(n));
        }
        if(mode==1) {
            body+="<div class='im-shade'></div>"+box(53,101,267,139,at(3,3,escape(label_of("overwrite")),"",fit(label_of("overwrite"),208),210)+at(3,19,escape(label_of("ask")),"",fit(label_of("ask"),208),210),11.f,"save-window")+
                yes_no(next.value("window_cursor",0u),221,122,251,163,20);
        } else if(mode==2) {
            // Texts the original placed on one row (status 7 splits lines in two) are joined.
            std::vector<std::pair<int,std::pair<int,std::string>>> rows;
            for(const auto& l:next.value("message",json::array())) {
                auto same=std::find_if(rows.begin(),rows.end(),[&](const auto& r){return r.first==l.value("y",0);});
                if(same==rows.end())rows.push_back({l.value("y",0),{l.value("x",0),l.value("text",std::string())}});
                else same->second.second+=l.value("text",std::string());
            }
            std::string lines;
            for(const auto& [y,row]:rows)
                lines+="<div style='position:absolute; left:"+px(row.first-29)+"; top:"+px(y-77)+"; line-height:"+px(15)+"; white-space:nowrap;'>"+multiline(row.second)+"</div>";
            body+="<div class='im-shade'></div>"+box(29,77,291,179,lines,9.f,"save-pak-message");
        }
        body+=hint(mode==1?"save_confirm_hint":mode==2?"save_message_hint":"save_slots_hint");
    }
    body+="</div>";
    save_doc=document(body,true);save_doc->SetClass("modal",false);
}

// ユニット能力／パイロット能力 (ability_page.cpp): the two nine-row lists (layouts
// 0x6D / 0x6E), the unit page (0x79), its weapon list (0x7A) and the pilot page (0x7B).
void ability_sync() {
    const auto next=ability_page::state();ability_request=next;
    if(!next.value("visible",false)){document_close(ability_doc);ability_stamp.clear();return;}
    const auto stamp=next.dump()+localization::catalog().locale+std::to_string(pixels_w)+"x"+std::to_string(pixels_h);
    if(ability_doc && ability_stamp==stamp)return;
    document_close(ability_doc);ability_stamp=stamp;
    const float u=std::min(pixels_w/320.f,pixels_h/240.f),ox=(pixels_w-320*u)/2,oy=(pixels_h-240*u)/2;
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    const float line=std::max(1.f,float(int(u+0.5f)))/u;
    const auto box=[&](float x0,float y0,float x1,float y1,const std::string& content,float font,const std::string& id="",const std::string& extra="") {
        return "<div class='im-panel'"+(id.empty()?"":" id='"+id+"'")+" style='left:"+px(x0-line)+"; top:"+px(y0-line)+"; width:"+px(x1-x0+1+2*line)+"; height:"+px(y1-y0+1+2*line)+
            "; border-width:"+px(line)+"; font-size:"+px(font)+"; line-height:"+px(16)+";"+extra+"'>"+content+"</div>";
    };
    const auto fit=[&](const std::string& text,float width){return std::min(12.5f,width/std::max(1.f,text_units(text)));};
    const auto span=[&](const std::string& text,float width,const std::string& cls="",float font=0,float gap=0){return "<span class='"+cls+"' style='width:"+px(width)+";"+(font?" font-size:"+px(font)+";":"")+(gap?" margin-left:"+px(gap)+";":"")+"'>"+escape(text)+"</span>";};
    const auto at=[&](float x,float y,const std::string& content,const std::string& cls="",float font=0,float width=0){return "<div class='"+cls+"' style='position:absolute; left:"+px(x)+"; top:"+px(y)+";"+(width?" width:"+px(width)+";":"")+(font?" font-size:"+px(font)+";":"")+" line-height:"+px(16)+"; white-space:nowrap;'>"+content+"</div>";};
    const auto& L=next.at("labels");const auto label_of=[&](const char* key){return L.value(key,std::string());};
    const auto number=[](const json& v){return v.is_number()?std::to_string(v.get<long long>()):std::string("---");};
    const auto dashes=[](const std::string& s){return s.empty()?std::string("--------"):s;};
    const auto hint=[&](const char* key){return "<div class='im-hint' style='left:0; top:"+px(224)+"; width:"+px(320)+"; font-size:"+px(6.5f)+";'>"+label(key)+"</div>";};
    const auto art_img=[&](const json& owner,float size){
        if(!owner.contains("art") || !owner.at("art").contains("path"))return std::string();
        const float w=owner.at("art").value("width",96.f),h=owner.at("art").value("height",96.f),scale=std::min(size/w,size/h);
        return "<img src='"+escape(image(owner.at("art").at("path").get<std::string>()))+"' style='width:"+px(w*scale)+"; height:"+px(h*scale)+"; margin:auto;'/>";
    };
    const std::string screen=next.value("screen",std::string());
    std::string body="<div class='im-root' id='ability' style='left:"+px(ox/u)+"; top:"+px(oy/u)+"; width:"+px(320)+"; height:"+px(240)+";'>";
    if(screen=="units" || screen=="pilots") {
        const bool pilots=screen=="pilots";
        const auto& rows=next.at("rows");const unsigned cursor=next.value("cursor",0u);
        std::string list;
        for(unsigned n=0;n<rows.size();++n) {
            const auto& r=rows[n];
            list+="<button id='ability:"+std::to_string(n)+"' class='im-row "+(n==cursor?"on":"")+"' style='height:"+px(16)+"; line-height:"+px(16)+"; padding:0 "+px(3)+";'>"+
                (pilots?span(r.value("name",std::string()),70)+span(dashes(r.value("unit",std::string())),150)+span(label_of("level"),34,"im-dim",fit(label_of("level"),32))+span(number(r.value("level",json())),16,"im-right")
                       :span(r.value("name",std::string()),126)+span(dashes(r.value("pilot",std::string())),80)+span(label_of("hp"),24,"im-dim")+span(number(r.value("hp",json())),40,"im-right"))+"</button>";
        }
        const auto page=std::to_string(next.value("page",0u)+1)+"/"+std::to_string(next.value("pages",1u));
        const auto& sub=next.value("sub",json::object());
        body+=box(21,21,299,219,
            "<div class='im-row' style='height:"+px(22)+"; line-height:"+px(22)+"; border-bottom-width:"+px(line)+"; border-bottom-color:#3a78e0;'>"+span(page,44,"im-right")+"<span style='width:"+px(2)+"; height:100%; border-left-width:"+px(line)+"; border-left-color:#3a78e0; margin-left:"+px(2)+";'></span><span style='width:"+px(228)+"; text-align:center;'>"+escape(label_of(pilots?"pilot_list":"unit_list"))+"</span></div>"
            "<div id='ability-list' style='margin-top:"+px(3)+";'>"+list+"</div>"
            "<div class='im-row' style='position:absolute; left:0; top:"+px(175)+"; width:100%; height:"+px(22)+"; line-height:"+px(22)+"; padding:0 "+px(3)+"; border-top-width:"+px(line)+"; border-top-color:#3a78e0;'>"+
                span(label_of("sub"),40,"im-dim",fit(label_of("sub"),38))+span(dashes(sub.value("name",std::string())),150)+span(label_of("level"),34,"im-dim",fit(label_of("level"),32))+span(sub.contains("level")?number(sub.at("level")):std::string("--"),16,"im-right")+"</div>",
            11.5f,"ability-panel");
        body+=hint("ability_list_hint");
    } else if(screen=="unit") {
        const auto& unit=next.at("unit");
        std::string parts;
        for(const auto& p:next.value("parts",json::array()))parts+="<div style='padding:0 "+px(3)+"; line-height:"+px(16)+";'>"+escape(p.get<std::string>())+"</div>";
        std::string types;
        for(const auto& tname:next.value("types",json::array()))types+=escape(tname.get<std::string>());
        std::string abilities;
        for(const auto& a:next.value("abilities",json::array()))abilities+="<div style='padding:0 "+px(3)+"; line-height:"+px(16)+";'>"+escape(a.get<std::string>())+"</div>";
        const long long hp=next.value("hp",0ll),hp_max=std::max(1ll,next.value("hp_max",1ll)),en=next.value("en",0ll),en_max=std::max(1ll,next.value("en_max",1ll));
        const std::string terrain=next.value("terrain",std::string("----"));
        const auto stat=[&](const char* key,const json& v,float y){return at(3,y,"<span class='im-dim'>"+escape(label_of(key))+"</span>")+at(60,y,number(v),"im-right",0,40);};
        body+=box(21,10,175,35,"<div style='padding:0 "+px(8)+"; line-height:"+px(24)+"; font-size:"+px(fit(unit.value("name",std::string()),140))+";'>"+escape(unit.value("name",std::string()))+"</div>",12.f,"ability-name")+
            box(21,36,175,58,"<div class='im-row' style='height:"+px(21)+"; line-height:"+px(21)+"; padding:0 "+px(3)+";'>"+span(label_of("size"),34,"im-dim")+span(next.value("size",std::string()),22)+span(label_of("repair"),48,"im-dim",fit(label_of("repair"),46),4)+span(number(next.value("repair",json())),40,"im-right")+"</div>",11.f,"ability-size")+
            box(21,59,175,131,"<div style='margin-top:"+px(3)+";'>"+parts+"</div>",11.f,"ability-parts")+
            box(21,132,155,163,at(6,2,"<span style='color:#ffd75e;'>"+escape(label_of("hp"))+"</span>","",9)+at(36,2,std::to_string(hp)+"/ "+std::to_string(hp_max),"",10)+
                "<div class='im-bar-back' style='left:"+px(32)+"; top:"+px(15)+"; width:"+px(89)+";'></div><div class='im-bar' style='left:"+px(32)+"; top:"+px(15)+"; width:"+px(89.f*float(hp)/float(hp_max))+";'></div>"+
                at(6,16,"<span style='color:#ffd75e;'>"+escape(label_of("en"))+"</span>","",9)+at(36,16,std::to_string(en)+"/ "+std::to_string(en_max),"",10)+
                "<div class='im-bar-back' style='left:"+px(93)+"; top:"+px(22)+"; width:"+px(28)+";'></div><div class='im-bar' style='left:"+px(93)+"; top:"+px(22)+"; width:"+px(28.f*float(en)/float(en_max))+";'></div>",11.f,"ability-gauges")+
            box(21,164,155,219,at(3,3,"<span class='im-dim'>"+escape(label_of("abilities"))+"</span>","",fit(label_of("abilities"),76))+at(83,3,escape(next.value("shield",std::string())),"",10)+"<div style='margin-top:"+px(22)+";'>"+abilities+"</div>",10.5f,"ability-abilities")+
            box(156,132,259,219,at(3,4,"<span class='im-dim'>"+escape(label_of("type"))+"</span>")+at(60,4,types,"im-right",0,40)+stat("move",next.value("move",json()),20)+stat("mobility",next.value("mobility",json()),36)+stat("armor",next.value("armor",json()),52)+stat("limit",next.value("limit",json()),68),11.f,"ability-stats")+
            box(260,132,299,219,at(3,4,"<span class='im-dim'>"+escape(label_of("terrain"))+"</span>","",fit(label_of("terrain"),34))+at(3,20,"<span class='im-dim'>"+escape(label_of("air"))+"</span>")+at(24,20,terrain.substr(0,1))+at(3,36,"<span class='im-dim'>"+escape(label_of("land"))+"</span>")+at(24,36,terrain.substr(1,1))+
                at(3,52,"<span class='im-dim'>"+escape(label_of("sea"))+"</span>")+at(24,52,terrain.substr(2,1))+at(3,68,"<span class='im-dim'>"+escape(label_of("space"))+"</span>")+at(24,68,terrain.substr(3,1)),11.f,"ability-terrain")+
            box(176,8,302,132,art_img(unit,118),12.f,"ability-art","display:flex; align-items:center; justify-content:center;");
        body+=hint("ability_unit_hint");
    } else if(screen=="weapons") {
        const auto page=std::to_string(next.value("page",0u)+1)+"/ "+std::to_string(next.value("pages",1u));
        body+=box(21,21,299,219,weapon_table(next,"ability:",u,line,page),10.5f,"ability-panel","padding:0;");
        body+=hint("ability_weapons_hint");
    } else if(screen=="pilot") {
        // Layout 0x7B: portrait, the hint bar, the name block, six stats, spirits,
        // skills and the terrain grid, on the original rectangles.
        const auto& p=next.at("pilot");const auto& unit=next.value("unit",json::object());const auto& stats=next.at("stats");const auto& over=next.value("over",json::object());
        const bool hidden=p.value("hidden",false);const int mobility=next.value("mobility",-1);
        const auto stat_value=[&](const char* key){return hidden?std::string("---"):number(stats.value(key,json()));};
        const auto plus_value=[&](const char* key){return hidden?std::string("---"):mobility<0?number(stats.value(key,json())):number(stats.value(key,json()))+"+ "+std::to_string(mobility);};
        const auto cls=[&](const char* key){return std::string("im-right ")+(over.value(key,false)?"im-down":"");};
        const float spirit_at[6][2]={{136,144},{192,144},{24,162},{80,162},{136,162},{192,162}},skill_at[3][2]={{96,184},{96,202},{168,202}};
        std::string spirits;
        const auto& sp=next.value("spirits",json::array());
        for(unsigned n=0;n<6;++n)spirits+=at(spirit_at[n][0]-18,spirit_at[n][1]-138,n<sp.size()?escape(sp[n].get<std::string>()):escape(label_of("unknown")),"",10.5f);
        std::string skills;
        const auto& sk=next.value("skills",json::array());
        for(unsigned n=0;n<sk.size() && n<3;++n)skills+=at(skill_at[n][0]-18,skill_at[n][1]-178,escape(sk[n].get<std::string>()),"",10.5f);
        const std::string terrain=next.value("terrain",std::string("----"));
        const auto dim=[&](const char* key){return "<span class='im-dim'>"+escape(label_of(key))+"</span>";};
        body+=box(18,10,110,101,art_img(p,88),12.f,"ability-art","display:flex; align-items:center; justify-content:center;")+
            box(111,10,299,35,"<div style='padding:0 "+px(6)+"; line-height:"+px(24)+"; text-align:right; font-size:"+px(fit(label_of("pilot"),176))+";' class='im-dim'>"+escape(label_of("pilot"))+"</div>",10.f,"ability-title")+
            box(111,36,299,101,at(9,4,escape(p.value("full_name",std::string())),"",fit(p.value("full_name",std::string()),170))+
                at(17,28,dim("morale"),"",10)+at(46,28,number(next.value("morale",json())),"im-right",10.5f,28)+at(81,28,dim("level"),"",10)+at(100,28,number(p.value("level",json())),"im-right",10.5f,20)+
                at(129,28,dim("next"),"",10)+at(154,28,next.contains("next")?number(next.at("next")):std::string("---"),"im-right",10.5f,30)+
                at(17,46,dim("sp"),"",10)+at(90,46,number(next.value("sp",json()))+"/ "+number(next.value("sp_max",json())),"im-right",10.5f,60),11.f,"ability-name")+
            box(18,102,299,137,at(6,4,dim("melee"))+at(40,4,stat_value("melee"),"im-right",0,32)+at(94,4,dim("evade"))+at(126,4,plus_value("evade"),cls("evade"),0,70)+at(214,4,dim("reaction"))+at(248,4,stat_value("reaction"),"im-right",0,32)+
                at(6,20,dim("ranged"))+at(40,20,stat_value("ranged"),"im-right",0,32)+at(94,20,dim("hit"))+at(126,20,plus_value("hit"),cls("hit"),0,70)+at(214,20,dim("skill"))+at(248,20,stat_value("skill"),"im-right",0,32),11.f,"ability-stats")+
            box(18,138,230,177,at(6,6,dim("spirits"),"",fit(label_of("spirits"),86))+spirits,11.f,"ability-spirits")+
            box(18,178,230,219,at(6,6,dim("skills"),"",fit(label_of("skills"),70))+skills,11.f,"ability-skills")+
            box(231,138,299,167,"<div style='text-align:center; line-height:"+px(28)+";' class='im-dim'>"+escape(label_of("terrain"))+"</div>",11.f,"ability-terrain-title")+
            box(231,168,264,193,at(3,4,dim("air"))+at(18,4,terrain.substr(0,1)),11.f)+box(265,168,299,193,at(3,4,dim("land"))+at(18,4,terrain.substr(1,1)),11.f)+
            box(231,194,264,219,at(3,4,dim("sea"))+at(18,4,terrain.substr(2,1)),11.f)+box(265,194,299,219,at(3,4,dim("space"))+at(18,4,terrain.substr(3,1)),11.f);
        body+=hint("ability_pilot_hint");
    }
    body+="</div>";
    ability_doc=document(body,true);ability_doc->SetClass("modal",false);
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
        // Original HUD: only the animation state and its toggle key, top centre.
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
    if(id=="intermission-funds" || id=="upgrade-funds"){funds_editing=id.substr(0,id.size()-6);return;}
    if(id.ends_with("-funds-input"))return;
    funds_editing.clear();
    if(id.starts_with("battle-") && !id.starts_with("battle-ui:") && battle_request.value("visible",false) && !settings_open){battle_page::answer(battle_request.at("serial"),id.substr(7));return;}
    if(id=="settings-open"){settings_open=true;settings_release.hold();input.clear();return;}
    if(id=="settings-close"){physical_held=held();settings_open=false;return;}
    if(id.starts_with("upgrade") && upgrade_request.value("visible",false) && !settings_open) {
        const auto serial=upgrade_request.at("serial").get<uint64_t>();const auto screen=upgrade_request.value("screen",std::string());const bool list=screen=="list" || screen=="weapons";
        if(id.starts_with("upgrade:")) {
            // A click on the cursor row confirms, elsewhere it moves the cursor.
            const unsigned n=unsigned(std::atoi(id.c_str()+8));
            if(n==upgrade_request.value("cursor",0u))upgrade_page::answer(serial,list?"choose":"choose:"+std::to_string(n));
            else upgrade_page::answer(serial,"move:"+std::to_string(n));
        } else if(id=="upgrade-confirm" || id=="upgrade-cancel" || id=="upgrade-dismiss")upgrade_page::answer(serial,id.substr(8));
        return;
    }
    if(id.starts_with("swap") && swap_request.value("visible",false) && !settings_open) {
        const auto serial=swap_request.at("serial").get<uint64_t>();const auto screen=swap_request.value("screen",std::string());
        const bool window=screen=="confirm" || (screen=="fairy_targets" && swap_request.value("mode",0u));
        if(id=="swap-yes")swap_page::answer(serial,window && swap_request.value(screen=="confirm"?"cursor":"window_cursor",0u)==0?"choose":"move:0");
        else if(id=="swap-no")swap_page::answer(serial,window && swap_request.value(screen=="confirm"?"cursor":"window_cursor",0u)==1?(screen=="confirm"?"back":"cancel"):"move:1");
        else if(id.starts_with("swap:") && !window) {
            const unsigned n=unsigned(std::atoi(id.c_str()+5));
            swap_page::answer(serial,n==swap_request.value("cursor",0u)?"choose":"move:"+std::to_string(n));
        }
        return;
    }
    if(id.starts_with("save") && save_request.value("visible",false) && !settings_open) {
        const auto serial=save_request.at("serial").get<uint64_t>();const auto screen=save_request.value("screen",std::string());
        const unsigned mode=save_request.value("mode",0u);
        if(screen=="choice" && save_request.value("waiting",false))return;
        if(mode==2)return;
        if(id=="save-yes")save_page::answer(serial,mode==1 && save_request.value("window_cursor",0u)==0?"choose":"move:0");
        else if(id=="save-no")save_page::answer(serial,mode==1 && save_request.value("window_cursor",0u)==1?"cancel":"move:1");
        else if(id.starts_with("save:") && mode==0) {
            const unsigned n=unsigned(std::atoi(id.c_str()+5));
            save_page::answer(serial,n==save_request.value("cursor",0u)?"choose":"move:"+std::to_string(n));
        }
        return;
    }
    if(id.starts_with("ability:") && ability_request.value("visible",false) && !settings_open) {
        const auto serial=ability_request.at("serial").get<uint64_t>();const auto screen=ability_request.value("screen",std::string());
        const unsigned n=unsigned(std::atoi(id.c_str()+8));
        if(n==ability_request.value("cursor",0u))ability_page::answer(serial,screen=="weapons"?"back":"choose");
        else ability_page::answer(serial,"move:"+std::to_string(n));
        return;
    }
    if(id.starts_with("parts") && parts_request.value("visible",false) && !settings_open) {
        const auto serial=parts_request.at("serial").get<uint64_t>();const auto screen=parts_request.value("screen",std::string());
        const unsigned mode=parts_request.value("mode",0u);
        if(id.starts_with("parts-slot:")) {
            // A click on a slot row moves the slot cursor; on the cursor row it opens
            // the inventory; with the inventory open it goes back to the slots first.
            const unsigned n=unsigned(std::atoi(id.c_str()+11));
            if(mode)parts_page::answer(serial,"cancel");
            else parts_page::answer(serial,n==parts_request.value("cursor",0u)?"choose":"move:"+std::to_string(n));
        } else if(id.starts_with("parts:")) {
            const unsigned n=unsigned(std::atoi(id.c_str()+6));
            if(screen=="slots" && !mode)parts_page::answer(serial,"choose");
            else {
                const unsigned at=screen=="slots"?parts_request.at("inventory").value("cursor",0u):parts_request.value("cursor",0u);
                parts_page::answer(serial,n==at?"choose":"move:"+std::to_string(n));
            }
        }
        return;
    }
    if(id.starts_with("intermission") && intermission_request.value("visible",false) && !settings_open) {
        // Item ids are "intermission:N" and "intermission-swap:N"; a click confirms.
        const auto serial=intermission_request.at("serial").get<uint64_t>();
        if(id.starts_with("intermission:"))intermission_page::answer(serial,"choose:"+id.substr(13));
        else if(id.starts_with("intermission-swap:"))intermission_page::answer(serial,"swap:"+id.substr(18));
        return;
    }
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
        if(id.starts_with("intermission-ui:"))settings::set_native_intermission_ui(id=="intermission-ui:native");
        if(id.starts_with("name-entry-ui:"))settings::set_native_name_entry_ui(id=="name-entry-ui:native");
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
    if((funds_editing=="intermission" && !intermission_page::state().value("visible",false)) || (funds_editing=="upgrade" && !upgrade_page::state().value("visible",false)))funds_editing.clear();
    link_sync();battle_sync();intermission_sync();upgrade_sync();parts_sync();ability_sync();swap_sync();save_sync();mini_sync();
    app_menu::update(language->ui("settings_open"),language->ui("dialogue_reload"));
    if(app_menu::take_settings_request())choose("settings-open");
    if(app_menu::take_reload_request())srw64::dialogue::request_reload();
    settings_sync();notices_sync();context->Update();input.update_rectangle();
    names::window_claim_input(request.visible || (names::owns_input() && held()));
    link_page::window_claim_input(link_request.visible || (link_page::owns_input() && held()));
    battle_page::window_claim_input(battle_request.value("visible",false) || (battle_page::owns_input() && held()));
    intermission_page::window_claim_input(intermission_request.value("visible",false) || (intermission_page::owns_input() && held()));
    upgrade_page::window_claim_input(upgrade_request.value("visible",false) || (upgrade_page::owns_input() && held()));
    parts_page::window_claim_input(parts_request.value("visible",false) || (parts_page::owns_input() && held()));
    ability_page::window_claim_input(ability_request.value("visible",false) || (ability_page::owns_input() && held()));
    swap_page::window_claim_input(swap_request.value("visible",false) || (swap_page::owns_input() && held()));
    save_page::window_claim_input(save_request.value("visible",false) || (save_page::owns_input() && held()));
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
    if(!settings_open && !names::request().visible && !link_request.visible && !battle_request.value("visible",false) && !intermission_request.value("visible",false) && !upgrade_request.value("visible",false) && !parts_request.value("visible",false) && !ability_request.value("visible",false) && !swap_request.value("visible",false) && !save_request.value("visible",false) &&
       (event.type==SDL_KEYDOWN || event.type==SDL_KEYUP))return false;
    if(!funds_editing.empty() && !settings_open){
        auto* doc=funds_editing=="intermission"?intermission_doc:upgrade_doc;
        auto* field=doc?dynamic_cast<Rml::ElementFormControlInput*>(doc->GetElementById(funds_editing+"-funds-input")):nullptr;
        const bool page_up=funds_editing=="intermission"?intermission_request.value("visible",false):upgrade_request.value("visible",false);
        if(!field || !page_up){funds_editing.clear();return true;}
        if(event.type==SDL_KEYDOWN && !event.key.repeat){
            const auto k=event.key.keysym.sym;
            if((k==SDLK_RETURN || k==SDLK_KP_ENTER) && input.accepts_submit()){
                std::string digits;for(char c:field->GetValue())if(c>='0' && c<='9')digits+=c;
                if(!digits.empty()){
                    if(funds_editing=="intermission")intermission_page::answer(intermission_request.at("serial").get<uint64_t>(),"funds:"+digits);
                    else upgrade_page::answer(upgrade_request.at("serial").get<uint64_t>(),"funds:"+digits);
                }
                funds_editing.clear();return true;
            }
            if(k==SDLK_ESCAPE){funds_editing.clear();return true;}
        }
        // Digits, backspace and the cursor keys belong to the edit box.
    } else if(settings_open){
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
    } else if(upgrade_request.value("visible",false)){
        if(event.type==SDL_KEYDOWN){
            const auto k=event.key.keysym.sym;const auto serial=upgrade_request.at("serial").get<uint64_t>();
            const auto screen=upgrade_request.value("screen",std::string());const bool list=screen=="list" || screen=="weapons",confirm=screen=="weapon";
            const auto window=upgrade_request.value("window",std::string());
            const unsigned count=confirm?2:unsigned(upgrade_request.at("rows").size()),at=upgrade_request.value("cursor",0u);
            if(confirm){
                if(window=="confirm" && (k==SDLK_UP || k==SDLK_DOWN)){upgrade_page::answer(serial,"move:"+std::to_string(at^1));return true;}
                if(event.key.repeat)return true;
                if(k==SDLK_RETURN || k==SDLK_z || k==SDLK_SPACE){upgrade_page::answer(serial,window!="confirm"?"dismiss":at==0?"confirm":"cancel");return true;}
                if(k==SDLK_ESCAPE || k==SDLK_x){upgrade_page::answer(serial,window=="confirm"?"cancel":"dismiss");return true;}
                return true;
            }
            if(window=="bonus"){if(!event.key.repeat && (k==SDLK_RETURN || k==SDLK_z || k==SDLK_SPACE || k==SDLK_ESCAPE || k==SDLK_x))upgrade_page::answer(serial,"dismiss");return true;}
            if(window.empty() && count && (k==SDLK_UP || k==SDLK_DOWN)){upgrade_page::answer(serial,"move:"+std::to_string((at+(k==SDLK_UP?count-1:1))%count));return true;}
            if(list && window.empty() && (k==SDLK_LEFT || k==SDLK_RIGHT)){
                const unsigned pages=upgrade_request.value("pages",1u),page=upgrade_request.value("page",0u);
                if(pages>1)upgrade_page::answer(serial,"page:"+std::to_string((page+(k==SDLK_LEFT?pages-1:1))%pages));return true;
            }
            if(event.key.repeat)return true;
            if(k==SDLK_RETURN || k==SDLK_z || k==SDLK_SPACE){
                if(window=="confirm")upgrade_page::answer(serial,"confirm");
                else if(!window.empty())upgrade_page::answer(serial,"dismiss");
                else upgrade_page::answer(serial,list?"choose":"choose:"+std::to_string(at));
                return true;
            }
            if(k==SDLK_ESCAPE || k==SDLK_x){upgrade_page::answer(serial,window=="confirm"?"cancel":window.empty()?"back":"dismiss");return true;}
        }
    } else if(swap_request.value("visible",false)){
        if(event.type==SDL_KEYDOWN){
            const auto k=event.key.keysym.sym;const auto serial=swap_request.at("serial").get<uint64_t>();
            const auto screen=swap_request.value("screen",std::string());
            const bool window=screen=="confirm" || (screen=="fairy_targets" && swap_request.value("mode",0u));
            if(window){
                const unsigned at=swap_request.value(screen=="confirm"?"cursor":"window_cursor",0u);
                if(k==SDLK_UP || k==SDLK_DOWN){swap_page::answer(serial,"move:"+std::to_string(at^1));return true;}
                if(event.key.repeat)return true;
                if(k==SDLK_RETURN || k==SDLK_z || k==SDLK_SPACE){swap_page::answer(serial,at==0?"choose":screen=="confirm"?"back":"cancel");return true;}
                if(k==SDLK_ESCAPE || k==SDLK_x){swap_page::answer(serial,screen=="confirm"?"back":"cancel");return true;}
                return true;
            }
            const unsigned count=unsigned(swap_request.at("rows").size()),at=swap_request.value("cursor",0u);
            if(count && (k==SDLK_UP || k==SDLK_DOWN)){swap_page::answer(serial,"move:"+std::to_string((at+(k==SDLK_UP?count-1:1))%count));return true;}
            if(k==SDLK_LEFT || k==SDLK_RIGHT){
                const unsigned pages=swap_request.value("pages",1u),page=swap_request.value("page",0u);
                if(pages>1)swap_page::answer(serial,"page:"+std::to_string((page+(k==SDLK_LEFT?pages-1:1))%pages));return true;
            }
            if(event.key.repeat)return true;
            if(k==SDLK_RETURN || k==SDLK_z || k==SDLK_SPACE){if(count)swap_page::answer(serial,"choose");return true;}
            if(k==SDLK_ESCAPE || k==SDLK_x){swap_page::answer(serial,"back");return true;}
        }
    } else if(save_request.value("visible",false)){
        if(event.type==SDL_KEYDOWN){
            const auto k=event.key.keysym.sym;const auto serial=save_request.at("serial").get<uint64_t>();
            const auto screen=save_request.value("screen",std::string());const unsigned mode=save_request.value("mode",0u);
            if(screen=="choice" && save_request.value("waiting",false))return true;
            if(mode==2){
                if(event.key.repeat)return true;
                if(k==SDLK_RETURN || k==SDLK_z || k==SDLK_SPACE){save_page::answer(serial,"choose");return true;}
                if(k==SDLK_ESCAPE || k==SDLK_x){save_page::answer(serial,"back");return true;}
                return true;
            }
            const unsigned at=save_request.value(mode==1?"window_cursor":"cursor",0u);
            if(k==SDLK_UP || k==SDLK_DOWN){save_page::answer(serial,"move:"+std::to_string(at^1));return true;}
            if(event.key.repeat)return true;
            if(k==SDLK_RETURN || k==SDLK_z || k==SDLK_SPACE){save_page::answer(serial,"choose");return true;}
            if(k==SDLK_ESCAPE || k==SDLK_x){save_page::answer(serial,mode==1?"cancel":"back");return true;}
        }
    } else if(ability_request.value("visible",false)){
        if(event.type==SDL_KEYDOWN){
            const auto k=event.key.keysym.sym;const auto serial=ability_request.at("serial").get<uint64_t>();
            const auto screen=ability_request.value("screen",std::string());
            const bool list=screen=="units" || screen=="pilots" || screen=="weapons";
            if(list){
                const unsigned count=unsigned(ability_request.at("rows").size()),at=ability_request.value("cursor",0u);
                if(count && (k==SDLK_UP || k==SDLK_DOWN)){ability_page::answer(serial,"move:"+std::to_string((at+(k==SDLK_UP?count-1:1))%count));return true;}
                if(k==SDLK_LEFT || k==SDLK_RIGHT){
                    const unsigned pages=ability_request.value("pages",1u),page=ability_request.value("page",0u);
                    if(pages>1)ability_page::answer(serial,"page:"+std::to_string((page+(k==SDLK_LEFT?pages-1:1))%pages));return true;
                }
            }
            if(event.key.repeat)return true;
            // The unit and pilot pages: L / R (Q / E, also the arrows) step through the list.
            if(!list && (k==SDLK_q || k==SDLK_LEFT)){ability_page::answer(serial,"prev");return true;}
            if(!list && (k==SDLK_e || k==SDLK_RIGHT)){ability_page::answer(serial,"next");return true;}
            if(k==SDLK_RETURN || k==SDLK_z || k==SDLK_SPACE){if(screen!="weapons" && screen!="pilot")ability_page::answer(serial,"choose");return true;}
            if(k==SDLK_ESCAPE || k==SDLK_x){ability_page::answer(serial,"back");return true;}
        }
    } else if(parts_request.value("visible",false)){
        if(event.type==SDL_KEYDOWN){
            const auto k=event.key.keysym.sym;const auto serial=parts_request.at("serial").get<uint64_t>();
            const auto screen=parts_request.value("screen",std::string());const unsigned mode=parts_request.value("mode",0u);
            const bool inventory=screen=="slots" && mode;
            const json& pane=inventory?parts_request.at("inventory"):parts_request;
            const unsigned count=screen=="slots" && !mode?unsigned(parts_request.at("unit").at("parts").size()):unsigned(pane.at("rows").size()),at=pane.value("cursor",0u);
            if(count && (k==SDLK_UP || k==SDLK_DOWN)){parts_page::answer(serial,"move:"+std::to_string((at+(k==SDLK_UP?count-1:1))%count));return true;}
            if((screen=="list" || inventory) && (k==SDLK_LEFT || k==SDLK_RIGHT)){
                const unsigned pages=pane.value("pages",1u),page=pane.value("page",0u);
                if(pages>1)parts_page::answer(serial,"page:"+std::to_string((page+(k==SDLK_LEFT?pages-1:1))%pages));return true;
            }
            if(event.key.repeat)return true;
            if(k==SDLK_RETURN || k==SDLK_z || k==SDLK_SPACE){parts_page::answer(serial,"choose");return true;}
            if(k==SDLK_ESCAPE || k==SDLK_x){parts_page::answer(serial,inventory?"cancel":"back");return true;}
        }
    } else if(intermission_request.value("visible",false)){
        // As the original: up and down wrap around and repeat while held, A confirms,
        // B only closes the swap window.
        if(event.type==SDL_KEYDOWN){
            const auto k=event.key.keysym.sym;const auto serial=intermission_request.at("serial").get<uint64_t>();
            const bool submenu=intermission_request.value("submenu",false);
            const unsigned count=submenu?2:unsigned(intermission_request.at("items").size());
            const unsigned at=intermission_request.value(submenu?"swap_cursor":"cursor",0u);
            const std::string prefix=submenu?"swap-":"";
            const bool reverse=k==SDLK_UP || (k==SDLK_TAB && (event.key.keysym.mod&KMOD_SHIFT));
            if(k==SDLK_UP || k==SDLK_DOWN || k==SDLK_TAB){intermission_page::answer(serial,prefix+"move:"+std::to_string((at+(reverse?count-1:1))%count));return true;}
            if(event.key.repeat)return true;
            if(k==SDLK_RETURN || k==SDLK_z || k==SDLK_SPACE){intermission_page::answer(serial,(submenu?"swap:":"choose:")+std::to_string(at));return true;}
            if((k==SDLK_ESCAPE || k==SDLK_x) && submenu){intermission_page::answer(serial,"swap-close");return true;}
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
    return consumed || settings_open || names::request().visible || link_request.visible || battle_request.value("visible",false) || intermission_request.value("visible",false) || upgrade_request.value("visible",false) || parts_request.value("visible",false) || ability_request.value("visible",false) || swap_request.value("visible",false) || save_request.value("visible",false);
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
void render_shutdown(){auto lock=lock_ui();ready=false;if(initialized){name_page.reset();Rml::Shutdown();initialized=false;context=nullptr;settings_doc=link_doc=notice_doc=battle_doc=intermission_doc=upgrade_doc=parts_doc=ability_doc=swap_doc=mini_doc=nullptr;}renderer.reset();}
void shutdown(){app_menu::shutdown();input.flush_sdl();SDL_StopTextInput();window=nullptr;names::window_claim_input(false);link_page::window_claim_input(false);intermission_page::window_claim_input(false);upgrade_page::window_claim_input(false);parts_page::window_claim_input(false);ability_page::window_claim_input(false);swap_page::window_claim_input(false);battle_page::window_claim_input(false);}
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
    result["input_owners"]={{"battle",battle_page::owns_input()},{"intermission",intermission_page::owns_input()},{"upgrade",upgrade_page::owns_input()},{"parts",parts_page::owns_input()},{"ability",ability_page::owns_input()},{"swap",swap_page::owns_input()},{"names",names::owns_input()},{"link",link_page::owns_input()},{"settings",settings_window::owns_input()},{"locale",settings::owns_input()}};
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
