#include "frontend.hpp"
#ifdef __ANDROID__
#include <jni.h>
#endif
#include "app_menu.hpp"
#include "ui_fonts.hpp"
#include "slant_decorator.hpp"
#include "name_page.hpp"
#include "text_input.hpp"
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
#include "save_store.hpp"
#include "library.hpp"
#include "battle_viewer.hpp"
#include "native_sprite.hpp"
#include "title_page.hpp"
#include "mini_stage.hpp"
#include "campaign_library.hpp"
#include "campaign_switch.hpp"
#include "settings_window.hpp"
#include "presentation_settings.hpp"
#include "game_frame.hpp"
#include "frame_rate.hpp"
#include "steam_deck.hpp"
#include "rule_fixes.hpp"
#include "cheats.hpp"
#include "bezel.hpp"
#include "post_filter.hpp"
#include "notices.hpp"
#include "update_check.hpp"
#include "bug_report.hpp"
#include "debug_ui.hpp"
#include "debug_protocol.hpp"
#include "modal_input.hpp"
#include "presentation/image_mode.hpp"
#include "input_mode.hpp"
#include "touch_pad.hpp"
#include "touch_scene.hpp"
#include "presentation/rgba_file.hpp"
#include "text/button_prompts.hpp"
#include "stb/stb_image.h"
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <map>
#include <set>
#include <tuple>

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
std::unique_ptr<SlantInstancer> slant_instancer;
// Two passes over the pages (docs/native/bezels-and-filters.md): those that belong to the
// game picture are drawn before a RetroArch filter, the rest after it. Every page starts
// with a <layer-mark>, chrome='1' for the rest; its render tells the proxy below whose
// geometry follows, and the proxy drops the other pass's. RmlUi paints an element's own
// background before its children, so a page's body background belongs to whatever the page
// before it ended on: every page therefore ends with a mark back to the game pass, painted
// last (layer-mark.layer-end). Without it a 4:3 bezel (chrome, always first) put the name
// and Link Battler pages' backgrounds in the chrome pass, over the whole filtered picture.
int layer_pass=-1,layer_now=0;
class LayerMark:public Rml::Element {
public:
    using Rml::Element::Element;
protected:
    void OnRender() override {layer_now=GetAttribute<int>("chrome",0);}
};
Rml::ElementInstancerGeneric<LayerMark> layer_mark_instancer;
class LayeredRender:public Rml::RenderInterface {
    Rml::RenderInterface* real;
    bool muted() const {return layer_pass>=0 && layer_now!=layer_pass;}
public:
    explicit LayeredRender(Rml::RenderInterface* to):real(to){}
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v,Rml::Span<const int> i) override {return real->CompileGeometry(v,i);}
    void RenderGeometry(Rml::CompiledGeometryHandle g,Rml::Vector2f t,Rml::TextureHandle x) override {if(!muted())real->RenderGeometry(g,t,x);}
    void ReleaseGeometry(Rml::CompiledGeometryHandle g) override {real->ReleaseGeometry(g);}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& d,const Rml::String& s) override {return real->LoadTexture(d,s);}
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> s,Rml::Vector2i d) override {return real->GenerateTexture(s,d);}
    void ReleaseTexture(Rml::TextureHandle x) override {real->ReleaseTexture(x);}
    void EnableScissorRegion(bool e) override {real->EnableScissorRegion(e);}
    void SetScissorRegion(Rml::Rectanglei r) override {real->SetScissorRegion(r);}
    void EnableClipMask(bool e) override {real->EnableClipMask(e);}
    void RenderToClipMask(Rml::ClipMaskOperation o,Rml::CompiledGeometryHandle g,Rml::Vector2f t) override {real->RenderToClipMask(o,g,t);}
    void SetTransform(const Rml::Matrix4f* m) override {real->SetTransform(m);}
    Rml::LayerHandle PushLayer() override {return real->PushLayer();}
    void CompositeLayers(Rml::LayerHandle s,Rml::LayerHandle d,Rml::BlendMode b,Rml::Span<const Rml::CompiledFilterHandle> f) override {real->CompositeLayers(s,d,b,f);}
    void PopLayer() override {real->PopLayer();}
    Rml::TextureHandle SaveLayerAsTexture() override {return real->SaveLayerAsTexture();}
    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override {return real->SaveLayerAsMaskImage();}
    Rml::CompiledFilterHandle CompileFilter(const Rml::String& n,const Rml::Dictionary& p) override {return real->CompileFilter(n,p);}
    void ReleaseFilter(Rml::CompiledFilterHandle f) override {real->ReleaseFilter(f);}
    Rml::CompiledShaderHandle CompileShader(const Rml::String& n,const Rml::Dictionary& p) override {return real->CompileShader(n,p);}
    void RenderShader(Rml::CompiledShaderHandle h,Rml::CompiledGeometryHandle g,Rml::Vector2f t,Rml::TextureHandle x) override {if(!muted())real->RenderShader(h,g,t,x);}
    void ReleaseShader(Rml::CompiledShaderHandle h) override {real->ReleaseShader(h);}
};
std::unique_ptr<LayeredRender> layered;
std::map<std::string,std::array<int,5>> art_bounds;  // opaque bounds and file width of unit art, by path
std::vector<Rml::byte> font, chinese_font, english_font, symbol_font, prompt_font;
std::map<std::string,std::string> images;
std::atomic_bool settings_open{}, physical_held{};
// The settings window opened from the title's MOD entry is the MOD manager: its own pages
// (campaigns, art, dialogue, music) instead of the settings'; closing it ends that.
std::atomic_bool mod_open{};
// Its entries are hidden for now (2026-10-03, user: MOD stays at the design stage,
// docs/design/mod-packages.md); the manager itself stays and opens by id.
constexpr bool mod_entry_shown=false;
constexpr const char* mod_pages[]={"campaigns","art","dialogue","audio"};
unsigned mod_page{};
// The Library opened from the title's corner beside MOD (library.hpp), in the same frame:
// units or characters in a list, the chosen one's details beside it. The list keeps its
// place per tab; the details follow the choice without rebuilding the window.
std::atomic_bool library_open{};
// The question, once, whether to check for updates on start-up (update_check.hpp), in the
// settings window's frame on the title; asked: put this run.
bool update_ask_open{},update_asked{};
unsigned library_tab{};
std::array<unsigned,2> library_index{};
bool library_tab_focus{};
// The last input was a controller: hints use the "_pad" labels (Steam Deck).
bool pad_mode{};
// The mouse is in use: hover highlights show only then, not under a pointer left resting
// on a page while the keys or a controller move the cursor.
bool pointer_mode{};
ModalInputRelease settings_release;
link_page::Request link_request;
std::array<bool,3> ticked{};
unsigned link_focus{};
bool link_waiting{};
Rml::ElementDocument *settings_doc{}, *link_doc{}, *notice_doc{};
std::string settings_stamp, link_stamp, notice_stamp;
json battle_request;
uint64_t battle_request_serial{};   // the encounter the battle page shows
Rml::ElementDocument* battle_doc{},*original_doc{};
std::string battle_stamp,original_stamp;
json intermission_request,upgrade_request,parts_request,ability_request,swap_request,save_request,title_request;
Rml::ElementDocument* title_doc{};
std::string title_stamp;
// "intermission" or "upgrade" while the player types a new 資金 figure into that page.
std::string funds_editing;
Rml::ElementDocument* upgrade_doc{},* parts_doc{},* ability_doc{},* swap_doc{},* save_doc{};
std::string upgrade_stamp,parts_stamp,ability_stamp,swap_stamp,save_stamp;
Rml::ElementDocument* intermission_doc{};
std::string intermission_stamp;
Rml::ElementDocument* mini_doc{};
std::string mini_stamp;
// The title screen's settings entry, for players with no menu bar (Steam Deck).
Rml::ElementDocument* home_doc{};
Rml::ElementDocument* home_chrome_doc{};   // the version and the settings entry, over a filter
std::string home_stamp;
// Every page's rebuild stamp, cleared when the hints change device.
std::array<std::string*,14> all_stamps() {
    return {&settings_stamp,&link_stamp,&notice_stamp,&battle_stamp,&original_stamp,&title_stamp,&upgrade_stamp,
            &parts_stamp,&ability_stamp,&swap_stamp,&save_stamp,&intermission_stamp,&mini_stamp,&home_stamp};
}
std::string preedit;
std::mutex notice_mutex;
std::deque<json> notices_pending, notices_kept;
struct Banner {std::string text;double until;};
std::deque<Banner> banners;
int pixels_w{},pixels_h{};
float pixel_ratio=1;
// The window and the game picture's width (game_frame.hpp), which place the pages drawn
// in the original's 320x240 coordinates.
std::string frame_stamp(){return std::to_string(pixels_w)+"x"+std::to_string(pixels_h)+"/"+std::to_string(frame::width(pixels_w,pixels_h));}
// Our pages' room, in screen pixels: the game picture while it is 4:3 (a bezel frames it,
// user 2026-10-06), else the whole window. The pages lay out in it and the dp follows it.
struct Area {float x,y,w,h;};
Area page_area() {
    if(settings::wide_picture())return {0,0,float(pixels_w),float(pixels_h)};
    const float u=frame::scale(float(pixels_w),float(pixels_h)),w=frame::width(float(pixels_w),float(pixels_h))*u,h=frame::kHeight*u;
    return {(pixels_w-w)/2,(pixels_h-h)/2,w,h};
}
// The View menu's window sizes (app_menu.hpp): n times the original's 240 lines, as wide
// as the picture is for the window's present shape, in points.
SDL_Point scaled_size(int w,int h,int n){return {int(std::lround(frame::width(float(w),float(h))*n)),int(frame::kHeight)*n};}
// The display's room for the window, below its title bar.
SDL_Rect window_room(int& top){
    SDL_Rect room{};SDL_GetDisplayUsableBounds(std::max(0,SDL_GetWindowDisplayIndex(window)),&room);
    top=0;SDL_GetWindowBordersSize(window,&top,nullptr,nullptr,nullptr);
    return room;
}
app_menu::WindowState window_menu_state(){
    app_menu::WindowState state;
    if(!window)return state;
    state.fullscreen=SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN;
    int w,h,top;SDL_GetWindowSize(window,&w,&h);const SDL_Rect room=window_room(top);
    for(int n=1;n<=app_menu::kScales;++n){
        const SDL_Point size=scaled_size(w,h,n);
        if(std::abs(size.x-w)<=1 && size.y==h)state.scale=n;
        if(size.x<=room.w && size.y+top<=room.h)state.largest=n;
    }
    return state;
}
// Keeps the window's centre where it was, inside the display's room.
void scale_window(int n){
    if(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)return;
    if(SDL_GetWindowFlags(window)&SDL_WINDOW_MAXIMIZED)SDL_RestoreWindow(window);
    int w,h,x,y,top;SDL_GetWindowSize(window,&w,&h);SDL_GetWindowPosition(window,&x,&y);
    const SDL_Point size=scaled_size(w,h,n);const SDL_Rect room=window_room(top);
    x=std::clamp(x+(w-size.x)/2,room.x,std::max(room.x,room.x+room.w-size.x));
    y=std::clamp(y+(h-size.y)/2,room.y+top,std::max(room.y+top,room.y+room.h-size.y));
    SDL_SetWindowSize(window,size.x,size.y);SDL_SetWindowPosition(window,x,y);
}
void toggle_fullscreen(){SDL_SetWindowFullscreen(window,(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)?0:SDL_WINDOW_FULLSCREEN_DESKTOP);}
// The settings page's window rows follow the window (a Deck shows none).
std::string window_stamp(){
    if(on_steam_deck())return {};
    const auto state=window_menu_state();
    return "w"+std::to_string(state.fullscreen)+std::to_string(state.scale)+std::to_string(state.largest);
}
float ui_density=1;  // screen pixels per dp, as set on the RmlUi context
std::unique_lock<std::mutex> lock_ui() {
    std::unique_lock lock(mutex);completed.wait(lock,[]{return !in_flight;});return lock;
}
std::string escape(const std::string& text){return Rml::StringUtilities::EncodeRml(text);}
// Button tokens ("{A}", "{Esc}") become what the player bound to them, as icons of the
// controller in use or keys (text/button_prompts.hpp, the SRW64Prompts font).
text::PadFamily pad_family(){return text::PadFamily(input::pad_family.load());}
input::Bindings hint_bindings=input::live_bindings().get();  // refreshed with the hints (sync)
text::PromptContext prompt_context(bool pad){return {pad,pad_family(),&hint_bindings,settings::key_display_name};}
// The native pages follow the player's bindings (input_bindings.hpp): a key bound to an N64
// button reads as that button's default key, which the page code knows (Z for A, Q for L,
// the arrows for the D-pad and the stick). Enter, Esc, Tab and the arrow keys keep their own
// meaning whatever else they are bound to, so no binding can lock the player out of a page.
// The animation button reads as C-down's key, which the pre-battle pages take. A key bound
// to something with no page meaning (the trigger functions) does nothing here, nor does an
// unbound letter: the letters are the page code's words (Z, X...), not the player's. Keys
// the debug interface presses (no window) are taken as page keys as they are. Returns the
// host button a key stands for (settings, language, Original / HD), for the caller; the
// controller bridge (pad_keys) already sends the default keys.
std::optional<input::Action> follow_bindings(SDL_Event& event) {
    if((event.type!=SDL_KEYDOWN && event.type!=SDL_KEYUP) || !event.key.windowID)return std::nullopt;
    const int key=int(event.key.keysym.scancode);
    using input::Action;
    namespace sc=input::scancode;
    if(key==sc::Return || key==sc::Escape || key==SDL_SCANCODE_TAB || key==sc::Up || key==sc::Down || key==sc::Left || key==sc::Right)return std::nullopt;
    const auto* action=input::action_of_key(hint_bindings,key);
    SDL_Keycode page=SDLK_UNKNOWN;
    if(!action) {
        if(key>=SDL_SCANCODE_A && key<=SDL_SCANCODE_Z){event.key.keysym.sym=SDLK_UNKNOWN;event.key.keysym.scancode=SDL_SCANCODE_UNKNOWN;}
        return std::nullopt;
    }
    switch(*action) {
    case Action::A:page=SDLK_z;break; case Action::B:page=SDLK_x;break; case Action::Z:page=SDLK_SPACE;break;
    case Action::Start:page=SDLK_RETURN;break; case Action::L:page=SDLK_q;break; case Action::R:page=SDLK_e;break;
    case Action::CUp:page=SDLK_i;break; case Action::CDown:page=SDLK_k;break; case Action::CLeft:page=SDLK_j;break; case Action::CRight:page=SDLK_l;break;
    case Action::DUp:case Action::StickUp:page=SDLK_UP;break; case Action::DDown:case Action::StickDown:page=SDLK_DOWN;break;
    case Action::DLeft:case Action::StickLeft:page=SDLK_LEFT;break; case Action::DRight:case Action::StickRight:page=SDLK_RIGHT;break;
    case Action::Animation:page=SDLK_k;break;
    case Action::Settings:case Action::Language:case Action::Images:return *action;
    default:break;
    }
    event.key.keysym.sym=page;
    event.key.keysym.scancode=page==SDLK_UNKNOWN?SDL_SCANCODE_UNKNOWN:SDL_GetScancodeFromKey(page);
    return std::nullopt;
}
std::string label(const std::string& key){
    const auto& catalog=localization::catalog();
    if(pad_mode){const auto pad=key+"_pad";if(auto value=catalog.ui(pad);value!=pad)return escape(text::expand_prompts(value,prompt_context(true)));}
    // A string with no controller variant still names the controller's buttons while it is in use.
    return escape(text::expand_prompts(catalog.ui(key),prompt_context(pad_mode)));
}
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
layer-mark {display:block; width:0; height:0;}
layer-mark.layer-end {position:absolute; left:0; top:0; z-index:2000000000;}
div,h1,h2,p { display: block; } h1 {font-size: 28dp; margin: 0 0 12dp;} h2 {font-size: 20dp; margin: 18dp 0 10dp;}
p {color: #9eafc3; margin: 10dp 0;} .modal {background-color: #0b1421;} p.credit {font-size: 12dp; color: #7f8fa3; margin-top: 16dp;}
.page {width: 88%; max-width: 1080dp; margin: 24dp auto; height: 90%; overflow-y: auto;}
button {display: inline-block; background-color: #152436; color: #d6e2ef; border: 1dp #304859; border-radius: 6dp; padding: 10dp 14dp; margin: 4dp; cursor: pointer; tab-index: auto;}
button:hover,button:focus {border-color: #9be4f7;} button.on {background-color: #23506a; border-color: #9be4f7;}
.pad button:focus {border-color: #ffd75e; background-color: #2c4a63;}
button:disabled {opacity: 0.45;} .row {display: flex;}
.card {width: 28%;} img {width: 86dp; height: 86dp; margin: 8dp;}
#fps {position: absolute; right: 6dp; top: 6dp; padding: 1dp 6dp; font-size: 12dp; color: #e6f6ff; background-color: #000000a8;}
#notices {width: 86%; margin: 8dp auto; text-align: center;} .banner {padding: 12dp; background-color: #122131ed; border: 1dp #9be4f7; margin-bottom: 6dp;}

.set-shade {position:absolute; left:0; top:0; width:100%; height:100%; display:flex; justify-content:center; align-items:center; background-color:#040712b8;}
.set-panel {display:flex; flex-direction:column; width:88%; max-width:1040dp; height:88%; max-height:820dp; box-sizing:border-box; padding:14dp 26dp 12dp; color:#e8eefc; background-color:#0c122cf2; border:1dp #3fd0ff; border-top:3dp #3fd0ff;}
.set-panel h1 {margin:0 0 8dp; font-size:24dp; letter-spacing:2dp;}
.set-tabs {display:flex; gap:6dp; margin-bottom:10dp;}
.set-tabs button {flex:1 1 0; min-width:0; margin:0; padding:0 6dp; height:34dp; line-height:32dp; box-sizing:border-box; text-align:center; white-space:nowrap; overflow:hidden; font-size:15dp; font-weight:bold; color:#a4b0d2; background-color:#0c122ceb; border:1dp #3fd0ff; border-radius:0;}
.set-tabs button.on {color:#0b1230; background-color:#3fd0ff; border-color:#3fd0ff;}
.set-tabs button:focus {color:#ffd75e; border-color:#ffd75e;}
.set-tabs button.on:focus {color:#0b1230; background-color:#ffd75e; border-color:#ffd75e;}
.set-body {flex:1 1 0; min-height:0; overflow-y:auto; padding-right:8dp;}
.set-body h2 {margin:16dp 0 4dp; padding-bottom:4dp; font-size:15dp; color:#3fd0ff; border-bottom:1dp #3fd0ff59;}
.set-body p {margin:3dp 0 0; font-size:12dp; line-height:1.35; color:#a4b0d2;}
.set-name {font-size:17dp; font-weight:bold; color:#e8eefc;}
.set-row {padding:10dp 12dp; border-bottom:1dp #3fd0ff1f;}
.set-path {margin:2dp 0 0; font-size:13dp; color:#7f8fa3;}
.set-message {margin:10dp 12dp; color:#9fe0ff;}
.set-note {font-size:13dp; color:#7f8fa3;}
.set-line {display:flex; align-items:center; gap:18dp;} .set-line .set-name {flex:1 1 0; min-width:0;}
.set-seg {display:flex; flex-shrink:0; border:2dp #3fd0ff; background-color:#0c122ceb;}
.set-seg button {margin:0; padding:7dp 16dp; border:0; border-radius:0; font-size:14dp; font-weight:bold; white-space:nowrap; color:#e8eefc; background-color:transparent;}
.set-seg button.on {color:#0b1230; background-color:#3fd0ff;}
.set-seg button:focus {color:#0b1230; background-color:#ffd75e;}
.set-actions {display:flex; flex-wrap:wrap; gap:8dp; margin-top:8dp;}
.set-actions button {margin:0; padding:7dp 14dp; font-size:14dp; color:#e8eefc; background-color:#0c122ceb; border:2dp #3fd0ff; border-radius:0;}
.set-actions button:focus {color:#ffd75e; border-color:#ffd75e;}
button.set-toggle,button.set-toggle.on {display:flex; align-items:center; gap:14dp; width:100%; box-sizing:border-box; margin:0; padding:9dp 12dp; text-align:left; color:#e8eefc; background-color:transparent; border:0; border-bottom:1dp #3fd0ff1f; border-radius:0;}
.set-toggle .set-name {flex:1 1 0; min-width:0; font-size:15dp; font-weight:normal;}
.set-toggle .switch {display:block; flex-shrink:0; width:38dp; height:20dp; box-sizing:border-box; padding:2dp; border-radius:10dp; background-color:#2a3550;}
.set-toggle .switch span {display:block; width:16dp; height:16dp; border-radius:8dp; background-color:#a4b0d2;}
.set-toggle.on .switch {background-color:#3fd0ff;} .set-toggle.on .switch span {margin-left:18dp; background-color:#0b1230;}
button.set-toggle:focus {background-color:#ffd75e24;} button.set-toggle:focus .set-name {color:#ffd75e;}
.set-key {display:flex; align-items:center; gap:16dp; padding:6dp 12dp; font-size:14dp; border-bottom:1dp #3fd0ff14;}
.set-key span {flex:1 1 0; min-width:0; color:#d6ddf2;} .set-key b {flex:0 0 46%; text-align:right; font-weight:normal; color:#ffd75e;}
.ctl-found {margin:4dp 0 0; font-size:13dp; color:#a4b0d2;}
.ctl-head {display:flex; padding:6dp 12dp 2dp; font-size:12dp; color:#a4b0d2;}
.ctl-head .n,.ctl-row .n {flex:1 1 0; min-width:0;}
.ctl-head .k,.ctl-row .k {flex:0 0 150dp; text-align:center;}
button.ctl-row {display:flex; align-items:center; width:100%; box-sizing:border-box; margin:0; padding:7dp 12dp; text-align:left; font-size:14dp; color:#e8eefc; background-color:transparent; border:0; border-bottom:1dp #3fd0ff1f; border-radius:0;}
button.ctl-row .k {color:#ffd75e; font-size:16dp;}
button.ctl-row:focus,button.ctl-row:hover {background-color:#3fd0ff26;}
.ctl-capture {position:absolute; left:0; top:0; width:100%; height:100%; display:flex; justify-content:center; align-items:center; background-color:#040712a8;}
.ctl-capture-box {width:540dp; padding:18dp 22dp; text-align:center; background-color:#0c122cf8; border:2dp #ffd75e;}
.ctl-capture-title {font-size:18dp; font-weight:bold; color:#ffd75e;}
.ctl-capture-box p {margin:8dp 0 12dp; font-size:13dp; color:#a4b0d2;}
.set-about {padding:0 12dp;} .set-about div {margin-top:6dp; font-size:15dp; color:#d6ddf2;}
.set-about h2.app {font-size:22dp; letter-spacing:1dp; color:#ffd75e; border-bottom-width:0;}
.set-panel.set-ask {width:auto; min-width:460dp; max-width:640dp; height:auto; max-height:88%;}
.set-ask .set-body {flex:0 0 auto;} .set-ask .set-seg {display:inline-flex; margin-top:12dp;}
.set-row > .set-seg {display:inline-flex; margin-top:8dp;}
.set-error {margin-top:8dp; font-size:13dp; color:#ff8d8d;}
.set-foot {display:flex; align-items:center; gap:12dp; margin-top:8dp; padding-top:8dp; border-top:1dp #3fd0ff40;}
.set-hint {flex:1 1 0; min-width:0; font-size:12dp; color:#a4b0d2;}
.set-foot button {margin:0; padding:6dp 18dp; font-size:14dp; font-weight:bold; color:#0b1230; background-color:#3fd0ff; border:2dp #3fd0ff; border-radius:0;}
.set-foot button:focus {background-color:#ffd75e; border-color:#ffd75e;}
body.pointer .set-tabs button:focus {color:#a4b0d2; border-color:#3fd0ff;}
body.pointer .set-tabs button:hover {color:#e8eefc;}
body.pointer .set-tabs button.on {color:#0b1230; background-color:#3fd0ff; border-color:#3fd0ff;}
body.pointer .set-seg button:focus {color:#e8eefc; background-color:transparent;}
body.pointer .set-seg button:hover {background-color:#3fd0ff40;}
body.pointer .set-seg button.on {color:#0b1230; background-color:#3fd0ff;}
body.pointer .set-actions button:focus {color:#e8eefc; border-color:#3fd0ff;}
body.pointer .set-actions button:hover {background-color:#3fd0ff29;}
body.pointer button.set-toggle:focus {background-color:transparent;} body.pointer button.set-toggle:focus .set-name {color:#e8eefc;}
body.pointer button.set-toggle:hover {background-color:#3fd0ff1a;}
body.pointer .set-foot button:focus {background-color:#3fd0ff; border-color:#3fd0ff;}
body.pointer .set-foot button:hover {background-color:#8fe4ff;}
/* The title's own pages (the Library, the battle viewer) hide the title under them. */
.set-shade.solid {background-color:#040712;}
.set-panel.lib-panel {width:96%; height:94%; max-width:1180dp; max-height:760dp; padding:10dp 18dp 8dp; background-color:#0c122c;}
.lib-top {display:flex; align-items:center; gap:18dp; margin-bottom:8dp;}
.lib-top h1 {margin:0; font-size:20dp; letter-spacing:1dp; white-space:nowrap;}
.lib-top .set-tabs {flex:1 1 0; margin:0;}
.lib-top .set-tabs button {height:30dp; line-height:28dp; font-size:14dp;}
.lib-panel .set-foot {margin-top:6dp; padding-top:6dp;}
.lib-panel .set-foot button {padding:4dp 16dp;}
.lib-main {flex:1 1 0; min-height:0; display:flex; gap:16dp;}
.lib-list {width:210dp; flex-shrink:0; overflow-y:auto; padding-right:6dp; border-right:1dp #3fd0ff40;}
button.lib-item {display:flex; align-items:center; gap:8dp; width:100%; box-sizing:border-box; margin:0; padding:5dp 8dp; text-align:left; white-space:nowrap; overflow:hidden; font-size:14dp;
    color:#c8d4ee; background-color:transparent; border-width:0 0 0 3dp; border-color:transparent; border-radius:0;}
button.lib-item .n {flex:1 1 0; min-width:0; overflow:hidden;}
button.lib-item .m {flex-shrink:0; font-size:11dp; color:#8f9bbd;}
button.lib-item .dot {width:6dp; height:6dp; flex-shrink:0; border-radius:3dp;}
button.lib-item .dot.enemy {background-color:#ff6b5a;}
button.lib-item.on {color:#ffffff; background-color:#23506a; border-color:#3fd0ff;}
button.lib-item:focus, .pad button.lib-item:focus {color:#ffd75e; border-color:#ffd75e;}
body.pointer button.lib-item:focus {color:#ffffff; border-color:#3fd0ff;} body.pointer button.lib-item:hover {background-color:#3fd0ff26;}
.lib-group {margin:10dp 0 2dp; padding:2dp 8dp; font-size:12dp; font-weight:bold; color:#3fd0ff; border-bottom:1dp #3fd0ff59; white-space:nowrap; overflow:hidden;}
.lib-group.first {margin-top:0;}
.lib-detail {flex:1 1 0; min-width:0; overflow-y:auto; padding-right:8dp;}
.lib-art {flex-shrink:0; display:flex; align-items:center; justify-content:center; background-color:#060a1c; border:1dp #3fd0ff59;}
.lib-art img {margin:0;}
.lib-name {font-size:26dp; font-weight:bold; color:#ffffff; white-space:nowrap; line-height:1.15;}
.lib-sub {font-size:13dp; color:#a4b0d2; white-space:nowrap;}
.lib-detail p.lib-note {font-size:12dp; color:#a4b0d2; margin:6dp 0 0;}
.none {color:#5d6890;}
/* The battle viewer (docs/design/battle-viewer.md §9.4; the canvas design 2026-10-04, after the Z special disc): one page, 反 left, 攻 right; a choice opens a box over the other half */
.set-panel.vw-panel {position:relative; width:96%; height:94%; max-width:1180dp; max-height:760dp; padding:8dp 14dp 8dp; background-color:#070b1df5;}
.vw-top {position:relative; display:flex; align-items:center; gap:12dp; height:30dp; margin-bottom:6dp;}
.vw-title {height:26dp; line-height:26dp; padding:0 24dp 0 12dp; font-size:16dp; font-weight:bold; letter-spacing:2dp; color:#0b1230; decorator:slant(#ffd75e #ffd75e 0dp 0dp 0dp 12dp 0dp 0dp);}
.vw-sub {font-size:10dp; letter-spacing:3dp; color:#4f6aa0;}
.vw-roles {position:absolute; left:50%; top:0; width:150dp; margin-left:-75dp; height:30dp; display:flex; align-items:center; justify-content:center; gap:8dp;}
.vw-roles .r {width:34dp; height:26dp; line-height:26dp; text-align:center; font-size:19dp; font-weight:bold;}
.vw-roles .r.def {color:#3fd0ff; border:2dp #3fd0ff; height:22dp; line-height:22dp;} .vw-roles .r.att {color:#0b1230; background-color:#ffb547;}
button.vw-swap {width:52dp; height:26dp; margin:0; padding:0; line-height:22dp; text-align:center; font-size:11dp; color:#3fd0ff; background-color:#0c122c; border:2dp #3fd0ff; border-radius:13dp;}
button.vw-swap:focus {color:#0b1230; background-color:#ffd75e; border-color:#ffd75e;}
body.pointer button.vw-swap:focus {color:#3fd0ff; background-color:#0c122c; border-color:#3fd0ff;} body.pointer button.vw-swap:hover {background-color:#3fd0ff40;}
.vw-body {flex:1 1 0; min-height:0; display:flex; flex-direction:column; gap:6dp;}
.vw-cols {flex:1 1 0; min-height:0; display:flex;}
.vw-col {flex:1 1 0; min-width:0; display:flex; flex-direction:column; gap:4dp;}
.vw-colgap {width:24dp; flex-shrink:0;}
.vw-card {position:relative; flex-shrink:0; overflow:hidden; background-color:#0d1430; border:1dp #2a3b66; border-top:3dp #ffb547;}
.vw-card.def {border-top-color:#3fd0ff;}
.vw-card .bg {position:absolute; left:0; top:0; right:0; bottom:0; overflow:hidden;}
.vw-card .bg img {margin-left:0;}
.vw-card .shade {position:absolute; left:0; top:0; right:0; bottom:0; background-color:#070b1d99;}
.vw-card .role {position:absolute; top:6dp; font-size:34dp; font-weight:bold; line-height:1;}
.vw-card.def .role {left:12dp; color:#3fd0ff;} .vw-card.att .role {right:12dp; color:#ffb547;}
.vw-card .badge {position:absolute; top:10dp; padding:0 6dp; font-size:10dp; line-height:16dp; font-weight:bold; color:#0b1230; background-color:#ffd75e;}
.vw-card.def .badge {right:12dp;} .vw-card.att .badge {left:12dp;}
.vw-card .art {position:absolute; left:0; right:0; top:4dp; display:flex; justify-content:center; align-items:center;}
.vw-card .art img, .vw-card .face img {margin:0;}
.vw-card.def .art img, .vw-card.def .face img {transform:scaleX(-1);}
.vw-card .face {position:absolute; bottom:28dp; border:1dp #3fd0ff; background-color:#0c122c; display:flex; align-items:center; justify-content:center; overflow:hidden;}
.vw-card.def .face {right:14dp;} .vw-card.att .face {left:14dp; border-color:#ffb547;}
.vw-card .foot {position:absolute; bottom:4dp; left:12dp; right:12dp; display:flex; align-items:center; gap:6dp; height:22dp;}
.vw-card.att .foot {justify-content:flex-end;}
.vw-card .hp {font-size:10dp; color:#8f9bbd;} .vw-card .hpv {font-size:14dp; font-weight:bold; color:#e8eefc;}
.vw-card .hpv.hurt {color:#ffd75e;} .vw-card .hpv.down {color:#ff6b5a;}
button.vw-cell {flex-shrink:0; display:flex; align-items:center; gap:8dp; height:30dp; margin:0; padding:0 10dp; text-align:left; font-size:13dp; color:#e8eefc; background-color:#0d1430; border:1dp #2a3b66; border-radius:0;}
button.vw-cell .k {flex-shrink:0; width:52dp; font-size:10dp; color:#8f9bbd;}
button.vw-cell .v {flex:1 1 0; min-width:0; white-space:nowrap; overflow:hidden;}
button.vw-cell .m {flex-shrink:0; font-size:10dp; color:#8f9bbd; white-space:nowrap;}
button.vw-cell .t {flex-shrink:0; padding:0 4dp; font-size:10dp; font-weight:bold; color:#0b1230; background-color:#8fd7ff;}
button.vw-cell .t.melee {background-color:#ffb547;}
button.vw-cell .thumb {flex-shrink:0; width:40dp; height:22dp; overflow:hidden; margin-left:-6dp;}
button.vw-cell .thumb img {margin:0;}
button.vw-cell .gates {flex-shrink:0; display:flex; gap:3dp;}
button.vw-cell .gates span {width:16dp; height:16dp; line-height:16dp; text-align:center; font-size:9dp; font-weight:bold; color:#4f5b80; border:1dp #2a3b66;}
button.vw-cell .gates span.lit {color:#8fe4ff; border-color:#3fd0ff;}
button.vw-cell.on {border-color:#ffd75e; background-color:#1d1a2e;}
button.vw-cell:focus, .pad button.vw-cell:focus {color:#ffd75e; border-color:#ffd75e;}
body.pointer button.vw-cell:focus {color:#e8eefc; border-color:#2a3b66;} body.pointer button.vw-cell.on:focus {border-color:#ffd75e;} body.pointer button.vw-cell:hover {border-color:#3fd0ff;}
button.vw-bgm {flex-shrink:0; display:flex; align-items:center; justify-content:center; gap:10dp; height:30dp; margin:0; padding:0 12dp; color:#e8eefc; background-color:#11112a; border:1dp #ffb54766; border-radius:0;}
button.vw-bgm .k {font-size:10dp; color:#ffb547;}
button.vw-bgm .t {font-size:13dp; letter-spacing:1dp; white-space:nowrap;}
button.vw-bgm.on, button.vw-bgm:focus {border-color:#ffd75e;}
body.pointer button.vw-bgm:focus {border-color:#ffb54766;} body.pointer button.vw-bgm.on:focus {border-color:#ffd75e;} body.pointer button.vw-bgm:hover {border-color:#3fd0ff;}
.vw-help {flex:1 1 0; min-width:0; font-size:12dp; color:#c8d4ee; white-space:nowrap;}
button.vw-start {margin:0; height:32dp; line-height:32dp; text-align:center; padding:0 34dp 0 38dp; font-size:16dp; font-weight:bold; letter-spacing:3dp; color:#0b1230; border:0; border-radius:0; background-color:transparent; decorator:slant(#ffd75e #ffd75e 0dp 0dp 12dp 0dp 0dp 12dp);}
button.vw-start:focus {decorator:slant(#ffffff #ffffff 0dp 0dp 12dp 0dp 0dp 12dp);}
body.pointer button.vw-start:focus {decorator:slant(#ffd75e #ffd75e 0dp 0dp 12dp 0dp 0dp 12dp);} body.pointer button.vw-start:hover {decorator:slant(#ffe48f #ffe48f 0dp 0dp 12dp 0dp 0dp 12dp);}
/* The slanted button is only its slant: none of the footers' or the controller's rectangle when focused. */
.set-panel.vw-panel button.vw-start, .set-panel.vw-panel button.vw-start:focus, .set-panel.vw-panel button.vw-start:hover, .pad .set-panel.vw-panel button.vw-start:focus,
body.pointer .set-panel.vw-panel button.vw-start:focus, body.pointer .set-panel.vw-panel button.vw-start:hover {background-color:transparent; border-width:0;}
.vw-list {flex:1 1 0; min-height:0; width:auto; border-right:0;}
/* The box a choice opens, over the other side */
.vw-pop {position:absolute; top:44dp; bottom:50dp; display:flex; flex-direction:column; background-color:#090e26fa; border:2dp #3fd0ff;}
.vw-pop.left {left:14dp;} .vw-pop.right {right:14dp;}
.vw-pop-head {flex-shrink:0; display:flex; align-items:center; gap:8dp; height:34dp; padding:0 8dp; border-bottom:1dp #2a3b66;}
.vw-pop-head .role {height:22dp; line-height:22dp; padding:0 16dp 0 8dp; font-size:13dp; font-weight:bold; color:#0b1230; decorator:slant(#ffb547 #ffb547 0dp 0dp 0dp 10dp 0dp 0dp);}
.vw-pop-head .role.def {decorator:slant(#3fd0ff #3fd0ff 0dp 0dp 0dp 10dp 0dp 0dp);}
.vw-pop-head .ttl {font-size:14dp; font-weight:bold; white-space:nowrap;}
.vw-pop-head .gap {flex:1 1 0;}
.vw-pager-row {flex-shrink:0; display:flex; align-items:center; gap:10dp; height:32dp; padding:0 8dp; border-bottom:1dp #2a3b66;}
.vw-pager-row .page {flex:1 1 0; min-width:0; text-align:center; font-size:15dp; font-weight:bold; color:#5be37d; white-space:nowrap;}
.vw-pager-row .no {font-size:12dp; color:#8fe4ff; white-space:nowrap;}
button.vw-pager {margin:0; width:28dp; height:26dp; padding:0; font-size:15dp; color:#3fd0ff; background-color:transparent; border:0; border-radius:0;}
button.vw-pager:focus {color:#0b1230; background-color:#ffd75e;}
body.pointer button.vw-pager:focus {color:#3fd0ff; background-color:transparent;} body.pointer button.vw-pager:hover {background-color:#3fd0ff26;}
.vw-cats {flex-shrink:0; display:flex; padding:6dp 8dp 2dp;}
.vw-cats button {margin:0; height:24dp; line-height:22dp; padding:0 12dp; font-size:12dp; color:#c8d4ee; background-color:#0c122c; border:1dp #3fd0ff; border-radius:0;}
.vw-cats button.on {background-color:#3fd0ff; color:#0b1230; font-weight:bold;}
.vw-cats button:focus {background-color:#ffd75e; color:#0b1230;}
body.pointer .vw-cats button:focus {background-color:#0c122c; color:#c8d4ee;} body.pointer .vw-cats button.on:focus {background-color:#3fd0ff; color:#0b1230;}
.vw-plist {flex:1 1 0; min-height:0; overflow-y:auto; padding:4dp 0;}
.vw-song-group {padding:6dp 10dp 1dp; font-size:10dp; font-weight:bold; letter-spacing:1dp; color:#3fd0ff; white-space:nowrap;}
button.vw-li {display:flex; align-items:center; gap:10dp; width:100%; box-sizing:border-box; height:34dp; margin:0; padding:0 10dp; text-align:left; font-size:13dp; color:#e8eefc; background-color:transparent; border:0; border-left:3dp transparent; border-radius:0;}
button.vw-li .ico {flex-shrink:0; width:34dp; height:30dp; display:flex; align-items:center; justify-content:center; overflow:hidden; background-color:#070b1d;}
button.vw-li .ico img {margin:0;}
button.vw-li .ph {font-size:14dp; font-weight:bold; color:#2a3b66;}
button.vw-li.scene {height:44dp;} button.vw-li.scene .ico {width:64dp; height:36dp;}
button.vw-li.song {height:26dp;} button.vw-li.auto {height:32dp;}
/* Name and subtitle (a pilot's skills) both give way, each in proportion to its width, then fit their share:
   a fixed subtitle and a badge left a long name less than its 60 % floor (Deck, the largest size). */
button.vw-li .n {flex:1 1 auto; min-width:0; white-space:nowrap; overflow:hidden;}
button.vw-li .s {flex:0 1 auto; min-width:0; font-size:10dp; color:#8f9bbd; white-space:nowrap; overflow:hidden;}
button.vw-li .t {flex-shrink:0; padding:0 4dp; font-size:10dp; font-weight:bold; color:#0b1230; background-color:#8fd7ff;}
button.vw-li .t.melee {background-color:#ffb547;}
button.vw-li .badge {flex-shrink:0; padding:0 5dp; font-size:9dp; line-height:15dp; font-weight:bold; color:#0b1230; background-color:#3fd0ff;}
button.vw-li.on {background-color:#2a1d3a; border-left-color:#ffd75e; color:#ffd75e; font-weight:bold;}
button.vw-li:focus {background-color:#3fd0ff26;} button.vw-li.on:focus {background-color:#3a2a4e;}
body.pointer button.vw-li:focus {background-color:transparent;} body.pointer button.vw-li.on:focus {background-color:#2a1d3a;} body.pointer button.vw-li:hover {background-color:#3fd0ff1a;}
.vw-pop-foot {flex-shrink:0; display:flex; align-items:center; gap:8dp; height:42dp; padding:0 8dp; border-top:1dp #2a3b66;}
.vw-pop-foot .set-hint {flex:1 1 0; min-width:0; white-space:nowrap;}
.vw-pop-foot button {margin:0; height:28dp; line-height:28dp; padding:0 14dp; font-size:13dp; color:#0b1230; background-color:#3fd0ff; border:0; border-radius:0;}
.vw-pop-foot button:focus {background-color:#ffd75e;}
body.pointer .vw-pop-foot button:focus {background-color:#3fd0ff;} body.pointer .vw-pop-foot button:hover {background-color:#8fe4ff;}
.vw-pop-foot button.vw-start {height:28dp; line-height:28dp; padding:0 26dp 0 30dp; font-size:14dp; background-color:transparent;}
.vw-resbox {flex:1 1 0; min-height:0; display:flex; gap:8dp; padding:8dp;}
.vw-resbox .col {flex:1 1 0; min-width:0; display:flex; flex-direction:column; border:1dp #2a3b66;}
.vw-resbox .col.dmg {flex:0 0 auto; width:44%;}
.vw-resbox .h {padding:4dp 8dp; font-size:11dp; font-weight:bold; color:#ffb547;}
.vw-resbox .gap {flex:1 1 0;}
button.vw-opt {display:flex; align-items:center; gap:6dp; width:100%; box-sizing:border-box; height:28dp; margin:0; padding:0 8dp; text-align:left; font-size:12dp; color:#e8eefc; background-color:transparent; border:0; border-left:3dp transparent; border-radius:0;}
button.vw-opt .g {flex:1 1 0;}
button.vw-opt .s {flex-shrink:0; font-size:9dp; font-weight:normal; color:#6c789c;}
button.vw-opt.on {background-color:#2a1d3a; border-left-color:#ffd75e; color:#ffd75e; font-weight:bold;}
button.vw-opt[disabled] {color:#4f5b80;}
button.vw-opt:focus {background-color:#3fd0ff26;} button.vw-opt.on:focus {background-color:#3a2a4e;}
body.pointer button.vw-opt:focus {background-color:transparent;} body.pointer button.vw-opt.on:focus {background-color:#2a1d3a;} body.pointer button.vw-opt:hover {background-color:#3fd0ff1a;}
.vw-resbox .seg {display:flex; gap:3dp; padding:0 6dp 4dp;}
.vw-resbox .seg button, .vw-resbox .steps button {flex:1 1 0; min-width:0; margin:0; height:24dp; line-height:24dp; padding:0; text-align:center; font-size:10dp;}
.vw-resbox .seg button.on {background-color:#3fd0ff; color:#0b1230; font-weight:bold;}
.vw-resbox .dmgv {text-align:center; font-size:20dp; font-weight:bold; color:#ffffff; padding:4dp 0;}
.vw-resbox .dmgv.down {color:#ff8d80;}
.vw-resbox .steps {display:flex; gap:3dp; padding:0 6dp 6dp;}
button.vw-listen {display:flex; align-items:center; gap:6dp; margin:0; height:26dp; white-space:nowrap; padding:0 12dp; font-size:12dp; font-weight:bold; color:#0b1230; background-color:#3fd0ff; border:0; border-radius:0;}
button.vw-listen.on {background-color:#ffd75e;}
button.vw-listen:focus {background-color:#ffffff;}
body.pointer button.vw-listen:focus {background-color:#3fd0ff;} body.pointer button.vw-listen.on:focus {background-color:#ffd75e;} body.pointer button.vw-listen:hover {background-color:#8fe4ff;}
/* Both pages: the art square and the side beside it */
.lx-hero {display:flex; gap:16dp;}
.lx-side {flex:1 1 0; min-width:0; display:flex; flex-direction:column; gap:6dp;}
.lx-name {display:flex; align-items:baseline; flex-wrap:wrap; gap:4dp 10dp;}
.lx-name .gold {margin-left:auto; padding:2dp 10dp; font-size:13dp; font-weight:bold; color:#0b1230; background-color:#ffd75e; white-space:nowrap;}
.lx-name .sub {font-size:14dp; color:#c8d4ee; white-space:nowrap;}
.lx-tags {display:flex; flex-wrap:wrap; align-items:center; gap:5dp 8dp;}
.lx-tags > span {padding:2dp 9dp; font-size:12dp; color:#c8d4ee; border:1dp #3fd0ff59; white-space:nowrap;}
.lx-tags > span.on {color:#e8eefc; background-color:#152436;}
.lx-name .tag {padding:1dp 8dp; font-size:12dp; border:1dp #3fd0ff59; white-space:nowrap;}
.lx-name .tag.ally {color:#9ff0c8; border-color:#5fd08a80;} .lx-name .tag.enemy {color:#ffb0a6; border-color:#ff6b5a80;} .lx-name .tag.role {color:#ffe9a8; border-color:#ffd75e80;}
.lx-love {display:flex; flex-wrap:wrap; align-items:center; gap:6dp 10dp; width:100%;}
.lx-love .k {font-size:12dp; color:#a4b0d2;}
.lx-love .who {display:flex; align-items:center; gap:6dp; padding:1dp 8dp 1dp 1dp; font-size:13dp; border:1dp #3fd0ff59;}
.lx-love .face {width:26dp; height:26dp; display:flex; align-items:center; justify-content:center; background-color:#060a1c; overflow:hidden;}
.lx-love .face img {margin:0;}
.lx-love .dir {font-size:10dp; padding:0 5dp; color:#ffc2dd; border:1dp #ffc2dd80;}
.lx-love .dir.both {color:#2a1426; background-color:#ffc2dd; border-color:#ffc2dd;}
.lx-stats {display:flex; flex-wrap:wrap; row-gap:5dp;}
.lx-stat {width:25%; box-sizing:border-box; padding-right:12dp;}
.lx-stats.three .lx-stat {width:33.3%;}
.lx-stat .k {display:block; font-size:12dp; color:#a4b0d2;}
.lx-stat .row {display:flex; align-items:baseline;}
.lx-stat .a {font-size:16dp; font-weight:bold;} .lx-stat .to {font-size:10dp; color:#5d6890; margin:0 3dp;} .lx-stat .b {font-size:14dp; font-weight:bold; color:#8fe4ff;}
.lx-stat.legend .k {font-size:9dp; line-height:1.2; white-space:normal; color:#8f9bbd;} .lx-stat.legend .a {font-size:12dp;} .lx-stat.legend .b {font-size:12dp;}
.lx-stat .bar {display:flex; height:5dp; margin-top:3dp; background-color:#1a2448;}
.lx-stat .bar .p1 {height:5dp; background-color:#e8eefc;} .lx-stat .bar .p2 {height:5dp; background-color:#3fd0ff;}
.lx-stats.unit .lx-stat .bar .p1 {background-color:#3fd0ff;}
.lx-left {flex-shrink:0; display:flex; flex-direction:column; gap:8dp;}
.lx-terrain {display:flex; align-items:center; gap:4dp;}
.lx-terrain .k {font-size:12dp; color:#a4b0d2; margin-right:4dp;}
.lx-terrain .t {width:32dp; height:32dp; display:flex; flex-direction:column; align-items:center; justify-content:center; background-color:#1a2448; border:1dp #3fd0ff59; font-size:9dp; color:#a4b0d2;}
.lx-terrain .t .r {font-size:14dp; font-weight:bold; color:#c8d4ee; line-height:1;}
.lx-terrain .t.a {background-color:#3fd0ff; border-color:#3fd0ff; color:#0b1230;} .lx-terrain .t.a .r {color:#0b1230;}
.lx-terrain .t.none {border-color:#2c3a66;} .lx-terrain .t.none .r {color:#5d6890;}
.lx-head {margin:12dp 0 5dp; padding-bottom:4dp; font-size:13dp; font-weight:bold; color:#3fd0ff; border-bottom:1dp #3fd0ff59;}
.lx-head span {margin-left:10dp; font-size:11dp; font-weight:normal; color:#8f9bbd;}
/* Units: the weapon table */
.lib-wrow {display:flex; align-items:center; padding:4dp 0; font-size:13dp; border-bottom:1dp #3fd0ff1f;}
.lib-wrow.head {color:#a4b0d2; font-size:11dp; padding:0 0 5dp;}
.lib-wrow .n {flex:1 1 0; min-width:0; overflow:hidden; line-height:1.5;}
.lib-wrow .n .name {white-space:nowrap;}
.lib-wrow .c {width:40dp; flex-shrink:0; text-align:right; white-space:nowrap;}
.lib-wrow .c.big {width:48dp; font-size:15dp; font-weight:bold;}
.lib-wrow .c.full {color:#8fe4ff;} .lib-wrow .c.ty {width:34dp;}
.lu-flag {display:inline-block; white-space:nowrap; margin-left:2dp; padding:0 5dp; font-size:10dp; font-weight:bold; color:#e8eefc; background-color:#2c3a66;}
.lu-flag.unlock {color:#0b1230; background-color:#ffd75e;}
.lu-type {display:inline-block; width:22dp; height:16dp; line-height:16dp; text-align:center; font-size:11dp; font-weight:bold; color:#0b1230;}
.lu-type.t1 {background-color:#3fd0ff;} .lu-type.t2 {background-color:#5fd08a;} .lu-type.t3 {background-color:#c49bff;} .lu-type.t4 {background-color:#ffb46b;}
.lu-legend {display:flex; flex-wrap:wrap; align-items:center; gap:4dp 14dp; margin-top:7dp; font-size:11dp; color:#a4b0d2;}
.lu-legend .item {display:flex; align-items:center; gap:5dp;}
/* Characters: spirit cards and skill tracks */
.lx-spirits {display:flex; gap:6dp;}
.lx-spirits .card {flex:1 1 0; min-width:0; padding:3dp 8dp; background-color:#152436; border:1dp #3fd0ff59;}
.lx-spirits .n {font-size:14dp; font-weight:bold; white-space:nowrap; overflow:hidden;}
.lx-spirits .row {display:flex; font-size:11dp;} .lx-spirits .lv {flex:1 1 0; color:#ffd75e;} .lx-spirits .cost {color:#8f9bbd;}
.lx-track {display:flex; align-items:center; gap:12dp; height:28dp;}
.lx-track .k {width:120dp; flex-shrink:0; font-size:12dp; font-weight:bold; white-space:nowrap; overflow:hidden;}
.lx-track .line {flex:1 1 0; position:relative; height:28dp; margin-right:10dp;}
.lx-track .rail {position:absolute; left:0; right:0; top:7dp; height:2dp; background-color:#2c3a66;}
.lx-track .mark {position:absolute; top:0; width:0; height:28dp;}
.lx-track .tick {position:absolute; left:-1dp; top:2dp; width:3dp; height:12dp; background-color:#ffd75e;}
.lx-track .lv {position:absolute; left:-14dp; top:15dp; width:28dp; text-align:center; font-size:10dp; color:#ffd75e;}

.bp-dim {position:absolute; left:0; top:0; width:100%; height:100%; background-color:#070a1655;}
.bp-tint {position:absolute; top:0; width:35%; height:100%;}
.bp-tint.left {left:0; decorator:horizontal-gradient(#78144659 #080c1e00);}
.bp-tint.right {right:0; decorator:horizontal-gradient(#080c1e00 #0a3c6e59);}
.battle-page {position:relative; display:flex; flex-direction:column; width:100%; max-width:1500dp; height:100%; margin:0 auto; box-sizing:border-box; padding:12dp 18dp 8dp; gap:7dp; color:#e8eefc; font-size:11dp;}
.bp-row {display:flex; justify-content:space-between; align-items:stretch;}
.bp-mid {flex:1 1 0; min-height:0; align-items:center;}
.bp-side {width:31%; box-sizing:border-box;} .bp-center {width:35.5%; box-sizing:border-box;}
.battle-page.touch .bp-bottom .bp-side {width:37.5%;} .battle-page.touch .bp-bottom .bp-center {width:24%;}
.battle-page.touch .bp-pilot-head img {width:64dp; height:64dp;} .battle-page.touch .bp-stat {padding:3dp 5dp;}
.battle-page.touch .bp-segment div {white-space:nowrap;} .battle-page.touch .bp-segment button {margin:0; padding:5dp 7dp; font-size:12dp;}
.battle-page.touch .bp-banner {height:110dp;} .battle-page.touch .bp-detail {margin-top:3dp; padding:2dp 12dp;}
.bp-banner {height:124dp; overflow:hidden; padding:6dp 22dp 6dp 12dp; decorator:slant(#0c122ceb #ff6fa8 2dp 0dp 0dp 0dp 0dp 16dp);}
.bp-banner.right {padding:6dp 12dp 6dp 22dp; decorator:slant(#0c122ceb #3fd0ff 2dp 0dp 16dp 0dp 0dp 0dp);}
.bp-phase {display:flex; justify-content:center; align-items:flex-start; padding-top:2dp;}
.bp-phase div {width:104dp; height:26dp; line-height:26dp; text-align:center; font-size:12dp; font-weight:bold; color:#a4b0d2; decorator:slant(#0c122ceb #3fd0ff 1dp 0dp 0dp 10dp 0dp 0dp);}
.bp-phase div.enemy {decorator:slant(#0c122ceb #3fd0ff 1dp 0dp 10dp 0dp 0dp 0dp);}
.bp-phase div.enemy.on {color:#0b1230; decorator:slant(#ff6fa8 #ff6fa8 1dp 0dp 10dp 0dp 0dp 0dp);}
.bp-phase div.player.on {color:#0b1230; decorator:slant(#3fd0ff #3fd0ff 1dp 0dp 0dp 10dp 0dp 0dp);}
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
.bp-clash {display:flex; align-items:flex-end; padding:9dp 22dp 9dp; decorator:slant(#0c122cf0 #3fd0ff 0dp 3dp 0dp 0dp 19dp 19dp);}
.bp-clash-side {flex:1; min-width:0;} .bp-clash-side.left {text-align:right;}
.bp-damage {font-size:46dp; line-height:48dp; font-weight:bold; color:#ffd75e;}
.bp-damage.d5 {font-size:42dp;} .narrow .bp-damage.d4 {font-size:42dp;} .narrow .bp-damage.d5 {font-size:36dp;}
.narrow .bp-clash {padding:9dp 14dp;}
.bp-caption {font-size:10dp; color:#a4b0d2;}
.bp-arrow {width:62dp; text-align:center; padding-bottom:16dp; font-size:8dp; line-height:10dp; color:#ffd75e;}
.bp-arrow div {font-size:13dp;}
.bp-versus {display:flex; align-items:center; margin-top:5dp; padding:3dp 20dp; decorator:slant(#0c122ceb #0c122ceb 0dp 0dp 10dp 0dp 0dp 10dp); font-size:20dp;}
.bp-versus span {flex:1; color:#3fd0ff;} .bp-versus span.left {text-align:right; color:#ff6fa8;}
.bp-versus b {width:100dp; margin:0 10dp; padding:2dp 0; text-align:center; font-size:11dp; color:#0b1230; decorator:slant(#3fd0ff #3fd0ff 0dp 0dp 5dp 5dp 0dp 0dp);}
.bp-detail {margin-top:5dp; padding:4dp 12dp; background-color:#0c122ce0; font-size:10dp; color:#d6ddf2;}
.bp-detail div {display:flex; align-items:center; margin:2dp 0;}
.bp-detail span {flex:1;} .bp-detail span.left {text-align:right;} .bp-detail span.good {color:#57e389;}
.bp-detail b {width:124dp; text-align:center; color:#a4b0d2; font-weight:normal;}
.battle-response {margin:6dp auto 0; text-align:center; font-size:11dp;}
.battle-response span {display:inline-block; padding:3dp 11dp; background-color:#3fd0ff29; border:1dp #3fd0ff;} .battle-response b {color:#3fd0ff;}
.bp-pilot {padding:7dp 9dp; background-color:#0c122ceb; border:2dp #ff6fa8;} .bp-pilot.right {border-color:#3fd0ff;}
.bp-pilot-head {display:flex; gap:8dp;}
.bp-pilot-head img {width:96dp; height:96dp; margin:0; border:2dp #ff6fa8;} .right .bp-pilot-head img {border-color:#3fd0ff;}
.narrow .bp-pilot-head img {width:64dp; height:64dp;} .narrow .bp-stat {padding:3dp 5dp;} .narrow .battle-actions button {margin:2dp; padding:5dp 7dp;}
.narrow .bp-tag {font-size:10dp;} .narrow .weapon-cost {font-size:11dp; line-height:13dp; height:26dp;} .narrow .bp-caption,.narrow .bp-detail {font-size:11dp;}
.narrow .bp-arrow {font-size:10dp; line-height:12dp;}
.bp-pilot-info {flex:1; min-width:0;}
.bp-pilot-name {display:flex; justify-content:space-between; align-items:flex-end; font-size:17dp; font-weight:bold; height:24dp;}
.bp-pilot-name .level {font-size:12dp; font-weight:normal; color:#a4b0d2;}
.bp-stats {display:flex; gap:4dp; margin-top:3dp;}
.bp-stat {flex:1 1 auto; display:flex; justify-content:space-between; gap:6dp; padding:3dp 7dp; font-size:13dp; white-space:nowrap; background-color:#ff6fa81f;} .right .bp-stat {background-color:#3fd0ff1f;}
.bp-stat span {color:#a4b0d2;} .bp-stat b.spent {color:#ffd75e;}
.battle-spirits {display:flex; flex-wrap:wrap; margin-top:5dp; height:46dp; overflow-y:auto; font-size:12dp; gap:3dp;}
.right .battle-spirits {justify-content:flex-end;}
.battle-spirits span {display:inline-block; padding:1dp 5dp; height:16dp; color:#7e88a8; border:1dp #4a5578; background-color:#ffffff0a;}
.battle-spirits span.active {color:#ffd75e; border-color:#ffd75e; background-color:#ffd75e2e; font-weight:bold;}
.battle-spirits span.defensive {color:#57e389; border-color:#57e389; background-color:#57e3892e; font-weight:bold;}
.battle-spirits .muted {border:0; color:#7e88a8;}
.battle-defenses {border-top:1dp #ff6fa859; margin-top:6dp; padding-top:4dp; height:18dp; font-size:13dp; color:#57e389;} .right .battle-defenses {border-color:#3fd0ff59;}
.battle-defense-note {font-size:11dp; white-space:normal; color:#a4b0d2; margin-top:2dp;}
.battle-defense-conditions {height:14dp; overflow:hidden; white-space:nowrap;}
.battle-effects {height:44dp; overflow-y:auto; font-size:12dp; line-height:1.3;}
.battle-effects b {color:#a4b0d2; font-weight:normal;}
.battle-effect {margin:2dp 0; color:#d6ddf2;} .battle-effect strong {color:#57e389;} .battle-effect strong.off {color:#a4b0d2;}
.battle-actions {text-align:center; display:flex; flex-direction:column; justify-content:center;}
.battle-actions button {margin:3dp; padding:5dp 11dp; font-size:12dp; background-color:#0c122ceb; border:2dp #3fd0ff; border-radius:0; color:#e8eefc;}
.battle-actions button:hover,.battle-actions button:focus {background-color:#3fd0ff40; border-color:#ffd75e;}
.battle-actions #battle-confirm {display:block; width:220dp; margin:0 auto 5dp; padding:8dp 0; border:0; background-color:transparent; decorator:slant(#12a53c #12a53c 2dp 0dp 11dp 11dp 0dp 0dp); color:#fff; font-size:19dp; font-weight:bold; letter-spacing:3dp;}
.battle-actions #battle-confirm:hover,.battle-actions #battle-confirm:focus {background-color:transparent; decorator:slant(#16bf46 #ffd75e 2dp 0dp 11dp 11dp 0dp 0dp);}
.bp-segment {margin:2dp 0 4dp;} .bp-segment div {display:inline-block; border:2dp #3fd0ff; background-color:#0c122ceb;}
.battle-actions .bp-segment button {margin:0; border:0; padding:5dp 17dp; font-size:12dp; font-weight:bold; background-color:transparent;}
.battle-actions .bp-segment button.on {background-color:#3fd0ff; color:#0b1230;}
.battle-actions .bp-segment button:hover,.battle-actions .bp-segment button:focus {background-color:#ffd75e; color:#0b1230;}
.bp-original {position:absolute; bottom:14dp; left:0; width:100%; text-align:center; font-size:15dp; color:#a4b0d2;}
.bp-original div {display:inline-block; padding:5dp 16dp; background-color:#0c122ceb; border:1dp #3fd0ff;}
.bp-original .key {color:#ffd75e;} .bp-original b {color:#e8eefc;}
.bh-frame {position:absolute; box-sizing:border-box; border-color:#3162c5;}
.bh-fill {position:absolute; left:0; top:0; width:100%; height:100%; box-sizing:border-box; border-color:#101020; background-color:#000020c0;}
.bh-rule {position:absolute;} .bh-rule.blue {background-color:#3162c5;} .bh-rule.dark {background-color:#101020;}
.bh-text {position:absolute; white-space:nowrap;}
.bh-num {position:absolute; white-space:nowrap; text-align:right; font-weight:normal; color:#ffffff;} .bh-num.key {text-align:left; color:#ffff00;} .bh-num.slash {text-align:center;}
.bh-bar {position:absolute; background-color:#ff0000;} .bh-bar div {height:100%; background-color:#00ff00;}
.bh-item {position:absolute; display:block; box-sizing:border-box; margin:0; border:0; border-radius:0; background-color:transparent; color:#ffffff; font-weight:bold; text-align:left; white-space:nowrap; overflow:hidden;}
.bh-item:hover {border:0;} body.pointer .bh-item:hover {background-color:#00ff0040;} .bh-item:focus,body.pointer .bh-item:focus {border:0; background-color:#00ff0080;}
.home-entry {position:absolute; left:14dp; top:12dp; margin:0; padding:5dp 14dp; font-size:13dp; pointer-events:auto; background-color:#0c122cd0; border-color:#3fd0ff;}
/* Library and MOD lettered like the title ring's items (sprite_text menu_style; size,
   outline and shadow are set in game pixels by home_sync): the dim grey-blue of an
   unchosen item, no box; light blue when pointed at or chosen, as the ring's current item is. */
.home-corner {position:absolute; right:6vw; bottom:6vh; display:flex; align-items:baseline; pointer-events:none;}
button.home-mod {margin:0 0 0 2vw; padding:0 1vh; line-height:1.2;
    background-color:transparent; border-width:0; border-radius:0; color:#6c768d; pointer-events:auto;}
button.home-mod:hover, button.home-mod:focus, .pad button.home-mod:focus {color:#c4dcff; background-color:transparent; border-width:0;}
/* The title's corners: settings top left, the frame-rate readout top right, the version
   bottom left, Library and MOD bottom right. */
.home-version {position:absolute; left:14dp; bottom:10dp; font-size:13dp; color:#ffffffb0; pointer-events:none; font-effect:outline(1dp #000000c0);}
.home-update {position:absolute; left:14dp; bottom:32dp; margin:0; padding:4dp 12dp; font-size:13dp; font-weight:bold; pointer-events:auto; color:#0b1230; background-color:#ffd75e; border-color:#ffd75e; border-radius:0;}
.home-update:hover, .home-update:focus {color:#0b1230; background-color:#fff0b0; border-color:#fff0b0;}
.dlc-meta {color:#a4b0d2; font-size:13dp; margin-top:2dp;}
.bp-hints {text-align:center; font-size:15dp; color:#a4b0d2; height:21dp; white-space:nowrap; overflow:hidden;}
.narrow .bp-hints {font-size:16dp; height:22dp;}
.bp-hints span {margin:0 10dp;} .bp-hints b {color:#ffd75e; font-weight:normal;}
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
body.pointer .im-panel button:hover {background-color:#00c80055;} .im-panel button:focus {border:0;}
.im-panel button.on,.im-panel button.on:hover {background-color:#00c800;} .im-panel button:disabled {opacity:1;} .im-panel button.tp-playing {color:#8fe8ff;} .im-panel button.on.tp-playing {color:#ffffff;}
.im-line {display:flex; justify-content:space-between;} .im-line span {display:inline-block;}
.im-hint {position:absolute; text-align:center; font-weight:normal; color:#d6e2efb0; white-space:nowrap;}
.im-refused {color:#ffd75e;}
.im-funds {display:inline-block; text-align:right; color:#ffffff; font-weight:bold; background-color:transparent; border:0; border-radius:0; margin:0; padding:0;}
body.pointer .im-funds:hover,.im-funds:focus {background-color:#00c80055; border:0;}
.im-funds-input {display:inline-block; text-align:right; color:#ffffff; font-weight:bold; background-color:#122131; border:0; border-radius:0; margin:0; padding:0 2dp; tab-index:auto;}
.im-funds-input selection {color:#0b1421; background-color:#9be4f7;}
.im-row {display:flex; align-items:center;} .im-row span {display:inline-block; white-space:nowrap; overflow:hidden;}
.im-right {text-align:right;} .im-center {text-align:center;} .im-dim {color:#9eafc3;}
.im-gauge {font-family: srw64-ui; letter-spacing:0;} .im-gauge b {font-weight:normal; color:#ff6fa8;} .im-gauge i {font-style:normal; color:#ffd75e;}
.im-up {color:#7dff8a;} .im-down {color:#ff8d8d;}
.im-badge {display:inline-block; text-align:center; vertical-align:middle; border-radius:6dp; border-width:1dp; border-color:#e8f0ff; color:#ffffff; font-weight:bold; margin-right:1dp; overflow:hidden;}
.im-mark {color:#ffffff; vertical-align:middle;} .im-mark.map {color:#de416a;}
.im-badge.melee {background-color:#c8501e;} .im-badge.ranged {background-color:#2d6fd8;} .im-badge.post {background-color:#2a9a4a;} .im-badge.beam {background-color:#a04fd0;} .im-badge.map {background-color:#c09a1a; border-radius:3dp;} .im-bar {position:absolute; height:2dp; background-color:#00c800;} .im-bar-back {position:absolute; height:2dp; background-color:#123a2a;} .im-panel button.dim {background-color:#00c80055;}
.im-shade {position:absolute; left:0; top:0; width:100%; height:100%; background-color:#04071266;}
.im-panel img {display:block;}


)";
void document_close(Rml::ElementDocument*& doc) {
    if(doc){doc->Close();doc=nullptr;}
}
bool held();
void choose(const std::string& id);
void battle_buttons(uint32_t pressed,bool tab=false);
struct Actions : Rml::EventListener {
    void ProcessEvent(Rml::Event& event) override {
        for(auto* el=event.GetTargetElement();el;el=el->GetParentNode())
            if(el->GetTagName()=="button"){choose(el->GetId());break;}
    }
} actions;
// One-line text marked 'fit' that runs past its box after layout gets a smaller face,
// in proportion, down to 60 % (or its data-fit-min): names and notes whose length the page cannot know.
void fit_lines(Rml::ElementDocument* doc) {
    Rml::ElementList lines;doc->QuerySelectorAll(lines,".fit");
    // A flex row gives a shrinking item back some room, so a second look settles it.
    for(int pass=0;pass<3;++pass) {
        bool changed=false;
        for(auto* line:lines) {
            const float room=line->GetClientWidth(),need=line->GetScrollWidth();
            if(room<=0 || need<=room+.5f)continue;
            // data-fit-from keeps the face it started with, for the layout audit's shrink report and for a
            // later call on the same page (a redrawn part), whose floor stays relative to that face.
            const float size=line->GetComputedValues().font_size(),from=line->GetAttribute<float>("data-fit-from",size),floor=from*line->GetAttribute<float>("data-fit-min",.6f);
            if(size<=floor+.01f)continue;
            // 2 % under the ratio: the row then gives a little back, and three passes left 1-2 dp over (4:3 audit).
            line->SetProperty("font-size",std::to_string(std::max(floor,size*room/need*.98f))+"px");line->SetAttribute("data-fit-from",from);changed=true;
        }
        if(!changed)break;
        doc->UpdateDocument();
    }
}
// chrome: drawn over a RetroArch filter instead of under it (LayerMark above).
// A page (not chrome) keeps to page_area() by a transparent border on its body: the body's
// background stays inside it (RmlUi paints backgrounds in the padding box), and what a page
// places absolutely, in window pixels (the original's 320x240 coordinates), stays where it
// was (an unpositioned body is not their containing block; the window is).
Rml::ElementDocument* document(const std::string& body,bool modal,bool chrome=false) {
    std::string inset;
    if(!chrome){const auto a=page_area();if(a.x>=1 || a.y>=1)inset="box-sizing: border-box; border-width: "+std::to_string(int(a.y))+"px "+std::to_string(int(a.x))+"px; border-color: #00000000; ";}
    auto* doc=context->LoadDocumentFromMemory("<rml><head><style>"+std::string(css)+locale_font_css(localization::catalog().locale)+"</style></head><body style='"+inset+"pointer-events: "+std::string(modal?"auto":"none")+";' class='"+(modal?"modal":"")+(pointer_mode?" pointer":"")+"'>"+(chrome?"<layer-mark chrome='1'/>":"<layer-mark/>")+body+"<layer-mark class='layer-end'/></body></rml>");
    if(!doc)throw std::runtime_error("Cannot create shared UI document");
    if(chrome)doc->SetAttribute("data-chrome","1");
    doc->AddEventListener("click",&actions);doc->Show(Rml::ModalFlag::None,Rml::FocusFlag::None);
    doc->UpdateDocument();fit_lines(doc);return doc;
}
std::string button(const std::string& id,const std::string& text,bool on=false,bool disabled=false,const std::string& cls="") {
    return "<button id='"+escape(id)+"' class='"+cls+(on?" on":"")+"'"+(disabled?" disabled":"")+">"+text+"</button>";
}
std::string image(const std::string& path) {
    if(path.empty())return {};
    if(auto found=images.find(path);found!=images.end())return found->second;
    if(!presentation::alpha_beside(path).empty()) {
        // A released HD portrait: JPEG colour with its alpha beside it.
        auto rgba=presentation::load_rgba(path);
        const auto name="portrait-"+std::to_string(images.size());
        renderer->queue_image_from_bytes_rgba32(name,std::vector<char>(rgba.pixels.begin(),rgba.pixels.end()),uint32_t(rgba.width),uint32_t(rgba.height));
        return images[path]=name;
    }
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot load shared UI portrait: "+path);
    // RmlUi normalizes leading slashes in URLs. Stable relative resource keys
    // avoid platform-specific filesystem paths leaking into its URL resolver.
    const auto name="portrait-"+std::to_string(images.size());
    renderer->queue_image_from_bytes_file(name,{std::istreambuf_iterator<char>(file),{}});images[path]=name;return name;
}
// Page portraits: the whole HD image while the applied image mode is HD and the asset
// carries one (docs/native/native-portraits-hd.md), otherwise the ROM original.
bool hd_portraits() {return presentation::image_mode.current()==1;}
std::string portrait_path(const json& art) {
    return hd_portraits() && art.contains("hd") ? art.at("hd").get<std::string>() : art.at("path").get<std::string>();
}
// The HD portraits are 768 px and the pages show them at 56-160 dp. RmlUi samples
// without mipmaps, so a straight minification aliases; resample once per displayed
// width with an area filter (premultiplied, so edges keep their colour). Images at or
// below the width, such as the 96 px originals, load unchanged.
std::string image(const std::string& path,int width) {
    if(path.empty() || width<=0)return image(path);
    const auto key=path+"@"+std::to_string(width);
    if(auto found=images.find(key);found!=images.end())return found->second;
    const auto file=presentation::load_rgba(path);
    const int w=file.width,h=file.height;
    const uint8_t* data=file.pixels.data();
    if(width>=w)return images[key]=image(path);
    const int ow=width,oh=std::max(1,int(float(h)*width/w+.5f));
    std::vector<float> row(size_t(ow)*h*4),out(size_t(ow)*oh*4);
    const float sx=float(w)/ow,sy=float(h)/oh;
    auto premultiplied=[&](int x,int y,int c){const uint8_t* p=data+(size_t(y)*w+x)*4;return c==3?p[3]/255.f:p[c]/255.f*p[3]/255.f;};
    for(int y=0;y<h;++y)for(int ox=0;ox<ow;++ox){
        const float x0=ox*sx,x1=x0+sx;
        for(int c=0;c<4;++c){
            float sum=0;
            for(int x=int(x0);x<w && x<x1;++x)sum+=premultiplied(x,y,c)*(std::min(x1,x+1.f)-std::max(x0,float(x)));
            row[(size_t(y)*ow+ox)*4+c]=sum/sx;
        }
    }
    for(int oy=0;oy<oh;++oy)for(int ox=0;ox<ow;++ox){
        const float y0=oy*sy,y1=y0+sy;
        for(int c=0;c<4;++c){
            float sum=0;
            for(int y=int(y0);y<h && y<y1;++y)sum+=row[(size_t(y)*ow+ox)*4+c]*(std::min(y1,y+1.f)-std::max(y0,float(y)));
            out[(size_t(oy)*ow+ox)*4+c]=sum/sy;
        }
    }
    std::vector<char> bytes(size_t(ow)*oh*4);
    for(size_t i=0;i<size_t(ow)*oh;++i){
        const float a=out[i*4+3];
        for(int c=0;c<3;++c)bytes[i*4+c]=char(std::clamp(int((a>0?out[i*4+c]/a:0)*255+.5f),0,255));
        bytes[i*4+3]=char(std::clamp(int(a*255+.5f),0,255));
    }
    const auto name="portrait-"+std::to_string(images.size());
    renderer->queue_image_from_bytes_rgba32(name,bytes,uint32_t(ow),uint32_t(oh));
    return images[key]=name;
}
int dp_pixels(float dp){return int(dp*ui_density+.5f);}

// The settings window (docs/native/settings-window.md): an overlay panel over the game
// with one page per category. Pages in tab order; the id is what presentation.json
// keeps as settings_page, so the window reopens where it was left.
constexpr const char* settings_pages[]={"general","interface","rules","cheats","saves","controls","feedback","about"};
unsigned settings_page{};
int settings_built=-1;  // the page the open window shows; -1 once it closes
// The control to focus once the window is rebuilt: an id, "first" for the page's first
// setting, or empty to keep the focus it had.
std::string settings_focus;
#ifndef SRW64_VERSION
#define SRW64_VERSION "?"
#endif
// The Controls page (docs/native/controls-remapping.md): one row per function, with its key
// and its controller input; select one to rebind it on either.
struct Capture {
    std::vector<input::Action> queue;  // what the next press goes to
    std::string name;                  // the function being set, for the prompt
    uint64_t since=0;                  // SDL ticks when it began: it gives up after 6 s
    bool refused=false;                // a key kept for the shortcuts was pressed
    bool release=false;                // after a controller press: wait until the controller is let go
    std::array<bool,input::pad_axis::Count> axis_rest{};  // an axis must come back to rest between presses
} capture;
using input::Action;
// The functions, in list order; each row's name is controls_row_<id>. The stick moves as
// the D-pad does and is not listed (user, 2026-09-28).
struct ControlRow {const char* id;Action action;};
constexpr ControlRow control_rows[]={
    {"a",Action::A},{"b",Action::B},{"start",Action::Start},{"l",Action::L},{"r",Action::R},
    {"aux_left",Action::AuxLeft},{"aux_right",Action::AuxRight},{"c_left",Action::CLeft},{"c_up",Action::CUp},{"c_down",Action::CDown},
    {"animation",Action::Animation},{"settings",Action::Settings},{"language",Action::Language},{"images",Action::Images},
    {"d_up",Action::DUp},{"d_down",Action::DDown},{"d_left",Action::DLeft},{"d_right",Action::DRight},{"z",Action::Z}};
void start_capture(const std::string& id) {
    for(const auto& row:control_rows)if(id==row.id) {
        capture=Capture{};
        capture.queue={row.action};
        capture.name=label(std::string("controls_row_")+row.id);capture.since=SDL_GetTicks64();capture.axis_rest.fill(true);
        return;
    }
}
// While a capture waits, every key and controller input goes to it: Esc gives up, the
// shortcut keys are refused, anything else is bound to the function on the device it
// came from.
bool capture_event(const SDL_Event& event) {
    const auto bind=[&](auto assign) {
        auto bindings=input::live_bindings().get();
        assign(bindings,capture.queue.front());
        input::live_bindings().set(bindings);
        capture.queue.erase(capture.queue.begin());capture.refused=false;capture.since=SDL_GetTicks64();
    };
    switch(event.type) {
    case SDL_KEYDOWN: {
        if(event.key.repeat)return true;
        const auto key=event.key.keysym.scancode;
        if(key==SDL_SCANCODE_UNKNOWN)return true;
        if(key==SDL_SCANCODE_ESCAPE){capture.queue.clear();return true;}
        if(key==SDL_SCANCODE_F5 || key==SDL_SCANCODE_F6 || key==SDL_SCANCODE_F7 || key==SDL_SCANCODE_F8){capture.refused=true;return true;}
        bind([&](input::Bindings& b,input::Action a){input::assign_key(b,a,int(key));});
        return true;
    }
    case SDL_CONTROLLERBUTTONDOWN:
        bind([&](input::Bindings& b,input::Action a){input::assign_pad(b,a,input::button(uint8_t(event.cbutton.button)));});
        capture.release=true;
        return true;
    case SDL_CONTROLLERAXISMOTION: {
        const auto axis=event.caxis.axis;const int value=event.caxis.value;
        if(axis>=input::pad_axis::Count)return true;
        if(std::abs(value)<8000)capture.axis_rest[axis]=true;
        else if(std::abs(value)>24000 && capture.axis_rest[axis]) {
            capture.axis_rest[axis]=false;
            bind([&](input::Bindings& b,input::Action a){input::assign_pad(b,a,input::axis(uint8_t(axis),value<0?-1:1));});
            capture.release=true;
        }
        return true;
    }
    case SDL_KEYUP: case SDL_CONTROLLERBUTTONUP: case SDL_TEXTINPUT: case SDL_TEXTEDITING: return true;
    default: return false;
    }
}
std::string capture_prompt() {
    if(capture.queue.empty())return {};
    auto prompt=label("controls_capture");
    if(const auto at=prompt.find("{name}");at!=std::string::npos)prompt.replace(at,6,capture.name);
    return "<div class='ctl-capture'><div class='ctl-capture-box'><div class='ctl-capture-title'>"+prompt+"</div>"
        "<p>"+label(capture.refused?"controls_reserved":"controls_capture_note")+"</p>"+button("controls-cancel",label("controls_cancel"))+"</div></div>";
}
// The Controls page body: the controller found, the functions to rebind, the fixed
// shortcuts and restore.
std::string controls_page() {
    std::string body;
    const auto pad_name=srw64_pad_name();
    auto found=pad_name.empty()?label("controls_no_pad"):label("controls_detected");
    if(const auto at=found.find("{name}");at!=std::string::npos)found.replace(at,6,escape(pad_name));
    body+="<p class='ctl-found'>"+found+"</p><p>"+label("controls_keyboard_note")+"</p>";
    body+="<p>"+label("controls_list_note")+"</p><div class='ctl-head'><span class='n'></span><span class='k'>"+label("controls_keyboard")+"</span><span class='k'>"+label("controls_controller")+"</span></div>";
    const auto keys=prompt_context(false),pads=prompt_context(true);
    for(const auto& row:control_rows) {
        // The row's own action, as a token the prompts know.
        const std::string token=std::string("{")+[&]{for(const auto& t:text::action_tokens)if(t.count==1 && t.members[0]==row.action)return std::string(t.token);return std::string();}()+"}";
        body+="<button id='controls-bind:"+std::string(row.id)+"' class='nav ctl-row'><span class='n'>"+label(std::string("controls_row_")+row.id)+"</span>"
            "<span class='k'>"+escape(text::expand_prompts(token,keys))+"</span><span class='k'>"+escape(text::expand_prompts(token,pads))+"</span></button>";
    }
    body+="<div class='set-row nav'><div class='set-line'><div class='set-name'>"+label("controls_fixed")+"</div><div class='set-seg'>"+
        button("controls-reset",label("controls_reset"))+"</div></div><p>"+escape(text::expand_prompts(localization::catalog().ui("controls_fixed_list"),prompt_context(false)))+"</p></div>";
    return body;
}
// The セーブ page (save_store.hpp): autosave choices, the card written for every emulator,
// and the slots of the emulator files in saves/import to take in as extended slots. The
// folder is read when the page is first built and on 再読み込み; a slot whose checksum
// differs needs a second press, which imports it with the sum redone.
nlohmann::json save_candidates;
bool save_candidates_read{};
std::string save_message,save_force;
std::string settings_row(const std::string& key,const std::string& choices,const std::string& more={},const std::string& note={});
std::string settings_choice(const std::string& key,const std::string& prefix,std::initializer_list<const char*> modes,const std::string& current,bool disabled=false);
std::string with_number(const std::string& key,unsigned n) {
    auto text=localization::catalog().ui(key);
    if(const auto at=text.find("{n}");at!=std::string::npos)text.replace(at,3,std::to_string(n));
    return escape(text);
}
std::string saves_page() {
    if(!save_store::enabled())return "<p>"+label("settings_saves_off")+"</p>";
    const auto choices=save_store::settings();
    std::string body=settings_choice("settings_autosave","autosave",{"on","off"},choices.autosave?"on":"off",false);
    std::string intermission,turn;
    for(const unsigned n:{1u,3u,5u,10u}) {
        intermission+=button("autosave-intermission:"+std::to_string(n),std::to_string(n),n==choices.intermission,!choices.autosave);
        turn+=button("autosave-turn:"+std::to_string(n),std::to_string(n),n==choices.turn,!choices.autosave);
    }
    body+=settings_row("settings_autosave_intermission",intermission)+settings_row("settings_autosave_turn",turn);
    const auto folder=save_store::library_directory();
    body+=settings_row("settings_save_export",button("save-export",label("settings_save_export_button")),"<p class='set-path'>"+escape((folder/"export").string())+"</p>");
    if(!save_candidates_read){save_candidates=save_store::import_candidates();save_candidates_read=true;}
    body+=settings_row("settings_save_import",button("save-import-refresh",label("settings_save_import_refresh")),"<p class='set-path'>"+escape((folder/"import").string())+"</p>");
    for(const auto& file:save_candidates) {
        const auto name=file.value("file",std::string());
        std::string slots;
        if(file.value("unknown",false))slots="<span class='set-note'>"+label("settings_save_import_unknown")+"</span>";
        else for(unsigned i=0;i<file.at("slots").size();++i) {
            const auto& slot=file.at("slots")[i];
            if(!slot.value("used",false))continue;
            const auto id="save-import:"+std::to_string(i)+":"+name;
            const auto key=slot.value("intact",false)?"settings_save_import_slot":save_force==id?"settings_save_import_confirm":"settings_save_import_damaged";
            slots+=button(id,with_number(key,i+1));
        }
        body+="<div class='set-row nav'><div class='set-line'><div class='set-name'>"+escape(name)+" <span class='set-note'>"+escape(file.value("format",std::string()))+"</span></div><div class='set-seg'>"+slots+"</div></div></div>";
    }
    if(!save_message.empty())body+="<p class='set-message'>"+escape(save_message)+"</p>";
    return body;
}
// The Cheats page (docs/gameplay/cheats.md): five switches, then each pilot's level
// behind a row that opens the list; levels change only while the インターミッション
// menu is up.
bool cheat_levels_open=false;
std::string cheats_page() {
    std::string body="<p>"+label("cheats_note")+"</p>";
    for(const auto& entry:cheats::catalog)
        body+=button("cheat:"+std::string(entry.id),"<span class='set-name'>"+label("cheat_"+std::string(entry.id))+"</span><span class='switch'><span></span></span>",settings::cheats()&entry.bit,false,"set-toggle nav");
    body+=settings_row("cheat_levels",button("cheat-levels",label(cheat_levels_open?"cheat_levels_close":"cheat_levels_open"),cheat_levels_open));
    if(!cheat_levels_open)return body;
    const auto pilots=cheats::pilots();
    if(pilots.empty())return body+"<p>"+label("cheat_levels_away")+"</p>";
    for(const auto& pilot:pilots) {
        std::string steps;
        unsigned slot=0;   // ids stay unique where two steps reach the same level
        const auto step=[&](int to,const std::string& text){
            const unsigned level=unsigned(std::clamp(to,1,99));
            steps+=button("cheat-level:"+std::to_string(pilot.index)+":"+std::to_string(level)+":"+std::to_string(slot++),text,false,level==pilot.level);
        };
        step(1,"1");step(int(pilot.level)-10,"-10");step(int(pilot.level)-1,"-1");step(int(pilot.level)+1,"+1");step(int(pilot.level)+10,"+10");step(99,"99");
        body+="<div class='set-row nav'><div class='set-line'><div class='set-name'>"+escape(pilot.name)+" <span class='set-note'>Lv "+std::to_string(pilot.level)+"</span></div><div class='set-seg'>"+steps+"</div></div></div>";
    }
    return body;
}
bool touch_active();
// The Steam Deck and Android: full screen, no window, no file manager to open a folder in.
bool handheld() {
#ifdef __ANDROID__
    return true;
#else
    return on_steam_deck();
#endif
}
#ifdef __ANDROID__
// SRW64Activity.openUserFolder: the Files app at a folder of the data folder.
bool open_folder(const std::string& path) {
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    if(!env || !activity)return false;
    const jclass type=env->GetObjectClass(activity);
    const jmethodID method=env->GetMethodID(type,"openUserFolder","(Ljava/lang/String;)Z");
    bool opened=false;
    if(method) {
        const jstring text=env->NewStringUTF(path.c_str());
        opened=env->CallBooleanMethod(activity,method,text);
        env->DeleteLocalRef(text);
    }
    if(env->ExceptionCheck()){env->ExceptionClear();opened=false;}
    env->DeleteLocalRef(type);env->DeleteLocalRef(activity);
    return opened;
}
bool can_open_folder(){return true;}
#else
bool open_folder(const std::string& path){return SDL_OpenURL(("file://"+path).c_str())==0;}
bool can_open_folder(){return !handheld() && !touch_active();}
#endif
// Bezel and filter (docs/native/bezels-and-filters.md): a picker inside the General page
// that walks the player's folder and RetroArch's, one level at a time.
std::string browse_kind;            // "bezel", "filter" or empty while closed
std::filesystem::path browse_dir;   // empty: the list of starting folders
std::string browser() {
    const bool bezels=browse_kind=="bezel";
    const auto roots=bezels?settings::bezel_roots():settings::filter_roots();
    const auto current=bezels?settings::bezel():settings::filter();
    const auto row=[](const std::string& id,const std::string& text,bool on=false){return button(id,"<span class='set-name'>"+text+"</span>",on,false,"set-toggle nav");};
    std::string out;
    if(browse_dir.empty()) {
        for(const auto& root:roots) {
            const char* key=root.kind==settings::LookFolder::builtin?"settings_browse_builtin":root.kind==settings::LookFolder::mine?
                (bezels?"settings_bezel_mine":"settings_filter_mine"):"settings_browse_retroarch";
            out+=row("browse-dir:"+root.path.string(),label(key)+" <span class='set-note'>"+escape(root.path.string())+"</span>");
            // The player's folder opens in the file manager, where there is one to open: on
            // Android the Files app, through the app's UserFilesProvider.
            if(root.kind==settings::LookFolder::mine && can_open_folder())
                out+=row("open-folder:"+root.path.string(),label("settings_browse_open"));
        }
        return out;
    }
    out+=row("browse-up",label("settings_browse_up")+" <span class='set-note'>"+escape(browse_dir.string())+"</span>");
    std::vector<std::filesystem::path> folders,files;
    std::error_code error;
    for(std::filesystem::directory_iterator entry(browse_dir,error),end;!error && entry!=end;entry.increment(error)) {
        const auto& path=entry->path();
        auto extension=path.extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(entry->is_directory(error))folders.push_back(path);
        else if(bezels?(extension==".png" || extension==".cfg"):extension==".slangp")files.push_back(path);
    }
    std::sort(folders.begin(),folders.end());std::sort(files.begin(),files.end());
    for(const auto& folder:folders)out+=row("browse-dir:"+folder.string(),escape(folder.filename().string())+"/");
    for(const auto& file:files)out+=row("browse-pick:"+file.string(),escape(file.stem().string()),file.string()==current);
    if(folders.empty() && files.empty())out+="<p>"+label("settings_browse_empty")+"</p>";
    return out;
}
// The General page's bezel and filter rows.
std::string look_rows() {
    std::string body;
    const bool square=!settings::wide_picture();
    const auto bezel=settings::bezel();
    body+=settings_row("settings_bezel",button("bezel-off",label("settings_look_off"),bezel.empty(),!square)+
        button("browse:bezel",bezel.empty()?label("settings_look_choose"):escape(std::filesystem::path(bezel).stem().string()),!bezel.empty(),!square),
        square && browse_kind=="bezel"?browser():std::string());
    const auto status=post_filter::status();
    const auto filter=settings::filter();
    std::string more;
    if(!status.available)more+="<p class='set-note'>"+label("settings_filter_unavailable")+"</p>";
    else if(status.loading)more+="<p class='set-note'>"+label("settings_filter_loading")+"</p>";
    else if(!status.error.empty())more+="<p class='set-note'>"+label("settings_filter_error")+" "+escape(status.error.substr(0,status.error.find('\n')).substr(0,200))+"</p>";
    if(status.available && browse_kind=="filter")more+=browser();
    body+=settings_row("settings_filter",button("filter-off",label("settings_look_off"),filter.empty(),!status.available)+
        button("browse:filter",filter.empty()?label("settings_look_choose"):escape(std::filesystem::path(filter).stem().string()),!filter.empty(),!status.available),more);
    std::string scales;
    for(const unsigned n:{1u,2u,4u,0u})
        scales+=button("filter-scale:"+std::to_string(n),label("settings_filter_scale_"+std::to_string(n)),settings::filter_scale()==n,!status.available || filter.empty());
    body+=settings_row("settings_filter_scale",scales);
    return body;
}
std::string look_stamp() {
    const auto status=post_filter::status();
    return settings::bezel()+"|"+settings::filter()+"|"+std::to_string(settings::filter_scale())+"|"+browse_kind+"|"+browse_dir.string()+"|"+
        std::to_string(status.available)+std::to_string(status.loading)+status.preset+status.error;
}
// The bezel under every other document while the picture is 4:3 and one is chosen.
Rml::ElementDocument* bezel_doc=nullptr;
std::string bezel_shown;
void bezel_sync() {
    const auto chosen=settings::bezel();
    const bool on=!chosen.empty() && !settings::wide_picture();
    const auto next=on?chosen+"|"+frame_stamp():std::string();
    if(next!=bezel_shown) {
        bezel_shown=next;
        if(bezel_doc){bezel_doc->Close();bezel_doc=nullptr;}
        if(on)try {
            struct Seen {std::string file;int w,h;bezel::Hole hole;};
            static std::map<std::string,Seen> seen;
            auto found=seen.find(chosen);
            if(found==seen.end()) {
                const auto file=bezel::image_of(chosen);
                if(file.empty())throw std::runtime_error("no overlay image in "+chosen);
                const auto rgba=presentation::load_rgba(file);
                found=seen.emplace(chosen,Seen{file.string(),rgba.width,rgba.height,bezel::find_hole(rgba.pixels.data(),rgba.width,rgba.height)}).first;
            }
            const auto& b=found->second;
            const float u=frame::scale(float(pixels_w),float(pixels_h)),pw=frame::width(float(pixels_w),float(pixels_h))*u,ph=frame::kHeight*u;
            const auto r=bezel::place(b.w,b.h,b.hole,{(pixels_w-pw)/2,(pixels_h-ph)/2,pw,ph},float(pixels_w),float(pixels_h));
            const auto px=[](float v){return std::to_string(int(std::lround(v)))+"px";};
            bezel_doc=document("<img src='"+escape(image(b.file))+"' style='position:absolute; left:"+px(r.x)+"; top:"+px(r.y)+"; width:"+px(r.w)+"; height:"+px(r.h)+";'/>",false,true);
        } catch(const std::exception& error) {
            fprintf(stderr,"SRW64_BEZEL error=%s\n",error.what());
        }
    }
    if(bezel_doc)bezel_doc->PushToBack();
}
std::string cheats_stamp() {
    std::string stamp=std::to_string(settings::cheats())+(cheat_levels_open?"+":"-");
    if(!cheat_levels_open)return stamp;
    for(const auto& pilot:cheats::pilots())stamp+=","+std::to_string(pilot.index)+":"+std::to_string(pilot.level)+pilot.name;
    return stamp;
}
// One setting: its name with its choices beside it, and its note on a line of its own.
// RmlUi breaks lines only at spaces, so a Chinese or Japanese note needs the whole
// width (test_settings_window.py checks that each fits the smallest window).
// note: another key for the line under the name than key_note.
std::string settings_row(const std::string& key,const std::string& choices,const std::string& more,const std::string& note) {
    return "<div class='set-row nav'><div class='set-line'><div class='set-name'>"+label(key)+"</div><div class='set-seg'>"+choices+"</div></div><p>"+label(note.empty()?key+"_note":note)+"</p>"+more+"</div>";
}
// A setting with one button per mode: ids prefix:mode, labels key_mode.
std::string settings_choice(const std::string& key,const std::string& prefix,std::initializer_list<const char*> modes,const std::string& current,bool disabled) {
    std::string choices;
    for(const std::string mode:modes)choices+=button(prefix+":"+mode,label(key+"_"+mode),mode==current,disabled);
    return settings_row(key,choices);
}
// The button a row offers first: its selected one, else its first enabled one.
Rml::Element* settings_choice_of(Rml::Element* row) {
    if(row->GetTagName()=="button")return row->HasAttribute("disabled")?nullptr:row;
    Rml::ElementList buttons;row->QuerySelectorAll(buttons,"button");
    for(auto* b:buttons)if(b->IsClassSet("on") && !b->HasAttribute("disabled"))return b;
    for(auto* b:buttons)if(!b->HasAttribute("disabled"))return b;
    return nullptr;
}
// The rows keys and the controller move between: the tab bar, then every setting.
Rml::ElementList settings_rows() {
    Rml::ElementList rows;settings_doc->QuerySelectorAll(rows,".nav");
    std::erase_if(rows,[](Rml::Element* row){return !settings_choice_of(row);});
    return rows;
}
void settings_focus_row(Rml::Element* row,int direction) {
    auto* choice=settings_choice_of(row);if(!choice)return;
    choice->Focus();
    // Moving up onto a group's first row brings its heading into view too.
    if(auto* heading=row->GetPreviousSibling();direction<0 && heading && heading->GetTagName()=="h2")
        heading->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
    row->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
}
// The page's first setting, or the tab bar on a page with nothing to choose.
void settings_focus_first() {
    const auto rows=settings_rows();
    // Row 0 is the tab bar, except in the DLC view, which has none.
    if(!rows.empty())settings_focus_row(rows[rows.size()>1?1:0],0);
}
// Installed campaigns, read once: they do not change while the game runs.
const std::vector<campaign_library::Entry>& installed_campaigns() {
    static const auto entries=campaign_library::list();
    return entries;
}
// A campaign is entered or left on the title's ring menu only, where nothing unsaved is
// lost and a stage can be loaded (mini_stage::load_file).
bool on_title(){return intro::title_major()==3;}
// The title waiting for START or on its ring: where a viewer battle starts and comes back to.
bool viewer_title(){const auto major=intro::title_major();return major==2 || major==3;}
// The MOD manager (docs/design/custom-campaign.md §8): additional scenarios, art, dialogue
// and languages, music and voices, one page each, in the settings window's frame.
std::string mod_campaigns_page() {
    const auto locale=localization::catalog().locale;
    const bool switchable=campaign_switch::available() && on_title();
    std::string body="<p>"+label("dlc_note")+"</p>";
    const auto current=campaign::info();
    if(current)
        body+="<div class='set-row nav'><div class='set-line'><div class='set-name'>"+label("mod_playing")+" "+escape(campaign_library::text(current->name,locale))+
            "</div><div class='set-seg'>"+button("dlc-leave",label("dlc_leave"),false,!switchable)+"</div></div></div>";
    const auto& entries=installed_campaigns();
    if(entries.empty())body+="<p>"+label("dlc_empty")+"</p>";
    for(size_t i=0;i<entries.size();++i) {
        const auto& entry=entries[i];
        const bool playing=current && current->id==entry.id;
        std::string meta=with_number("dlc_stages",unsigned(entry.stages));
        if(!entry.version.empty())meta+="  ·  v"+escape(entry.version);
        if(!entry.author.empty())meta+="  ·  "+escape(entry.author);
        meta+="  ·  "+label(entry.saves?"dlc_saves":"dlc_new");
        body+="<div class='set-row nav'><div class='set-line'><div class='set-name'>"+escape(campaign_library::text(entry.name,locale))+"</div><div class='set-seg'>"+
            button("dlc-enter:"+std::to_string(i),label(playing?"mod_current":"dlc_enter"),false,playing || !switchable)+"</div></div>";
        if(const auto description=campaign_library::text(entry.description,locale);!description.empty())body+="<p>"+escape(description)+"</p>";
        body+="<p class='dlc-meta'>"+meta+"</p></div>";
    }
    if(!switchable && campaign_switch::available())body+="<p class='dlc-meta'>"+label("mod_title_only")+"</p>";
    return body;
}
std::string mod_art_page() {
    std::string body=settings_choice("settings_images","images",{"original","hd"},presentation::image_mode.requested()?"hd":"original",!presentation::image_mode.enabled());
    const char* art=std::getenv("SRW64_ART_PACK");
    if(art && *art)body+="<p>"+label("mod_art_installed")+"</p><p class='set-path'>"+escape(std::filesystem::path(art).parent_path().string())+"</p>";
    else body+="<p>"+label("mod_art_missing")+"</p>";
    return body+"<p class='dlc-meta'>"+label("mod_art_note")+"</p>";
}
std::string mod_dialogue_page() {
    const auto folder=dialogue::text_overrides_dir();
    std::string body=settings_row("mod_dialogue_reload",button("mod-dialogue-reload",label("dialogue_reload")),
        folder.empty()?std::string():"<p class='set-path'>"+escape(folder.string())+"</p>");
    const auto summary=dialogue::text_summary();
    if(summary.contains("locales"))for(const auto& [locale,row]:summary.at("locales").items()) {
        auto line=localization::catalog().ui("mod_dialogue_locale");
        for(const auto& [token,value]:{std::pair{"{l}",localization::display_name(locale)},std::pair{"{n}",std::to_string(row.value("entries",0u))},
                                       std::pair{"{f}",std::to_string(row.value("files",0u))},std::pair{"{k}",std::to_string(row.value("problems",0u))}})
            if(const auto at=line.find(token);at!=std::string::npos)line.replace(at,std::string_view(token).size(),value);
        body+="<p class='dlc-meta'>"+escape(line)+"</p>";
    }
    return body+"<p class='dlc-meta'>"+label("mod_dialogue_note")+"</p>";
}
// The update check's words (update_check.hpp) with {version}, {date} and {error} filled in.
std::string update_words(const std::string& key,const update::Status& s) {
    auto text=localization::catalog().ui(key);
    for(const auto& [mark,value]:{std::pair<std::string,std::string>{"{version}",s.latest.version},{"{date}",s.latest.date},{"{error}",s.error}})
        if(const auto at=text.find(mark);at!=std::string::npos)text.replace(at,mark.size(),value);
    for(const char* empty:{"（）"," ()"})if(const auto at=text.find(empty);at!=std::string::npos)text.erase(at,std::string(empty).size());
    return escape(text);
}
// The HD pack's words, with {version} (the website's) and {installed}.
std::string update_hd_words(const std::string& key,const update::Status& s) {
    auto text=localization::catalog().ui(key);
    for(const auto& [mark,value]:{std::pair<std::string,std::string>{"{version}",s.hd.latest},{"{installed}",s.hd.version}})
        if(const auto at=text.find(mark);at!=std::string::npos)text.replace(at,mark.size(),value);
    return escape(text);
}
// A path to show in a row: the home folder as ~, and a long one by its end (the button
// beside it copies the whole), so a folder with no spaces cannot run off the page.
std::string short_path(std::string path) {
#ifdef _WIN32
    const char* home=std::getenv("USERPROFILE");
#else
    const char* home=std::getenv("HOME");
#endif
    if(home && *home && path.starts_with(home))path="~"+path.substr(std::strlen(home));
    constexpr size_t keep=56;
    if(path.size()>keep+4) {
        size_t from=path.size()-keep;
        while(from<path.size() && (static_cast<unsigned char>(path[from])&0xC0)==0x80)++from;  // not inside a UTF-8 character
        path="…"+path.substr(from);
    }
    return path;
}
// The debug interface for AI agents (settings debug_interface): its switch, and while on
// where it listens and the run directory a client attaches to, with a button to copy it.
// --debug turns it on for the run; the switch then shows on and cannot turn it off.
std::string debug_row() {
    const bool forced=settings::debug_interface_forced(),on=forced || settings::debug_interface();
    std::string choices;
    for(const std::string mode:{"on","off"})choices+=button("debug:"+mode,label("settings_debug_"+mode),(mode=="on")==on,forced);
    std::string more;
    if(forced)more+="<p>"+label("settings_debug_forced")+"</p>";
    if(const auto endpoint=settings::debug_endpoint();on && !endpoint.address.empty()) {
        for(const auto& [key,mark,value]:{std::tuple<const char*,std::string,std::string>{"settings_debug_status","{address}",endpoint.address},
                                          {"settings_debug_run","{run}",short_path(endpoint.run)}}) {
            auto text=localization::catalog().ui(key);
            if(const auto at=text.find(mark);at!=std::string::npos)text.replace(at,mark.size(),value);
            more+="<p class='set-path'>"+escape(text)+"</p>";
        }
        more+="<div class='set-seg'>"+button("debug-copy-run",label("settings_debug_copy"))+"</div>";
    }
#ifdef __ANDROID__
    // The phone listens on an abstract socket that adb forwards, with no token (debug_transport.cpp).
    return settings_row("settings_debug",choices,more,"settings_debug_note_android");
#else
    return settings_row("settings_debug",choices,more);
#endif
}
// A bug report for an issue (bug_report.hpp, docs/native/bug-report.md). The game's side
// of report.json: what it is set to now. bug_report adds the system, the settings files
// and the logs.
std::filesystem::path report_zip;
std::string report_message;
nlohmann::json report_facts() {
    nlohmann::json rules=nlohmann::json::array(),cheats_on=nlohmann::json::array();
    for(const auto& entry:rules::catalog)if(rules::active_fixes()&entry.fix)rules.push_back(entry.id);
    for(const auto& entry:cheats::catalog)if(settings::cheats()&entry.bit)cheats_on.push_back(entry.id);
    const auto hd=update::status(update::site_language(localization::catalog().locale)).hd;
    const auto name=[](const std::string& path){return path.empty()?std::string():std::filesystem::path(path).filename().string();};
    return {{"version",SRW64_VERSION},{"locale",localization::catalog().locale},{"ui_size",settings::ui_size_name(settings::ui_size())},
        {"images",presentation::image_mode.current()==1?"hd":"original"},{"hd_pack",{{"installed",hd.installed},{"version",hd.version}}},
        {"wide",settings::wide_picture()},{"filter",name(settings::filter())},{"bezel",name(settings::bezel())},
        {"rules",rules},{"cheats",cheats_on},{"controller",srw64_pad_name()},{"touch",touch_active()},{"handheld",handheld()},
        {"debug_interface",settings::debug_interface_forced() || settings::debug_interface()},
        {"window",srw64_window_status()},{"graphics",srw64_graphics_info()}};
}
// The Feedback page (docs/native/bug-report.md): the platform summary to paste into an
// issue, the report zip to attach, and where each kind of feedback goes.
std::string info_message;
std::string feedback_page() {
    std::string info;
    for(size_t at=0;at<info_message.size();) {
        const auto end=std::min(info_message.find('\n',at),info_message.size());
        info+="<p class='set-path'>"+escape(info_message.substr(at,end-at))+"</p>";
        at=end+1;
    }
    std::string report;
    if(!report_message.empty())report+="<p class='set-path'>"+escape(report_message)+"</p>";
    if(!report_zip.empty())report+="<div class='set-seg'>"+(can_open_folder()?button("report-open",label("settings_report_open")):
        button("report-copy",label("settings_report_copy")))+"</div>";
    return settings_row("settings_info",button("info-copy",label("settings_info_copy")),info)+
        settings_row("settings_report",button("report-export",label("settings_report_export")),report)+
        settings_row("settings_feedback_links",button("report-issue",label("settings_report_issue"))+button("feedback-text",label("settings_feedback_text")));
}
// The About page's rows: the project's pages, then the update check and its switch, then
// the debug interface.
std::string about_rows() {
    std::string rows=settings_row("settings_about_links",button("about-link:site",label("about_link_site"))+
        button("about-link:source",label("about_link_source"))+button("about-link:issues",label("about_link_issues")));
    if(!update::supported())return rows+debug_row();
    const auto s=update::status(update::site_language(localization::catalog().locale));
    using update::State;
    const char* key=s.state==State::Checking?"update_status_checking":s.state==State::Current?"update_status_current":
        s.state==State::Available?"update_status_available":s.state==State::Failed?"update_status_failed":"update_status_unchecked";
    std::string actions=button("update-check",label("update_check"),false,s.state==State::Checking);
    if(s.state==State::Available) {
        actions+=button("update-download",label("update_download"),true);
        if(!s.latest.notes.empty())actions+=button("update-notes",label("update_notes"));
    }
    rows+="<div class='set-row nav'><div class='set-line'><div class='set-name'>"+label("settings_update")+"</div><div class='set-seg'>"+actions+
        "</div></div><p>"+update_words(key,s)+"</p></div>";
    // The HD pack, with versions of its own: the one installed, and the website's when newer.
    std::string hd=update_hd_words(!s.hd.installed?"update_hd_missing":s.hd.version.empty()?"update_hd_unknown":"update_hd_installed",s);
    if(s.hd.available)hd+=" "+update_hd_words("update_hd_available",s);
    rows+="<div class='set-row nav'><div class='set-line'><div class='set-name'>"+label("settings_update_hd")+"</div><div class='set-seg'>"+
        button("update-hd-download",label("update_hd_download"),s.hd.available)+"</div></div><p>"+hd+"</p></div>";
    const auto automatic=update::automatic();
    rows+=settings_choice("settings_update_auto","update-auto",{"on","off"},automatic?(*automatic?"on":"off"):"");
    return rows+debug_row();
}
// Asked once on the title: whether to check on start-up. Closing it is a no.
std::string update_ask_panel() {
    return "<div class='set-shade'><div class='set-panel set-ask'><h1>"+label("update_check")+"</h1><div id='set-body' class='set-body'>"
        "<div class='set-row nav'><div class='set-name'>"+label("update_ask_text")+"</div><p>"+label("update_ask_note")+"</p><div class='set-seg'>"+
        button("update-ask:on",label("update_ask_on"),true)+button("update-ask:off",label("update_ask_off"))+"</div></div></div>"
        "<div class='set-foot'><div class='set-hint'></div>"+button("settings-close",label("settings_close"))+"</div></div></div>";
}
// A page in the system browser; a notice with the address when there is none.
void open_page(const std::string& url) {
    if(url.empty() || update::open_url(url))return;
    auto text=localization::catalog().ui("update_open_failed");
    if(const auto at=text.find("{url}");at!=std::string::npos)text.replace(at,5,url);
    notices::post("update",text);
}
std::string mod_panel() {
    std::string body="<div class='set-shade'><div class='set-panel'><h1>"+label("mod_title")+"</h1><div class='set-tabs nav'>";
    for(unsigned i=0;i<std::size(mod_pages);++i)body+=button("mod-page:"+std::string(mod_pages[i]),label("mod_page_"+std::string(mod_pages[i])),i==mod_page,false,"set-tab");
    body+="</div><div id='set-body' class='set-body'>";
    const std::string page=mod_pages[mod_page];
    if(page=="campaigns")body+=mod_campaigns_page();
    else if(page=="art")body+=mod_art_page();
    else if(page=="dialogue")body+=mod_dialogue_page();
    else body+="<p>"+label("mod_audio_note")+"</p>";
    body+="</div><div class='set-foot'><div class='set-hint'>"+label("mod_hint")+"</div>"+button("settings-close",label("settings_close"))+"</div></div></div>";
    return body;
}

// The Library (library.hpp): units or characters grouped by work on the left, the chosen
// entry on the right. Up and down choose, left and right jump between works, C-up and
// C-down scroll the details, L / R switch between units and characters.
const char* marker_glyph(const std::string& token);
void library_turn(int step);
const nlohmann::json& library_entries(){return library::contents().at(library_tab?"pilots":"units");}
std::string library_number(const json& v){return v.is_number()?std::to_string(v.get<long long>()):std::string("-");}
std::string library_signed(const json& v){const int n=v.is_number()?v.get<int>():0;return n>0?"+"+std::to_string(n):std::to_string(n);}
// A label with a number filled in for "{n}".
std::string library_with(const char* key,const std::string& n) {
    auto text=localization::catalog().ui(key);
    if(const auto at=text.find("{n}");at!=std::string::npos)text.replace(at,3,n);
    return escape(text);
}
// The battle pose or portrait, as large as the box allows.
std::string library_art(const json& entry,float box) {
    if(!entry.contains("art") || !entry.at("art").contains("path"))return {};
    const auto& art=entry.at("art");
    const float w=art.value("width",96.f),h=art.value("height",96.f),scale=std::min(box/w,box/h);
    return "<img src='"+escape(image(portrait_path(art),int(w*scale*ui_density+.5f)))+"' style='width:"+std::to_string(w*scale)+"dp; height:"+std::to_string(h*scale)+"dp;'/>";
}
// 格／射 before a weapon's name and P／B／MAP after it, as the weapon pages draw them.
std::string library_marker(const std::string& token,const json& icons) {
    const char* glyph=marker_glyph(token);
    if(glyph && !symbol_font.empty() && (hd_portraits() || icons.empty()))
        return "<span class='im-mark"+std::string(token=="MAP"?" map":"")+"' style='font-size:15dp; margin:0 1dp;'>"+glyph+"</span>";
    if(const auto found=icons.find(token);found!=icons.end() && found->contains("path")) {
        const float w=found->value("width",8.f),h=found->value("height",10.f),scale=1.3f;
        return "<img src='"+escape(image(found->at("path").get<std::string>(),int(w*scale*ui_density+.5f)))+"' style='display:inline-block; width:"+std::to_string(w*scale)+
            "dp; height:"+std::to_string(h*scale)+"dp; vertical-align:middle; margin:0 1dp;'/>";
    }
    const char* cls=token=="格"?"melee":token=="射"?"ranged":token=="P"?"post":token=="B"?"beam":"map";
    return "<span class='im-badge "+std::string(cls)+"' style='width:"+(token=="MAP"?"26":"13")+"dp; height:13dp; line-height:13dp; font-size:8dp;'>"+escape(token)+"</span>";
}
// The four terrain ranks, A lit.
std::string library_tiles(const json& L,const std::string& letters) {
    const char* keys[]={"air","land","sea","space"};
    std::string out="<div class='lx-terrain'><span class='k'>"+escape(L.value("terrain",std::string()))+"</span>";  // tiles
    for(unsigned n=0;n<4 && n<letters.size();++n) {
        const auto rank=letters.substr(n,1);
        out+="<div class='t"+std::string(rank=="A"?" a":rank=="-"?" none":"")+"'><span>"+escape(L.value(keys[n],std::string()))+"</span><span class='r'>"+escape(rank=="-"?"–":rank)+"</span></div>";
    }
    return out+"</div>";
}
// The work, and a unit's model number, above the name.
std::string library_work(const json& entry) {
    std::string line=entry.contains("work_name")?entry.at("work_name").get<std::string>():localization::catalog().ui("library_work_other");
    if(entry.contains("model"))line+="  ·  "+entry.at("model").get<std::string>();
    return escape(line);
}
const char* library_type_names[]={"","Ⅰ","Ⅱ","Ⅲ","Ⅳ"};
std::string library_type_tag(unsigned type){return "<span class='lu-type t"+std::to_string(type)+"'>"+library_type_names[type]+"</span>";}
// The two pages share one layout (docs/native/library.md): the art in a large square on
// the left with the terrain under it; beside it the work (and a unit's model), the name
// (and a character's full name) with a gold tag at its right (a unit's upgrade cap, a
// character's two-action level), the tags and the stats as bars; then the page's own sections.
std::string library_hero(const json& entry,const std::string& sub,const std::string& badge,const json& L,const std::string& tags,const std::string& stats,const std::string& name_tags="") {
    // The square takes about 30 % of the details' width, 150-220 dp: 164 on a Steam Deck at
    // the Largest size (865 dp across), 220 on a larger screen. The panel is 96 % of the
    // window up to 1180 dp less its padding; the list 210 dp and the gaps take 246.
    const float window=float(pixels_w)/std::max(.1f,ui_density),panel=std::min(window*.96f,1180.f)-36;
    const float side=std::clamp((panel-246)*.30f,150.f,220.f);
    const auto box=std::to_string(int(side));
    // The terrain sits under the square, which leaves the side shorter.
    return "<div class='lx-hero'><div class='lx-left' style='width:"+box+"dp;'><div class='lib-art lx-art' style='width:"+box+"dp; height:"+box+"dp;'>"+library_art(entry,side-12)+"</div>"+
        library_tiles(L,entry.value("terrain",std::string()))+"</div><div class='lx-side'><div class='lib-sub'>"+library_work(entry)+"</div>"
        "<div class='lx-name'><span class='lib-name fit'>"+escape(entry.value("name",std::string()))+"</span>"+(sub.empty()?std::string():"<span class='sub'>"+escape(sub)+"</span>")+
        name_tags+"<span class='gold'>"+badge+"</span></div>"+(tags.empty()?std::string():"<div class='lx-tags'>"+tags+"</div>")+stats+"</div></div>";
}
// A stat tile: the value, an optional level-99 value, and a bar (one or two parts).
std::string library_tile(const std::string& key,const std::string& value,const std::string& high,float first,float second) {
    return "<div class='lx-stat'><span class='k'>"+key+"</span><div class='row'><span class='a"+std::string(value=="–"?" none":"")+"'>"+value+"</span>"+
        (high.empty()?std::string():"<span class='to'>→</span><span class='b'>"+high+"</span>")+"</div><div class='bar'><div class='p1' style='width:"+std::to_string(std::clamp(first,0.f,100.f))+
        "%;'></div><div class='p2' style='width:"+std::to_string(std::clamp(second,0.f,100.f-std::clamp(first,0.f,100.f)))+"%;'></div></div></div>";
}
std::string library_section(const std::string& title,const std::string& note){return "<div class='lx-head'>"+title+(note.empty()?std::string():"<span>"+note+"</span>")+"</div>";}
// The unit page: the stats against about the top tenth of all units (HP 22000, armor
// 2300...), so ordinary machines do not vanish beside the 65000-HP bosses; the weapon
// table with each weapon's power at the unit's cap and its upgrade type.
std::string library_unit(const json& u,const json& L,const json& W,const json& types) {
    const auto l=[&](const char* key){return escape(L.value(key,std::string()));};
    const auto& C=localization::catalog();
    const unsigned cap=u.value("cap",0u);
    std::string kinds;
    for(const auto& t:u.at("types"))kinds+=(kinds.empty()?"":"・")+t.get<std::string>();
    std::string tags="<span>"+escape(kinds)+"</span><span>"+l("size")+" "+escape(u.value("size",std::string()))+"</span>";
    for(const auto& a:u.at("abilities"))tags+="<span class='on'>"+escape(a.get<std::string>())+"</span>";
    if(u.contains("shield"))tags+="<span class='on'>"+escape(u.value("shield",std::string()))+"</span>";
    tags+="<span>"+label("library_slots")+" "+std::to_string(u.value("slots",0u))+"</span><span>"+l("repair")+" "+library_number(u["repair"])+"</span>";
    const auto stat=[&](const char* key,const char* field,float scale){return library_tile(l(key),library_number(u[field]),"",u.value(field,0.f)/scale*100,0);};
    const std::string badge=label("library_cap")+" "+library_with("library_cap_value",std::to_string(cap));
    std::string body=library_hero(u,"",badge,L,tags,
        "<div class='lx-stats three unit'>"+stat("hp","hp",22000)+stat("en","en",300)+stat("move","move",10)+stat("mobility","mobility",130)+stat("armor","armor",2300)+stat("limit","limit",380)+"</div>");
    const auto w=[&](const char* key){return escape(W.value(key,std::string()));};
    const json icons=W.value("icons",json::object());
    const auto& weapons=u.at("weapons");
    body+=library_section(w("weapon"),"")+"<div class='lib-wrow head'><span class='n'></span><span class='c'>"+w("power")+"</span><span class='c full'>"+label("library_full")+
        "</span><span class='c ty'>"+label("library_type")+"</span><span class='c'>"+w("range")+"</span><span class='c'>"+w("hit")+"</span><span class='c fit'>"+label("library_critical")+
        "</span><span class='c'>"+w("ammo")+"</span><span class='c'>EN</span><span class='c'>"+label("library_morale")+"</span></div>";
    if(weapons.empty())body+="<p class='lib-note'>"+label("library_no_weapons")+"</p>";
    std::set<unsigned> present;
    const auto none="<span class='none'>–</span>";
    const auto dash=[&](const json& v){return v.is_number() && v.get<int>()?std::to_string(v.get<int>()):std::string(none);};
    for(const auto& r:weapons) {
        std::string before,after;
        for(const auto& m:r.value("markers",json::array()))(m=="格" || m=="射"?before:after)+=library_marker(m.get<std::string>(),icons);
        // The tags may wrap under the name in a narrow table (a Steam Deck at the Largest size).
        std::string flags;
        if(r.contains("unlock"))flags+=" <span class='lu-flag unlock'>"+label("library_unlock")+"</span>";
        if(r.value("combo",false))flags+=" <span class='lu-flag'>"+label("library_combo")+"</span>";
        const unsigned a=r.value("range_min",0u),b=r.value("range_max",0u),type=r.value("type",0u);
        if(type)present.insert(type);
        body+="<div class='lib-wrow'><span class='n'><span class='name'>"+before+escape(r.value("display_name",r.value("name",std::string())))+after+"</span>"+flags+"</span>"
            "<span class='c big'>"+library_number(r["power"])+"</span><span class='c big full'>"+(type?library_number(r["full"]):std::string(none))+"</span>"
            "<span class='c ty'>"+(type?library_type_tag(type):std::string(none))+"</span>"
            "<span class='c'>"+(a==b?std::to_string(a):std::to_string(a)+"–"+std::to_string(b))+"</span><span class='c'>"+library_signed(r["hit"])+"</span><span class='c'>"+library_signed(r["critical"])+"</span>"
            "<span class='c'>"+(r.contains("ammo")?library_number(r["ammo"]):std::string(none))+"</span><span class='c'>"+dash(r.value("en",json()))+"</span><span class='c'>"+dash(r.value("morale",json()))+"</span></div>";
    }
    // Each type on this machine: what its steps up to the cap add, and cost.
    if(!present.empty()) {
        body+="<div class='lu-legend'><span>"+library_with("library_full_note",std::to_string(cap))+"</span>";
        for(const unsigned type:present) {
            unsigned power=0,price=0;
            for(unsigned n=0;n<cap && n<15;++n){power+=types[type-1]["power"][n].get<unsigned>();price+=types[type-1]["price"][n].get<unsigned>();}
            auto line=C.ui("library_type_line");
            for(const auto& [token,value]:{std::pair{"{p}",std::to_string(power)},std::pair{"{c}",std::to_string(price)}})
                if(const auto at=line.find(token);at!=std::string::npos)line.replace(at,3,value);
            // Text straight inside a flex box is dropped (RmlUi); it gets its own span.
            body+="<span class='item'>"+library_type_tag(type)+"<span>"+escape(line)+"</span></span>";
        }
        body+="</div>";
    }
    return body;
}
// The character page: the side, role and 恋爱補正 partners as tags; stats at level 1
// and 99; spirit commands as cards; each skill's levels on a 1-99 track.
std::string library_pilot(const json& p,const json& L) {
    const auto l=[&](const char* key){return escape(L.value(key,std::string()));};
    const std::string name=p.value("name",std::string()),full=p.value("full_name",std::string());
    // Sub-pilots and fairies: the original's pilot page prints their stats as --- (pilot +4
    // & 0xC0); so does this one, with no two-action level. SP and spirits still count.
    const bool role=p.contains("role"),stats=p.contains("stats");
    // The side and role sit on the name's row; the 恋爱補正 partners take the tags' row.
    std::string side;
    if(p.contains("enemy"))side+="<span class='tag "+std::string(p.value("enemy",false)?"enemy":"ally")+"'>"+label(p.value("enemy",false)?"library_enemy":"library_ally")+"</span>";
    if(role)side+="<span class='tag role'>"+label(p.value("role",std::string())=="fairy"?"library_fairy":"library_sub_pilot")+"</span>";
    std::string love;
    if(p.contains("love")) {
        love+="<div class='lx-love'><span class='k'>"+label("library_love")+"</span>";
        for(const auto& partner:p.at("love"))
            love+="<div class='who'><div class='face'>"+library_art(partner,28)+"</div><span>"+escape(partner.value("name",std::string()))+"</span><span class='dir"+
                std::string(partner.value("mutual",false)?" both":"")+"'>"+label(partner.value("mutual",false)?"library_love_mutual":"library_love_one_way")+"</span></div>";
        love+="</div>";
    }
    std::string extra;
    if(!stats)extra+="<p class='lib-note'>"+label("library_no_stats")+"</p>";
    else {
        // Level 1 and level 99: every pilot gains the same each level (800A6238, rerun by the
        // level-up at 801FC4EC): +1 melee, ranged, skill and reaction, +2 hit, evade and SP.
        const auto& s=p.at("stats");
        const auto stat=[&](const char* key,unsigned gain){
            const unsigned v=s.value(key,0u);
            if(role && std::string_view(key)!="sp")return library_tile(l(key),"–","",0,0);
            return library_tile(l(key),std::to_string(v),std::to_string(v+98*gain),v/3.4f,98*gain/3.4f);
        };
        // Four to a row; the eighth cell says which figure is which level.
        extra+="<div class='lx-stats'>"+stat("melee",1)+stat("ranged",1)+stat("skill",1)+stat("reaction",1)+stat("hit",2)+stat("evade",2)+stat("sp",2)+
            "<div class='lx-stat legend'><span class='k'>"+label("library_growth_note")+"</span><div class='row'><span class='a'>Lv1</span><span class='to'>→</span><span class='b'>Lv99</span></div></div></div>";
    }
    const std::string badge=label("library_double_move")+" "+(p.contains("double_move") && !role?"Lv"+library_number(p["double_move"]):std::string("–"));
    std::string body=library_hero(p,full!=name?full:std::string(),badge,L,love,extra,side);
    if(!stats)return body;
    body+=library_section(l("spirits"),label("library_spirit_note"))+"<div class='lx-spirits'>";
    for(const auto& sp:p.value("spirits",json::array()))
        body+="<div class='card'><div class='n'>"+escape(sp.value("name",std::string()))+"</div><div class='row'><span class='lv'>Lv"+std::to_string(sp.value("level",0u))+
            "</span><span class='cost'>"+library_with("library_spirit_cost",std::to_string(sp.value("cost",0u)))+"</span></div></div>";
    if(p.value("spirits",json::array()).empty())body+="<p class='lib-note'>-</p>";
    body+="</div>";
    const auto& skills=p.value("skills",json::array());
    if(!skills.empty()) {
        body+=library_section(l("skills"),label("library_skill_note"));
        for(const auto& skill:skills) {
            const auto& steps=skill.at("levels");
            const auto first=steps.front().value("name",std::string()),last=steps.back().value("name",std::string());
            // "NT L4 – L9": the last level's own "L9" after the first's full name.
            const auto at_level=last.rfind('L');
            const auto range=steps.size()>1?" – "+(at_level==std::string::npos?last:last.substr(at_level)):std::string();
            body+="<div class='lx-track'><span class='k'>"+escape(first+range)+"</span><div class='line'><div class='rail'></div>";
            for(const auto& step:steps) {
                const float at=(step.value("level",1.f)-1)/98.f*100;
                body+="<div class='mark' style='left:"+std::to_string(at)+"%;'><div class='tick'></div><div class='lv'>"+std::to_string(step.value("level",0u))+"</div></div>";
            }
            body+="</div></div>";
        }
    }
    return body;
}
std::string library_detail() {
    const auto& entries=library_entries();
    if(entries.empty())return {};
    const auto& entry=entries[std::min<size_t>(library_index[library_tab],entries.size()-1)];
    const auto& all=library::contents();
    return library_tab?library_pilot(entry,all.at("labels")):library_unit(entry,all.at("labels"),all.at("weapon_labels"),all.at("upgrade_types"));
}
std::string library_panel() {
    // Laid out for a Steam Deck at the Largest size (865 x 540 dp): nearly the whole window,
    // the title and tabs on one row, a fixed-width list.
    std::string body="<div class='set-shade solid'><div class='set-panel lib-panel'><div class='lib-top'><h1>"+label("library_title")+"</h1><div class='set-tabs nav'>";
    const char* tabs[]={"units","pilots"};
    for(unsigned i=0;i<2;++i)
        body+=button("lib-tab:"+std::to_string(i),label("library_tab_"+std::string(tabs[i]))+"  ("+std::to_string(library::contents().at(tabs[i]).size())+")",i==library_tab,false,"set-tab");
    body+="</div></div><div class='lib-main'><div id='lib-list' class='lib-list'>";
    const auto& entries=library_entries();
    // A heading over each work's entries.
    int work=-2;
    for(size_t i=0;i<entries.size();++i) {
        if(const int next=entries[i].value("work",-1);next!=work) {
            body+="<div class='lib-group"+std::string(work==-2?" first":"")+"'>"+escape(next<0?localization::catalog().ui("library_work_other"):entries[i].value("work_name",std::string()))+"</div>";
            work=next;
        }
        // Units carry their model number; characters who first appear as enemies a red dot.
        const auto& e=entries[i];
        std::string text=library_tab?"<span class='dot"+std::string(e.value("enemy",false)?" enemy":"")+"'></span><span class='n fit'>"+escape(e.value("name",std::string()))+"</span>"
                                    :"<span class='n fit'>"+escape(e.value("name",std::string()))+"</span>"+(e.contains("model")?"<span class='m'>"+escape(e.at("model").get<std::string>())+"</span>":std::string());
        body+=button("lib-item:"+std::to_string(i),text,i==library_index[library_tab],false,"lib-item");
    }
    body+="</div><div id='lib-detail' class='lib-detail'>"+library_detail()+"</div></div>"
        "<div class='set-foot'><div class='set-hint'>"+label("library_hint")+"</div>"+button("settings-close",label("settings_close"))+"</div></div></div>";
    return body;
}
// Chooses entry `index` of the tab: the list marks and shows it, the details are redrawn.
void library_select(unsigned index,bool focus=true) {
    if(!settings_doc)return;
    const auto& entries=library_entries();
    if(entries.empty())return;
    index=std::min<unsigned>(index,unsigned(entries.size()-1));
    if(auto* old=settings_doc->GetElementById("lib-item:"+std::to_string(library_index[library_tab])))old->SetClass("on",false);
    const bool changed=index!=library_index[library_tab];
    library_index[library_tab]=index;
    if(auto* item=settings_doc->GetElementById("lib-item:"+std::to_string(index))) {
        item->SetClass("on",true);
        if(focus) {
            item->Focus();
            // A work's first entry brings its heading into view too.
            if(auto* heading=item->GetPreviousSibling();heading && heading->IsClassSet("lib-group"))heading->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
            item->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
        }
    }
    if(!changed)return;
    if(auto* detail=settings_doc->GetElementById("lib-detail")){detail->SetInnerRML(library_detail());detail->SetScrollTop(0);}
    settings_doc->UpdateDocument();fit_lines(settings_doc);
}
// Scrolls the details by `dp` (C-up / C-down, held: a little every frame).
void library_scroll(float dp) {
    if(auto* detail=settings_doc?settings_doc->GetElementById("lib-detail"):nullptr)detail->SetScrollTop(detail->GetScrollTop()+dp*context->GetDensityIndependentPixelRatio());
}
// Left and right jump between works: to the next work's first entry, or back to this
// work's first entry and from there to the previous work's.
unsigned library_work_step(int step) {
    const auto& entries=library_entries();
    const auto work=[&](size_t i){return entries[i].value("work",-1);};
    size_t i=std::min<size_t>(library_index[library_tab],entries.empty()?0:entries.size()-1);
    if(entries.empty())return 0;
    if(step>0) {
        size_t j=i;
        while(j<entries.size() && work(j)==work(i))++j;
        return unsigned(j<entries.size()?j:i);
    }
    size_t first=i;
    while(first>0 && work(first-1)==work(i))--first;
    if(first<i || first==0)return unsigned(first);
    size_t previous=first-1;
    while(previous>0 && work(previous-1)==work(first-1))--previous;
    return unsigned(previous);
}
void library_move(int dy,int dx) {
    if(!settings_doc)return;
    auto* focus=context->GetFocusElement();
    const bool on_tab=focus && focus->IsClassSet("set-tab");
    if(dx) {
        if(on_tab){library_turn(dx);return;}
        library_select(library_work_step(dx));
        return;
    }
    if(on_tab){if(dy>0)library_select(library_index[library_tab]);return;}
    const int next=int(library_index[library_tab])+dy;
    if(next<0) {
        if(auto* tab=settings_doc->GetElementById("lib-tab:"+std::to_string(library_tab))){tab->Focus();if(auto* list=settings_doc->GetElementById("lib-list"))list->SetScrollTop(0);}
        return;
    }
    library_select(unsigned(next));
}
// The battle viewer (battle_viewer.hpp, docs/design/battle-viewer.md §9.4, the canvas design of
// 2026-10-04 after the Z special disc's): on the title beside Library and MOD. One page: 反
// (the defender, left, facing right) and 攻 (the attacker, right), each a card (its unit,
// pilot and scene) and a column of rows: unit, pilot, weapon, what happens when it is hit,
// scene; the song below. A row opens a box over the other half, so the side being chosen
// for stays in view and shows each choice as the keys reach it; 決定 keeps it, 戻る puts
// back what was there. As in the Z disc the unit comes first: its pilots are only those
// who may fly it, and a unit its pilot cannot fly brings its default pilot. A reaction the
// original could not show is greyed with the reason (battle-formulas.md: the shield and
// the sword are unit equipment and pilot skills, a parry needs a parryable weapon, a
// barrier its kind of weapon). Damage leaves 10 HP unless set. 戦闘開始 plays it through
// the title demo's own path; the page opens again when the battle is over.
std::atomic_bool viewer_open{};
struct ViewerSide{unsigned unit=0,pilot=0,weapon=0;};
std::array<ViewerSide,2> viewer_side{};   // 0 attacks (攻), 1 defends (反)
// What happens to each side when it is hit: side 1 by the attack, side 0 by the counter.
struct ViewerHit{unsigned reaction=0;int damage=-1;bool destroy=false;};   // damage -1: leave 10 HP
std::array<ViewerHit,2> viewer_hit{};
bool viewer_ready=false,viewer_counter=true;
std::array<unsigned,2> viewer_scene{12,12};   // harbor, both sides
int viewer_song=-1;                            // a サウンドセレクト row, -1 the attacker's
std::string viewer_picker,viewer_focus;        // the open box ("unit:0" etc.); the element to focus next
// What the page held when a box opened (戻る puts it back), the unit box's work page and the
// scene box's category.
struct ViewerState{std::array<ViewerSide,2> side;std::array<ViewerHit,2> hit;bool counter;std::array<unsigned,2> scene;int song;};
ViewerState viewer_saved{};
int viewer_page=0;
unsigned viewer_cat=0;
enum ViewerReaction:unsigned{hit,shield,barrier,dodge,bunshin,parry,reaction_count};
const char* viewer_reaction_keys[]={"hit","shield","barrier","dodge","bunshin","parry"};
// Who may fly each unit, and its default pilot (tools/content/battle_viewer_pilots.py: stage
// deployments, のりかえ categories, the unit's other forms; the default is the pair the
// stages deploy most), so every pairing has the quotes the game wrote for it.
struct ViewerCrew{unsigned unit,pilot;std::vector<unsigned> actors;};
const ViewerCrew viewer_crews[]={
#include "battle_viewer_pilots.inc"
};
const ViewerCrew* viewer_crew(unsigned unit) {
    for(const auto& c:viewer_crews)if(c.unit==unit)return &c;
    return nullptr;
}
bool viewer_flies(unsigned actor,unsigned unit) {
    const auto* crew=viewer_crew(unit);
    return crew && std::find(crew->actors.begin(),crew->actors.end(),actor)!=crew->actors.end();
}
bool viewer_flies_any(unsigned actor) {
    for(const auto& c:viewer_crews)if(std::find(c.actors.begin(),c.actors.end(),actor)!=c.actors.end())return true;
    return false;
}
const json& viewer_units(){return library::contents().at("units");}
const json& viewer_pilots(){return library::contents().at("pilots");}
const json& viewer_unit(unsigned s){return viewer_units()[std::min<size_t>(viewer_side[s].unit,viewer_units().size()-1)];}
const json& viewer_pilot(unsigned s){return viewer_pilots()[std::min<size_t>(viewer_side[s].pilot,viewer_pilots().size()-1)];}
// A unit's weapons without the map weapons (a map attack has no battle animation).
std::vector<const json*> viewer_weapons(unsigned s) {
    std::vector<const json*> list;
    for(const auto& w:viewer_unit(s).at("weapons")) {
        const auto& markers=w.value("markers",json::array());
        if(std::find(markers.begin(),markers.end(),"MAP")==markers.end())list.push_back(&w);
    }
    return list;
}
const json* viewer_weapon(unsigned s) {
    const auto list=viewer_weapons(s);
    return list.empty()?nullptr:list[std::min<size_t>(viewer_side[s].weapon,list.size()-1)];
}
// A new unit's weapon: the attacker's last (the finisher), the defender's strongest.
void viewer_default_weapon(unsigned s) {
    const auto list=viewer_weapons(s);
    viewer_side[s].weapon=0;
    if(list.empty())return;
    if(s==0){viewer_side[s].weapon=unsigned(list.size()-1);return;}
    for(size_t i=0;i<list.size();++i)if(list[i]->value("power",0u)>list[viewer_side[s].weapon]->value("power",0u))viewer_side[s].weapon=unsigned(i);
}
// An entry's index by its id ("id" of a unit, "actor" of a pilot), or -1.
int viewer_index(const json& list,const char* field,unsigned id) {
    for(size_t i=0;i<list.size();++i)if(list[i].value(field,~0u)==id)return int(i);
    return -1;
}
// A new unit keeps its pilot if they may fly it, else takes its default one.
void viewer_fit_pilot(unsigned s) {
    const unsigned unit=viewer_unit(s).value("id",0u);
    const auto* crew=viewer_crew(unit);
    if(!crew || viewer_flies(viewer_pilot(s).value("actor",~0u),unit))return;
    if(const int i=viewer_index(viewer_pilots(),"actor",crew->pilot);i>=0)viewer_side[s].pilot=unsigned(i);
}
// The first time: ゴッドガンダム and ドモン against マスターガンダム and 東方不敗.
void viewer_defaults() {
    if(viewer_ready || viewer_units().empty() || viewer_pilots().empty())return;
    viewer_ready=true;
    const auto find=[](const json& list,const char* field,unsigned id){
        for(size_t i=0;i<list.size();++i)if(list[i].value(field,~0u)==id)return unsigned(i);
        return 0u;
    };
    viewer_side[0]={find(viewer_units(),"id",1),find(viewer_pilots(),"actor",4),0};
    viewer_side[1]={find(viewer_units(),"id",28),find(viewer_pilots(),"actor",18),0};
    viewer_default_weapon(0);viewer_default_weapon(1);
}
// Side t hit by the other side's weapon: the locale key of why a reaction cannot be shown,
// empty when it can (docs/gameplay/battle-formulas.md, the defence order).
std::string viewer_gate(unsigned t,unsigned reaction) {
    const auto& unit=viewer_unit(t);
    const unsigned equipment=unit.value("equipment_bits",0u),abilities=unit.value("ability_bits",0u),skills=viewer_pilot(t).value("skill_bits",0u);
    const auto* weapon=viewer_weapon(t^1);
    const unsigned flags=weapon?weapon->value("flags",0u):0;
    switch(reaction) {
    case shield:return !(equipment&2)?"viewer_gate_no_shield":!(skills&2)?"viewer_gate_no_skill":"";
    case barrier:
        if((abilities&0x4000) && (skills&0x20))return "";               // aura barrier: any weapon
        if(!(abilities&(0x20|0x200|0x800)))return "viewer_gate_no_barrier";
        return flags&0x02?"":"viewer_gate_not_beam";                   // I-field, beam coat, planet defensor
    case bunshin:return abilities&0x163011?"":"viewer_gate_no_bunshin";
    case parry:return !(equipment&1)?"viewer_gate_no_sword":!(skills&1)?"viewer_gate_no_skill":!(flags&0x08)?"viewer_gate_unparryable":"";
    default:return "";
    }
}
unsigned viewer_reaction(unsigned t){return viewer_gate(t,viewer_hit[t].reaction).empty()?viewer_hit[t].reaction:unsigned(hit);}
bool viewer_unhurt(unsigned t){const auto r=viewer_reaction(t);return r==dodge || r==bunshin || r==parry || (t==0 && !viewer_counter);}
unsigned viewer_hp(unsigned t){return viewer_unit(t).value("hp",1u);}
unsigned viewer_damage(unsigned t) {
    if(viewer_unhurt(t))return 0;
    const unsigned hp=viewer_hp(t);
    if(viewer_hit[t].destroy)return hp;
    const int fallback=viewer_reaction(t)==barrier?0:int(hp)-10;
    return unsigned(std::clamp(viewer_hit[t].damage<0?fallback:viewer_hit[t].damage,0,int(hp)-1));
}
// The record's reaction code (§2.2): a barrier is held at 0 damage and broken above it.
int viewer_code(unsigned t) {
    const unsigned abilities=viewer_unit(t).value("ability_bits",0u),skills=viewer_pilot(t).value("skill_bits",0u);
    const bool broken=viewer_damage(t)>0;
    switch(viewer_reaction(t)) {
    case shield:return 0x11;
    case dodge:return 0x16;
    case parry:return 0x12;
    case bunshin:
        for(const auto [bit,code]:std::initializer_list<std::pair<unsigned,int>>{{0x20000,6},{0x1000,7},{0x2000,8},{0x1,9},{0x40000,0xA},{0x100000,0xB},{0x10,0xC}})
            if(abilities&bit)return code;
        return 0xC;
    case barrier:
        if((abilities&0x4000) && (skills&0x20))return broken?0xF:4;
        if(abilities&0x20)return broken?0xD:2;
        if(abilities&0x800)return broken?0x10:5;
        return broken?0xE:3;
    default:return -1;
    }
}
json viewer_choice() {
    json c;
    for(unsigned s=0;s<2;++s) {
        json side={{"unit",viewer_unit(s).at("id")},{"pilot",viewer_pilot(s).at("actor")}};
        if(const auto* w=viewer_weapon(s))side["weapon"]=w->at("number");
        c[s?"defender":"attacker"]=side;
    }
    c["reaction"]=viewer_code(1);c["damage"]=viewer_damage(1);c["destroy"]=viewer_damage(1)>=viewer_hp(1);
    c["counter"]=viewer_counter && viewer_weapon(1);
    c["counter_reaction"]=viewer_code(0);c["counter_damage"]=viewer_damage(0);c["counter_destroy"]=viewer_damage(0)>=viewer_hp(0);
    c["scene"]={viewer_scene[0],viewer_scene[1]};
    const auto songs=battle_viewer::songs();
    c["song"]=viewer_song>=0 && viewer_song<int(songs.size())?songs[size_t(viewer_song)].value("song",-1):-1;
    return c;
}
float viewer_card_height=150,viewer_col_width=380;
std::string viewer_kind(){return viewer_picker.substr(0,viewer_picker.find(':'));}
unsigned viewer_pick_side(){const auto colon=viewer_picker.find(':');return colon==std::string::npos?0u:unsigned(std::stoul(viewer_picker.substr(colon+1)));}
bool viewer_picking(const std::string& kind,unsigned s){return !viewer_picker.empty() && viewer_kind()==kind && viewer_pick_side()==s;}
std::string viewer_name(const json& e){return escape(e.value("name",std::string()));}
// A template's %1 and %2 (the template already escaped, the values too).
std::string viewer_fill(std::string text,const std::string& a,const std::string& b=std::string()) {
    if(const auto at=text.find("%1");at!=std::string::npos)text.replace(at,2,a);
    if(const auto at=text.find("%2");at!=std::string::npos)text.replace(at,2,b);
    return text;
}
std::string viewer_initial(const std::string& name) {
    if(name.empty())return {};
    const auto c=uint8_t(name[0]);
    return name.substr(0,c>=0xF0?4:c>=0xE0?3:c>=0xC0?2:1);
}
// A pilot's skills in a line: each one's first name.
std::string viewer_skills(const json& p) {
    std::string out;
    for(const auto& skill:p.value("skills",json::array())) {
        const auto& levels=skill.value("levels",json::array());
        if(levels.empty())continue;
        if(!out.empty())out+=" · ";
        out+=levels.front().value("name",std::string());
    }
    return out;
}
// The song the attacker brings (no song chosen), its row's text.
std::string viewer_attacker_song() {
    const int number=battle_viewer::default_song(viewer_unit(0).value("id",0u),viewer_pilot(0).value("actor",0u));
    for(const auto& row:battle_viewer::songs())if(row.value("song",-2)==number)return escape(row.value("text",std::string()));
    return label("viewer_song_keep");
}
int viewer_song_number(int row) {
    const auto songs=battle_viewer::songs();
    if(row>=0 && row<int(songs.size()))return songs[size_t(row)].value("song",-1);
    return battle_viewer::default_song(viewer_unit(0).value("id",0u),viewer_pilot(0).value("actor",0u));
}
std::string viewer_scene_key(unsigned i) {
    const auto scenes=battle_viewer::scenes();
    return scenes[std::min<size_t>(i,scenes.size()-1)].get<std::string>();
}
std::string viewer_scene_name(unsigned i){return label("viewer_scene_"+viewer_scene_key(i));}
// Ground, air or space: the scene box's tabs 1-3.
unsigned viewer_scene_cat(unsigned i) {
    const auto key=viewer_scene_key(i);
    return key=="space" || key=="moon"?2:key=="sky" || key=="otherworld"?1:0;
}
const char* viewer_cat_keys[]={"viewer_cat_all","viewer_cat_ground","viewer_cat_air","viewer_cat_space"};
// A scene's thumbnail from the HD pack, w dp wide (16:9), empty without one. The
// thumbnails are 480-pixel JPEGs: always through the resampler, which decodes them (RmlUi's
// own loader does not).
std::string viewer_scene_img(unsigned i,float w,const std::string& style=std::string()) {
    const auto path=srw64::sprites::viewer_scene_image(viewer_scene_key(i));
    if(path.empty())return {};
    return "<img src='"+escape(image(path,std::min(dp_pixels(w),479)))+"' style='width:"+std::to_string(w)+"dp; height:"+std::to_string(w*9/16)+"dp;"+style+"'/>";
}
// The unit box's pages: the works with a unit someone may fly, in the Library's order, and
// their units (one per id).
struct ViewerWork{int work;std::string name;std::vector<unsigned> units;};
const std::vector<ViewerWork>& viewer_works() {
    static std::vector<ViewerWork> cache;
    static std::string cached;
    const auto& list=viewer_units();
    const auto key=localization::catalog().locale+std::to_string(list.size());
    if(key==cached)return cache;
    cache.clear();
    std::set<unsigned> seen;
    for(size_t i=0;i<list.size();++i) {
        const auto& e=list[i];
        const unsigned id=e.value("id",~0u);
        if(!viewer_crew(id) || !seen.insert(id).second)continue;
        const int work=e.value("work",-1);
        auto w=std::find_if(cache.begin(),cache.end(),[&](const auto& x){return x.work==work;});
        if(w==cache.end())w=cache.insert(cache.end(),{work,work<0?localization::catalog().ui("library_work_other"):e.value("work_name",std::string()),{}});
        w->units.push_back(unsigned(i));
    }
    cached=key;
    return cache;
}
int viewer_work_of(unsigned unit) {
    const auto& works=viewer_works();
    for(size_t w=0;w<works.size();++w)
        if(std::find(works[w].units.begin(),works[w].units.end(),unit)!=works[w].units.end())return int(w);
    return 0;
}
// The BGM list by work: a song belongs to the work whose pilots and units play it (the
// pilots' table for those who fly, the units' table and the super modes' songs,
// battle_viewer.cpp); the songs nobody plays (the map and story music) go last under
// 其他. In the list's order.
struct ViewerSongGroup{int work;std::string name;std::vector<size_t> rows;};
std::vector<ViewerSongGroup> viewer_song_groups() {
    static std::vector<ViewerSongGroup> cache;
    static std::string cached;
    const auto songs=battle_viewer::songs();
    const auto key=localization::catalog().locale+std::to_string(songs.size());
    if(key==cached)return cache;
    std::map<int,std::map<int,int>> votes;   // song -> work -> weight
    std::map<int,std::string> names;
    for(const auto& p:viewer_pilots()) {
        if(!viewer_flies_any(p.value("actor",~0u)))continue;   // story-only people (a resistance youth) would mislead
        const int work=p.value("work",-1),song=battle_viewer::pilot_song(p.value("actor",~0u));
        if(work>=0 && song>0){votes[song][work]+=2;names[work]=p.value("work_name",std::string());}
    }
    for(const auto& u:viewer_units()) {
        const int work=u.value("work",-1),song=battle_viewer::default_song(u.value("id",~0u),~0u);
        if(work>=0 && song>0){votes[song][work]+=1;names[work]=u.value("work_name",std::string());}
    }
    std::vector<ViewerSongGroup> groups;
    ViewerSongGroup other{-1,localization::catalog().ui("library_work_other"),{}};
    for(size_t i=0;i<songs.size();++i) {
        const auto found=votes.find(songs[i].value("song",-1));
        if(found==votes.end()){other.rows.push_back(i);continue;}
        const int work=std::max_element(found->second.begin(),found->second.end(),[](const auto& a,const auto& b){return a.second<b.second;})->first;
        auto group=std::find_if(groups.begin(),groups.end(),[&](const auto& g){return g.work==work;});
        if(group==groups.end())group=groups.insert(groups.end(),{work,names[work],{}});
        group->rows.push_back(i);
    }
    if(!other.rows.empty())groups.push_back(std::move(other));
    cache=std::move(groups);cached=key;
    return cache;
}
// Z's 大/中/小 damage: three quarters, half and a quarter of the HP.
int viewer_preset(unsigned t,unsigned k){const int hp=int(viewer_hp(t));return k==1?hp*3/4:k==2?hp/2:hp/4;}
// What happens to side t when it is hit, in a line: the reaction and the damage.
std::string viewer_summary(unsigned t) {
    if(t==0 && !viewer_counter)return label("viewer_counter_off");
    const auto r=viewer_reaction(t);
    std::string text=label(std::string("viewer_res_")+viewer_reaction_keys[r])+" · ";
    if(viewer_unhurt(t))return text+label("viewer_sum_unhurt");
    if(viewer_damage(t)>=viewer_hp(t))return text+label("viewer_destroy");
    text+=viewer_fill(label("viewer_sum_damage"),std::to_string(viewer_damage(t)));
    if(viewer_hit[t].damage<0 && r!=barrier)text+=label("viewer_sum_rest");
    return text;
}
std::string viewer_card(unsigned s) {
    const bool def=s==1;
    const float h=viewer_card_height,w=viewer_col_width,art=h-36,face=std::clamp(h*0.4f,52.f,96.f);
    const unsigned hp=viewer_hp(s),after=hp-viewer_damage(s);
    std::string body="<div class='vw-card "+std::string(def?"def":"att")+"' style='height:"+std::to_string(h)+"dp;'>";
    // The side's scene behind it, dimmed.
    // (A layer of its own: RmlUi clips a positioned element only by a positioned parent's own overflow.)
    if(const auto bg=viewer_scene_img(viewer_scene[s],w*1.1f," margin-top:"+std::to_string((h-w*1.1f*9/16)/2)+"dp;");!bg.empty())body+="<div class='bg'>"+bg+"</div><div class='shade'></div>";
    body+="<div class='role'>"+label(def?"viewer_defender":"viewer_attacker")+"</div><div class='art' style='height:"+std::to_string(art)+"dp;'>"+library_art(viewer_unit(s),art)+"</div>"
        "<div class='face' style='width:"+std::to_string(face)+"dp; height:"+std::to_string(face)+"dp;'>"+library_art(viewer_pilot(s),face-2)+"</div>";
    if(!viewer_picker.empty() && viewer_kind()!="song" && viewer_kind()!="result" && viewer_pick_side()==s)body+="<div class='badge'>"+label("viewer_previewing")+"</div>";
    body+="<div class='foot'><span class='hp'>HP</span><span class='hpv'>"+std::to_string(hp)+"</span><span class='hp'>→</span><span class='hpv"+
        std::string(after==0?" down":after<hp?" hurt":"")+"'>"+std::to_string(after)+"</span></div>";
    return body+"</div>";
}
// A row of a side's column: the key, the value, then anything else.
std::string viewer_cell(unsigned s,const std::string& what,const std::string& key,const std::string& value,const std::string& more=std::string(),const std::string& mid=std::string(),const std::string& before=std::string()) {
    const bool on=!viewer_picker.empty() && viewer_pick_side()==s && (viewer_kind()==what || (viewer_kind()=="weapon" && what=="counter_weapon") || (viewer_kind()=="scene" && what=="scenes"));
    return button("vw-"+what+":"+std::to_string(s),before+"<span class='k'>"+key+"</span>"+mid+"<span class='v fit'>"+value+"</span>"+more,on,false,"vw-cell");
}
std::string viewer_weapon_tag(const json& w) {
    const auto& markers=w.value("markers",json::array());
    if(markers.empty())return {};
    const bool melee=std::find(markers.begin(),markers.end(),"格")!=markers.end();
    return "<span class='t"+std::string(melee?" melee":"")+"'>"+escape(markers.front().get<std::string>())+"</span>";
}
std::string viewer_weapon_cell(unsigned s) {
    const std::string what=s?"counter_weapon":"weapon",key=label(s?"viewer_counter_weapon":"viewer_weapon");
    if(s==1 && !viewer_counter)return viewer_cell(1,what,key,label("viewer_counter_off"));
    const auto* w=viewer_weapon(s);
    if(!w)return viewer_cell(s,what,key,"–");
    return viewer_cell(s,what,key,escape(w->value("display_name",w->value("name",std::string()))),"",viewer_weapon_tag(*w));
}
std::string viewer_rows(unsigned s) {
    const auto& u=viewer_unit(s);
    const auto& p=viewer_pilot(s);
    std::string body=viewer_cell(s,"unit",label("viewer_unit"),viewer_name(u),u.contains("model")?"<span class='m'>"+escape(u.at("model").get<std::string>())+"</span>":std::string());
    const auto skills=viewer_skills(p);
    body+=viewer_cell(s,"pilot",label("viewer_pilot"),viewer_name(p),skills.empty()?std::string():"<span class='m'>"+escape(skills)+"</span>");
    body+=viewer_weapon_cell(s);
    // The reactions this side can show, lit (Z's 屏 貫 防).
    std::string gates="<span class='gates'>";
    for(const auto [r,key]:std::initializer_list<std::pair<unsigned,const char*>>{{shield,"viewer_gate_short_shield"},{barrier,"viewer_gate_short_barrier"},{parry,"viewer_gate_short_parry"}})
        gates+="<span class='"+std::string(viewer_gate(s,r).empty()?"lit":"")+"'>"+label(key)+"</span>";
    body+=viewer_cell(s,"result",label(s?"viewer_row_hit":"viewer_row_counter_hit"),viewer_summary(s),gates+"</span>");
    const auto thumb=viewer_scene_img(viewer_scene[s],40);
    body+=viewer_cell(s,"scenes",label("viewer_row_scene"),viewer_scene_name(viewer_scene[s]),"<span class='m'>"+label(viewer_cat_keys[1+viewer_scene_cat(viewer_scene[s])])+"</span>","",
        thumb.empty()?std::string():"<span class='thumb'>"+thumb+"</span>");
    return body;
}
// The line at the bottom: what the open box's choice does, else the page's keys.
std::string viewer_help() {
    const auto kind=viewer_kind();
    const unsigned s=viewer_pick_side();
    if(viewer_picker.empty())return label("viewer_hint");
    if(kind=="unit") {
        const auto pilot=viewer_name(viewer_pilot(s));
        return viewer_side[s].pilot==viewer_saved.side[s].pilot?viewer_fill(label("viewer_keep_pilot"),pilot):viewer_fill(label("viewer_help_unit_default"),pilot);
    }
    if(kind=="pilot")return viewer_fill(label("viewer_help_pilot"),viewer_name(viewer_unit(s)));
    if(kind=="weapon") {
        const auto* w=viewer_weapon(s);
        return label(w && (w->value("flags",0u)&0x08)?"viewer_help_weapon_parry":"viewer_help_weapon");
    }
    if(kind=="scene")return label("viewer_scene_note");
    if(kind=="song")return label(battle_viewer::listened()>=0?"viewer_listen_note":"viewer_help_song");
    return label(std::string("viewer_note_")+viewer_reaction_keys[viewer_reaction(s)]);
}
// A row of a box's list.
std::string viewer_li(int value,const std::string& cls,const std::string& icon,const std::string& text,const std::string& sub,const std::string& badge,bool on) {
    return button("vw-pick:"+std::to_string(value),(icon.empty()?std::string():"<span class='ico'>"+icon+"</span>")+"<span class='n fit'>"+text+"</span>"+
        (sub.empty()?std::string():"<span class='s fit'>"+sub+"</span>")+(badge.empty()?std::string():"<span class='badge'>"+badge+"</span>"),on,false,"vw-li"+cls);
}
std::string viewer_icon(const json& e,float box) {
    const auto art=library_art(e,box);
    return art.empty()?"<span class='ph'>"+escape(viewer_initial(e.value("name",std::string())))+"</span>":art;
}
// The box a row opens, over the other side (the song's over the defender's).
std::string viewer_popup() {
    const auto kind=viewer_kind();
    const unsigned s=viewer_pick_side();
    const bool song=kind=="song",left=song || s==0;
    std::string title,extra,pager,list;
    if(kind=="unit") {
        const auto& works=viewer_works();
        const auto& work=works[size_t(viewer_page)%works.size()];
        title=label("viewer_choose_unit");
        // The work, on a row of its own under the title (Z's).
        pager="<div class='vw-pager-row'>"+button("vw-page:-1","◀",false,false,"vw-pager")+"<span class='page fit'>"+escape(work.name)+"</span><span class='no'>"+
            std::to_string(size_t(viewer_page)%works.size()+1)+" / "+std::to_string(works.size())+"</span>"+button("vw-page:1","▶",false,false,"vw-pager")+"</div>";
        for(const unsigned i:work.units) {
            const auto& e=viewer_units()[i];
            list+=viewer_li(int(i),"",viewer_icon(e,30),viewer_name(e),e.contains("model")?escape(e.at("model").get<std::string>()):std::string(),
                i==viewer_saved.side[s].unit?label("viewer_badge_now"):std::string(),i==viewer_side[s].unit);
        }
    } else if(kind=="pilot") {
        const auto* crew=viewer_crew(viewer_unit(s).value("id",0u));
        const size_t count=crew?crew->actors.size():0;
        title=viewer_fill(label("viewer_choose_pilot_crew"),viewer_name(viewer_unit(s)),std::to_string(count));
        for(size_t n=0;n<count;++n) {
            const int i=viewer_index(viewer_pilots(),"actor",crew->actors[n]);
            if(i<0)continue;
            const auto& e=viewer_pilots()[size_t(i)];
            const bool now=unsigned(i)==viewer_saved.side[s].pilot;
            list+=viewer_li(i,"",viewer_icon(e,30),viewer_name(e),escape(viewer_skills(e)),now?label("viewer_badge_now"):crew->actors[n]==crew->pilot?label("viewer_badge_default"):std::string(),unsigned(i)==viewer_side[s].pilot);
        }
    } else if(kind=="weapon") {
        title=label(s?"viewer_choose_counter_weapon":"viewer_choose_weapon");
        const auto weapons=viewer_weapons(s);
        for(size_t i=0;i<weapons.size();++i) {
            const auto& w=*weapons[i];
            std::string sub;
            for(const auto& m:w.value("markers",json::array()))if(m!="格" && m!="射")sub+=escape(m.get<std::string>())+" ";
            if(w.contains("unlock"))sub+=label("library_unlock")+" ";
            if(w.value("combo",false))sub+=label("library_combo")+" ";
            sub+=std::to_string(w.value("power",0u));
            list+=viewer_li(int(i),"","",viewer_weapon_tag(w)+" "+escape(w.value("display_name",w.value("name",std::string()))),sub,
                i==viewer_saved.side[s].weapon?label("viewer_badge_now"):std::string(),i==viewer_side[s].weapon);
        }
    } else if(kind=="scene") {
        title=label("viewer_choose_scene");
        const unsigned count=unsigned(battle_viewer::scenes().size());
        for(unsigned i=0;i<count;++i) {
            if(viewer_cat && viewer_scene_cat(i)!=viewer_cat-1)continue;
            const auto thumb=viewer_scene_img(i,62);
            list+=viewer_li(int(i)," scene",thumb.empty()?"<span class='ph'>"+viewer_initial(viewer_scene_name(i))+"</span>":thumb,viewer_scene_name(i),label(viewer_cat_keys[1+viewer_scene_cat(i)]),
                i==viewer_saved.scene[s]?label("viewer_badge_now"):std::string(),i==viewer_scene[s]);
        }
    } else if(song) {
        title=label("viewer_choose_song");
        const bool listening=battle_viewer::listened()>=0;
        extra=button("vw-listen","<span>"+std::string(listening?"■ ":"▶ ")+label(listening?"viewer_listen_stop":"viewer_listen")+"</span>",listening,false,"vw-listen");
        const auto songs=battle_viewer::songs();
        list+=viewer_li(-1," auto","","<span class='t melee'>"+label("viewer_song_attacker")+"</span> "+viewer_attacker_song(),"",std::string(),viewer_song<0);
        for(const auto& group:viewer_song_groups()) {
            list+="<div class='vw-song-group'>"+escape(group.name)+"</div>";
            for(const size_t i:group.rows) {
                char no[8];std::snprintf(no,sizeof no,"%02zu",i+1);
                list+=viewer_li(int(i)," song","",std::string(no)+"　"+escape(songs[i].value("text",std::string())),"",int(i)==viewer_saved.song?label("viewer_badge_now"):std::string(),int(i)==viewer_song);
            }
        }
    } else title=label(s?"viewer_choose_result_def":"viewer_choose_result_att");
    std::string body="<div id='vw-pop' class='vw-pop "+std::string(left?"left":"right")+"' style='width:"+std::to_string(viewer_col_width+2)+"dp;'><div class='vw-pop-head'>"
        "<div class='role"+std::string(song || s==1?" def":"")+"'>"+(song?std::string("BGM"):label(s?"viewer_defender":"viewer_attacker"))+"</div><div class='ttl fit'>"+title+"</div><div class='gap'></div>"+extra+"</div>"+pager;
    if(kind=="scene") {
        body+="<div class='vw-cats'>";
        for(unsigned c=0;c<4;++c)body+=button("vw-cat:"+std::to_string(c),label(viewer_cat_keys[c]),c==viewer_cat);
        body+="</div>";
    }
    if(kind=="result") {
        // Z's two lists: what happens, then how much.
        const unsigned t=s;
        const bool live=t==1 || viewer_counter,still=viewer_unhurt(t);
        body+="<div class='vw-resbox'><div class='col'><div class='h'>"+label("viewer_res_title")+"</div>";
        if(t==0)body+="<div class='seg'>"+button("vw-counter:0",label("viewer_counter_off"),!viewer_counter)+button("vw-counter:1",label("viewer_counter_on"),viewer_counter)+"</div>";
        for(unsigned r=0;r<reaction_count;++r) {
            const auto gate=viewer_gate(t,r);
            body+=button("vw-react:"+std::to_string(t)+":"+std::to_string(r),"<span>"+label(std::string("viewer_res_")+viewer_reaction_keys[r])+"</span><span class='g'></span>"+
                (gate.empty()?std::string():"<span class='s'>"+label(gate)+"</span>"),live && r==viewer_reaction(t),!live || !gate.empty(),"vw-opt");
        }
        body+="</div><div class='col dmg'><div class='h'>"+label("viewer_dmg_title")+"</div>";
        const auto& h=viewer_hit[t];
        const auto preset=[&](unsigned k,const std::string& text,const std::string& sub,bool on){
            return button("vw-preset:"+std::to_string(t)+":"+std::to_string(k),"<span>"+text+"</span><span class='g'></span><span class='s'>"+sub+"</span>",on && !still,still,"vw-opt");
        };
        body+=preset(0,label("viewer_rest"),label("viewer_default"),h.damage<0 && !h.destroy);
        body+=preset(1,label("viewer_dmg_big"),viewer_fill(label("viewer_dmg_left"),"25"),!h.destroy && h.damage==viewer_preset(t,1));
        body+=preset(2,label("viewer_dmg_mid"),viewer_fill(label("viewer_dmg_left"),"50"),!h.destroy && h.damage==viewer_preset(t,2));
        body+=preset(3,label("viewer_dmg_small"),viewer_fill(label("viewer_dmg_left"),"75"),!h.destroy && h.damage==viewer_preset(t,3));
        body+=preset(4,label("viewer_destroy"),"",h.destroy);
        const unsigned damage=viewer_damage(t);
        const auto step=[&](int by){return button("vw-dmg:"+std::to_string(t)+":"+std::to_string(by),(by>0?"+":"−")+std::to_string(std::abs(by)),false,still);};
        body+="<div class='gap'></div><div class='dmgv"+std::string(damage>=viewer_hp(t)?" down":"")+"'>"+(still?std::string("–"):std::to_string(damage))+"</div>"
            "<div class='steps'>"+step(-1000)+step(-100)+step(100)+step(1000)+"</div></div></div>";
    } else body+="<div id='vw-list' class='vw-plist'>"+list+"</div>";
    body+="<div class='vw-pop-foot'><div class='set-hint fit'>"+label(song?"viewer_song_hint":kind=="unit" || kind=="scene"?"viewer_pop_hint_page":"viewer_pop_hint")+"</div>"+button("vw-cancel",label("viewer_back"))+button("vw-confirm",label("viewer_confirm"),false,false,"vw-start")+"</div></div>";
    return body;
}
std::string viewer_panel() {
    // The panel in dp (96 % x 94 % of the window, at most 1180 x 760): the columns share its
    // width, and the cards take the height the rows and bars leave.
    const float panel_h=std::min(760.f,float(pixels_h)/ui_density*0.94f),panel_w=std::min(1180.f,float(pixels_w)/ui_density*0.96f);
    viewer_card_height=std::clamp(panel_h-318.f,130.f,420.f);
    viewer_col_width=(panel_w-28-24)/2;
    std::string song=viewer_song>=0 && viewer_song<int(battle_viewer::songs().size())?escape(battle_viewer::songs()[size_t(viewer_song)].value("text",std::string())):std::string();
    const bool attacker_song=song.empty();
    if(attacker_song)song=viewer_attacker_song();
    std::string body="<div class='set-shade solid'><div class='set-panel vw-panel'><div class='vw-top'><div class='vw-title'>"+label("viewer_title")+"</div><div class='vw-sub'>BATTLE VIEWER</div>"
        "<div class='vw-roles'><span class='r def'>"+label("viewer_defender")+"</span>"+button("vw-swap",label("viewer_swap"),false,false,"vw-swap")+"<span class='r att'>"+label("viewer_attacker")+"</span></div></div>"
        "<div id='set-body' class='vw-body'><div class='vw-cols'><div class='vw-col'>"+viewer_card(1)+viewer_rows(1)+"</div><div class='vw-colgap'></div><div class='vw-col'>"+viewer_card(0)+viewer_rows(0)+"</div></div>"+
        button("vw-songs","<span class='k'>BGM</span><span class='t'>『"+song+"』</span>"+(attacker_song?"<span class='k'>"+label("viewer_song_attacker")+"</span>":std::string()),viewer_kind()=="song",false,"vw-bgm")+
        "</div><div class='set-foot'><div class='vw-help fit'>"+viewer_help()+"</div>"+button("settings-close",label("settings_close"))+button("vw-start",label("viewer_start"),false,false,"vw-start")+"</div>";
    if(!viewer_picker.empty())body+=viewer_popup();
    return body+"</div></div>";
}
void viewer_listen(bool on){battle_viewer::listen_song(on?viewer_song_number(viewer_song):-1);}
// The row a box belongs to, focused again when it closes.
std::string viewer_row_id() {
    const auto kind=viewer_kind();
    const auto side=std::to_string(viewer_pick_side());
    if(kind=="song")return "vw-songs";
    if(kind=="scene")return "vw-scenes:"+side;
    if(kind=="weapon")return side=="1"?"vw-counter_weapon:1":"vw-weapon:0";
    return "vw-"+kind+":"+side;
}
void viewer_close_picker() {
    viewer_focus=viewer_row_id();
    battle_viewer::listen_song(-1);
    viewer_picker.clear();
}
// 戻る: the page as the box found it.
void viewer_cancel_picker() {
    viewer_side=viewer_saved.side;viewer_hit=viewer_saved.hit;viewer_counter=viewer_saved.counter;viewer_scene=viewer_saved.scene;viewer_song=viewer_saved.song;
    viewer_close_picker();
}
void viewer_open_picker(const std::string& kind,unsigned s) {
    if(!viewer_picker.empty())viewer_close_picker();   // another row: the open box's choice stays
    viewer_saved={viewer_side,viewer_hit,viewer_counter,viewer_scene,viewer_song};
    viewer_picker=kind+":"+std::to_string(s);
    viewer_page=viewer_work_of(viewer_side[s].unit);viewer_cat=0;
    if(kind=="result")viewer_focus="vw-react:"+std::to_string(s)+":"+std::to_string(viewer_reaction(s));
    else viewer_focus="vw-pick:"+std::to_string(kind=="unit"?int(viewer_side[s].unit):kind=="pilot"?int(viewer_side[s].pilot):kind=="weapon"?int(viewer_side[s].weapon):
                                                kind=="scene"?int(viewer_scene[s]):viewer_song);
}
// A list row reached or tapped: the page shows it at once. Tapping (or A on) the row
// already shown keeps it and closes the box.
void viewer_pick(int i,bool confirm) {
    const auto kind=viewer_kind();
    const unsigned s=viewer_pick_side();
    viewer_focus="vw-pick:"+std::to_string(i);
    const int now=kind=="unit"?int(viewer_side[s].unit):kind=="pilot"?int(viewer_side[s].pilot):kind=="weapon"?int(viewer_side[s].weapon):kind=="scene"?int(viewer_scene[s]):viewer_song;
    if(i==now){if(confirm)viewer_close_picker();return;}
    if(kind=="unit") {
        // From what the box found: its pilot if they may fly it, else the unit's default.
        viewer_side[s].unit=unsigned(i);viewer_side[s].pilot=viewer_saved.side[s].pilot;viewer_fit_pilot(s);
        if(viewer_side[s].unit==viewer_saved.side[s].unit){viewer_side[s].weapon=viewer_saved.side[s].weapon;viewer_hit=viewer_saved.hit;}
        else{viewer_default_weapon(s);viewer_hit={};}
    } else if(kind=="pilot")viewer_side[s].pilot=unsigned(i);
    else if(kind=="weapon")viewer_side[s].weapon=unsigned(i);
    else if(kind=="scene")viewer_scene[s]=unsigned(i);
    else if(kind=="song"){viewer_song=i;if(battle_viewer::listened()>=0)viewer_listen(true);}   // listening follows
}
// The unit box's next or previous work, its first unit shown.
void viewer_turn_page(int step) {
    const auto& works=viewer_works();
    viewer_page=(viewer_page+int(works.size())+step)%int(works.size());
    viewer_pick(int(works[size_t(viewer_page)].units.front()),false);
}
// The scene box's category: the scene stays if it is in it, else its first is shown.
void viewer_set_cat(unsigned cat) {
    viewer_cat=cat%4;
    const unsigned s=viewer_pick_side(),count=unsigned(battle_viewer::scenes().size());
    if(!viewer_cat || viewer_scene_cat(viewer_scene[s])==viewer_cat-1){viewer_focus="vw-pick:"+std::to_string(viewer_scene[s]);return;}
    for(unsigned i=0;i<count;++i)if(viewer_scene_cat(i)==viewer_cat-1){viewer_pick(int(i),false);return;}
}
void viewer_choose(const std::string& id) {
    const auto arg=[&](size_t from){return std::stoi(id.substr(from));};
    viewer_focus=id;
    for(const char* kind:{"unit","pilot","weapon","counter_weapon","result","scenes"})
        if(const std::string prefix=std::string("vw-")+kind+":";id.starts_with(prefix)) {
            const std::string_view k=kind;
            if(k=="counter_weapon" && !viewer_counter){viewer_counter=true;return;}
            viewer_open_picker(k=="counter_weapon"?"weapon":k=="scenes"?"scene":kind,unsigned(arg(prefix.size())));
            return;
        }
    if(id=="vw-songs"){viewer_open_picker("song",0);return;}
    if(id.starts_with("vw-pick:") && !viewer_picker.empty()){viewer_pick(arg(8),true);return;}
    if(id=="vw-confirm"){viewer_close_picker();return;}
    if(id=="vw-cancel"){viewer_cancel_picker();return;}
    if(id.starts_with("vw-page:")){viewer_turn_page(arg(8));return;}
    if(id.starts_with("vw-cat:")){viewer_set_cat(unsigned(arg(7)));return;}
    if(id=="vw-listen") {   // C-left keeps the focus on the song
        if(auto* focus=context->GetFocusElement();focus && !focus->GetId().empty())viewer_focus=focus->GetId();
        viewer_listen(battle_viewer::listened()<0);
        return;
    }
    if(id=="vw-swap"){std::swap(viewer_side[0],viewer_side[1]);std::swap(viewer_scene[0],viewer_scene[1]);viewer_default_weapon(0);viewer_default_weapon(1);viewer_hit={};return;}
    if(id.starts_with("vw-react:")){const unsigned t=id[9]=='1';viewer_hit[t]={unsigned(arg(11))%reaction_count,-1,false};return;}
    if(id.starts_with("vw-preset:")) {
        const unsigned t=id[10]=='1',k=unsigned(arg(12));
        viewer_hit[t].destroy=k==4;
        viewer_hit[t].damage=k==0 || k==4?-1:viewer_preset(t,k);
        return;
    }
    if(id.starts_with("vw-dmg:")) {
        const unsigned t=id[7]=='1';
        viewer_hit[t].damage=std::clamp(int(viewer_damage(t))+arg(9),0,int(viewer_hp(t))-1);viewer_hit[t].destroy=false;
        return;
    }
    if(id.starts_with("vw-counter:")){viewer_counter=id.back()=='1';return;}
    if(id=="vw-start" && viewer_title() && !battle_viewer::busy()) {
        try{battle_viewer::request({{"choice",viewer_choice()}});}
        catch(const std::exception& error){notices::post("viewer",error.what());return;}
        physical_held=held();viewer_open=false;settings_open=false;
    }
}
std::string viewer_stamp() {
    std::string stamp=viewer_picker+"|"+std::to_string(viewer_page)+","+std::to_string(viewer_cat)+","+std::to_string(battle_viewer::listened()>=0)+"|"+std::to_string(viewer_counter)+"|"+
        std::to_string(viewer_scene[0])+","+std::to_string(viewer_scene[1])+"|"+std::to_string(viewer_song);
    for(const auto& s:viewer_side)stamp+="|"+std::to_string(s.unit)+","+std::to_string(s.pilot)+","+std::to_string(s.weapon);
    for(const auto& h:viewer_hit)stamp+="|"+std::to_string(h.reaction)+","+std::to_string(h.damage)+","+std::to_string(h.destroy);
    return stamp+"|"+std::to_string(battle_viewer::songs().size());
}
// Keys and the controller: the nearest button that way (in the open box, else on the
// page); left and right stay in the row. Reaching a list row shows it; in the unit box,
// left and right with nothing there turn the work.
void viewer_move(int dy,int dx) {
    if(!settings_doc)return;
    Rml::ElementList all;
    if(auto* pop=settings_doc->GetElementById("vw-pop"))pop->QuerySelectorAll(all,"button");
    else settings_doc->QuerySelectorAll(all,"button");
    std::erase_if(all,[](Rml::Element* b){return b->HasAttribute("disabled");});
    auto* focus=context->GetFocusElement();
    if(!focus || std::find(all.begin(),all.end(),focus)==all.end()) {
        if(!all.empty()){all.front()->Focus();all.front()->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));}
        return;
    }
    struct Box{float l,t,r,b;};
    const auto box=[](Rml::Element* e){
        const auto at=e->GetAbsoluteOffset(Rml::BoxArea::Border);const auto size=e->GetBox().GetSize(Rml::BoxArea::Border);
        return Box{at.x,at.y,at.x+size.x,at.y+size.y};
    };
    const auto from=box(focus);
    Rml::Element* best=nullptr;float score=1e9f;
    for(auto* e:all) {
        if(e==focus)continue;
        const auto b=box(e);
        float along,across;
        if(dx) {
            if(dx>0?(b.l+b.r)<=(from.l+from.r)+2:(b.l+b.r)>=(from.l+from.r)-2)continue;
            along=dx>0?b.l-from.r:from.l-b.r;
            across=std::max(0.f,std::max(b.t,from.t)-std::min(b.b,from.b));
            if(across>0)continue;   // left and right stay in the row
        } else {
            if(dy>0?(b.t+b.b)<=(from.t+from.b)+2:(b.t+b.b)>=(from.t+from.b)-2)continue;
            along=dy>0?b.t-from.b:from.t-b.b;
            across=std::max(0.f,std::max(b.l,from.l)-std::min(b.r,from.r));
        }
        const float value=std::max(along,0.f)+across*3;
        if(value<score){score=value;best=e;}
    }
    if(!best) {
        if(dx && viewer_kind()=="unit" && focus->GetId().starts_with("vw-pick:"))viewer_turn_page(dx);
        return;
    }
    best->Focus();best->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
    if(const auto id=best->GetId();id.starts_with("vw-pick:") && !viewer_picker.empty())viewer_pick(std::stoi(id.substr(8)),false);
}
void viewer_sync() {
    viewer_defaults();
    const auto stamp="viewer"+viewer_stamp()+localization::catalog().locale+std::to_string(hd_portraits())+std::to_string(pad_mode)+frame_stamp()+std::to_string(ui_density);
    if(settings_doc && stamp==settings_stamp){settings_doc->PullToFront();return;}
    auto* focused=context->GetFocusElement();
    const std::string focus_id=!viewer_focus.empty()?viewer_focus:settings_doc && focused && focused->GetOwnerDocument()==settings_doc?focused->GetId():std::string();
    // The open list keeps its scroll through the rebuild a choice makes.
    static std::string list_picker;
    float scroll=-1;
    if(auto* list=settings_doc?settings_doc->GetElementById("vw-list"):nullptr;list && list_picker==viewer_picker)scroll=list->GetScrollTop();
    list_picker=viewer_picker;
    document_close(settings_doc);settings_stamp=stamp;settings_built=-1;viewer_focus.clear();
    settings_doc=document(viewer_panel(),true,true);settings_doc->SetClass("modal",false);settings_doc->PullToFront();settings_doc->Focus();
    settings_doc->UpdateDocument();
    if(auto* list=settings_doc->GetElementById("vw-list");list && scroll>=0)list->SetScrollTop(scroll);
    auto* focus=focus_id.empty()?nullptr:settings_doc->GetElementById(focus_id);
    if(focus && focus->HasAttribute("disabled"))focus=nullptr;
    if(!focus && !pointer_mode) {
        focus=settings_doc->GetElementById(viewer_picker.empty()?"vw-start":"vw-confirm");
        if(auto* list=viewer_picker.empty()?nullptr:settings_doc->GetElementById("vw-list");list && list->GetNumChildren())
            for(int n=0;n<list->GetNumChildren();++n)if(list->GetChild(n)->GetTagName()=="button"){focus=list->GetChild(n);break;}
    }
    if(focus){focus->Focus();focus->ScrollIntoView(Rml::ScrollIntoViewOptions(scroll>=0?Rml::ScrollAlignment::Nearest:Rml::ScrollAlignment::Center));}
}
void settings_sync() {
    if(!settings_open){update_ask_open=false;mod_open=false;library_open=false;viewer_open=false;viewer_picker.clear();battle_viewer::listen_song(-1);document_close(settings_doc);settings_stamp.clear();settings_built=-1;return;}
    if(viewer_open){viewer_sync();return;}
    if(update_ask_open && !library_open && !mod_open) {
        const auto stamp="update-ask"+localization::catalog().locale+std::to_string(pad_mode)+frame_stamp()+std::to_string(ui_density);
        if(settings_doc && stamp==settings_stamp){settings_doc->PullToFront();return;}
        document_close(settings_doc);settings_stamp=stamp;settings_built=-1;settings_focus.clear();
        settings_doc=document(update_ask_panel(),true,true);settings_doc->SetClass("modal",false);settings_doc->PullToFront();settings_doc->Focus();
        settings_doc->UpdateDocument();
        if(!pointer_mode)settings_focus_first();
        return;
    }
    if(library_open) {
        const auto stamp="library"+std::to_string(library_tab)+localization::catalog().locale+std::to_string(hd_portraits())+std::to_string(pad_mode)+frame_stamp()+std::to_string(ui_density);
        if(settings_doc && stamp==settings_stamp){settings_doc->PullToFront();return;}
        document_close(settings_doc);settings_stamp=stamp;settings_built=-1;settings_focus.clear();
        settings_doc=document(library_panel(),true,true);settings_doc->SetClass("modal",false);settings_doc->PullToFront();settings_doc->Focus();
        settings_doc->UpdateDocument();
        auto* tab=settings_doc->GetElementById("lib-tab:"+std::to_string(library_tab));
        if(library_tab_focus && tab)tab->Focus();
        else library_select(library_index[library_tab],!pointer_mode);
        library_tab_focus=false;
        return;
    }
    if(mod_open) {
        const auto stamp="mod"+std::to_string(mod_page)+localization::catalog().locale+window_stamp()+std::to_string(on_title())+
            std::to_string(presentation::image_mode.requested())+std::to_string(presentation::image_mode.enabled())+dialogue::text_summary().dump();
        if(settings_doc && stamp==settings_stamp){settings_doc->PullToFront();return;}
        auto* focused=context->GetFocusElement();
        const std::string focus_id=!settings_focus.empty()?settings_focus:settings_doc && focused && focused->GetOwnerDocument()==settings_doc?focused->GetId():std::string();
        document_close(settings_doc);settings_stamp=stamp;settings_built=-1;settings_focus.clear();
        settings_doc=document(mod_panel(),true,true);settings_doc->SetClass("modal",false);settings_doc->PullToFront();settings_doc->Focus();
        settings_doc->UpdateDocument();
        auto* focus=focus_id.empty() || focus_id=="first"?nullptr:settings_doc->GetElementById(focus_id);
        if(focus){focus->Focus();focus->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));}
        else if(focus_id=="first" || !pointer_mode)settings_focus_first();
        return;
    }
    if(settings_built<0) {
        // Opening: the page the window was last left on.
        const auto saved=settings::settings_page();
        settings_page=0;
        for(unsigned i=0;i<std::size(settings_pages);++i)if(saved==settings_pages[i])settings_page=i;
    }
    const auto stamp=localization::catalog().locale+std::to_string(rules::active_fixes())+std::to_string(presentation::image_mode.requested())+settings::battle_ui_name(settings::battle_ui())+settings::ui_size_name(settings::ui_size())+std::to_string(settings::wide_picture())+std::to_string(settings::native_intermission_ui())+
        std::to_string(settings::native_name_entry_ui())+std::to_string(settings::native_title_ui())+std::to_string(settings::show_fps())+std::to_string(settings::dialogue_hints_always())+std::to_string(settings_page)+
        std::to_string(presentation::image_mode.enabled())+std::to_string(settings::owns_input())+std::to_string(settings::failed())+window_stamp()+
        // The Controls page: bindings, a capture waiting, the controller and its icons.
        std::to_string(input::live_bindings().revision())+capture_prompt()+srw64_pad_name()+std::to_string(int(pad_family()))+
        // The セーブ page: its choices, the import folder as last read and the last result.
        [&]{const auto c=save_store::settings();return std::to_string(c.autosave)+std::to_string(c.intermission)+std::to_string(c.turn);}()+
        save_candidates.dump()+save_message+save_force+report_message+info_message+
        // The General page's bezel and filter rows and their picker.
        (settings_pages[settings_page]==std::string("general")?look_stamp():std::string())+
        // The Cheats page: its switches, the levels row and the pilots the menu lists.
        (settings_pages[settings_page]==std::string("cheats")?cheats_stamp():std::string())+
        // The About page: the update check as it goes.
        (settings_pages[settings_page]==std::string("about")?std::to_string(update::status("en").serial)+std::to_string(settings::debug_interface())+
            settings::debug_endpoint().address:std::string());
    if(settings_doc && stamp==settings_stamp){settings_doc->PullToFront();return;}
    // A rebuilt window keeps its focused control and scroll position, so a controller
    // does not lose its place; another page starts at its top.
    auto* focused=context->GetFocusElement();
    const std::string focus_id=!settings_focus.empty()?settings_focus:settings_doc && focused && focused->GetOwnerDocument()==settings_doc?focused->GetId():std::string();
    auto* old_body=settings_doc?settings_doc->GetElementById("set-body"):nullptr;
    const float scroll=old_body && settings_built==int(settings_page)?old_body->GetScrollTop():0;
    document_close(settings_doc);settings_stamp=stamp;settings_focus.clear();
    const std::string page=settings_pages[settings_page];
    std::string body="<div class='set-shade'><div class='set-panel'><h1>"+label("settings_title")+"</h1><div class='set-tabs nav'>";
    for(const std::string id:settings_pages)body+=button("settings-page:"+id,label("settings_page_"+id),id==page,false,"set-tab");
    body+="</div><div id='set-body' class='set-body'>";
    if(page=="general") {
        std::string locales;
        for(const auto& [locale,catalog]:localization::registered())locales+=button("locale:"+locale,escape(localization::display_name(locale)),locale==localization::catalog().locale,settings::owns_input());
        body+=settings_row("settings_language",locales);
        // The MOD manager, for a controller, which cannot reach the title's button.
        if(mod_entry_shown)body+=settings_row("mod_row",button("mod-open",label("mod_open")));
        body+=settings_row("library_row",button("library-open",label("library_open")));
        body+=settings_row("viewer_row",button("viewer-open",label("viewer_open")));
        body+=settings_choice("settings_images","images",{"original","hd"},presentation::image_mode.requested()?"hd":"original",!presentation::image_mode.enabled());
        body+=settings_choice("settings_aspect","aspect",{"wide","original"},settings::wide_picture()?"wide":"original");
        body+=look_rows();
        // A handheld or a phone plays full screen and has no window to size.
        if(!handheld()) {
            const auto window_state=window_menu_state();
            body+=settings_choice("settings_window","window",{"windowed","fullscreen"},window_state.fullscreen?"fullscreen":"windowed");
            std::string sizes;
            for(int n=1;n<=app_menu::kScales;++n)
                sizes+=button("window-size:"+std::to_string(n),std::to_string(n)+"×",n==window_state.scale,window_state.fullscreen || n>window_state.largest);
            body+=settings_row("settings_window_size",sizes);
        }
    } else if(page=="interface") {
        body+=settings_choice("settings_ui_size","ui-size",{"standard","large","largest"},settings::ui_size_name(settings::ui_size()));
        body+=settings_choice("settings_battle_ui","battle-ui",{"native","hd","original"},settings::battle_ui_name(settings::battle_ui()));
        body+=settings_choice("settings_intermission_ui","intermission-ui",{"native","original"},settings::native_intermission_ui()?"native":"original");
        body+=settings_choice("settings_name_entry_ui","name-entry-ui",{"native","original"},settings::native_name_entry_ui()?"native":"original");
        body+=settings_choice("settings_title_ui","title-ui",{"native","original"},settings::native_title_ui()?"native":"original");
        body+=settings_choice("settings_fps","fps",{"off","on"},settings::show_fps()?"on":"off");
        body+=settings_choice("settings_dialogue_hints","dialogue-hints",{"auto","always"},settings::dialogue_hints_always()?"always":"auto");
    } else if(page=="rules") {
        std::string presets;
        for(const auto& preset:rules::presets)presets+=button("preset:"+std::string(preset.key),label(std::string(preset.key)));
        body+="<div class='set-row nav'><div class='set-name'>"+label("settings_presets")+"</div><p>"+label("rules_note")+"</p><div class='set-actions'>"+presets+"</div></div>";
        for(auto group:{rules::Kind::correction,rules::Kind::difficulty}){
            body+="<h2>"+label(group==rules::Kind::correction?"rules_group_corrections":"rules_group_difficulty")+"</h2>";
            for(const auto& entry:rules::catalog)if(entry.kind==group)
                body+=button("rule:"+std::string(entry.id),"<span class='set-name'>"+label(rules::ui_key(entry.id))+"</span><span class='switch'><span></span></span>",rules::active_fixes()&entry.fix,false,"set-toggle nav");
        }
    } else if(page=="cheats") {
        body+=cheats_page();
    } else if(page=="saves") {
        body+=saves_page();
    } else if(page=="controls") {
        body+=controls_page();
    } else if(page=="feedback") {
        body+=feedback_page();
    } else {
        auto version=localization::catalog().ui("settings_about_version");
        if(const auto at=version.find("{version}");at!=std::string::npos)version.replace(at,9,SRW64_VERSION);
        // The HarmonyOS Sans licence asks for a visible notice wherever it is used.
        body+="<div class='set-about'><h2 class='app'>Marchwind64</h2><div>"+label("settings_about_tagline")+"</div><div>"+escape(version)+"</div></div>"+about_rows()+
            "<div class='set-about'><h2>"+label("font_credit")+"</h2><div>"+label("settings_about_font")+"</div>"
            // PromptFont asks for an attribution in the credits.
            "<h2>"+label("settings_about_prompts_title")+"</h2><div>"+label("settings_about_prompts")+"</div>"
            // librashader is MPL 2.0; its licence ships beside the app (package_macos.py).
            "<h2>librashader</h2><div>"+label("settings_about_librashader")+"</div></div>";
    }
    body+="</div>";
    if(settings::failed())body+="<div class='set-error'>"+label("settings_error")+"</div>";
    body+="<div class='set-foot'><div class='set-hint'>"+label("settings_hint")+"</div>"+button("settings-close",label("settings_close"))+"</div></div>"+capture_prompt()+"</div>";
    settings_doc=document(body,true,true);settings_doc->SetClass("modal",false);settings_doc->PullToFront();settings_doc->Focus();
    settings_doc->UpdateDocument();
    if(scroll>0)if(auto* page_body=settings_doc->GetElementById("set-body"))page_body->SetScrollTop(scroll);
    settings_built=int(settings_page);
    // Keys and the controller always have a focused control; the mouse needs none.
    auto* focus=focus_id.empty() || focus_id=="first"?nullptr:settings_doc->GetElementById(focus_id);
    if(focus){focus->Focus();focus->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));}
    else if(focus_id=="first" || !pointer_mode)settings_focus_first();
}
// Turns the settings window to a page and remembers it for the next time it opens.
void settings_show(unsigned page,const std::string& focus) {
    if(page==settings_page)return;
    settings_page=page;settings_focus=focus;settings::set_settings_page(settings_pages[page]);
}
// The settings window on its About page (the application menu, the title's update hint).
void open_about(const std::string& focus) {
    const unsigned about=unsigned(std::size(settings_pages)-1);
    if(settings_open && !library_open && !mod_open && !viewer_open && !update_ask_open) {
        if(settings_page!=about)settings_show(about,focus);
        else settings_focus=focus;
        return;
    }
    settings::set_settings_page(settings_pages[about]);
    update_ask_open=false;library_open=false;mod_open=false;viewer_open=false;
    settings_open=true;settings_built=-1;settings_focus=focus;settings_release.hold();input.clear();
}
// L1/R1, Q/E and the page keys: the next or previous page, wrapping round. The focus
// stays on the tab bar if it was there, else moves to the new page's first setting.
void library_turn(int step) {
    auto* focus=context->GetFocusElement();
    library_tab_focus=focus && focus->IsClassSet("set-tab");
    library_tab=(library_tab+2+step)%2;
}
void settings_turn(int step) {
    if(viewer_open) {   // L/R: the unit box's work, the scene box's category
        if(viewer_kind()=="unit")viewer_turn_page(step);
        else if(viewer_kind()=="scene")viewer_set_cat(viewer_cat+4+unsigned(step));
        return;
    }
    if(library_open){library_turn(step);return;}
    if(mod_open) {   // the MOD manager turns its own pages
        const unsigned count=std::size(mod_pages);
        mod_page=(mod_page+count+step)%count;
        auto* focus=context->GetFocusElement();
        settings_focus=focus && focus->IsClassSet("set-tab")?"mod-page:"+std::string(mod_pages[mod_page]):"first";
        return;
    }
    const unsigned count=std::size(settings_pages),page=(settings_page+count+step)%count;
    auto* focus=context->GetFocusElement();
    settings_show(page,focus && focus->IsClassSet("set-tab")?"settings-page:"+std::string(settings_pages[page]):"first");
}
// Keys and the controller on the settings window: up and down move between rows (the
// tab bar, then each setting), left and right between a row's choices, and on the tab
// bar left and right turn the page. A page with nothing to choose scrolls instead.
void settings_move(int dy,int dx) {
    if(viewer_open){viewer_move(dy,dx);return;}
    if(library_open){library_move(dy,dx);return;}
    if(!settings_doc)return;
    const auto rows=settings_rows();
    auto* focus=context->GetFocusElement();
    int at=-1;
    for(int i=0;i<int(rows.size()) && at<0;++i)for(auto* e=focus;e;e=e->GetParentNode())if(e==rows[i]){at=i;break;}
    if(at<0){settings_focus_first();return;}
    if(dx) {
        if(at==0){settings_turn(dx);return;}
        Rml::ElementList buttons;rows[at]->QuerySelectorAll(buttons,"button");
        std::erase_if(buttons,[](Rml::Element* b){return b->HasAttribute("disabled");});
        const auto current=std::find(buttons.begin(),buttons.end(),focus);
        if(current==buttons.end())return;
        const auto next=current-buttons.begin()+dx;
        if(next>=0 && next<int(buttons.size())){buttons[next]->Focus();buttons[next]->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));}
        return;
    }
    auto* page_body=settings_doc->GetElementById("set-body");
    if(rows.size()==1) {
        if(page_body)page_body->SetScrollTop(page_body->GetScrollTop()+dy*48*context->GetDensityIndependentPixelRatio());
        return;
    }
    const int next=std::clamp(at+dy,0,int(rows.size())-1);
    if(next==at)return;
    if(next==0 && page_body)page_body->SetScrollTop(0);
    settings_focus_row(rows[next],dy);
}
void link_sync() {
    const auto next=link_page::request();
    if(!next.visible){link_request=next;document_close(link_doc);link_stamp.clear();return;}
    if(next.serial!=link_request.serial){
        ticked=next.scheduled;link_focus=0;link_waiting=false;
        for(unsigned i=0;i<3;++i)if(!next.joined[i] && !next.scheduled[i]){link_focus=i;break;}
    }
    link_request=next;
    std::string stamp=std::to_string(next.serial)+localization::catalog().locale+std::to_string(link_focus)+std::to_string(link_waiting)+std::to_string(hd_portraits());
    for(bool value:ticked)stamp+=value?'1':'0';
    if(link_doc && link_stamp==stamp)return;
    document_close(link_doc);link_stamp=stamp;
    // The hints are one line each in Japanese (no spaces to break at, rmlui-cjk-wrap), so they shrink to fit.
    std::string body="<div class='page'><h1>"+label("link_title")+"</h1><p class='fit'>"+label("link_hint")+"</p><div class='row'>";
    const char* keys[]={"f91","goshogun","zambot"};
    for(unsigned i=0;i<3;++i){
        std::string content;
        for(const auto& path:hd_portraits()?next.portraits_hd[i]:next.portraits[i]){content+="<img src='"+image(path,dp_pixels(86))+"'/>";}
        content+="<h2 class='fit'>"+label(std::string("link_series_")+keys[i])+"</h2><p>"+label(std::string("link_lead_")+keys[i])+"</p><p>"+label(std::string("link_units_")+keys[i])+"</p><p>"+label(std::string("link_crew_")+keys[i])+"</p>";
        if(next.joined[i] || next.scheduled[i] || ticked[i])content+="<p>"+label(next.joined[i]?"link_joined":next.scheduled[i]?"link_scheduled":"link_ticked")+"</p>";
        body+=button("link:"+std::to_string(i),content,ticked[i] || i==link_focus,link_waiting || next.joined[i] || next.scheduled[i],"card");
    }
    body+="</div><p class='fit'>"+label("link_keyboard_hint")+"</p>"+button("link-back",label("link_back"),false,link_waiting)+button("link-next",label("link_confirm"),true,link_waiting)+"</div>";
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
    std::string body="<div id='battle-card-"+side+"' class='bp-side bp-banner "+side+"'><div class='bp-title'><span class='bp-name fit'>"+escape(c.at("unit_name").get<std::string>())+"</span>";
    body+="<span class='bp-tag"+std::string(first?" first":"")+"'>"+(first?label("battle_first"):label("battle_second")+" · "+response)+"</span></div>";
    body+="<div class='bp-weapon fit'>"+(armed?escape(c.at("weapon_name").get<std::string>()):label("battle_none"))+"</div>";
    for(const auto* kind:{"hp","en"}) {
        const int value=c.at(kind),maximum=c.at(std::string("max_")+kind);
        body+="<div class='battle-resource'><span class='resource-key'>"+std::string(kind==std::string("hp")?"HP":"EN")+"</span><div class='battle-track'><div style='width:"+std::to_string(maximum?std::clamp(value*100/maximum,0,100):0)+"%;'></div></div><span class='resource-value'>"+battle_number(value)+" / "+battle_number(maximum)+"</span></div>";
    }
    body+="<div class='weapon-cost'>";
    if(armed)body+=label("battle_cost")+" "+battle_number(c.at("en_cost"))+" · "+label("battle_ammo")+" "+(c.at("ammo").get<int>()<0?"—":battle_number(c.at("ammo")))+" · "+label("battle_hit_mod")+" "+battle_number(c.at("hit_modifier"),true)+" · "+label("battle_crit_mod")+" "+battle_number(c.at("critical_modifier"),true);
    else body+="—";
    return body+"</div></div>";
}
// Middle row: the unit art, cropped to its drawn pixels and sized to the column
// and to the space left between the banner and pilot rows (at most 6x).
std::string battle_unit(const json& c,bool left) {
    std::string body="<div class='bp-side battle-unit'>";
    if(const auto unit=c.value("unit_art",json::object());unit.contains("path")) {
        // The whole HD pose (8x, docs/design/unit-pose-hd.md) in HD mode, else the ROM pose.
        const auto path=portrait_path(unit);
        auto found=art_bounds.find(path);
        if(found==art_bounds.end()) {
            const auto file=presentation::load_rgba(path);
            int x0=file.width,y0=file.height,x1=0,y1=0;
            // Bounds of the visible pixels; faint alpha (upscaler residue) does not count.
            for(int y=0;y<file.height;++y)for(int x=0;x<file.width;++x)if(file.pixels[(size_t(y)*file.width+x)*4+3]>=16){x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x+1);y1=std::max(y1,y+1);}
            if(x1<=x0 || y1<=y0){x0=0;y0=0;x1=file.width;y1=file.height;}
            found=art_bounds.emplace(path,std::array<int,5>{x0,y0,x1-x0,y1-y0,file.width}).first;
        }
        const auto [x,y,w,h,file_w]=found->second;
        const float logical_w=page_area().w/ui_density,logical_h=page_area().h/ui_density;
        const float limit_w=logical_w*.31f-8,limit_h=std::clamp(logical_h-436,120.f,360.f);
        // At most 6x the ROM pixels, whichever file is drawn. The file loads unresampled: `rect` is in its pixels.
        const float rom_px=float(unit.value("width",96.f))/std::max(1.f,float(file_w));
        // Sized by the unit's size class (SS..LL from its record, 2026-09-26): an LL fills the
        // space, smaller classes take a fixed share of it, so a fighter reads smaller than a battleship.
        static constexpr float share[]={.30f,.52f,.70f,.84f,1.15f};  // LL breaks out of its box a little
        const float part=share[std::clamp(c.value("size",4),0,4)];
        const float scale=std::min({limit_w*part/w,limit_h*part/h,6.f*rom_px});
        // A first guess at the room; battle_fit_units sizes it again to the laid-out middle row.
        body+="<img class='"+std::string(left?"face-right":"face-left")+"' src='"+escape(image(path))+"' rect='"+std::to_string(x)+" "+std::to_string(y)+" "+std::to_string(w)+" "+std::to_string(h)+"' style='width:"+std::to_string(w*scale)+"dp;height:"+std::to_string(h*scale)+"dp;'"+
            " data-w='"+std::to_string(w)+"' data-h='"+std::to_string(h)+"' data-part='"+std::to_string(part)+"' data-max='"+std::to_string(6.f*rom_px)+"'/>";
    }
    return body+"</div>";
}
// The unit pictures, sized again once the page is laid out: as large as the middle row
// between the banners and the pilot panels allows (at most 360 dp, 6x the ROM pixels),
// by the same size-class share as battle_unit's first guess.
void battle_fit_units(Rml::ElementDocument* doc) {
    auto* mid=doc?doc->GetElementById("battle-mid"):nullptr;
    if(!mid)return;
    const float room=std::clamp(mid->GetBox().GetSize().y/ui_density-8,80.f,360.f),limit_w=page_area().w/ui_density*.31f-8;
    Rml::ElementList pictures;mid->QuerySelectorAll(pictures,".battle-unit img");
    for(auto* img:pictures) {
        const float w=img->GetAttribute<float>("data-w",0),h=img->GetAttribute<float>("data-h",0),part=img->GetAttribute<float>("data-part",1);
        if(w<=0 || h<=0)continue;
        const float scale=std::min({limit_w*part/w,room*part/h,img->GetAttribute<float>("data-max",6)});
        img->SetProperty("width",std::to_string(w*scale)+"dp");img->SetProperty("height",std::to_string(h*scale)+"dp");
    }
}
// What the two pilot panels keep room for, the same on both sides so they line up: the
// rows of spirit tags, the special defenses line (parry, shield, clone: kept when either
// unit has one, whatever the weapon, so changing weapons does not move the page) and the
// ability lines. Sized for this encounter, not for the most any pilot could need (user,
// 2026-09-28: the empty room crowded the unit pictures on a Steam Deck).
struct PilotRoom {int spirit_rows=1;bool defenses=false;int effect_lines=0;};
struct PilotDefenses {std::string line,conditions;};
PilotDefenses battle_defenses(const json& c) {
    const auto& d=c.at("defense");
    bool parry=false,shield=false,clone=false;std::string clone_name;
    for(const auto& e:c.value("defensive_effects",json::array())) {
        if(!e.contains("chance") || !battle_has(e))continue;
        const auto id=e.at("id").get<std::string>();
        (id=="parry"?parry:id=="shield"?shield:clone)=true;
        if(id!="parry" && id!="shield")clone_name=label("battle_skill_"+id); // the unit's own variant
    }
    PilotDefenses out;
    const auto add=[](std::string& line,const std::string& text){line+=(line.empty()?"":" · ")+text;};
    if(parry)add(out.line,label("battle_parry")+" "+battle_number(d.at("parry"))+"%");
    if(shield)add(out.line,label("battle_shield")+" "+battle_number(d.at("shield"))+"%");
    if(clone)add(out.line,clone_name+" "+battle_number(d.at("clone"))+"%");
    if(parry && d.at("parry_reason")=="uncuttable")add(out.conditions,label("battle_uncuttable"));
    if((parry && d.at("parry_reason")=="sure_hit") || (clone && d.at("clone_reason")=="sure_hit"))add(out.conditions,label("battle_sure_hit"));
    if(clone && d.at("clone_reason")=="morale_low")add(out.conditions,label("battle_clone_morale"));
    if(shield && d.at("shield_reason")=="barrier_first")add(out.conditions,label("battle_barrier_first"));
    return out;
}
float text_units(const std::string& text);
// Rows the spirit tags take in a pilot panel's info column (12 dp text with 12 dp of
// padding and border each, 3 dp apart), a little pessimistic so a second row is never
// hidden: the column is the panel (31% of the page) less its padding, the face and a gap.
int battle_spirit_rows(const json& c,bool narrow) {
    const float page=std::min(page_area().w/ui_density,1500.f)-36,width=(page*.31f-22-(narrow?68:100)-8)*.95f;
    int rows=1;float x=0;
    for(const auto& spirit:c.value("spirit_grid",json::array())) {
        const float w=text_units(spirit.at("name").get<std::string>())*12+12;
        if(x>0 && x+3+w>width){++rows;x=w;}else x+=(x>0?3:0)+w;
    }
    return rows;
}
int battle_effect_lines(const json& c) {
    const auto html=battle_effects(c);int lines=0;
    for(size_t at=html.find("<div class='battle-effect'>");at!=std::string::npos;at=html.find("<div class='battle-effect'>",at+1))++lines;
    return lines;
}
PilotRoom battle_pilot_room(const json& a,const json& b,bool narrow) {
    return {std::max({1,battle_spirit_rows(a,narrow),battle_spirit_rows(b,narrow)}),
            !battle_defenses(a).line.empty() || !battle_defenses(b).line.empty(),
            std::max(battle_effect_lines(a),battle_effect_lines(b))};
}
// Bottom panel: pilot identity, spirit state, special defenses and abilities.
std::string battle_pilot(const json& c,bool left,const PilotRoom& room) {
    const std::string side=left?"left":"right";
    std::string body="<div id='battle-pilot-"+side+"' class='bp-side bp-pilot "+side+"'><div class='bp-pilot-head'>";
    if(const auto face=c.value("portrait",json::object());face.contains("path"))body+="<img src='"+escape(image(portrait_path(face),dp_pixels(96)))+"'/>";
    body+="<div class='bp-pilot-info'><div class='bp-pilot-name'><span>"+escape(c.at("pilot_name").get<std::string>())+"</span><span class='level'>Lv "+battle_number(c.at("level"))+"</span></div>";
    body+="<div class='bp-stats'><div class='bp-stat'><span>"+label("battle_morale")+"</span><b>"+battle_number(c.at("morale"))+"</b></div>";
    body+="<div class='bp-stat'><span>SP</span><b class='"+std::string(c.at("sp").get<int>()<c.at("max_sp").get<int>()?"spent":"")+"'>"+battle_number(c.at("sp"))+" / "+battle_number(c.at("max_sp"))+"</b></div></div>"
        "<div class='battle-spirits' style='height:"+std::to_string(room.spirit_rows*23-2)+"dp;'>";
    const auto spirits=c.value("spirit_grid",json::array());
    if(spirits.empty())body+="<span class='muted'>"+label("battle_spirits_none")+"</span>";
    for(const auto& spirit:spirits) {
        const bool active=spirit.at("active");const unsigned id=spirit.at("id");
        body+="<span class='"+std::string(!active?"":id==5 || id==9 || id==18?"defensive":"active")+"'>"+escape(spirit.at("name").get<std::string>())+"</span>";
    }
    const auto defenses=battle_defenses(c);
    // Without a defenses line on either side only its rule stays, as the divider.
    body+="</div></div></div><div class='battle-defenses'"+std::string(room.defenses?"":" style='height:0;padding-top:0;'")+">"+defenses.line+"</div>"
        "<div class='battle-defense-note battle-defense-conditions fit'>"+defenses.conditions+"</div>";
    auto effects=battle_effects(c);
    // A line is 12 dp at 1.3 with 2 dp above and below: 20 each and 2 over, or the list scrolls.
    effects.replace(0,std::string("<div class='battle-effects'>").size(),"<div class='battle-effects' style='height:"+std::to_string(room.effect_lines?room.effect_lines*20+2:0)+"dp;'>");
    return body+effects+"</div>";
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
    // Four and five digits step the figure down so it stays inside its half of the panel
    // (on the Deck the panel also gives up some side padding); `fit` catches any width left over.
    const auto damage=[&](const json& c){const auto text=value(c,"damage");return "<div class='bp-damage fit"+std::string(text.size()>=5?" d5":text.size()==4?" d4":"")+"'>"+text+"</div>";};
    std::string body="<div class='bp-center'><div class='bp-clash'><div class='bp-clash-side left'>"+damage(enemy)+"<div class='bp-caption'>"+label("battle_damage_if_hit")+"</div></div>";
    body+="<div class='bp-arrow'><div>"+std::string(player_first?"◀━━":"━━▶")+"</div>"+label(player_first?"battle_first_player":"battle_first_enemy")+"</div>";
    body+="<div class='bp-clash-side right'>"+damage(player)+"<div class='bp-caption'>"+label("battle_damage_if_hit")+"</div></div></div>";
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
    const auto stamp=next.dump()+localization::catalog().locale+frame_stamp()+funds_editing;
    if(intermission_doc && intermission_stamp==stamp)return;
    document_close(intermission_doc);intermission_stamp=stamp;
    const float u=frame::scale(pixels_w,pixels_h),ox=(pixels_w-320*u)/2,oy=(pixels_h-240*u)/2;
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    const float line=std::max(1.f,float(int(u+0.5f)))/u;   // the border, one original pixel
    const auto panel=[&](float x0,float y0,float x1,float y1,const std::string& content,float font,const std::string& id="") {
        return "<div class='im-panel'"+(id.empty()?"":" id='"+id+"'")+" style='left:"+px(x0-line)+"; top:"+px(y0-line)+"; width:"+px(x1-x0+1+2*line)+"; height:"+px(y1-y0+1+2*line)+
            "; border-width:"+px(line)+"; font-size:"+px(font)+"; line-height:"+px(16)+";'>"+content+"</div>";
    };
    const auto fit=[&](const std::string& text,float width){return std::min(10.5f,width/std::max(1.f,text_units(text)));};
    const auto& items=next.at("items");const bool restricted=next.value("restricted",false),submenu=next.value("submenu",false);
    const unsigned cursor=next.value("cursor",0u);
    float item_font=10.5f;for(const auto& item:items)item_font=std::min(item_font,fit(item.get<std::string>(),67));
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
        float swap_font=10.5f;std::string options;
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
// 16 with the original 格／射 icon before the name and the P／B／MAP icons after it,
// then the selected weapon's 弾数, terrain letters, 必要気力, 消費EN, 必要技能 and
// クリティカル補正 in the boxed bands at y 160 and 180. Returns the panel body.
// The weapon markers in the symbol font, at U+E000 + their ROM glyph id.
const char* marker_glyph(const std::string& token) {
    return token=="格"?"\uE0F4":token=="射"?"\uE0F3":token=="P"?"\uE0F1":token=="B"?"\uE0F2":token=="MAP"?"\uE23F":nullptr;
}
std::string weapon_table(const json& next,const std::string& id_prefix,float u,float line,const std::string& page_text) {
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    // A plain name cell gives 4 of its units to a margin on its right, so a name that fills its
    // column (and shrinks to it) still parts from the next column, as the original's half-width text did.
    const auto span=[&](const std::string& text,float width,const std::string& cls="",float font=0,float gap=0){
        const bool name=cls.empty();
        return "<span class='fit "+cls+"' style='width:"+px(name?width-4:width)+";"+(name?" margin-right:"+px(4)+";":"")+(font?" font-size:"+px(font)+";":"")+(gap?" margin-left:"+px(gap)+";":"")+"'>"+escape(text)+"</span>";};
    const auto cell=[&](float x0,float x1,float y0,float y1,const std::string& content,const std::string& extra=""){
        return "<div style='position:absolute; left:"+px(x0-21)+"; top:"+px(y0-21)+"; width:"+px(x1-x0)+"; height:"+px(y1-y0)+"; border-width:"+px(line)+"; border-color:#3a78e0; box-sizing:border-box; line-height:"+px(y1-y0-2*line)+"; white-space:nowrap; overflow:hidden;"+extra+"'>"+content+"</div>";
    };
    const auto& W=next.at("weapon_labels");const auto wl=[&](const char* key){return W.value(key,std::string());};
    const auto number=[](const json& v){return v.is_number()?std::to_string(v.get<long long>()):std::string("---");};
    const auto signed_number=[](const json& v){const int n=v.is_number()?v.get<int>():0;return n>0?"+"+std::to_string(n):n<0?std::to_string(n):std::string("\u00b1 0");};
    const auto range=[](const json& r){const unsigned a=r.value("range_min",0u),b=r.value("range_max",0u);return a==b?std::to_string(a):std::to_string(a)+"\uff5e"+std::to_string(b);};
    // HD images: the symbol font's markers (U+E000 + ROM glyph id, drawn after the ROM
    // icons, build_symbol_font.py). Original images: the icons cut from the ROM font
    // (battle_assets.py) at their pixel size. A text badge only when neither is there.
    const json icons=W.value("icons",json::object());
    const bool vector_marks=!symbol_font.empty() && (hd_portraits() || icons.empty());
    const auto badge=[&](const std::string& token){
        if(const char* glyph=marker_glyph(token); glyph && vector_marks)
            return "<span class='im-mark"+std::string(token=="MAP"?" map":"")+"' style='font-size:"+px(13)+"; margin:0 "+px(1)+";'>"+glyph+"</span>";
        if(const auto found=icons.find(token);found!=icons.end() && found->contains("path")) {
            const float w=found->value("width",8.f),h=found->value("height",10.f);
            // Inline: the panel's portraits are blocks (.im-panel img).
            return "<img src='"+escape(image(found->at("path").get<std::string>(),int(w*u+.5f)))+"' style='display:inline-block; width:"+px(w)+"; height:"+px(h)+
                "; vertical-align:middle; margin:0 "+px(1)+";'/>";
        }
        const char* cls=token=="格"?"melee":token=="射"?"ranged":token=="P"?"post":token=="B"?"beam":"map";
        const float w=token=="MAP"?22.f:10.f;
        return "<span class='im-badge "+std::string(cls)+"' style='width:"+px(w)+"; height:"+px(11)+"; line-height:"+px(11)+"; font-size:"+px(token=="MAP"?6.f:7.f)+";'>"+escape(token)+"</span>";
    };
    const auto& rows=next.at("rows");const unsigned cursor=next.value("cursor",0u);
    std::string body=cell(21,68,21,44,"<div style='text-align:right; padding-right:"+px(4)+";'>"+escape(page_text)+"</div>")+
        cell(68,164,21,44,"<div style='text-align:center;' class='im-dim fit'>"+escape(wl("weapon"))+"</div>")+
        cell(164,220,21,44,"<div style='text-align:center;' class='im-dim fit'>"+escape(wl("power"))+"</div>")+
        cell(220,260,21,44,"<div style='text-align:center;' class='im-dim fit'>"+escape(wl("range"))+"</div>")+
        cell(260,299,21,44,"<div style='text-align:center;' class='im-dim fit'>"+escape(wl("hit"))+"</div>");
    std::string list;
    for(unsigned n=0;n<rows.size();++n) {
        const auto& r=rows[n];
        std::string name;
        const auto markers=r.value("markers",json::array());
        for(const auto& m:markers)if(m=="格" || m=="射")name+=badge(m.get<std::string>());
        // The name keeps to the room its marker icons leave and shrinks there (fit_lines), rather than the
        // column clipping the P／B／MAP icon.
        float room=146;for(const auto& m:markers)room-=m=="MAP"?26:15;
        name+="<span class='fit' data-fit-min='0.5' style='max-width:"+px(room)+"; vertical-align:middle;'>"+escape(r.value("display_name",r.value("name",std::string())))+"</span>";
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
    const auto letter=[&](float x0,float x1,const char* key,const std::string& v){return cell(x0,x1,160,180,"<div class='im-row' style='padding:0 "+px(3)+";'>"+span(wl(key),16,"im-dim",std::min(10.5f,15.f/std::max(1.f,text_units(wl(key)))))+span(v,x1-x0-24,"im-right")+"</div>");};
    body+=cell(21,100,160,180,"<div class='im-row' style='padding:0 "+px(3)+";'>"+span(wl("ammo"),30,"im-dim")+span(ammo,42,"im-right")+"</div>")+
        cell(100,140,160,180,"<div style='text-align:center; font-size:"+px(std::min(10.5f,36.f/std::max(1.f,text_units(wl("terrain")))))+";' class='im-dim'>"+escape(wl("terrain"))+"</div>")+
        letter(140,180,"air",terrain.substr(0,1))+letter(180,220,"land",terrain.substr(1,1))+letter(220,260,"sea",terrain.substr(2,1))+letter(260,299,"space",terrain.substr(3,1))+
        cell(21,164,180,220,"<div class='im-row' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(3)+";'>"+span(wl("morale"),60,"im-dim")+span(need(sel.value("morale",json()),unit.value("morale",-1)),76,low_morale?"im-right im-down":"im-right")+"</div>"
            "<div class='im-row' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(3)+";'>"+span(wl("en"),60,"im-dim")+span(need(sel.value("en",json()),unit.value("en",-1)),76,low_en?"im-right im-down":"im-right")+"</div>","line-height:"+px(19)+";")+
        cell(164,299,180,220,"<div class='im-row' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(3)+";'>"+span(wl("skill"),56,"im-dim")+span(sel.value("skill_name",std::string()),72)+"</div>"
            "<div class='im-row' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(3)+";'>"+span(wl("critical"),80,"im-dim",std::min(10.5f,78/std::max(1.f,text_units(wl("critical")))))+span(signed_number(sel.value("critical",json()))+"%",48,"im-right")+"</div>","line-height:"+px(19)+";");
    return body;
}

// The panel behind a unit's battle picture on the unit ability, upgrade and swap confirm
// pages is left clear: the game fills it in the panel's colour and draws its focus lines
// over that (host/focus_lines.hpp), as bright as the original's. The picture is drawn at
// the original's size (one pixel for one), so the lines show around it as they did.
constexpr const char* kLinesPanel="transparent";
float original_side(const json& owner,float most){
    const auto& art=owner.value("art",json::object());
    return std::min(most,std::max(art.value("width",96.f),art.value("height",96.f)));
}
void upgrade_sync() {
    const auto next=upgrade_page::state();upgrade_request=next;
    if(!next.value("visible",false)){document_close(upgrade_doc);upgrade_stamp.clear();return;}
    const auto stamp=next.dump()+localization::catalog().locale+frame_stamp()+funds_editing;
    if(upgrade_doc && upgrade_stamp==stamp)return;
    document_close(upgrade_doc);upgrade_stamp=stamp;
    const float u=frame::scale(pixels_w,pixels_h),ox=(pixels_w-320*u)/2,oy=(pixels_h-240*u)/2;
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    const float line=std::max(1.f,float(int(u+0.5f)))/u;
    const auto box=[&](float x0,float y0,float x1,float y1,const std::string& content,float font,const std::string& id="",const std::string& extra="") {
        return "<div class='im-panel'"+(id.empty()?"":" id='"+id+"'")+" style='left:"+px(x0-line)+"; top:"+px(y0-line)+"; width:"+px(x1-x0+1+2*line)+"; height:"+px(y1-y0+1+2*line)+
            "; border-width:"+px(line)+"; font-size:"+px(font)+"; line-height:"+px(16)+";"+extra+"'>"+content+"</div>";
    };
    const auto fit=[&](const std::string& text,float width){return std::min(10.5f,width/std::max(1.f,text_units(text)));};
    // The list rows and their labels share one face, a size under the panels around them.
    const float face=10.5f;const auto lfit=[&](const std::string& text,float width){return std::min(face,fit(text,width));};
    // A plain name cell gives 4 of its units to a margin on its right, so a name that fills its
    // column (and shrinks to it) still parts from the next column, as the original's half-width text did.
    const auto span=[&](const std::string& text,float width,const std::string& cls="",float font=0,float gap=0){
        const bool name=cls.empty();
        return "<span class='fit "+cls+"' style='width:"+px(name?width-4:width)+";"+(name?" margin-right:"+px(4)+";":"")+(font?" font-size:"+px(font)+";":"")+(gap?" margin-left:"+px(gap)+";":"")+"'>"+escape(text)+"</span>";};
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
            "<div class='im-row' style='position:absolute; left:0; top:"+px(153)+"; width:100%; box-sizing:border-box; height:"+px(22)+"; line-height:"+px(22)+"; padding:0 "+px(3)+"; border-top-width:"+px(line)+"; border-top-color:#3a78e0;'>"+
                span(label_of("mobility"),44,"im-dim")+span(number(sel.value("mobility",json())),30,"im-right")+span(label_of("armor"),40,"im-dim",0,22)+span(number(sel.value("armor",json())),40,"im-right")+span(label_of("limit"),40,"im-dim",0,22)+span(number(sel.value("limit",json())),30,"im-right")+"</div>"
            "<div class='im-row' style='position:absolute; left:0; top:"+px(176)+"; width:100%; box-sizing:border-box; height:"+px(22)+"; line-height:"+px(22)+"; padding:0 "+px(3)+"; border-top-width:"+px(line)+"; border-top-color:#3a78e0;'>"+
                span(label_of("pilot"),52,"im-dim")+span(pilot.empty()?"--------":pilot,100,"",0,6)+span(label_of("funds"),40,"im-dim",0,10)+funds_field("upgrade",next.value("funds",0u),px(60),px(10.5f),px(22))+"</div>",
            face,"upgrade-panel");
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
                "<div class='im-row' style='height:"+px(24)+"; line-height:"+px(24)+"; padding:0 "+px(3)+";'>"+span(w.value("display_name",w.value("name",std::string())),150,"",std::min(10.f,fit(w.value("display_name",w.value("name",std::string())),148)))+"<span class='im-gauge' style='width:"+px(120)+"; font-size:"+px(9)+";'>"+gauge+"</span></div>"
                "<div class='im-row' style='height:"+px(20)+"; line-height:"+px(20)+"; padding:0 "+px(3)+";'>"+span(wl("power"),44,"im-dim")+span(number(w.at("power")),40,"im-right")+span("▶",14,"im-dim")+span(number(w.value("preview",json())),40,"im-right")+
                    span(label_of("funds"),32,"im-dim",0,10)+funds_field("upgrade",next.value("funds",0u),px(56),px(11),px(20))+"</div>",10.f,"upgrade-weapon")+
                box(181,107,299,123,"<div class='im-row' style='height:"+px(15)+"; line-height:"+px(15)+"; padding:0 "+px(3)+";'>"+span(label_of("price"),32,"im-dim")+span(number(w.value("price",json())),76,"im-right")+"</div>",10.f,"upgrade-weapon-price");
            if(const std::string raised=cap_legend(w.value("original_cap",0u),w.value("cap",0u));!raised.empty())
                body+="<div class='im-dim' style='position:absolute; left:"+px(24)+"; top:"+px(110)+"; width:"+px(150)+"; font-size:"+px(7)+"; line-height:"+px(12)+";'>"+raised+"</div>";
            if(window=="confirm")
                body+=box(261,133,291,171,"<button id='upgrade-confirm' class='"+std::string(cursor==0?"on":"")+"' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(2)+";'>"+escape(label_of("yes"))+"</button><button id='upgrade-cancel' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(2)+";'>"+escape(label_of("no"))+"</button>",8.f,"upgrade-choice");
            else body+=box(85,61,235,83,"<button id='upgrade-dismiss' class='fit' style='height:"+px(22)+"; line-height:"+px(22)+"; text-align:center;'>"+escape(label_of(window=="poor"?"poor":"maxed"))+"</button>",fit(label_of(window=="poor"?"poor":"maxed"),146),"upgrade-message");
        } else if(window=="bonus") {
            // Layout 0x8C: 「X を最大まで改造したので、特別ボーナスとして Y が追加されます」.
            const auto& b=next.at("bonus");const auto& bl=next.at("bonus_labels");
            body+="<div class='im-shade'></div>"+box(29,77,291,179,"<button id='upgrade-dismiss' style='height:"+px(100)+"; padding:"+px(6)+" "+px(8)+"; line-height:"+px(18)+";'>"+
                escape(b.value("upgraded",std::string()))+"<br/>"+escape(bl[0].get<std::string>())+"<br/>"+escape(bl[1].get<std::string>())+"<br/>"+escape(b.value("unlocked",std::string()))+"<br/>"+escape(bl[2].get<std::string>())+"</button>",10.f,"upgrade-bonus");
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
                (raised.empty()?std::string():"<div class='im-dim' style='padding:0 "+px(4)+"; font-size:"+px(7)+"; line-height:"+px(12)+";'>"+raised+"</div>"),10.f,"upgrade-money");
        std::string art;
        if(unit.contains("art") && unit.at("art").contains("path")) {
            const float w=unit.at("art").value("width",96.f),h=unit.at("art").value("height",96.f),scale=std::min(1.f,std::min(118.f/w,118.f/h));
            art="<img src='"+escape(image(portrait_path(unit.at("art")),int(w*scale*u+.5f)))+"' style='width:"+px(w*scale)+"; height:"+px(h*scale)+"; margin:auto;'/>";
        }
        body+=box(176,8,302,132,art,10.f,"upgrade-art",std::string("display:flex; align-items:center; justify-content:center; background-color:")+kLinesPanel+";")+
            box(21,133,302,219,lines,10.5f,"upgrade-rows");
        const auto window=next.value("window",std::string());
        if(!window.empty()) {
            body+="<div class='im-shade'></div>";
            if(window=="confirm")
                body+=box(53,101,267,139,"<div style='padding:"+px(3)+" "+px(4)+";'>"+escape(label_of("ask"))+"</div>",10.f,"upgrade-window")+
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
    const auto stamp=next.dump()+localization::catalog().locale+frame_stamp();
    if(parts_doc && parts_stamp==stamp)return;
    document_close(parts_doc);parts_stamp=stamp;
    const float u=frame::scale(pixels_w,pixels_h),ox=(pixels_w-320*u)/2,oy=(pixels_h-240*u)/2;
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    const float line=std::max(1.f,float(int(u+0.5f)))/u;
    const auto box=[&](float x0,float y0,float x1,float y1,const std::string& content,float font,const std::string& id="",const std::string& extra="") {
        return "<div class='im-panel'"+(id.empty()?"":" id='"+id+"'")+" style='left:"+px(x0-line)+"; top:"+px(y0-line)+"; width:"+px(x1-x0+1+2*line)+"; height:"+px(y1-y0+1+2*line)+
            "; border-width:"+px(line)+"; font-size:"+px(font)+"; line-height:"+px(16)+";"+extra+"'>"+content+"</div>";
    };
    const auto fit=[&](const std::string& text,float width){return std::min(10.5f,width/std::max(1.f,text_units(text)));};
    // The list rows and their labels share one face, a size under the panels around them.
    const float face=10.5f;const auto lfit=[&](const std::string& text,float width){return std::min(face,fit(text,width));};
    // A plain name cell gives 4 of its units to a margin on its right, so a name that fills its
    // column (and shrinks to it) still parts from the next column, as the original's half-width text did.
    const auto span=[&](const std::string& text,float width,const std::string& cls="",float font=0,float gap=0){
        const bool name=cls.empty();
        return "<span class='fit "+cls+"' style='width:"+px(name?width-4:width)+";"+(name?" margin-right:"+px(4)+";":"")+(font?" font-size:"+px(font)+";":"")+(gap?" margin-left:"+px(gap)+";":"")+"'>"+escape(text)+"</span>";};
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
                span(r.value("name",std::string()),126)+span(dashes(r.value("pilot",std::string())),90)+span(label_of("level"),36,"im-dim",lfit(label_of("level"),34))+span(r.contains("level")?number(r.at("level")):std::string(),18,"im-right")+"</button>";
        }
        const auto& sel=rows.empty()?json::object():rows[std::min<unsigned>(cursor,rows.size()-1)];
        const auto page=std::to_string(next.value("page",0u)+1)+"/"+std::to_string(next.value("pages",1u));
        body+=box(21,21,299,219,
            "<div class='im-row' style='height:"+px(22)+"; line-height:"+px(22)+"; border-bottom-width:"+px(line)+"; border-bottom-color:#3a78e0;'>"+span(page,44,"im-right")+"<span style='width:"+px(2)+"; height:100%; border-left-width:"+px(line)+"; border-left-color:#3a78e0; margin-left:"+px(2)+";'></span><span style='width:"+px(228)+"; text-align:center;'>"+escape(label_of("title"))+"</span></div>"
            "<div id='parts-list' style='margin-top:"+px(3)+";'>"+list+"</div>"
            "<div style='position:absolute; left:0; top:"+px(157)+"; width:100%; height:"+px(42)+"; border-top-width:"+px(line)+"; border-top-color:#3a78e0;'>"
                "<span class='im-dim' style='position:absolute; left:"+px(3)+"; top:"+px(4)+"; width:"+px(88)+"; font-size:"+px(lfit(label_of("equipped"),86))+";'>"+escape(label_of("equipped"))+"</span>"+slot_grid(sel,4)+"</div>",
            face,"parts-panel");
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
        body+=box(21,21,163,43,"<div class='im-row' style='height:"+px(22)+"; line-height:"+px(22)+";'>"+span(page_text,44,"im-right")+"<span style='width:"+px(2)+"; height:100%; border-left-width:"+px(line)+"; border-left-color:#3a78e0; margin-left:"+px(2)+";'></span><span style='width:"+px(90)+"; text-align:center; font-size:"+px(fit(label_of("select_title"),88))+";'>"+escape(label_of("select_title"))+"</span></div>",10.f,"parts-title")+
            box(21,43,163,113,"<div id='parts-slots' style='margin-top:"+px(1)+";'>"+slots+"</div>",10.5f,"parts-slot-panel")+
            box(21,113,163,219,"<div style='margin-top:"+px(5)+";'>"+stats+"</div>",10.f,"parts-stats")+
            box(163,21,299,43,"<div style='padding:0 "+px(4)+"; line-height:"+px(22)+"; font-size:"+px(fit(unit.value("name",std::string()),128))+";'>"+escape(unit.value("name",std::string()))+"</div>",10.5f,"parts-unit")+
            box(163,43,299,160,arrows+"<div id='parts-inventory'>"+list+"</div>",10.f,"parts-inventory-panel")+
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
                "<span class='im-dim' style='position:absolute; left:"+px(3)+"; top:"+px(3)+"; width:"+px(88)+"; font-size:"+px(lfit(label_of("equipped"),86))+";'>"+escape(label_of("equipped"))+"</span>"+slot_grid(next.at("unit"),3)+"</div>"
            "<div id='parts-holders' style='margin-top:"+px(43)+";'>"+list+"</div>",face,"parts-panel");
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
    const auto stamp=next.dump()+localization::catalog().locale+frame_stamp()+std::to_string(hd_portraits());
    if(swap_doc && swap_stamp==stamp)return;
    document_close(swap_doc);swap_stamp=stamp;
    const float u=frame::scale(pixels_w,pixels_h),ox=(pixels_w-320*u)/2,oy=(pixels_h-240*u)/2;
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    const float line=std::max(1.f,float(int(u+0.5f)))/u;
    const auto box=[&](float x0,float y0,float x1,float y1,const std::string& content,float font,const std::string& id="",const std::string& extra="") {
        return "<div class='im-panel'"+(id.empty()?"":" id='"+id+"'")+" style='left:"+px(x0-line)+"; top:"+px(y0-line)+"; width:"+px(x1-x0+1+2*line)+"; height:"+px(y1-y0+1+2*line)+
            "; border-width:"+px(line)+"; font-size:"+px(font)+"; line-height:"+px(16)+";"+extra+"'>"+content+"</div>";
    };
    const auto fit=[&](const std::string& text,float width){return std::min(10.5f,width/std::max(1.f,text_units(text)));};
    // The list rows and their labels share one face, a size under the panels around them.
    const float face=10.5f;const auto lfit=[&](const std::string& text,float width){return std::min(face,fit(text,width));};
    // A plain name cell gives 4 of its units to a margin on its right, so a name that fills its
    // column (and shrinks to it) still parts from the next column, as the original's half-width text did.
    const auto span=[&](const std::string& text,float width,const std::string& cls="",float font=0,float gap=0){
        const bool name=cls.empty();
        return "<span class='fit "+cls+"' style='width:"+px(name?width-4:width)+";"+(name?" margin-right:"+px(4)+";":"")+(font?" font-size:"+px(font)+";":"")+(gap?" margin-left:"+px(gap)+";":"")+"'>"+escape(text)+"</span>";};
    const auto at=[&](float x,float y,const std::string& content,const std::string& cls="",float font=0,float width=0){return "<div class='"+cls+"' style='position:absolute; left:"+px(x)+"; top:"+px(y)+";"+(width?" width:"+px(width)+";":"")+(font?" font-size:"+px(font)+";":"")+" line-height:"+px(16)+"; white-space:nowrap;'>"+content+"</div>";};
    const auto& L=next.at("labels");const auto label_of=[&](const char* key){return L.value(key,std::string());};
    const auto dim=[&](const char* key){return "<span class='im-dim'>"+escape(label_of(key))+"</span>";};
    const auto number=[](const json& v){return v.is_number()?std::to_string(v.get<long long>()):std::string("--");};
    const auto dashes=[](const std::string& s){return s.empty()?std::string("--------"):s;};
    const auto hint=[&](const char* key){return "<div class='im-hint' style='left:0; top:"+px(224)+"; width:"+px(320)+"; font-size:"+px(6.5f)+";'>"+label(key)+"</div>";};
    const auto art_img=[&](const json& owner,float size){
        if(!owner.contains("art") || !owner.at("art").contains("path"))return std::string();
        const float w=owner.at("art").value("width",96.f),h=owner.at("art").value("height",96.f),scale=std::min(size/w,size/h);
        return "<img src='"+escape(image(portrait_path(owner.at("art")),int(w*scale*u+.5f)))+"' style='width:"+px(w*scale)+"; height:"+px(h*scale)+"; margin:auto;'/>";
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
                span(r.value("name",std::string()),70)+span(dashes(r.value("unit",std::string())),150)+span(label_of("level"),34,"im-dim",lfit(label_of("level"),32))+span(number(r.value("level",json())),16,"im-right")+"</button>";
        }
        const auto page=std::to_string(next.value("page",0u)+1)+"/"+std::to_string(next.value("pages",1u));
        const auto& sub=next.value("sub",json::object());
        const char* sub_key=screen=="pilots"?"sub":"fairy";
        body+=box(21,21,299,219,
            "<div class='im-row' style='height:"+px(22)+"; line-height:"+px(22)+"; border-bottom-width:"+px(line)+"; border-bottom-color:#3a78e0;'>"+span(page,44,"im-right")+"<span style='width:"+px(2)+"; height:100%; border-left-width:"+px(line)+"; border-left-color:#3a78e0; margin-left:"+px(2)+";'></span><span style='width:"+px(228)+"; text-align:center;'>"+escape(label_of("title"))+"</span></div>"
            "<div id='swap-list' style='margin-top:"+px(3)+";'>"+list+"</div>"
            "<div class='im-row' style='position:absolute; left:0; top:"+px(175)+"; width:100%; box-sizing:border-box; height:"+px(22)+"; line-height:"+px(22)+"; padding:0 "+px(3)+"; border-top-width:"+px(line)+"; border-top-color:#3a78e0;'>"+
                span(label_of(sub_key),40,"im-dim",lfit(label_of(sub_key),38))+span(dashes(sub.value("name",std::string())),150)+span(label_of("level"),34,"im-dim",lfit(label_of("level"),32))+span(sub.contains("level")?number(sub.at("level")):std::string("--"),16,"im-right")+"</div>",
            face,"swap-panel");
        body+=hint("swap_list_hint");
    } else if(screen=="targets" || screen=="fairy_targets") {
        const bool fairy=screen=="fairy_targets";
        const auto& rows=next.at("rows");const unsigned cursor=next.value("cursor",0u);const auto& p=next.at("pilot");const auto& sub=next.value("sub",json::object());
        std::string list;
        for(unsigned n=0;n<rows.size();++n) {
            const auto& r=rows[n];
            list+="<button id='swap:"+std::to_string(n)+"' class='im-row "+(n==cursor?"on":"")+"' style='height:"+px(16)+"; line-height:"+px(16)+"; padding:0 "+px(3)+";'>"+
                (fairy?span(r.value("name",std::string()),128)+span(dashes(r.value("unit",std::string())),96)+span(label_of("level"),34,"im-dim",lfit(label_of("level"),32))+span(number(r.value("level",json())),16,"im-right")
                      :span(r.value("name",std::string()),128)+span(dashes(r.value("pilot",std::string())),80)+span(label_of("hp"),24,"im-dim")+span(number(r.value("hp",json())),40,"im-right"))+"</button>";
        }
        const auto page=std::to_string(next.value("page",0u)+1)+"/"+std::to_string(next.value("pages",1u));
        body+=box(18,10,299,219,
            "<div style='position:absolute; left:0; top:0; width:"+px(96)+"; height:"+px(94)+"; display:flex; align-items:center; justify-content:center;'>"+art_img(p,88)+"</div>"+
            at(94,4,escape(page),"im-right",10,40)+at(190,4,"<span class='im-dim'>"+escape(label_of("title"))+"</span>","",11)+
            at(98,32,escape(p.value("full_name",std::string())),"",fit(p.value("full_name",std::string()),120),120)+at(230,32,dim("level"))+at(262,32,number(p.value("level",json())),"im-right",0,20)+
            at(98,52,dim(fairy?"fairy":"sub"))+at(134,52,dashes(sub.value("name",std::string())),"",0,96)+at(230,52,dim("level"))+at(262,52,sub.contains("level")?number(sub.at("level")):std::string("--"),"im-right",0,20)+
            "<div style='position:absolute; left:0; top:"+px(94)+"; width:100%; border-top-width:"+px(line)+"; border-top-color:#3a78e0;'></div>"
            "<div id='swap-list' style='position:absolute; left:0; top:"+px(95)+"; width:100%;'>"+list+"</div>",face,"swap-panel");
        if(fairy && next.value("mode",0u)) {
            const auto& target_row=rows.empty()?json::object():rows[std::min<unsigned>(cursor,rows.size()-1)];
            body+="<div class='im-shade'></div>"+box(53,101,267,139,"<div style='padding:"+px(3)+" "+px(4)+";'>"+escape(target_row.value("name",std::string()))+escape(label_of("board"))+"<br/>"+escape(label_of("ask"))+"</div>",10.f,"swap-window")+
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
        body+=box(18,10,110,101,"<div style='position:absolute; left:0; top:0; width:100%; height:"+px(72)+"; display:flex; align-items:center; justify-content:center;'>"+art_img(p,70)+"</div>"+at(0,74,escape(p.value("name",std::string())),"im-center",fit(p.value("name",std::string()),86),90),10.f,"swap-portrait")+
            box(111,10,175,35,"<div class='im-row' style='height:"+px(24)+"; line-height:"+px(24)+"; padding:0 "+px(3)+";'>"+span(label_of("level"),34,"im-dim")+span(number(p.value("level",json())),22,"im-right")+"</div>",10.f,"swap-level")+
            box(111,36,175,58,"<div class='im-row' style='height:"+px(21)+"; line-height:"+px(21)+"; padding:0 "+px(3)+";'>"+span(label_of("hp"),20,"im-dim")+span(number(to.value("hp",json())),36,"im-right")+"</div>",10.f,"swap-hp")+
            box(18,102,175,122,"<div class='im-row' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(3)+";'>"+span(label_of("sub"),34,"im-dim")+span(dashes(sub.value("name",std::string())),112)+"</div>",10.f,"swap-fairy")+
            box(18,123,175,162,"<div style='padding:"+px(3)+" "+px(3)+"; line-height:"+px(16)+";' class='im-dim'>"+escape(label_of("current"))+"<br/>"+escape(label_of("after"))+"</div>",fit(label_of("after"),150),"swap-notes")+
            box(18,163,175,219,"<div class='im-row' style='height:"+px(17)+"; line-height:"+px(17)+"; padding:0 "+px(3)+";'>"+span(label_of("limit"),34,"im-dim")+span(number(to.value("limit",json())),40,"im-right")+"</div>"
                "<div class='im-row' style='height:"+px(17)+"; line-height:"+px(17)+"; padding:0 "+px(3)+";'>"+span(label_of("evade"),34,"im-dim")+span(plus(evade),60,evade.value("over",false)?"im-right im-down":"im-right")+span("("+std::to_string(evade.value("now",0))+")",50,"im-right im-dim")+"</div>"
                "<div class='im-row' style='height:"+px(17)+"; line-height:"+px(17)+"; padding:0 "+px(3)+";'>"+span(label_of("hit"),34,"im-dim")+span(plus(hit),60,hit.value("over",false)?"im-right im-down":"im-right")+span("("+std::to_string(hit.value("now",0))+")",50,"im-right im-dim")+"</div>",10.f,"swap-stats")+
            box(176,8,302,132,"<div style='position:absolute; left:0; top:0; width:100%; height:"+px(108)+"; display:flex; align-items:center; justify-content:center;'>"+art_img(to,original_side(to,104))+"</div>"+at(0,108,escape(to.value("name",std::string())),"im-center",fit(to.value("name",std::string()),120),126),10.f,"swap-art",std::string("background-color:")+kLinesPanel+";")+
            box(176,133,258,219,"<div style='padding:"+px(2)+" "+px(3)+"; line-height:"+px(15)+";'>"+escape(label_of("board"))+"<br/>"+escape(label_of("ask"))+"</div>",fit(label_of("board"),78),"swap-question")+
            yes_no("swap",cursor,215,168,250,206,18)+
            box(259,133,302,219,at(3,3,"<span class='im-dim'>"+escape(label_of("terrain"))+"</span>","",fit(label_of("terrain"),38))+at(3,20,dim("air"))+at(26,20,terrain.substr(0,1))+at(3,36,dim("land"))+at(26,36,terrain.substr(1,1))+
                at(3,52,dim("sea"))+at(26,52,terrain.substr(2,1))+at(3,68,dim("space"))+at(26,68,terrain.substr(3,1)),10.f,"swap-terrain");
        body+=hint("swap_confirm_hint");
    }
    body+="</div>";
    swap_doc=document(body,true);swap_doc->SetClass("modal",false);
}

// データセーブ (save_page.cpp): the medium choice (layout 0x6A with the 0x72 pause box)
// and the two-slot page (0x73) with the overwrite window (0x74) and the Controller Pak
// message box (0x8C), at the original positions. With extended slots the list runs on
// in pages of two; page 1 is the cartridge's own slots.
void save_sync() {
    const auto next=save_page::state();save_request=next;
    if(!next.value("visible",false)){document_close(save_doc);save_stamp.clear();return;}
    const auto stamp=next.dump()+localization::catalog().locale+frame_stamp()+std::to_string(hd_portraits());
    if(save_doc && save_stamp==stamp)return;
    document_close(save_doc);save_stamp=stamp;
    const float u=frame::scale(pixels_w,pixels_h),ox=(pixels_w-320*u)/2,oy=(pixels_h-240*u)/2;
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    const float line=std::max(1.f,float(int(u+0.5f)))/u;
    const auto box=[&](float x0,float y0,float x1,float y1,const std::string& content,float font,const std::string& id="",const std::string& extra="") {
        return "<div class='im-panel'"+(id.empty()?"":" id='"+id+"'")+" style='left:"+px(x0-line)+"; top:"+px(y0-line)+"; width:"+px(x1-x0+1+2*line)+"; height:"+px(y1-y0+1+2*line)+
            "; border-width:"+px(line)+"; font-size:"+px(font)+"; line-height:"+px(16)+";"+extra+"'>"+content+"</div>";
    };
    const auto fit=[&](const std::string& text,float width){return std::min(10.5f,width/std::max(1.f,text_units(text)));};
    // A plain name cell gives 4 of its units to a margin on its right, so a name that fills its
    // column (and shrinks to it) still parts from the next column, as the original's half-width text did.
    const auto span=[&](const std::string& text,float width,const std::string& cls="",float font=0,float gap=0){
        const bool name=cls.empty();
        return "<span class='fit "+cls+"' style='width:"+px(name?width-4:width)+";"+(name?" margin-right:"+px(4)+";":"")+(font?" font-size:"+px(font)+";":"")+(gap?" margin-left:"+px(gap)+";":"")+"'>"+escape(text)+"</span>";};
    const auto at=[&](float x,float y,const std::string& content,const std::string& cls="",float font=0,float width=0){return "<div class='"+cls+"' style='position:absolute; left:"+px(x)+"; top:"+px(y)+";"+(width?" width:"+px(width)+";":"")+(font?" font-size:"+px(font)+";":"")+" line-height:"+px(16)+"; white-space:nowrap;'>"+content+"</div>";};
    const auto& L=next.at("labels");const auto label_of=[&](const char* key){return L.value(key,std::string());};
    const auto number=[](const json& v){return v.is_number()?std::to_string(v.get<long long>()):std::string("--");};
    const auto hint=[&](const char* key){return "<div class='im-hint' style='left:0; top:"+px(224)+"; width:"+px(320)+"; font-size:"+px(6.5f)+";'>"+label(key)+"</div>";};
    const auto art_img=[&](const json& owner,float size){
        if(!owner.contains("art") || !owner.at("art").contains("path"))return std::string();
        const float w=owner.at("art").value("width",96.f),h=owner.at("art").value("height",96.f),scale=std::min(size/w,size/h);
        return "<img src='"+escape(image(portrait_path(owner.at("art")),int(w*scale*u+.5f)))+"' style='width:"+px(w*scale)+"; height:"+px(h*scale)+"; margin:auto;'/>";
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
        // The original draws the medium and the sentence as two labels; Chinese runs them together.
        const std::string message=media[cursor]+(localization::catalog().locale.starts_with("zh")?"":" ")+label_of("save_to");
        body+=box(85,125,235,147,at(0,3,escape(message),"im-center",fit(message,146),150),10.f,"save-message");
        if(next.value("waiting",false))
            body+=box(101,109,219,131,at(0,3,escape(label_of("checking")),"im-center",fit(label_of("checking"),114),118),10.f,"save-checking");
        body+=hint(next.value("waiting",false)?"save_message_hint":"save_choice_hint");
    } else if(screen=="slots") {
        const unsigned cursor=next.value("cursor",0u),mode=next.value("mode",0u),medium=next.value("medium",0u);
        body+=box(117,21,203,43,at(0,3,escape(media[medium]),"im-center",fit(media[medium],80),86),10.f,"save-title");
        const auto& slots=next.at("slots");
        const unsigned count=next.value("count",2u),page=next.value("page",0u),pages=next.value("pages",1u);
        if(count>2) {
            const std::string cartridge=localization::catalog().ui("save_cartridge"),number=std::to_string(page+1)+" / "+std::to_string(pages);
            if(page==0)body+=at(21,50,escape(cartridge),"im-dim",fit(cartridge,80));
            body+=at(239,50,"&lt; "+escape(number)+" &gt;","im-right im-dim",0,60);
        }
        for(unsigned n=0;n<slots.size();++n) {
            const auto& s=slots[n];const float y0=69+80*n;const bool used=s.value("used",false);
            const unsigned index=s.value("index",n),slot=s.value("number",n+1);
            const std::string kind=s.value("kind",std::string("slot"));
            // An autosave is named by when it was made; a slot 3+ may carry the player's note.
            const std::string name=kind=="slot"?label_of("slot")+std::to_string(slot):localization::catalog().ui(kind=="turn"?"save_auto_turn":"save_auto");
            std::string time=s.value("time",std::string());
            if(time.size()>=16)time=time.substr(5,2)+"/"+time.substr(8,2)+" "+time.substr(11,5);
            body+=box(21,y0,87,y0+70,"<div style='position:absolute; left:0; top:0; width:100%; height:"+px(70)+"; display:flex; align-items:center; justify-content:center;'>"+(used?art_img(s,64):std::string())+"</div>",10.f,"save-face:"+std::to_string(index));
            std::string rows="<button id='save:"+std::to_string(index)+"' class='im-row "+(index==cursor?"on":"")+"' style='height:"+px(19)+"; line-height:"+px(19)+"; padding:0 "+px(2)+";'>"+
                span(name,68,"",fit(name,66))+
                (used?span(s.value("name",std::string()),90,"",fit(s.value("name",std::string()),86))+
                    // A turn autosave's record does not say the protagonist's level.
                    (kind=="turn"?std::string():span(label_of("level"),34,"im-dim",fit(label_of("level"),32))+span(number(s.value("level",json())),14,"im-right")):std::string())+"</button>";
            if(used && kind=="turn") {
                // A turn autosave: the map in progress, its turn, the funds.
                auto turn=localization::catalog().ui("save_turn");
                if(const auto at=turn.find("{n}");at!=std::string::npos)turn.replace(at,3,number(s.value("map_turn",json())));
                rows+=at(3,20,escape(turn),"",fit(turn,60))+at(110,20,escape(time),"im-right im-dim",0,96)+
                    at(2,37,escape(s.value("title",std::string())),"",fit(s.value("title",std::string()),204),204)+
                    at(104,54,escape(label_of("funds")),"im-dim",fit(label_of("funds"),38))+at(144,54,number(s.value("funds",json())),"im-right",0,62);
            } else if(used) {
                // 第  話 carries the number in its blanks, as the original's %2d at x=104.
                std::string episode=label_of("episode");const std::string count=number(s.value("episode",json()));
                if(const auto blank=episode.find("  ");blank!=std::string::npos)episode.replace(blank,2,count.size()>1?count:" "+count);
                else if(const auto one=episode.find(' ');one!=std::string::npos)episode.replace(one,1,count);
                else episode+=count;
                const std::string title=s.value("title",std::string())+" "+label_of("clear");
                if(kind!="slot")rows+=at(70,20,escape(time),"im-right im-dim",fit(time,134),136);
                rows+=at(3,20,"<span class='im-dim'>"+escape(episode.substr(0,episode.find(count)))+"</span>"+escape(count)+"<span class='im-dim'>"+escape(episode.substr(episode.find(count)+count.size()))+"</span>","",fit(episode,60))+
                    at(2,37,escape(title),"",fit(title,204),204)+
                    at(3,54,escape(label_of("turns")),"im-dim",fit(label_of("turns"),54))+at(58,54,number(s.value("turns",json())),"im-right",0,24)+
                    at(104,54,escape(label_of("funds")),"im-dim",fit(label_of("funds"),38))+at(144,54,number(s.value("funds",json())),"im-right",0,62);
            }
            body+=box(87,y0,299,y0+70,rows,10.f,"save-slot:"+std::to_string(index));
        }
        if(mode==1 || mode==3) {
            // 3: deleting a slot 3+ or an autosave, in the overwrite window's place.
            const std::string first=mode==3?localization::catalog().ui("save_delete"):label_of("overwrite");
            body+="<div class='im-shade'></div>"+box(53,101,267,139,at(3,3,escape(first),"",fit(first,208),210)+at(3,19,escape(label_of("ask")),"",fit(label_of("ask"),208),210),10.f,"save-window")+
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
        const bool loading=next.value("context",std::string())=="title";
        // R deletes a slot 3+ or an autosave; the hint says so where it can.
        const bool erase=next.value("tools",json::object()).value("delete",false);
        body+=hint(mode==1 || mode==3?"save_confirm_hint":mode==2?"save_message_hint":
                   loading?(erase?"title_load_hint_delete":count>2?"title_load_hint_pages":"title_load_hint"):
                   erase?"save_slots_hint_delete":count>2?"save_slots_hint_pages":"save_slots_hint");
    }
    body+="</div>";
    save_doc=document(body,true);save_doc->SetClass("modal",false);
}

// Title screen pages (title_page.cpp): オプション (layout 0x5A) and the サウンドセレクト /
// カラオケモード song lists (0x5B / 0x61, ten rows with the arrows of 0x5C), at the
// original positions. The ロード screens are the データセーブ page above.
void title_sync() {
    const auto next=title_page::state();title_request=next;
    if(!next.value("visible",false)){document_close(title_doc);title_stamp.clear();return;}
    const auto stamp=next.dump()+localization::catalog().locale+frame_stamp();
    if(title_doc && title_stamp==stamp)return;
    document_close(title_doc);title_stamp=stamp;
    const float u=frame::scale(pixels_w,pixels_h),ox=(pixels_w-320*u)/2,oy=(pixels_h-240*u)/2;
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    const float line=std::max(1.f,float(int(u+0.5f)))/u;
    const auto box=[&](float x0,float y0,float x1,float y1,const std::string& content,float font,const std::string& id="") {
        return "<div class='im-panel'"+(id.empty()?"":" id='"+id+"'")+" style='left:"+px(x0-line)+"; top:"+px(y0-line)+"; width:"+px(x1-x0+1+2*line)+"; height:"+px(y1-y0+1+2*line)+
            "; border-width:"+px(line)+"; font-size:"+px(font)+"; line-height:"+px(16)+";'>"+content+"</div>";
    };
    const auto fit=[&](const std::string& text,float width){return std::min(12.5f,width/std::max(1.f,text_units(text)));};
    const auto at=[&](float x,float y,float w,float h,const std::string& content,const std::string& extra=""){
        return "<div style='position:absolute; left:"+px(x)+"; top:"+px(y)+"; width:"+px(w)+"; height:"+px(h)+"; line-height:"+px(h)+";"+extra+"'>"+content+"</div>";};
    const auto hint=[&](const char* key){return "<div class='im-hint' style='left:0; top:"+px(224)+"; width:"+px(320)+"; font-size:"+px(6.5f)+";'>"+label(key)+"</div>";};
    const std::string screen=next.value("screen",std::string()),title=next.value("title",std::string());
    std::string body="<div class='im-root' id='title-page' style='left:"+px(ox/u)+"; top:"+px(oy/u)+"; width:"+px(320)+"; height:"+px(240)+";'>";
    if(screen=="options") {
        body+=box(133,21,187,43,at(0,3,54,16,escape(title),"text-align:center;"),fit(title,50),"tp-title");
        const auto& items=next.at("items");const unsigned cursor=next.value("cursor",0u);
        float font=12.5f;
        // The label and the value have their own cells (62 + 34): each must fit its own.
        for(const auto& item:items)font=std::min(font,item.contains("value")?std::min(fit(item.value("label",std::string()),60),fit(item.value("value",std::string()),33)):fit(item.value("label",std::string()),94));
        std::string rows;
        for(unsigned n=0;n<items.size();++n) {
            const auto& item=items[n];
            rows+="<button id='tp-option:"+std::to_string(n)+"' class='im-row "+(n==cursor?"on":"")+"' style='position:absolute; left:0; top:"+px(3+16*n)+"; height:"+px(16)+"; line-height:"+px(16)+"; padding:0 "+px(2)+";'>"+
                "<span style='width:"+px(item.contains("value")?62:96)+";'>"+escape(item.value("label",std::string()))+"</span>"+
                (item.contains("value")?"<span class='im-right' style='width:"+px(34)+";'>"+escape(item.value("value",std::string()))+"</span>":std::string())+"</button>";
        }
        body+=box(109,84,213,139,rows,font,"tp-options");
        body+=hint("title_options_hint");
    } else {
        const auto& songs=next.at("songs");const unsigned count=unsigned(songs.size()),current=next.value("current",0u),top=next.value("top",0u),rows=next.value("rows",10u);
        const bool exit=next.value("exit_focus",false);const std::string exit_label=next.value("exit",std::string());
        std::string content="<div style='position:absolute; left:0; top:"+px(22)+"; width:100%; height:"+px(line)+"; background-color:#3a78e0;'></div>"
            "<div style='position:absolute; left:"+px(207)+"; top:0; width:"+px(line)+"; height:"+px(22)+"; background-color:#3a78e0;'></div>"+
            at(20,3,180,16,escape(title),"font-size:"+px(fit(title,178))+";")+
            "<button id='tp-exit' class='"+std::string(exit?"on":"")+"' style='position:absolute; left:"+px(208)+"; top:"+px(3)+"; width:"+px(70)+"; height:"+px(16)+"; line-height:"+px(16)+"; padding:0; text-align:center; font-size:"+px(fit(exit_label,64))+";'>"+escape(exit_label)+"</button>";
        float font=12.5f;
        for(unsigned r=0;r<rows && top+r<count;++r)font=std::min(font,fit(songs[top+r].value("text",std::string()),214));
        for(unsigned r=0;r<rows && top+r<count;++r) {
            const unsigned n=top+r;const auto& song=songs[n];
            content+="<button id='tp-song:"+std::to_string(n)+"' class='"+std::string(n==current && !exit?"on":"")+(song.value("playing",false)?" tp-playing":"")+"' style='position:absolute; left:"+px(1)+"; top:"+px(30+16*r)+"; width:"+px(275)+
                "; height:"+px(16)+"; line-height:"+px(16)+"; padding:0 0 0 "+px(58)+"; font-size:"+px(font)+";'>"+
                (song.value("playing",false)?"<span style='position:absolute; left:"+px(42)+"; top:0; font-size:"+px(7)+";'>&#x25B6;</span>":std::string())+escape(song.value("text",std::string()))+"</button>";
        }
        if(top>0)content+=at(129,22,20,9,"&#x25B2;","text-align:center; color:#e0402a; font-size:"+px(7)+";");
        if(top+rows<count)content+=at(129,189,20,9,"&#x25BC;","text-align:center; color:#e0402a; font-size:"+px(7)+";");
        body+=box(21,21,299,219,content,12.5f,"tp-songs");
        body+=hint(screen=="karaoke"?"title_karaoke_hint":"title_sound_hint");
    }
    body+="</div>";
    title_doc=document(body,true);title_doc->SetClass("modal",false);
}

// ユニット能力／パイロット能力 (ability_page.cpp): the two nine-row lists (layouts
// 0x6D / 0x6E), the unit page (0x79), its weapon list (0x7A) and the pilot page (0x7B).
void ability_sync() {
    const auto next=ability_page::state();ability_request=next;
    if(!next.value("visible",false)){document_close(ability_doc);ability_stamp.clear();return;}
    const auto stamp=next.dump()+localization::catalog().locale+frame_stamp()+std::to_string(hd_portraits());
    if(ability_doc && ability_stamp==stamp)return;
    document_close(ability_doc);ability_stamp=stamp;
    const float u=frame::scale(pixels_w,pixels_h),ox=(pixels_w-320*u)/2,oy=(pixels_h-240*u)/2;
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    const float line=std::max(1.f,float(int(u+0.5f)))/u;
    const auto box=[&](float x0,float y0,float x1,float y1,const std::string& content,float font,const std::string& id="",const std::string& extra="") {
        return "<div class='im-panel'"+(id.empty()?"":" id='"+id+"'")+" style='left:"+px(x0-line)+"; top:"+px(y0-line)+"; width:"+px(x1-x0+1+2*line)+"; height:"+px(y1-y0+1+2*line)+
            "; border-width:"+px(line)+"; font-size:"+px(font)+"; line-height:"+px(16)+";"+extra+"'>"+content+"</div>";
    };
    const auto fit=[&](const std::string& text,float width){return std::min(10.5f,width/std::max(1.f,text_units(text)));};
    // The list rows and their labels share one face, a size under the panels around them.
    const float face=10.5f;const auto lfit=[&](const std::string& text,float width){return std::min(face,fit(text,width));};
    // A plain name cell gives 4 of its units to a margin on its right, so a name that fills its
    // column (and shrinks to it) still parts from the next column, as the original's half-width text did.
    const auto span=[&](const std::string& text,float width,const std::string& cls="",float font=0,float gap=0){
        const bool name=cls.empty();
        return "<span class='fit "+cls+"' style='width:"+px(name?width-4:width)+";"+(name?" margin-right:"+px(4)+";":"")+(font?" font-size:"+px(font)+";":"")+(gap?" margin-left:"+px(gap)+";":"")+"'>"+escape(text)+"</span>";};
    const auto at=[&](float x,float y,const std::string& content,const std::string& cls="",float font=0,float width=0){return "<div class='"+cls+"' style='position:absolute; left:"+px(x)+"; top:"+px(y)+";"+(width?" width:"+px(width)+";":"")+(font?" font-size:"+px(font)+";":"")+" line-height:"+px(16)+"; white-space:nowrap;'>"+content+"</div>";};
    const auto& L=next.at("labels");const auto label_of=[&](const char* key){return L.value(key,std::string());};
    const auto number=[](const json& v){return v.is_number()?std::to_string(v.get<long long>()):std::string("---");};
    const auto dashes=[](const std::string& s){return s.empty()?std::string("--------"):s;};
    const auto hint=[&](const char* key){return "<div class='im-hint' style='left:0; top:"+px(224)+"; width:"+px(320)+"; font-size:"+px(6.5f)+";'>"+label(key)+"</div>";};
    const auto art_img=[&](const json& owner,float size){
        if(!owner.contains("art") || !owner.at("art").contains("path"))return std::string();
        const float w=owner.at("art").value("width",96.f),h=owner.at("art").value("height",96.f),scale=std::min(size/w,size/h);
        return "<img src='"+escape(image(portrait_path(owner.at("art")),int(w*scale*u+.5f)))+"' style='width:"+px(w*scale)+"; height:"+px(h*scale)+"; margin:auto;'/>";
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
                (pilots?span(r.value("name",std::string()),70)+span(dashes(r.value("unit",std::string())),150)+span(label_of("level"),34,"im-dim",lfit(label_of("level"),32))+span(number(r.value("level",json())),16,"im-right")
                       :span(r.value("name",std::string()),126)+span(dashes(r.value("pilot",std::string())),80)+span(label_of("hp"),24,"im-dim")+span(number(r.value("hp",json())),40,"im-right"))+"</button>";
        }
        const auto page=std::to_string(next.value("page",0u)+1)+"/"+std::to_string(next.value("pages",1u));
        const auto& sub=next.value("sub",json::object());
        body+=box(21,21,299,219,
            "<div class='im-row' style='height:"+px(22)+"; line-height:"+px(22)+"; border-bottom-width:"+px(line)+"; border-bottom-color:#3a78e0;'>"+span(page,44,"im-right")+"<span style='width:"+px(2)+"; height:100%; border-left-width:"+px(line)+"; border-left-color:#3a78e0; margin-left:"+px(2)+";'></span><span style='width:"+px(228)+"; text-align:center;'>"+escape(label_of(pilots?"pilot_list":"unit_list"))+"</span></div>"
            "<div id='ability-list' style='margin-top:"+px(3)+";'>"+list+"</div>"
            "<div class='im-row' style='position:absolute; left:0; top:"+px(175)+"; width:100%; box-sizing:border-box; height:"+px(22)+"; line-height:"+px(22)+"; padding:0 "+px(3)+"; border-top-width:"+px(line)+"; border-top-color:#3a78e0;'>"+
                span(label_of("sub"),40,"im-dim",lfit(label_of("sub"),38))+span(dashes(sub.value("name",std::string())),150)+span(label_of("level"),34,"im-dim",lfit(label_of("level"),32))+span(sub.contains("level")?number(sub.at("level")):std::string("--"),16,"im-right")+"</div>",
            face,"ability-panel");
        body+=hint("ability_list_hint");
    } else if(screen=="unit") {
        const auto& unit=next.at("unit");
        std::string parts;
        for(const auto& p:next.value("parts",json::array()))parts+="<div style='padding:0 "+px(3)+"; line-height:"+px(16)+";'>"+escape(p.get<std::string>())+"</div>";
        std::string types;
        // 陸空 run together as the original's glyphs do; English abbreviations need a separator (Lnd/Air).
        for(const auto& tname:next.value("types",json::array())) {
            const auto name=tname.get<std::string>();
            if(!types.empty() && !name.empty() && uint8_t(name[0])<0x80)types+="/";
            types+=escape(name);
        }
        // Side by side, wrapping, as the original lays them from (25,188): the box holds two
        // lines, and a unit shows up to three (ビルバイン: 変形, 分身, オーラバリア).
        // More than the two lines hold at 10.5 sets them all smaller, rather than losing one.
        std::string abilities;
        const auto& ability_names=next.value("abilities",json::array());
        float ability_units=0;for(const auto& a:ability_names)ability_units+=text_units(a.get<std::string>())+.8f;
        const float ability_font=std::max(7.f,std::min(10.5f,220/std::max(1.f,ability_units)));
        for(const auto& a:ability_names) {
            const auto name=a.get<std::string>();   // one that alone overruns a line (128 less its 8 margin) gets smaller still
            abilities+="<span style='display:inline-block; white-space:nowrap; margin-right:"+px(8*ability_font/10.5f)+"; font-size:"+px(std::min(ability_font,118/std::max(1.f,text_units(name))))+";'>"+escape(name)+"</span> ";
        }
        const long long hp=next.value("hp",0ll),hp_max=std::max(1ll,next.value("hp_max",1ll)),en=next.value("en",0ll),en_max=std::max(1ll,next.value("en_max",1ll));
        const std::string terrain=next.value("terrain",std::string("----"));
        const auto stat=[&](const char* key,const json& v,float y){return at(3,y,"<span class='im-dim'>"+escape(label_of(key))+"</span>")+at(60,y,number(v),"im-right",0,40);};
        body+=box(21,10,175,35,"<div style='padding:0 "+px(8)+"; line-height:"+px(24)+"; font-size:"+px(fit(unit.value("name",std::string()),140))+";'>"+escape(unit.value("name",std::string()))+"</div>",10.f,"ability-name")+
            box(21,36,175,58,"<div class='im-row' style='height:"+px(21)+"; line-height:"+px(21)+"; padding:0 "+px(3)+";'>"+span(label_of("size"),34,"im-dim")+span(next.value("size",std::string()),22)+span(label_of("repair"),48,"im-dim",fit(label_of("repair"),46),4)+span(number(next.value("repair",json())),40,"im-right")+"</div>",10.f,"ability-size")+
            box(21,59,175,131,"<div style='margin-top:"+px(3)+";'>"+parts+"</div>",10.f,"ability-parts")+
            box(21,132,155,163,at(6,2,"<span style='color:#ffd75e;'>"+escape(label_of("hp"))+"</span>","",9)+at(36,2,std::to_string(hp)+"/ "+std::to_string(hp_max),"",10)+
                "<div class='im-bar-back' style='left:"+px(32)+"; top:"+px(15)+"; width:"+px(89)+";'></div><div class='im-bar' style='left:"+px(32)+"; top:"+px(15)+"; width:"+px(89.f*float(hp)/float(hp_max))+";'></div>"+
                at(6,16,"<span style='color:#ffd75e;'>"+escape(label_of("en"))+"</span>","",9)+at(36,16,std::to_string(en)+"/ "+std::to_string(en_max),"",10)+
                "<div class='im-bar-back' style='left:"+px(93)+"; top:"+px(22)+"; width:"+px(28)+";'></div><div class='im-bar' style='left:"+px(93)+"; top:"+px(22)+"; width:"+px(28.f*float(en)/float(en_max))+";'></div>",10.f,"ability-gauges")+
            box(21,164,155,219,at(3,3,"<span class='im-dim'>"+escape(label_of("abilities"))+"</span>","",fit(label_of("abilities"),76))+at(83,3,escape(next.value("shield",std::string())),"",10)+"<div style='margin-top:"+px(22)+"; padding:0 "+px(3)+"; line-height:"+px(16*ability_font/10.5f)+"; white-space:normal;'>"+abilities+"</div>",10.5f,"ability-abilities")+
            box(156,132,259,219,at(3,4,"<span class='im-dim'>"+escape(label_of("type"))+"</span>")+at(60,4,types,"im-right fit",0,40)+stat("move",next.value("move",json()),20)+stat("mobility",next.value("mobility",json()),36)+stat("armor",next.value("armor",json()),52)+stat("limit",next.value("limit",json()),68),10.f,"ability-stats")+
            box(260,132,299,219,at(3,4,"<span class='im-dim'>"+escape(label_of("terrain"))+"</span>","",fit(label_of("terrain"),34))+at(3,20,"<span class='im-dim'>"+escape(label_of("air"))+"</span>")+at(24,20,terrain.substr(0,1))+at(3,36,"<span class='im-dim'>"+escape(label_of("land"))+"</span>")+at(24,36,terrain.substr(1,1))+
                at(3,52,"<span class='im-dim'>"+escape(label_of("sea"))+"</span>")+at(24,52,terrain.substr(2,1))+at(3,68,"<span class='im-dim'>"+escape(label_of("space"))+"</span>")+at(24,68,terrain.substr(3,1)),10.f,"ability-terrain")+
            box(176,8,302,132,art_img(unit,original_side(unit,118)),10.f,"ability-art",std::string("display:flex; align-items:center; justify-content:center; background-color:")+kLinesPanel+";");
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
        // Cells 56 apart; the last column has only the 36 left before the frame.
        for(unsigned n=0;n<6;++n)spirits+=at(spirit_at[n][0]-18,spirit_at[n][1]-138,n<sp.size()?escape(sp[n].get<std::string>()):escape(label_of("unknown")),"fit",10.5f,
                                             std::min(54.f,208-(spirit_at[n][0]-18)));
        std::string skills;
        const auto& sk=next.value("skills",json::array());
        // Three slots as the original (no pilot has more): one wide line, then two halves.
        const float skill_w[3]={130,70,58};
        for(unsigned n=0;n<sk.size() && n<3;++n)skills+=at(skill_at[n][0]-18,skill_at[n][1]-178,escape(sk[n].get<std::string>()),"fit",10.5f,skill_w[n]);
        const std::string terrain=next.value("terrain",std::string("----"));
        const auto dim=[&](const char* key){return "<span class='im-dim'>"+escape(label_of(key))+"</span>";};
        body+=box(18,10,110,101,art_img(p,88),10.f,"ability-art","display:flex; align-items:center; justify-content:center;")+
            box(111,10,299,35,"<div style='padding:0 "+px(6)+"; line-height:"+px(24)+"; text-align:right; font-size:"+px(fit(label_of("pilot"),176))+";' class='im-dim'>"+escape(label_of("pilot"))+"</div>",10.f,"ability-title")+
            box(111,36,299,101,at(9,4,escape(p.value("full_name",std::string())),"",fit(p.value("full_name",std::string()),170))+
                at(17,28,dim("morale"),"fit",10,27)+at(46,28,number(next.value("morale",json())),"im-right",10.5f,28)+at(81,28,dim("level"),"fit",10,18)+at(100,28,number(p.value("level",json())),"im-right",10.5f,20)+
                at(129,28,dim("next"),"fit",10,24)+at(154,28,next.contains("next")?number(next.at("next")):std::string("---"),"im-right",10.5f,30)+
                at(17,46,dim("sp"),"",10)+at(90,46,number(next.value("sp",json()))+"/ "+number(next.value("sp_max",json())),"im-right",10.5f,60),10.f,"ability-name")+
            box(18,102,299,137,at(6,4,dim("melee"))+at(40,4,stat_value("melee"),"im-right",0,32)+at(94,4,dim("evade"))+at(126,4,plus_value("evade"),cls("evade"),0,70)+at(214,4,dim("reaction"))+at(248,4,stat_value("reaction"),"im-right",0,32)+
                at(6,20,dim("ranged"))+at(40,20,stat_value("ranged"),"im-right",0,32)+at(94,20,dim("hit"))+at(126,20,plus_value("hit"),cls("hit"),0,70)+at(214,20,dim("skill"))+at(248,20,stat_value("skill"),"im-right",0,32),10.f,"ability-stats")+
            box(18,138,230,177,at(6,6,dim("spirits"),"",fit(label_of("spirits"),86))+spirits,10.f,"ability-spirits")+
            box(18,178,230,219,at(6,6,dim("skills"),"",fit(label_of("skills"),70))+skills,10.f,"ability-skills")+
            box(231,138,299,167,"<div style='text-align:center; line-height:"+px(28)+";' class='im-dim'>"+escape(label_of("terrain"))+"</div>",10.f,"ability-terrain-title")+
            // Each cell is 33 wide: the label shrinks to the 17 in front of the rank ("Lnd A").
            box(231,168,264,193,at(3,4,dim("air"),"",std::min(10.f,17.f/text_units(label_of("air"))))+at(22,4,terrain.substr(0,1)),10.f)+
            box(265,168,299,193,at(3,4,dim("land"),"",std::min(10.f,17.f/text_units(label_of("land"))))+at(22,4,terrain.substr(1,1)),10.f)+
            box(231,194,264,219,at(3,4,dim("sea"),"",std::min(10.f,17.f/text_units(label_of("sea"))))+at(22,4,terrain.substr(2,1)),10.f)+
            box(265,194,299,219,at(3,4,dim("space"),"",std::min(10.f,17.f/text_units(label_of("space"))))+at(22,4,terrain.substr(3,1)),10.f);
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
    // Only this box takes the pointer while the entry waits: the page beneath stays open to
    // clicks, such as the title's DLC and settings entries. Entering covers the screen.
    std::string body="<div style='position:absolute; bottom:32dp; left:25%; width:50%; text-align:center; pointer-events:auto;'>";
    // A campaign (DLC) starts from here: its name, not a mini stage's file name.
    const auto info=campaign::info();
    body+=entering?"<h2>"+label("mini_entering")+"</h2>":button("mini-enter",label(info?"dlc_start":"mini_enter"));
    body+="<p>"+escape(info?campaign_library::text(info->name,localization::catalog().locale):state.value("name",std::string{}))+"</p></div>";
    mini_doc=document(body,entering,true);
}
// PRESS START and the ring menu (title 主状態 2 and 3): the settings window's entry, for
// players with no menu bar. A controller shows the View button; a tap or click opens it.
// Beside it, the additional scenarios (DLC): installed campaigns in the main game, the
// way back to it in a campaign (campaign_library.hpp).
bool touch_active();
void home_sync() {
    const bool settings_entry=pad_mode || !app_menu::available();
    // Not while the opening's works fly past or the logo comes in, nor in a demo.
    if(!intro::title_waiting() || settings_open){document_close(home_doc);document_close(home_chrome_doc);home_stamp.clear();return;}
    // With touch controls the Library, MOD and settings are touch buttons along the top.
    const bool touch=touch_active();
    const auto stamp=localization::catalog().locale+(pad_mode?"+pad":"")+(settings_entry?"s":"")+(touch?"t":"")+frame_stamp()+
        (update::status("en").state==update::State::Available?"u"+update::status("en").latest.version:std::string())+
        (update::status("en").hd.available?"h"+update::status("en").hd.latest:std::string());
    if((home_doc || home_chrome_doc) && stamp==home_stamp)return;
    document_close(home_doc);document_close(home_chrome_doc);home_stamp=stamp;
    // Library and MOD bottom right, lettered like the ring (menu_style: 14 game pixels, a
    // 1-pixel outline, a 0.8-pixel shadow); the settings entry top left.
    const float u=frame::scale(pixels_w,pixels_h);
    const auto px=[&](float v){return std::to_string(std::max(1,int(v*u+0.5f)))+"px";};
    const auto lettered=[&](const char* id,const char* key) {
        return "<button id='"+std::string(id)+"' class='home-mod' style='font-size:"+px(14)+"; font-effect:outline("+px(1)+" #0a0d17), shadow("+
            px(.8f)+" "+px(.8f)+" #00000073);'>"+label(key)+"</button>";
    };
    // The lettering belongs to the title picture: its corner is the picture's, so a 4:3
    // picture keeps it inside (under a bezel's frame otherwise), and a filter reaches it.
    const float pw=frame::width(pixels_w,pixels_h)*u,ph=frame::kHeight*u;
    const auto corner="right:"+std::to_string(int((pixels_w-pw)/2+pw*.06f))+"px; bottom:"+std::to_string(int((pixels_h-ph)/2+ph*.06f))+"px;";
    if(!touch)home_doc=document("<div class='home-corner' style='"+corner+"'>"+lettered("library-open","library_open")+lettered("viewer-open","viewer_open")+(mod_entry_shown?lettered("mod-open","mod_open"):std::string())+"</div>",false);
    std::string chrome="<div class='home-version'>v"+escape(SRW64_VERSION)+"</div>";
    if(settings_entry && !touch)chrome+="<button id='settings-open' class='home-entry'>"+label("settings_open")+"</button>";
    // A newer release, found by the update check: a word in the corner that opens the About page.
    // A newer HD pack says so in the same place when the program itself is current.
    if(const auto s=update::status("en");s.state==update::State::Available && !touch)
        chrome+="<button id='update-open' class='home-update'>"+update_words("update_title_hint",s)+"</button>";
    else if(s.hd.available && !touch)
        chrome+="<button id='update-open' class='home-update'>"+update_hd_words("update_title_hint_hd",s)+"</button>";
    home_chrome_doc=document(chrome,false,true);
}
// The HD original (battle_ui "hd"): the original screen redrawn in its own 320x240
// coordinates, like the intermission pages. Two panels (window layout 0x45, frame 1196;
// 801D4660 / 801E7A28), the player's on the right; when an enemy attacks, the menu
// (0x46, frame 1197; 801D51A8). The same words, figures and ??? for an unknown enemy.
std::string battle_hd_page(const json& next) {
    const float u=frame::scale(pixels_w,pixels_h),ox=(pixels_w-320*u)/2,oy=(pixels_h-240*u)/2;
    const auto px=[&](float v){return std::to_string(int(v*u+0.5f))+"px";};
    const float line=std::max(1.f,float(int(u+0.5f)))/u;   // one original pixel
    const auto box=[&](float x,float y,float w,float h){return "left:"+px(x)+"; top:"+px(y)+"; width:"+px(w)+"; height:"+px(h)+";";};
    const auto& words=next.at("words");
    const auto word=[&](const char* key){return words.value(key,std::string());};
    // A 14-pixel ROM text row whose cell starts at (x, y). The ROM's kana are half as wide
    // as the font's, so a long row is first narrowed (to 70%, English 80%, as the original
    // screen's text in ui_text.cpp) and only then set smaller.
    const auto text=[&](float x,float y,float w,const std::string& s,bool right=false) {
        const float natural=std::max(1.f,text_units(s))*12.5f;
        const bool ascii=std::all_of(s.begin(),s.end(),[](unsigned char c){return c<0x80;});
        const float squeeze=std::clamp(w/natural,ascii?.8f:.7f,1.f),font=std::min(12.5f,12.5f*w/(natural*squeeze)),inner=w/squeeze;
        return "<div class='bh-text' style='"+box(x,y-1,w,16)+"'><div class='fit' style='position:absolute; top:0; left:"+px(right?w-inner:0)+"; width:"+px(inner)+"; height:"+px(16)+
            "; line-height:"+px(16)+"; font-size:"+px(font)+"; text-align:"+(right?"right":"left")+"; transform-origin:"+(right?"right":"left")+" center; transform:scale("+
            std::to_string(squeeze)+", 1);'>"+escape(s)+"</div></div>";
    };
    // The number pool's 8x8 cells (white, grey shadow), right-aligned in `cells` of them.
    const auto figure=[&](float x,float y,float cells,const std::string& s,const char* cls="bh-num") {
        return "<div class='"+std::string(cls)+"' style='"+box(x,y-1,cells*8,10)+" line-height:"+px(10)+"; font-size:"+px(10.5f)+
            "; font-effect:shadow("+px(1)+" "+px(1)+" #848484);'>"+escape(s)+"</div>";
    };
    const auto gauge=[&](float x,float y,float w,const json& value,const json& maximum_value) {
        const int maximum=maximum_value.get<int>();
        const int filled=maximum>0?std::clamp(value.get<int>(),0,maximum)*int(w)/maximum:0;
        return "<div class='bh-bar' style='"+box(x,y,w,2)+"'><div style='width:"+px(float(filled))+";'></div></div>";
    };
    const auto frame=[&](float x,float y,float w,float h) {
        return "<div class='bh-frame' style='"+box(x,y,w,h)+" border-width:"+px(line)+";'><div class='bh-fill' style='border-width:"+px(line)+";'></div></div>";
    };
    const int response=next.value("response",0);
    const auto panel=[&](const json& c,float x,bool defender) {
        const bool known=c.value("known",true),armed=c.at("weapon").get<int>()>=0;
        const auto shown=[&](const char* key,const char* unknown){return known?std::to_string(c.at(key).get<int>()):std::string(unknown);};
        std::string b=frame(x+19,19,138,122);
        b+="<div class='bh-rule blue' style='"+box(x+20,51,136,line)+"'></div><div class='bh-rule dark' style='"+box(x+20,52,136,line)+"'></div>";
        b+=figure(x+36,26,2,"HP","bh-num key")+figure(x+53,26,5,shown("hp","?????"))+figure(x+91,26,1,"/","bh-num slash")+figure(x+102,26,5,shown("max_hp","?????"));
        b+=gauge(x+52,35,88,c.at("hp"),c.at("max_hp"));
        b+=figure(x+36,39,2,"EN","bh-num key")+figure(x+53,39,3,shown("en","???"))+figure(x+74,39,1,"/","bh-num slash")+figure(x+86,39,3,shown("max_en","???"));
        b+=gauge(x+112,42,28,c.at("en"),c.at("max_en"));
        b+=text(x+24,56,130,c.at("unit_name").get<std::string>());
        b+=text(x+24,72,74,c.at("pilot_name").get<std::string>())+text(x+100,72,35,word("level"),true);
        b+=text(x+137,72,16,c.value("level_known",true)?std::to_string(c.at("level").get<int>()):std::string("??"),true);
        // 回避／防御 once the defender has chosen it, else its weapon or 反撃不能.
        const auto weapon=defender && response==1?word("evade"):defender && response==2?word("defending"):armed?c.at("weapon_name").get<std::string>():word("no_counter");
        b+=text(x+24,88,130,weapon);
        b+=text(x+24,104,32,word("morale"))+text(x+56,104,24,std::to_string(c.at("morale").get<int>()),true);
        // 命中率    %: the figure goes in the blank run, whatever the words around it.
        auto hit=word("hit");std::string after="%";
        if(const auto gap=hit.find("  ");gap!=std::string::npos) {
            const auto rest=hit.find_first_not_of(' ',gap);
            after=rest==std::string::npos?std::string():hit.substr(rest);
            hit.resize(gap);
        } else if(!hit.empty() && hit.back()=='%')hit.pop_back();
        b+=text(x+24,122,46,hit)+text(x+72,122,24,armed?std::to_string(c.at("hit").get<int>()):std::string("---"),true)+text(x+96,122,16,after);
        return b;
    };
    const bool player_attacks=next.at("attacker").at("side")==0;
    std::string body="<div class='im-root' id='battle-hd' style='left:"+px(ox/u)+"; top:"+px(oy/u)+"; width:"+px(320)+"; height:"+px(240)+";'>";
    body+="<div id='battle-card-left'>"+panel(next.at(player_attacks?"defender":"attacker"),0,player_attacks)+"</div>";
    body+="<div id='battle-card-right'>"+panel(next.at(player_attacks?"attacker":"defender"),144,!player_attacks)+"</div>";
    if(next.value("mode",0)==2) {
        body+=frame(123,147,74,74);
        const std::pair<const char*,const char*> items[]={{"battle-confirm","start"},{"battle-weapon","weapon"},{"battle-evade","evade"},{"battle-defend","defend"}};
        for(unsigned n=0;n<4;++n) {
            const auto name=word(items[n].second);
            body+="<button id='"+std::string(items[n].first)+"' class='bh-item' style='"+box(124,150+16.f*n,71,17)+" line-height:"+px(17)+"; padding:0 0 0 "+px(4)+
                "; font-size:"+px(std::min(12.5f,63/std::max(1.f,text_units(name))))+";'>"+escape(name)+"</button>";
        }
    }
    body+="</div>";
    // The animation switch, as on the original screen (battle_sync).
    return body+"<div class='bp-original'><div><span class='key'>"+escape(text::expand_prompts("{Anim}",prompt_context(pad_mode)))+"</span> "+
        label("battle_animation")+" \xc2\xb7 <b>"+label(next.value("animation",true)?"battle_on":"battle_off")+"</b></div></div>";
}
// Its keys follow the original screen: A starts (or takes the menu entry), B goes back
// to the target, or to the weapon list when an enemy attacks; C-down the animation.
void battle_hd_buttons(uint32_t pressed) {
    const bool menu=battle_request.value("mode",0)==2;
    if(pressed&(0x0004|input::pad_animation)){choose("battle-animation");return;}
    if(pressed&0x4000){choose(menu?"battle-weapon":"battle-back");return;}
    static constexpr const char* items[]={"battle-confirm","battle-weapon","battle-evade","battle-defend"};
    auto* focus=context->GetFocusElement();
    const auto at=std::find_if(std::begin(items),std::end(items),[&](const char* id){return focus && focus->GetId()==id;});
    const int index=at==std::end(items)?0:int(at-std::begin(items));
    if(menu && (pressed&(0x0F00|(0xFu<<16)))) {
        const bool up=pressed&(0x0800|0x0200|(1u<<16)|(1u<<18));
        if(auto* e=battle_doc->GetElementById(items[(index+(up?3:1))%4]))e->Focus();
        return;
    }
    if(pressed&(0x8000|0x1000))choose(menu?items[index]:"battle-confirm");
}
bool touch_battle_layout();
touch_pad::Layout touch_layout();
void battle_sync() {
    const auto next=battle_page::state();battle_request=next;
    if(!next.value("visible",false)) {
        document_close(battle_doc);battle_stamp.clear();
        // Original HUD: only the animation state and its toggle key, top centre.
        const std::string stamp=next.value("original",false)?"original"+std::to_string(next.value("animation",true))+localization::catalog().locale:"";
        if(stamp!=original_stamp) {
            document_close(original_doc);original_stamp=stamp;
            if(!stamp.empty())original_doc=document("<div class='bp-original'><div><span class='key'>"+escape(text::expand_prompts("{Anim}",prompt_context(pad_mode)))+"</span> "+label("battle_animation")+" \xc2\xb7 <b>"+label(next.value("animation",true)?"battle_on":"battle_off")+"</b></div></div>",false,true);
        }
        return;
    }
    document_close(original_doc);original_stamp.clear();
    // With touch the page is laid out for it whatever the style (docs/design/touch-controls.md).
    const bool touch=touch_battle_layout();
    const bool hd_original=next.value("style",std::string())=="hd" && !touch;
    // The window and the interface size set the unit pictures' room (ui_density).
    const auto stamp=next.dump()+localization::catalog().locale+std::to_string(hd_portraits())+frame_stamp()+"@"+std::to_string(ui_density)+(hd_original && pad_mode?"+pad":"")+(touch?"+touch":"");
    if(battle_doc && battle_stamp==stamp)return;
    if(hd_original) {
        // The menu cursor stays where it was when the same encounter redraws.
        auto* focus=battle_doc && context->GetFocusElement() && context->GetFocusElement()->GetOwnerDocument()==battle_doc?context->GetFocusElement():nullptr;
        const auto kept=focus && battle_request_serial==next.value("serial",uint64_t{})?focus->GetId():std::string("battle-confirm");
        document_close(battle_doc);battle_stamp=stamp;battle_request_serial=next.value("serial",uint64_t{});
        battle_doc=document(battle_hd_page(next),true);battle_doc->SetClass("modal",false);
        if(auto* e=battle_doc->GetElementById(kept))e->Focus();
        return;
    }
    // A redraw of the same encounter (the hints turning to the controller's, the language,
    // the window) keeps the focused button; a new one, a new response or the spirit list
    // opening or closing starts again.
    static bool spirit_menu_before=false;
    auto* focus=battle_doc && context->GetFocusElement() && context->GetFocusElement()->GetOwnerDocument()==battle_doc?context->GetFocusElement():nullptr;
    const bool same=battle_request_serial==next.value("serial",uint64_t{}) && spirit_menu_before==next.value("spirit_menu",false);
    const auto kept=focus && same?focus->GetId():std::string();
    battle_request_serial=next.value("serial",uint64_t{});spirit_menu_before=next.value("spirit_menu",false);
    document_close(battle_doc);battle_stamp=stamp;
    // Keep our unit on the right, matching the original battle HUD. Direction
    // follows screen position, not attacker/defender role or faction arithmetic.
    const bool player_attacks=next.at("attacker").at("side")==0,responding=next.value("mode",0)==2;
    const auto& enemy=next.at(player_attacks?"defender":"attacker");const auto& player=next.at(player_attacks?"attacker":"defender");
    const int response=next.value("response",0);
    const auto player_response=label(response==1?"battle_evade":response==2?"battle_defend":player.at("weapon").get<int>()<0?"battle_none":"battle_counter");
    const auto enemy_response=label(enemy.at("weapon").get<int>()<0?"battle_none":"battle_counter");
    const bool selecting_spirit=next.value("spirit_menu",false);
    // Under 1000 dp wide (a larger interface size on a small screen) the pilots' faces and
    // the buttons' padding shrink, so the figures keep one line.
    const bool narrow=page_area().w/ui_density<1000;
    // Touch: the top strip for settings and the animation, the bottom corners for the stick
    // and the buttons, which take over the page's own (touch_pad.hpp BattlePage).
    std::string page_style,bottom_style;
    if(touch) {
        const float mm=touch_layout().mm;
        page_style=" style='padding-top:"+std::to_string(int(8*mm))+"px;'";
        bottom_style=" style='padding-left:"+std::to_string(int(26*mm))+"px; padding-right:"+std::to_string(int(32*mm))+"px;'";
    }
    std::string body="<div class='bp-dim'></div><div class='bp-tint left'></div><div class='bp-tint right'></div><div class='battle-page"+std::string(narrow?" narrow":"")+(touch?" touch":"")+"'"+page_style+"><div class='bp-row'>";
    body+=battle_banner(enemy,true,!player_attacks,enemy_response)+"<div class='bp-center bp-phase'><div class='enemy"+std::string(player_attacks?"":" on")+"'>"+label("battle_phase_enemy")+"</div><div class='player"+std::string(player_attacks?" on":"")+"'>"+label("battle_phase_player")+"</div></div>"+battle_banner(player,false,player_attacks,player_response)+"</div>";
    body+="<div id='battle-mid' class='bp-row bp-mid'>"+battle_unit(enemy,true)+battle_clash(enemy,player,player_attacks,player_response,responding)+battle_unit(player,false)+"</div>";
    const auto room=battle_pilot_room(enemy,player,narrow);
    body+="<div class='bp-row bp-bottom'"+bottom_style+">"+battle_pilot(enemy,true,room)+"<div class='bp-center battle-actions'>";
    if(!touch)body+="<div>"+button("battle-confirm",label("battle_confirm"),false,selecting_spirit)+"</div>";
    if(responding) {
        body+="<div class='bp-segment'><div>"+button("battle-counter",label("battle_counter"),response==0,selecting_spirit);
        body+=button("battle-evade",label("battle_evade"),response==1,selecting_spirit);
        body+=button("battle-defend",label("battle_defend"),response==2,selecting_spirit)+"</div></div>";
    }
    if(!touch) {
        body+="<div>"+button("battle-weapon",label("battle_change_weapon"),false,selecting_spirit);
        body+=button("battle-spirits",label("battle_spirits"),false,selecting_spirit);
        body+=button("battle-animation",label("battle_animation")+" · "+label(next.value("animation",true)?"battle_on":"battle_off"),false,selecting_spirit);
        if(next.value("can_cancel",false))body+=button("battle-back",label("battle_back"),false,selecting_spirit);
        body+="</div>";
    }
    body+="</div>"+battle_pilot(player,false,room)+"</div><div class='bp-hints'>";
    // The bound keys or the controller's buttons (battle_buttons below), as the hints elsewhere.
    const auto hint=[&](const char* token,const std::string& text){body+="<span><b>"+escape(text::expand_prompts(token,prompt_context(pad_mode)))+"</b> "+text+"</span>";};
    // With touch the buttons name themselves.
    if(!touch) {
        hint("{A}",label("battle_confirm"));hint("{L}",label("battle_change_weapon"));
        hint("{R}",label("battle_spirits"));
        if(responding)hint("{CLeft}",label("battle_guard_switch"));
        hint("{Anim}",label("battle_animation"));
        if(next.value("can_cancel",false))hint("{B}",label("battle_back"));
    }
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
    battle_doc=document(body,true);battle_doc->SetClass("modal",false);
    if(auto* first=kept.empty()?nullptr:battle_doc->GetElementById(kept))first->Focus();
    else if(auto* first=battle_doc->GetElementById(selecting_spirit?"battle-spirit-back":touch?(responding?"battle-counter":""):"battle-confirm"))first->Focus();
    battle_doc->UpdateDocument();battle_fit_units(battle_doc);

}

void choose(const std::string& id) {
    if(id=="mini-enter"){mini_stage::hotkey();return;}
    if(id=="intermission-funds" || id=="upgrade-funds"){funds_editing=id.substr(0,id.size()-6);return;}
    if(id.ends_with("-funds-input"))return;
    funds_editing.clear();
    // The update check (update_check.hpp): the start-up question, the About page, the title's hint.
    if(id.starts_with("update-ask:") || (update_ask_open && id=="settings-close")) {
        update::set_automatic(id=="update-ask:on");
        update_ask_open=false;physical_held=held();settings_open=false;return;
    }
    if(id=="update-check"){update::check(true);settings_focus=id;return;}
    if(id.starts_with("update-auto:")){update::set_automatic(id.ends_with(":on"));settings_focus=id;return;}
    if(id=="update-download" || id=="update-notes") {
        const auto s=update::status(update::site_language(localization::catalog().locale));
        open_page(id=="update-download"?s.latest.download:s.latest.notes);settings_focus=id;return;
    }
    if(id=="update-hd-download"){open_page(update::status(update::site_language(localization::catalog().locale)).hd.download);settings_focus=id;return;}
    if(id.starts_with("about-link:")) {
        const auto which=id.substr(11);
        open_page(which=="site"?std::string(update::kSite)+"/"+update::site_language(localization::catalog().locale)+"/":
                  which=="issues"?std::string(update::kIssues):std::string(update::kSource));
        settings_focus=id;return;
    }
    if(id=="update-open"){open_about(update::status("en").state==update::State::Available?"update-download":"update-hd-download");return;}
    if(id.starts_with("battle-") && !id.starts_with("battle-ui:") && battle_request.value("visible",false) && !settings_open){battle_page::answer(battle_request.at("serial"),id.substr(7));return;}
    if(id=="settings-open"){settings_open=true;settings_release.hold();input.clear();return;}
    // The MOD manager. Its campaign page switches campaign (or back to the main game) on the
    // title screen: a switch asks the launcher for the next start and closes the game as
    // the window would.
    if(id=="library-open"){viewer_open=false;library_open=true;mod_open=false;settings_open=true;settings_release.hold();input.clear();return;}
    if(library_open && id.starts_with("lib-tab:")){library_tab=id.back()=='1';library_tab_focus=true;return;}
    if(library_open && id.starts_with("lib-item:")){library_select(unsigned(std::stoul(id.substr(9))),true);return;}
    if(id=="viewer-open"){viewer_open=true;library_open=false;mod_open=false;viewer_picker.clear();settings_open=true;settings_release.hold();input.clear();return;}
    if(viewer_open && id=="settings-close" && !viewer_picker.empty()){viewer_cancel_picker();return;}   // B in a box: back to the page as it was
    if(viewer_open && id.starts_with("vw-")){viewer_choose(id);return;}
    if(id=="mod-open"){viewer_open=false;library_open=false;mod_open=true;settings_open=true;settings_focus="first";settings_release.hold();input.clear();return;}
    if(mod_open && id.starts_with("mod-page:")) {
        for(unsigned i=0;i<std::size(mod_pages);++i)if(id.substr(9)==mod_pages[i]){mod_page=i;settings_focus=id;}
        return;
    }
    if(mod_open && id=="mod-dialogue-reload"){dialogue::request_reload();settings_focus=id;return;}
    // Entering or leaving swaps the game's saves for the campaign's (campaign_switch.hpp)
    // and closes the manager on the title, where the campaign starts.
    const auto switched=[&](const std::function<void()>& change,const std::string& notice) {
        if(!campaign_switch::available() || !on_title())return;
        try{change();settings_open=false;notices::post("campaign",notice);}
        catch(const std::exception& error){notices::post("campaign",error.what());}
    };
    if(id=="dlc-leave" && mod_open && campaign::active()){switched(campaign_switch::leave,localization::catalog().ui("dlc_left"));return;}
    if(id.starts_with("dlc-enter:") && mod_open) {
        const auto index=std::stoul(id.substr(10));
        if(index<installed_campaigns().size()) {
            const auto& entry=installed_campaigns()[index];
            auto notice=localization::catalog().ui("dlc_entered");
            if(const auto at=notice.find("{name}");at!=std::string::npos)notice.replace(at,6,campaign_library::text(entry.name,localization::catalog().locale));
            switched([&]{campaign_switch::enter(entry);},notice);
        }
        return;
    }
    // The Controls page (controls_page): a capture, restore.
    if(id.starts_with("controls-bind:")){start_capture(id.substr(14));settings_focus=id;return;}
    if(id=="controls-cancel"){capture.queue.clear();return;}
    if(id=="controls-reset"){input::live_bindings().set(input::default_bindings());return;}
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
        const bool window=mode==1 || mode==3;
        if(id=="save-yes")save_page::answer(serial,window && save_request.value("window_cursor",0u)==0?"choose":"move:0");
        else if(id=="save-no")save_page::answer(serial,window && save_request.value("window_cursor",0u)==1?"cancel":"move:1");
        else if(id.starts_with("save:") && mode==0) {
            const unsigned n=unsigned(std::atoi(id.c_str()+5));
            save_page::answer(serial,n==save_request.value("cursor",0u)?"choose":"move:"+std::to_string(n));
        }
        return;
    }
    if(id.starts_with("tp-") && title_request.value("visible",false) && !settings_open) {
        const auto serial=title_request.at("serial").get<uint64_t>();
        if(id.starts_with("tp-option:")) {
            const unsigned n=unsigned(std::atoi(id.c_str()+10));
            title_page::answer(serial,n==title_request.value("cursor",0u)?"choose":"move:"+std::to_string(n));
        } else if(id.starts_with("tp-song:"))title_page::answer(serial,"jump:"+id.substr(8));
        else if(id=="tp-exit")title_page::answer(serial,"exit");
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
    if(id.starts_with("settings-page:")){for(unsigned i=0;i<std::size(settings_pages);++i)if(id.substr(14)==settings_pages[i])settings_show(i,id);return;}
    try {
        if(id.starts_with("rule:"))for(const auto& entry:rules::catalog)if(entry.id==id.substr(5))rules::set_fixes(rules::active_fixes()^entry.fix);
        if(id=="cheat-levels")cheat_levels_open=!cheat_levels_open;
        if(id=="bezel-off"){settings::set_bezel("");browse_kind.clear();}
        if(id=="filter-off"){settings::set_filter("");browse_kind.clear();}
        if(id.starts_with("filter-scale:"))settings::set_filter_scale(unsigned(std::stoul(id.substr(13))));
        if(id.starts_with("browse:")){const auto kind=id.substr(7);browse_kind=browse_kind==kind?std::string():kind;browse_dir.clear();}
        if(id.starts_with("open-folder:")){std::error_code error;std::filesystem::create_directories(id.substr(12),error);open_folder(id.substr(12));}
        if(id.starts_with("browse-dir:")){browse_dir=std::filesystem::path(id.substr(11));std::error_code error;std::filesystem::create_directories(browse_dir,error);}
        if(id=="browse-up") {
            const auto roots=browse_kind=="bezel"?settings::bezel_roots():settings::filter_roots();
            browse_dir=std::any_of(roots.begin(),roots.end(),[](const auto& root){return root.path==browse_dir;})?std::filesystem::path():browse_dir.parent_path();
        }
        if(id.starts_with("browse-pick:")) {
            const auto path=id.substr(12);
            if(browse_kind=="bezel")settings::set_bezel(path);else settings::set_filter(path);
            browse_kind.clear();
        }
        if(id.starts_with("cheat:"))for(const auto& entry:cheats::catalog)if(entry.id==id.substr(6))settings::set_cheats(settings::cheats()^entry.bit);
        if(id.starts_with("cheat-level:"))if(const auto colon=id.find(':',12);colon!=std::string::npos)cheats::request_level(unsigned(std::stoul(id.substr(12,colon-12))),unsigned(std::stoul(id.substr(colon+1))));
        if(id.starts_with("preset:"))for(const auto& preset:rules::presets)if(preset.key==id.substr(7))rules::set_fixes(preset.fixes);
        if(id.starts_with("locale:") && !input.has_composition())settings::request_locale(id.substr(7));
        if(id.starts_with("images:") && presentation::image_mode.enabled())presentation::image_mode.request(id=="images:hd");
        if(id.starts_with("battle-ui:"))settings::set_battle_ui(settings::battle_ui_from(id.substr(10)));
        if(id.starts_with("aspect:"))settings::set_wide_picture(id=="aspect:wide");
        if(id.starts_with("window:") && (id=="window:fullscreen")!=bool(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN))toggle_fullscreen();
        if(id.starts_with("window-size:"))scale_window(std::stoi(id.substr(12)));
        if(id.starts_with("ui-size:"))for(const auto size:{settings::UiSize::Standard,settings::UiSize::Large,settings::UiSize::Largest})
            if(id.substr(8)==settings::ui_size_name(size))settings::set_ui_size(size);
        if(id.starts_with("intermission-ui:"))settings::set_native_intermission_ui(id=="intermission-ui:native");
        if(id.starts_with("name-entry-ui:"))settings::set_native_name_entry_ui(id=="name-entry-ui:native");
        if(id.starts_with("title-ui:"))settings::set_native_title_ui(id=="title-ui:native");
        if(id.starts_with("fps:"))settings::set_show_fps(id=="fps:on");
        if(id.starts_with("dialogue-hints:"))settings::set_dialogue_hints_always(id=="dialogue-hints:always");
        if(id.starts_with("debug:"))settings::set_debug_interface(id=="debug:on");
        if(id=="debug-copy-run")SDL_SetClipboardText(settings::debug_endpoint().run.c_str());
        if(id=="report-export") {
            const auto result=bug_report::write(settings::data_folder(),output,report_facts());
            report_zip=result.zip;
            auto text=localization::catalog().ui(result.zip.empty()?"settings_report_failed":"settings_report_done");
            const std::string mark=result.zip.empty()?"{error}":"{path}";
            if(const auto at=text.find(mark);at!=std::string::npos)text.replace(at,mark.size(),result.zip.empty()?result.error:short_path(result.zip.string()));
            report_message=text;
        }
        if(id=="report-open" && !report_zip.empty())open_folder(report_zip.parent_path().string());
        if(id=="report-copy" && !report_zip.empty())SDL_SetClipboardText(report_zip.string().c_str());
        if(id=="report-issue")open_page(std::string(update::kIssues)+"/new/choose");
        if(id=="info-copy") {
            const auto text=bug_report::summary(report_facts());
            SDL_SetClipboardText(text.c_str());
            info_message=localization::catalog().ui("settings_info_copied")+"\n"+text;
        }
        if(id=="feedback-text")open_page("https://srw64.dreamquest.club/"+update::site_language(localization::catalog().locale)+"/story/");
        if(id.starts_with("autosave")) {
            auto c=save_store::settings();
            if(id.starts_with("autosave:"))c.autosave=id=="autosave:on";
            else if(id.starts_with("autosave-intermission:"))c.intermission=unsigned(std::stoi(id.substr(22)));
            else if(id.starts_with("autosave-turn:"))c.turn=unsigned(std::stoi(id.substr(14)));
            save_store::set_settings(c);
        }
        if(id=="save-export") {
            std::string error;const auto files=save_store::export_all(error);
            save_message=error.empty()?localization::catalog().ui("settings_save_exported"):error;
            if(const auto at=save_message.find("{n}");at!=std::string::npos)save_message.replace(at,3,std::to_string(files.size()));
        }
        if(id=="save-import-refresh"){save_candidates_read=false;save_message.clear();save_force.clear();}
        if(id.starts_with("save-import:") && id.size()>14) {
            const unsigned index=unsigned(id[12]-'0');const auto file=id.substr(14);
            bool intact=false;
            for(const auto& f:save_candidates)if(f.value("file",std::string())==file && index<f.value("slots",nlohmann::json::array()).size())
                intact=f.at("slots")[index].value("intact",false);
            if(!intact && save_force!=id){save_force=id;save_message=localization::catalog().ui("settings_save_import_warning");return;}
            std::string error;
            const auto number=save_store::import_slot(file,index,!intact,error);
            save_force.clear();
            save_message=number?localization::catalog().ui("settings_save_imported"):error;
            if(const auto at=save_message.find("{n}");number && at!=std::string::npos)save_message.replace(at,3,std::to_string(*number));
        }
    } catch(const std::exception& error){notices::post("settings-error",error.what());}
}
// Newly pressed N64 buttons on the battle page, from the keyboard table in
// dispatch() or from the controller. One binding for both:
// pad/stick move the focus by rows (Tab through every control), A or START activates
// it, B goes back, L opens the weapon list, R the spirits, C-down toggles the battle
// animation, C-left swaps 回避 and 防御.
void battle_buttons(uint32_t pressed,bool tab) {
    if(!battle_doc || !battle_request.value("visible",false) || settings_open)return;
    if(battle_request.value("style",std::string())=="hd" && !touch_battle_layout()){battle_hd_buttons(pressed);return;}
    const bool spirits=battle_request.value("spirit_menu",false);
    if(pressed&0x4000){choose("battle-back");return;}
    // The touch layout: the stick's left and right pick the response; the page's other
    // buttons are touch buttons now (touch_pad.hpp BattlePage).
    if(touch_battle_layout() && !spirits && (pressed&(0x0F00|(0xFu<<16)))) {
        if(battle_request.value("mode",0)==2 && (pressed&(0x0300|(0xCu<<16)))) {
            static constexpr const char* responses[]={"battle-counter","battle-evade","battle-defend"};
            const int now=battle_request.value("response",0),step=(pressed&(0x0200|(4u<<16)))?-1:1;
            choose(responses[std::clamp(now+step,0,2)]);
        }
        return;
    }
    if(!spirits) {
        if(pressed&0x0020){choose("battle-weapon");return;}
        if(pressed&0x0010){choose("battle-spirits");return;}
        if(pressed&(0x0004|input::pad_animation)){choose("battle-animation");return;}
        // C-left (the Deck's X) swaps 回避 and 防御 when an enemy attacks, as the modern
        // games' one-button guard switch; from 反撃 it goes to 回避. It never goes back to
        // 反撃, which opens the weapon list.
        if((pressed&0x0002) && battle_request.value("mode",0)==2) {
            choose(battle_request.value("response",0)==1?"battle-defend":"battle-evade");return;
        }
    }
    if(pressed&(0x0F00|(0xFu<<16))) {
        auto* focus=context->GetFocusElement();
        if(spirits || tab) {
            // The spirit list, and Tab anywhere, go through the controls in order.
            std::vector<Rml::Element*> items;
            if(spirits) {
                for(const auto& o:battle_request.at("spirit_options"))if(o.at("enabled").get<bool>())
                    if(auto* e=battle_doc->GetElementById("battle-cast:"+battle_number(o.at("crew"))+":"+battle_number(o.at("slot"))))items.push_back(e);
                items.push_back(battle_doc->GetElementById("battle-spirit-back"));
            } else for(const char* id:{"battle-confirm","battle-counter","battle-evade","battle-defend","battle-weapon","battle-spirits","battle-animation","battle-back"})
                if(auto* e=battle_doc->GetElementById(id))items.push_back(e);
            auto it=std::find(items.begin(),items.end(),focus);
            const int index=it==items.end()?0:int(it-items.begin());
            const bool reverse=pressed&(0x0800|0x0200|(1u<<16)|(1u<<18));
            items[(index+(reverse?int(items.size())-1:1))%items.size()]->Focus();return;
        }
        // The page's rows: 開始, then 反撃｜回避｜防御 when an enemy attacks, then the other
        // commands. Up and down change row (up from any response reaches 開始), left and
        // right move along one. Down into the responses lands on the one chosen.
        static constexpr const char* response_ids[]={"battle-counter","battle-evade","battle-defend"};
        std::vector<std::vector<Rml::Element*>> rows;
        const auto row=[&](std::initializer_list<const char*> ids) {
            std::vector<Rml::Element*> r;
            for(const char* id:ids)if(auto* e=battle_doc->GetElementById(id))r.push_back(e);
            if(!r.empty())rows.push_back(std::move(r));
        };
        row({"battle-confirm"});row({"battle-counter","battle-evade","battle-defend"});
        row({"battle-weapon","battle-spirits","battle-animation","battle-back"});
        if(rows.empty())return;
        size_t y=0,x=0;
        for(size_t r=0;r<rows.size();++r)for(size_t c=0;c<rows[r].size();++c)if(rows[r][c]==focus){y=r;x=c;}
        const int dy=pressed&(0x0800|(1u<<16))?-1:pressed&(0x0400|(1u<<17))?1:0;
        const int dx=dy?0:pressed&(0x0200|(1u<<18))?-1:1;
        if(dy) {
            if((dy<0 && y==0) || (dy>0 && y+1==rows.size()))return;
            y+=dy;x=0;
            if(rows[y][0]->GetId()==response_ids[0])
                if(auto* chosen=battle_doc->GetElementById(response_ids[std::clamp(battle_request.value("response",0),0,2)]))
                    x=size_t(std::find(rows[y].begin(),rows[y].end(),chosen)-rows[y].begin())%rows[y].size();
        } else x=size_t(std::clamp(int(x)+dx,0,int(rows[y].size())-1));
        rows[y][x]->Focus();return;
    }
    if(pressed&(0x8000|0x1000)) {
        auto* focus=context->GetFocusElement();
        choose(focus && focus->GetId().starts_with("battle-")?focus->GetId():"battle-confirm");
    }
}
// The frame-rate readout (settings show_fps): the game's frames per second and the longest
// frame over the last half second (frame_rate.hpp), in the top right corner.
Rml::ElementDocument* fps_doc{};
std::string fps_stamp;
struct {double at=-1;uint64_t lists=0;std::string text;} fps_window;
void fps_sync() {
    if(!settings::show_fps()){document_close(fps_doc);fps_stamp.clear();fps_window.at=-1;return;}
    const double now=system.GetElapsedTime();
    const uint64_t lists=frame_rate::lists.load();
    if(fps_window.at<0){fps_window={now,lists,""};frame_rate::longest_us.exchange(0);}
    else if(now-fps_window.at>=0.5) {
        const double fps=double(lists-fps_window.lists)/(now-fps_window.at);
        const unsigned longest=unsigned(std::lround(frame_rate::longest_us.exchange(0)/1000.0));
        char text[64];std::snprintf(text,sizeof text,"%.0f FPS · %u ms",fps,longest);
        fps_window={now,lists,text};
    }
    const std::string stamp=fps_window.text+frame_stamp();
    if(stamp!=fps_stamp){document_close(fps_doc);fps_stamp=stamp;if(!fps_window.text.empty())fps_doc=document("<div id='fps'>"+escape(fps_window.text)+"</div>",false,true);}
    if(fps_doc)fps_doc->PullToFront();
}
// Touch controls by scene on a phone (touch_pad.hpp, docs/design/touch-controls.md), or
// with SRW64_TOUCH_PAD=1 and a mouse anywhere. They show from the first touch and hide when
// a controller or the keyboard plays. Android's back key (SDL_ANDROID_TRAP_BACK_BUTTON,
// graphics.cpp) is B while held.
bool touch_supported() {
#ifdef __ANDROID__
    return true;
#else
    static const bool on=std::getenv("SRW64_TOUCH_PAD") && std::string(std::getenv("SRW64_TOUCH_PAD"))=="1";
    return on;
#endif
}
bool touch_shown=true,touch_back=false,touch_mouse_eaten=false;
touch_pad::SceneId touch_scene_id=touch_pad::SceneId::Other;
touch_pad::Fingers touch_fingers;
std::atomic<uint32_t> touch_held{0};
Rml::ElementDocument* touch_doc{};
std::string touch_stamp;
// A press shorter than a frame still reaches the game: what a quick tap or the back key
// let go stays pressed until 80 ms after it went down.
constexpr uint64_t touch_min_ms=80;
std::map<int64_t,uint64_t> touch_down_at;
uint64_t touch_back_at=0;
std::atomic<uint32_t> touch_linger{0};
std::atomic<uint64_t> touch_linger_until{0};
void touch_keep(uint32_t bits,uint64_t down_at) {
    const uint64_t until=down_at+touch_min_ms;
    if(!bits || SDL_GetTicks64()>=until)return;
    touch_linger|=bits;
    if(touch_linger_until.load()<until)touch_linger_until=until;
}
bool touch_active(){return touch_supported() && touch_shown && touch_scene_id!=touch_pad::SceneId::Hidden;}
void set_pad_mode(bool on);
// Touch play hints the controller in the Steam Deck's names; a real controller's family
// comes back when it plays.
bool touch_family=false;
uint8_t touch_saved_family=0;
void touch_hints(bool touching) {
    if(touching) {
        if(!touch_family){touch_saved_family=input::pad_family;input::pad_family=1;touch_family=true;}
        set_pad_mode(true);
    } else if(touch_family){input::pad_family=touch_saved_family;touch_family=false;}
}
touch_pad::Layout touch_layout() {
    float ddpi=0;
    const float mm=SDL_GetDisplayDPI(0,&ddpi,nullptr,nullptr)==0 && ddpi>0?ddpi/25.4f:pixel_ratio*96/25.4f;
    return touch_pad::layout(float(pixels_w),float(pixels_h),mm);
}
// The scene: our all-touch windows, our pages, or the game's own.
touch_pad::SceneId touch_scene_now() {
    if(settings_open || library_open)return touch_pad::SceneId::Hidden;
    if(battle_request.value("visible",false))
        return battle_request.value("spirit_menu",false)?touch_pad::SceneId::BattleSpirits:touch_pad::SceneId::BattlePage;
    if(names::request().visible || link_request.visible ||
       intermission_request.value("visible",false) || upgrade_request.value("visible",false) || parts_request.value("visible",false) ||
       ability_request.value("visible",false) || swap_request.value("visible",false) || save_request.value("visible",false) ||
       title_request.value("visible",false))return touch_pad::SceneId::Page;
    // The game's own scene, as the input callback saw it (touch_scene.hpp).
    return touch_pad::SceneId(touch_scene::current().load());
}
void touch_publish(){touch_held=touch_fingers.buttons()|(touch_back?touch_pad::bits::B:0);}
// Our pre-battle page lays itself out for touch while the touch controls show.
bool touch_battle_layout(){return touch_supported() && touch_shown;}
// Does the mouse SDL makes from a finger belong to the controls (not to a page under them)?
bool touch_owns_point(float x,float y) {
    const auto layout=touch_layout();const auto scene=touch_pad::scene(touch_scene_id);
    return touch_pad::hit(layout,scene,x,y) || touch_pad::in_stick_area(layout,scene,x,y) || scene.tap_primary;
}
// The controls' events; true when they took them.
bool touch_event(const SDL_Event& event) {
    if(!touch_supported())return false;
    switch(event.type) {
    case SDL_FINGERDOWN: {
        touch_shown=true;touch_hints(true);
        if(!touch_active() || !touch_fingers.down(touch_layout(),touch_pad::scene(touch_scene_id),event.tfinger.fingerId,
                                                  event.tfinger.x*pixels_w,event.tfinger.y*pixels_h))return false;
        touch_down_at[event.tfinger.fingerId]=SDL_GetTicks64();
        touch_publish();return true;
    }
    case SDL_FINGERMOTION:
        if(!touch_fingers.move(touch_layout(),touch_pad::scene(touch_scene_id),event.tfinger.fingerId,event.tfinger.x*pixels_w,event.tfinger.y*pixels_h))return false;
        touch_publish();return true;
    case SDL_FINGERUP: {
        const uint32_t before=touch_fingers.buttons();
        // A button that opens one of our pages does it when let go on it.
        if(const auto* finger=touch_fingers.find(event.tfinger.fingerId);finger && finger->kind==touch_pad::Fingers::Kind::Button) {
            const auto& action=touch_pad::scene(touch_scene_id)[finger->slot];
            if(!action.command.empty() && touch_pad::hit(touch_layout(),touch_pad::scene(touch_scene_id),event.tfinger.x*pixels_w,event.tfinger.y*pixels_h)==finger->slot)
                choose(std::string(action.command));
        }
        if(!touch_fingers.up(event.tfinger.fingerId))return false;
        touch_keep(before&~touch_fingers.buttons(),touch_down_at[event.tfinger.fingerId]);
        touch_down_at.erase(event.tfinger.fingerId);
        touch_publish();return true;
    }
    // The mouse SDL makes from the first finger: not for the pages under the controls.
    case SDL_MOUSEBUTTONDOWN:
        if(event.button.which!=SDL_TOUCH_MOUSEID || !touch_active() || !touch_owns_point(event.button.x*pixel_ratio,event.button.y*pixel_ratio))return false;
        touch_mouse_eaten=true;return true;
    case SDL_MOUSEMOTION:
        return event.motion.which==SDL_TOUCH_MOUSEID && touch_mouse_eaten;
    case SDL_MOUSEBUTTONUP:
        if(event.button.which!=SDL_TOUCH_MOUSEID || !touch_mouse_eaten)return false;
        touch_mouse_eaten=false;return true;
    case SDL_KEYDOWN: case SDL_KEYUP:
        // Through sdl2-compat the back key has no scancode and SDL3's keycode (0x4000011A,
        // SDL2's SDLK_AC_BACK is 0x4000010E).
        if(event.key.keysym.scancode==SDL_SCANCODE_AC_BACK || event.key.keysym.sym==SDLK_AC_BACK || event.key.keysym.sym==SDL_Keycode(0x4000011A)) {
            if(event.type==SDL_KEYDOWN && !event.key.repeat)touch_back_at=SDL_GetTicks64();
            if(event.type==SDL_KEYUP && touch_back)touch_keep(touch_pad::bits::B,touch_back_at);
            touch_back=event.type==SDL_KEYDOWN;touch_publish();return true;
        }
        if(event.type==SDL_KEYDOWN && event.key.windowID)touch_shown=false;
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        touch_shown=false;break;
    case SDL_CONTROLLERAXISMOTION:
        if(std::abs(int(event.caxis.value))>input::axis_threshold)touch_shown=false;
        break;
    default:break;
    }
    if(!touch_shown){touch_hints(false);if(!touch_fingers.empty()){touch_fingers.clear();touch_publish();}}
    return false;
}
// A label's size in a button: CJK a whole em, Latin about half, so it fits the width.
float touch_label_size(const std::string& text,float room,float largest) {
    float ems=0;
    for(size_t i=0;i<text.size();){const auto c=uint8_t(text[i]);const int n=c<0x80?1:c<0xE0?2:c<0xF0?3:4;ems+=n==1?.58f:1.f;i+=n;}
    return std::min(largest,ems>0?room/ems:largest);
}
void touch_sync() {
    touch_scene_id=touch_scene_now();
    if(!touch_active()) {
        if(!touch_fingers.empty()){touch_fingers.clear();touch_publish();}
        document_close(touch_doc);touch_stamp.clear();return;
    }
    const auto layout=touch_layout();
    const auto scene=touch_pad::scene(touch_scene_id);
    const auto* stick=touch_fingers.stick();
    const float mm=layout.mm;
    const auto px=[](float v){return std::to_string(int(std::lround(v)))+"px";};
    std::string stamp=frame_stamp()+"/"+std::to_string(mm)+"/"+std::to_string(int(touch_scene_id))+"/"+localization::catalog().locale+"/"+
        std::to_string(touch_fingers.buttons());
    for(size_t i=0;i<touch_pad::slot_count;++i)stamp+=touch_fingers.pressed(touch_pad::Slot(i))?'1':'0';
    if(stick)stamp+="/"+std::to_string(int(stick->cx))+","+std::to_string(int(stick->cy))+","+std::to_string(int(stick->x/mm))+","+std::to_string(int(stick->y/mm));
    if(stamp==touch_stamp){if(touch_doc)touch_doc->PullToFront();return;}
    touch_stamp=stamp;document_close(touch_doc);
    // Physical sizes: pixels, whatever the interface size.
    const auto face=[&](bool down,float alpha){
        char fill[16];std::snprintf(fill,sizeof fill,"#ffffff%02x",int((down?.44f:.18f)*alpha*255));
        return std::string("background-color:")+fill+";border-width:"+px(.35f*mm)+";border-color:#ffffff99;color:#ffffffe0;text-align:center;font-weight:bold;";
    };
    std::string body;
    // The stick: under the thumb while held; at rest a faint one in the corner.
    if(scene.stick==touch_pad::Stick::Wide || scene.stick==touch_pad::Stick::Corner) {
        const bool corner=scene.stick==touch_pad::Stick::Corner;
        const float cx=stick?stick->cx:layout.rest_x,cy=stick?stick->cy:layout.rest_y,r=layout.stick_radius;
        const float alpha=stick||corner?1.f:.6f;
        body+="<div style='position:absolute;left:"+px(cx-r)+";top:"+px(cy-r)+";width:"+px(2*r)+";height:"+px(2*r)+";border-radius:"+px(r)+";"+face(false,alpha)+"'></div>";
        const uint32_t held=stick?stick->bits:0;
        const struct {uint32_t bit;float dx,dy;int turn;} arrows[]={{touch_pad::bits::Up,0,-1,0},{touch_pad::bits::Down,0,1,180},
            {touch_pad::bits::Left,-1,0,270},{touch_pad::bits::Right,1,0,90}};
        for(const auto& a:arrows) {
            const float ax=cx+a.dx*r*.7f,ay=cy+a.dy*r*.7f,size=2.8f*mm;
            body+="<div style='position:absolute;left:"+px(ax-size/2)+";top:"+px(ay-size/2)+";width:"+px(size)+";height:"+px(size)+
                  ";font-size:"+px(size*.8f)+";line-height:"+px(size)+";text-align:center;color:"+(held&a.bit?"#ffffff":"#ffffffa0")+
                  ";transform:rotate("+std::to_string(a.turn)+"deg);'>&#x25B2;</div>";
        }
        // The knob follows the thumb, as far as the rim.
        float kx=cx,ky=cy;
        if(stick){const float dx=stick->x-cx,dy=stick->y-cy,d=std::hypot(dx,dy),reach=r*.55f;kx=cx+(d>reach?dx*reach/d:dx);ky=cy+(d>reach?dy*reach/d:dy);}
        const float k=3.2f*mm;
        body+="<div style='position:absolute;left:"+px(kx-k)+";top:"+px(ky-k)+";width:"+px(2*k)+";height:"+px(2*k)+";border-radius:"+px(k)+";"+face(stick!=nullptr,alpha)+"'></div>";
    }
    for(size_t i=0;i<touch_pad::slot_count;++i) {
        const auto& action=scene.slots[i];
        if(!action.filled())continue;
        const auto& place=layout.slots[i];
        const float w=place.round?2*place.r:place.w,h=place.round?2*place.r:place.h;
        std::string text=label(std::string(action.label));
        if(action.command=="battle-animation")text+=" "+label(battle_request.value("animation",true)?"battle_on":"battle_off");
        const float size=touch_label_size(text,w*(place.round?.76f:.86f),place.round?(i==size_t(touch_pad::Slot::Primary)?3.8f:2.8f)*mm:std::min(2.4f*mm,h*.5f));
        body+="<div style='position:absolute;left:"+px(place.x-w/2)+";top:"+px(place.y-h/2)+";width:"+px(w)+";height:"+px(h)+";border-radius:"+px(h/2)+";"+
              face(touch_fingers.pressed(touch_pad::Slot(i)),1)+(action.command=="battle-confirm"?"background-color:#1fb85ac0;":"")+"font-size:"+px(size)+";line-height:"+px(h)+";white-space:nowrap;'>"+escape(text)+"</div>";
    }
    touch_doc=document(body,false,true);
    touch_doc->PullToFront();
}
void notices_sync() {
    const double now=system.GetElapsedTime();
    while(!banners.empty() && banners.front().until<=now)banners.pop_front();
    {std::lock_guard lock(notice_mutex);while(!notices_pending.empty() && banners.size()<3){banners.push_back({notices_pending.front().at("text"),now+6});notices_pending.pop_front();}}
    std::string stamp,body="<div id='notices'>";
    for(const auto& b:banners){stamp+=b.text+std::to_string(b.until);body+="<div class='banner'>"+escape(b.text)+"</div>";}
    body+="</div>";
    if(stamp!=notice_stamp){document_close(notice_doc);notice_stamp=stamp;if(!banners.empty())notice_doc=document(body,false,true);}
    if(notice_doc)notice_doc->PullToFront();
}
void initialize() {
    // The update check's state and, if the player chose it, the day's check (update_check.hpp).
    static const bool update_started=[]{update::init(SRW64_VERSION);update::check_on_start();return true;}();
    (void)update_started;
    layered=std::make_unique<LayeredRender>(renderer->get_rml_interface());
    Rml::SetSystemInterface(&system);Rml::SetRenderInterface(layered.get());
    if(!Rml::Initialise())throw std::runtime_error("Cannot initialize shared UI");
    initialized=true;
    Rml::Factory::RegisterElementInstancer("layer-mark",&layer_mark_instancer);
    slant_instancer=std::make_unique<SlantInstancer>();Rml::Factory::RegisterDecoratorInstancer("slant",slant_instancer.get());
    const auto bytes=[](const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return std::vector<Rml::byte>{std::istreambuf_iterator<char>(input),{}};};
    std::filesystem::path path;
    const char* font_dir=std::getenv("SRW64_FONT_DIR");
    if(const char* explicit_font=std::getenv("SRW64_UI_FONT"))path=explicit_font;
    else if(font_dir && *font_dir) {
        // The packaged fonts (tools/content/prepare_fonts.py): SC for every
        // language, Condensed for English documents, the symbol font last.
        // Both HarmonyOS files are variable; RmlUi opens the named instance
        // of the requested weight (Normal: Regular).
        const std::filesystem::path dir(font_dir);
        path=dir/"HarmonyOS_Sans_SC.ttf";
        for(const auto* name:{"HarmonyOS_Sans_SC.ttf","HarmonyOS_Sans_Condensed.ttf","SRW64Symbols.ttf","SRW64Prompts.ttf"})
            if(!std::filesystem::is_regular_file(dir/name))throw std::runtime_error("Missing font "+(dir/name).string()+": run tools/content/prepare_fonts.py");
    }
    else for(const auto* candidate:{"/System/Library/Fonts/Supplemental/Arial Unicode.ttf","C:/Windows/Fonts/msyh.ttc","/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"})
        if(std::filesystem::is_regular_file(candidate)){path=candidate;break;}
    font=bytes(path);
    if(font.empty() || !Rml::LoadFontFace(font,"srw64-ui",Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal,true))
        throw std::runtime_error("Shared UI needs a CJK font: set SRW64_UI_FONT to a local TTF/OTF/TTC file");
    if(!std::getenv("SRW64_UI_FONT") && font_dir && *font_dir) {
        const std::filesystem::path dir(font_dir);
        english_font=bytes(dir/"HarmonyOS_Sans_Condensed.ttf");symbol_font=bytes(dir/"SRW64Symbols.ttf");
        if(Rml::LoadFontFace(english_font,"srw64-ui-en",Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal,false))
            english_font_family()="srw64-ui-en";
        if(!Rml::LoadFontFace(symbol_font,"srw64-ui-symbols",Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal,true))
            throw std::runtime_error("Cannot load the symbol font");
        // Button icons (tools/content/build_prompt_font.py), reached only through the tokens' PUA characters.
        prompt_font=bytes(dir/"SRW64Prompts.ttf");
        if(!Rml::LoadFontFace(prompt_font,"srw64-ui-prompts",Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal,true))
            throw std::runtime_error("Cannot load the button prompt font");
    }
    else if(!std::getenv("SRW64_UI_FONT") && path.filename()=="Arial Unicode.ttf")
        for(const auto* chinese:{"/System/Library/Fonts/Hiragino Sans GB.ttc"})
            if(std::filesystem::is_regular_file(chinese)) {
                chinese_font=bytes(chinese);
                if(!chinese_font.empty() && Rml::LoadFontFace(chinese_font,"srw64-ui-zh",Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal,false))
                    chinese_font_family()="srw64-ui-zh";
                break;
            }
    context=Rml::CreateContext("game-ui",{pixels_w,pixels_h},nullptr,&input);
    if(!context)throw std::runtime_error("Cannot create shared UI context");input.bind(*context);
    name_page=std::make_unique<NamePage>(*context,NameActions{names::select,names::choose,names::review,
        [](const std::string& path,int dp){return image(path,dp_pixels(dp));}});
    std::ofstream(output/"shared-ui.json")<<json({{"schema","srw64.shared-ui.v1"},{"backend","SDL2/RmlUi/RT64"},{"font",path.string()},{"chinese_font",chinese_font_family()},{"english_font",english_font_family()}}).dump(2)<<'\n';
}
bool held() {
    int count=0;const auto* keys=SDL_GetKeyboardState(&count);
    for(int i=0;i<count;++i)if(keys[i])return true;
    return debug::keyboard().held()!=0;
}
bool dispatch(SDL_Event& event);
std::array<std::string*,14> all_stamps();
// Hints follow the last input device. Switching rebuilds every page once, the way
// a language change does, so no page shows the other device's keys.
void set_pad_mode(bool on) {
    if(pad_mode==on)return;
    pad_mode=on;input::pad_hints=on;
    for(auto* stamp:all_stamps())stamp->clear();
}
void set_pointer_mode(bool on) {
    if(pointer_mode==on)return;
    pointer_mode=on;
    for(int i=0;i<context->GetNumDocuments();++i)context->GetDocument(i)->SetClass("pointer",on);
}
// Controller presses, with held directions repeating like a held key (400 ms, then
// every 80 ms); `repeats` marks the repeated directions.
uint32_t pad_presses(uint32_t now,uint32_t pressed,uint32_t& repeats) {
    static constexpr uint32_t directions[]={0x0800|(1u<<16),0x0400|(1u<<17),0x0200|(1u<<18),0x0100|(1u<<19)};
    static std::array<uint64_t,4> next{};
    const uint64_t ms=SDL_GetTicks64();repeats=0;
    for(size_t i=0;i<std::size(directions);++i) {
        if(pressed&directions[i])next[i]=ms+400;
        else if((now&directions[i]) && ms>=next[i]){repeats|=directions[i];next[i]=ms+80;}
    }
    return pressed|repeats;
}
void send_key(SDL_Keycode key,bool repeat,Uint16 mod=KMOD_NONE) {
    // windowID 0 marks a key the controller sent: it keeps the controller hints.
    SDL_Event e{};e.type=SDL_KEYDOWN;e.key.state=SDL_PRESSED;e.key.repeat=repeat;
    e.key.keysym.sym=key;e.key.keysym.scancode=SDL_GetScancodeFromKey(key);e.key.keysym.mod=mod;
    dispatch(e);
    e.type=SDL_KEYUP;e.key.state=SDL_RELEASED;e.key.repeat=0;dispatch(e);
}
// The other native pages read the keyboard in the classic key map (Z = A,
// X = B, Enter = START, Space = Z, arrows = pad and stick, Q/E = L/R, IJKL = C;
// input::classic_keys), which follow_bindings turns the player's keys into.
// A controller drives them with the same keys, so a Steam Deck needs no keyboard.
// Funds entry and the name page take A as Enter and B as Esc.
void pad_keys(uint32_t now,uint32_t pressed) {
    const bool text=names::request().visible || !funds_editing.empty();
    const std::pair<uint32_t,SDL_Keycode> keys[]={{0x8000,text?SDLK_RETURN:SDLK_z},{0x4000,text?SDLK_ESCAPE:SDLK_x},
        {0x1000,SDLK_RETURN},{0x2000,SDLK_SPACE},{0x0020,SDLK_q},{0x0010,SDLK_e},{0x0008,SDLK_i},{0x0004,SDLK_k},
        {0x0002,SDLK_j},{0x0001,SDLK_l},{0x0800|(1u<<16),SDLK_UP},
        {0x0400|(1u<<17),SDLK_DOWN},{0x0200|(1u<<18),SDLK_LEFT},{0x0100|(1u<<19),SDLK_RIGHT}};
    uint32_t repeats=0;const uint32_t presses=pad_presses(now,pressed,repeats);
    for(const auto& [mask,key]:keys)if(presses&mask)send_key(key,(repeats&mask) && !(pressed&mask));
}
// The focused settings control, pressed by A, START, Enter, Z or Space.
void settings_press() {
    auto* focus=context->GetFocusElement();
    if(focus && settings_doc && focus->GetOwnerDocument()==settings_doc && focus->GetTagName()=="button")focus->Click();
}
// The settings window on a controller: L1/R1 turn the page, the directions move as in
// settings_move, A presses the focused control, B (or View again) closes the window.
void settings_pad(uint32_t now,uint32_t pressed) {
    if(!settings_doc)return;
    if(pressed&0x4000){choose("settings-close");return;}
    if(pressed&(0x8000|0x1000)){settings_press();return;}
    if(pressed&(0x0020|0x0010)){settings_turn(pressed&0x0020?-1:1);return;}
    if((pressed&0x0002) && viewer_open && viewer_kind()=="song"){viewer_choose("vw-listen");return;}   // C-left (the Deck's X)
    if(library_open && (now&(0x0008|0x0004))){library_scroll(now&0x0008?-9.f:9.f);return;}      // C-up / C-down held: the details scroll
    uint32_t repeats=0;const uint32_t presses=pad_presses(now,pressed,repeats);
    const int dy=presses&(0x0800|(1u<<16))?-1:presses&(0x0400|(1u<<17))?1:0;
    const int dx=presses&(0x0200|(1u<<18))?-1:presses&(0x0100|(1u<<19))?1:0;
    if(dy || dx)settings_move(dy,dy?0:dx);
}
void sync() {
    physical_held=held();
    // A dp is a point, fewer when the window is under 960 x 720 points, so the pages always
    // have that much room; the player's Interface size then enlarges it, as far as the
    // window keeps 800 x 540 dp (a Steam Deck's 1280 x 800: Large 1.25, Largest 1.48).
#ifdef __ANDROID__
    // An SDL point is a screen pixel there: lay out in Android's density-independent pixels
    // (160 per inch) instead, 3 screen pixels on the Seeker's 480 dpi.
    float ddpi=0;
    const float layout_ratio=SDL_GetDisplayDPI(0,&ddpi,nullptr,nullptr)==0 && ddpi>0?ddpi/160.f:pixel_ratio;
#else
    const float layout_ratio=pixel_ratio;
#endif
    // Measured on the pages' room (page_area), so a 4:3 picture keeps them 800 x 540 dp too.
    const auto room=page_area();
    const float points_w=room.w/layout_ratio,points_h=room.h/layout_ratio;
    const float fit=std::min({1.f,points_w/960.f,points_h/720.f});
    ui_density=layout_ratio*std::min(fit*settings::ui_scale(settings::ui_size()),std::max(fit,std::min(points_w/800.f,points_h/540.f)));
    context->SetDimensions({pixels_w,pixels_h});context->SetDensityIndependentPixelRatio(ui_density);input.set_scale(pixel_ratio);
    const auto language=localization::snapshot();localization::Scope scope(language);
    auto request=names::request();
    // Name page (name_page.cpp): it sizes and resolves its own images through the
    // action above; every name is a default, shown in the reading language
    // (docs/native/default-names.md).
    const auto shown=[&](names::Field field,std::u16string& name){name=utf16(names::default_names().display(field,utf8(name),language->locale));};
    for(auto& choice:request.choices)for(auto& person:choice.names){shown(names::Field::Name,person[0]);shown(names::Field::Surname,person[1]);}
    for(auto& person:request.names){shown(names::Field::Name,person[0]);shown(names::Field::Surname,person[1]);shown(names::Field::Nick,person[2]);}
    name_page->set_hd(presentation::image_mode.current()==1);
    {const auto a=page_area();name_page->set_area(a.x,a.y,a.w,a.h);}
    // Catalog owns all labels. No duplicate translation table in the frontend.
    auto labels=language->ui_labels();
    if(pad_mode)for(auto& [key,text]:labels)if(auto pad=labels.find(key+"_pad");pad!=labels.end())text=pad->second;
    for(auto& [key,value]:labels)value=text::expand_prompts(value,prompt_context(pad_mode));
    name_page->sync(request,labels,language->locale);
    {
        // Controller edges; a button already down when a page opens is not a press.
        static uint32_t pad_before=0;
        const uint32_t pad_now=srw64_pad_state(),pad_pressed=pad_now&~pad_before;pad_before=pad_now;
        if(pad_pressed){set_pad_mode(true);set_pointer_mode(false);}
        // New bindings from the Controls page: every hint is rebuilt with them.
        static uint64_t bindings_seen=input::live_bindings().revision();
        if(const auto revision=input::live_bindings().revision();revision!=bindings_seen) {
            bindings_seen=revision;hint_bindings=input::live_bindings().get();
            for(auto* stamp:all_stamps())stamp->clear();
        }
        // The host's buttons, from the controller or keys bound to them (the keyboard state
        // holds both): the settings window, the next language, Original / HD.
        static uint32_t host_before=0;
        const uint32_t host_now=srw64_keyboard_state()&(input::pad_view|input::pad_language|input::pad_images);
        const uint32_t host_pressed=host_now&~host_before;host_before=host_now;
        const bool view_pressed=host_pressed&input::pad_view;
        // Page requests stay null until their page first reports.
        const auto shown=[](const json& request){return request.is_object() && request.value("visible",false);};
        // A capture takes the controller's presses itself (capture_event), and once it has one
        // the page ignores the controller until it is let go.
        if(capture.release && !pad_now)capture.release=false;
        if(!capture.queue.empty() && SDL_GetTicks64()-capture.since>6000)capture.queue.clear();
        const bool capturing=!capture.queue.empty() || capture.release;
        if(!capturing && (host_pressed&input::pad_language))settings::request_locale(localization::next_locale(localization::catalog().locale));
        if(!capturing && (host_pressed&input::pad_images))presentation::image_mode.toggle();
        if(capturing && settings_open){}
        else if(view_pressed)choose(settings_open?"settings-close":"settings-open");
        else if(settings_open)settings_pad(pad_now,pad_pressed);
        else if(shown(battle_request)){if(pad_pressed)battle_buttons(pad_pressed);}
        else if(!settings_open && (names::request().visible || link_request.visible || shown(intermission_request) || shown(upgrade_request) || shown(parts_request) || shown(ability_request) || shown(swap_request) || shown(save_request) || shown(title_request)))
            pad_keys(pad_now,pad_pressed);
    }
    if((funds_editing=="intermission" && !intermission_page::state().value("visible",false)) || (funds_editing=="upgrade" && !upgrade_page::state().value("visible",false)))funds_editing.clear();
    link_sync();battle_sync();intermission_sync();upgrade_sync();parts_sync();ability_sync();swap_sync();save_sync();title_sync();mini_sync();home_sync();bezel_sync();
    app_menu::update({language->ui("settings_open"),language->ui("dialogue_reload"),language->ui("menu_view"),
                      language->ui("menu_fullscreen"),language->ui("menu_window_scale"),language->ui("menu_about"),language->ui("menu_check_updates")},window_menu_state());
    if(app_menu::take_settings_request())choose("settings-open");
    if(app_menu::take_about_request())open_about("");
    if(app_menu::take_update_request()){open_about("update-check");update::check(true);}
    // The first time the title waits, the question whether to check for updates on start-up.
    if(!update_asked && !settings_open && intro::title_waiting() && update::should_ask()) {
        update_asked=true;update_ask_open=true;settings_open=true;settings_release.hold();input.clear();
    }
    if(app_menu::take_reload_request())srw64::dialogue::request_reload();
    if(app_menu::take_fullscreen_request())toggle_fullscreen();
    if(const int n=app_menu::take_scale_request())scale_window(n);
    // A viewer battle back on the title opens the page again with the same choices.
    if(viewer_title() && battle_viewer::take_returned()){viewer_open=true;library_open=false;mod_open=false;settings_open=true;settings_release.hold();input.clear();}
    battle_viewer::set_page_open(settings_open && viewer_open);
    // Any page in the settings window's frame (the Library, MOD, the settings themselves)
    // holds the title still under it: no attract demo, no opening story.
    battle_viewer::hold_title(settings_open);
    settings_sync();notices_sync();fps_sync();touch_sync();context->Update();input.update_rectangle();
    names::window_claim_input(request.visible || (names::owns_input() && held()));
    link_page::window_claim_input(link_request.visible || (link_page::owns_input() && held()));
    battle_page::window_claim_input(battle_request.value("visible",false) || (battle_page::owns_input() && held()));
    intermission_page::window_claim_input(intermission_request.value("visible",false) || (intermission_page::owns_input() && held()));
    upgrade_page::window_claim_input(upgrade_request.value("visible",false) || (upgrade_page::owns_input() && held()));
    parts_page::window_claim_input(parts_request.value("visible",false) || (parts_page::owns_input() && held()));
    ability_page::window_claim_input(ability_request.value("visible",false) || (ability_page::owns_input() && held()));
    swap_page::window_claim_input(swap_request.value("visible",false) || (swap_page::owns_input() && held()));
    save_page::window_claim_input(save_request.value("visible",false) || (save_page::owns_input() && held()));
    title_page::window_claim_input(title_request.value("visible",false) || (title_page::owns_input() && held()));
}
bool dispatch(SDL_Event& event) {
    if(!context)return false;
    if(touch_event(event))return true;
    if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_CLOSE)return false;
    // A capture on the Controls page takes the next key or controller input.
    if(!capture.queue.empty() && settings_open && capture_event(event))return true;
    if(event.type==SDL_DROPFILE) {
        // Debug aid: a mini-stage file dropped on the title menu loads and enters, only while
        // the debug interface is on; otherwise mini stages come in through it (mini_stage.load).
        const std::string path=event.drop.file?event.drop.file:"";SDL_free(event.drop.file);
        if(!settings::debug_interface() && !settings::debug_interface_forced())return true;
        try {notices::post("mini-stage",mini_stage::load_file(path));}
        catch(const std::exception& error){notices::post("mini-stage-error",error.what());}
        return true;
    }
    // The keyboard (window keys, not the controller bridge's) and the mouse or touch
    // screen bring the keyboard hints back.
    // The mouse SDL makes from a finger keeps the controller hints (the on-screen controller's).
    const bool finger_mouse=(event.type==SDL_MOUSEMOTION && event.motion.which==SDL_TOUCH_MOUSEID) ||
        ((event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP) && event.button.which==SDL_TOUCH_MOUSEID) ||
        (event.type==SDL_MOUSEWHEEL && event.wheel.which==SDL_TOUCH_MOUSEID);
    if((event.type==SDL_KEYDOWN && event.key.windowID) || (!finger_mouse && (event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEWHEEL ||
       (event.type==SDL_MOUSEMOTION && std::abs(event.motion.xrel)+std::abs(event.motion.yrel)>6))))set_pad_mode(false);
    if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEWHEEL || (event.type==SDL_MOUSEMOTION && std::abs(event.motion.xrel)+std::abs(event.motion.yrel)>6))set_pointer_mode(true);
    else if(event.type==SDL_KEYDOWN && event.key.windowID)set_pointer_mode(false);
    if(input.event(event))return true;
    if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_F7 && !input.has_composition() &&
       !(event.key.keysym.mod&(KMOD_GUI|KMOD_ALT|KMOD_CTRL|KMOD_SHIFT))){
        if(!event.key.repeat)settings::request_locale(localization::next_locale(localization::catalog().locale));return true;
    }
    // F5 reads the dialogue text files again on every screen, native pages included.
    if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_F5 && !input.has_composition() &&
       !(event.key.keysym.mod&(KMOD_GUI|KMOD_ALT|KMOD_CTRL|KMOD_SHIFT))){
        if(!event.key.repeat)srw64::dialogue::request_reload();return true;
    }
#ifndef __APPLE__
    // F11 toggles full screen where there is no menu bar (a Mac has View > Full Screen, ⌃⌘F).
    if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_F11 && !on_steam_deck() &&
       !(event.key.keysym.mod&(KMOD_GUI|KMOD_ALT|KMOD_CTRL|KMOD_SHIFT))){
        if(!event.key.repeat)toggle_fullscreen();return true;
    }
#endif
    if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_COMMA && (event.key.keysym.mod&(KMOD_CTRL|KMOD_GUI))){choose("settings-open");return true;}
    // After a modal closes, game keys must not activate stale UI focus.
    if(!settings_open && !names::request().visible && !link_request.visible && !battle_request.value("visible",false) && !intermission_request.value("visible",false) && !upgrade_request.value("visible",false) && !parts_request.value("visible",false) && !ability_request.value("visible",false) && !swap_request.value("visible",false) && !save_request.value("visible",false) && !title_request.value("visible",false) &&
       (event.type==SDL_KEYDOWN || event.type==SDL_KEYUP))return false;
    // Typing into the funds box takes the keys as they are. The name page has nothing to
    // type since the names are fixed, so it follows the bindings too.
    if(funds_editing.empty())if(const auto host=follow_bindings(event)) {
        if(event.type==SDL_KEYDOWN && !event.key.repeat) {
            if(*host==input::Action::Settings)choose(settings_open?"settings-close":"settings-open");
            else if(*host==input::Action::Language)settings::request_locale(localization::next_locale(localization::catalog().locale));
            else presentation::image_mode.toggle();
        }
        return true;
    }
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
        // The game's own key map, as on the other pages: Z/Enter = A, X/Esc = B,
        // arrows and WASD = pad and stick, Q/E = L/R; the page keys and Ctrl+Tab turn too.
        if(event.type==SDL_KEYDOWN){
            const auto k=event.key.keysym.sym;const bool ctrl=event.key.keysym.mod&KMOD_CTRL,shift=event.key.keysym.mod&KMOD_SHIFT;
            if(k==SDLK_ESCAPE || k==SDLK_x){if(!event.key.repeat)choose("settings-close");return true;}
            if(k==SDLK_q || k==SDLK_PAGEUP || (k==SDLK_TAB && ctrl && shift)){if(!event.key.repeat)settings_turn(-1);return true;}
            if(library_open && (k==SDLK_i || k==SDLK_k)){library_scroll(k==SDLK_i?-60.f:60.f);return true;}   // C-up / C-down
            if(k==SDLK_e || k==SDLK_PAGEDOWN || (k==SDLK_TAB && ctrl)){if(!event.key.repeat)settings_turn(1);return true;}
            if(k==SDLK_j && viewer_open && viewer_kind()=="song"){if(!event.key.repeat)viewer_choose("vw-listen");return true;}   // C-left
            const int dy=k==SDLK_UP || k==SDLK_w?-1:k==SDLK_DOWN || k==SDLK_s?1:0,dx=k==SDLK_LEFT || k==SDLK_a?-1:k==SDLK_RIGHT || k==SDLK_d?1:0;
            if(dy || dx){settings_move(dy,dx);return true;}
            if(k==SDLK_RETURN || k==SDLK_KP_ENTER || k==SDLK_z || k==SDLK_SPACE){if(!event.key.repeat)settings_press();return true;}
        }
    } else if(battle_request.value("visible",false)){
        // The game's own key map, so the page reads like the rest of the game:
        // Z/Enter = A, X/Esc = B, arrows and WASD = pad and stick, Q/E = L/R, K = C-down, J = C-left.
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
            else if(k==SDLK_j)pressed=0x0002;
            if(pressed){battle_buttons(pressed,k==SDLK_TAB);return true;}
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
            const unsigned at=save_request.value(mode==1 || mode==3?"window_cursor":"cursor",0u);
            if((mode==1 || mode==3) && (k==SDLK_UP || k==SDLK_DOWN)){save_page::answer(serial,"move:"+std::to_string(at^1));return true;}
            // R deletes a slot 3+ or an autosave.
            if(screen=="slots" && mode==0 && !event.key.repeat && k==SDLK_e && save_request.value("tools",json::object()).value("delete",false)){
                save_page::answer(serial,"delete");return true;
            }
            // The slot list: up and down run through every slot, left and right turn the page.
            const unsigned count=std::max(1u,save_request.value("count",2u)),pages=(count+1)/2;
            if(k==SDLK_UP || k==SDLK_DOWN){save_page::answer(serial,"move:"+std::to_string((at+(k==SDLK_UP?count-1:1))%count));return true;}
            if(screen=="slots" && pages>1 && (k==SDLK_LEFT || k==SDLK_RIGHT)) {
                const unsigned page=(at/2+(k==SDLK_LEFT?pages-1:1))%pages;
                save_page::answer(serial,"move:"+std::to_string(std::min(page*2+at%2,count-1)));return true;
            }
            if(event.key.repeat)return true;
            if(k==SDLK_RETURN || k==SDLK_z || k==SDLK_SPACE){save_page::answer(serial,"choose");return true;}
            if(k==SDLK_ESCAPE || k==SDLK_x){save_page::answer(serial,mode==1 || mode==3?"cancel":"back");return true;}
        }
    } else if(title_request.value("visible",false)){
        if(event.type==SDL_KEYDOWN){
            const auto k=event.key.keysym.sym;const auto serial=title_request.at("serial").get<uint64_t>();
            if(title_request.value("screen",std::string())=="options") {
                const unsigned count=unsigned(title_request.at("items").size()),at=title_request.value("cursor",0u);
                if(count && (k==SDLK_UP || k==SDLK_DOWN)){title_page::answer(serial,"move:"+std::to_string((at+(k==SDLK_UP?count-1:1))%count));return true;}
            } else {
                // Held arrows repeat like the original's auto-repeat.
                if(k==SDLK_UP){title_page::answer(serial,"move:-1");return true;}
                if(k==SDLK_DOWN){title_page::answer(serial,"move:+1");return true;}
                if(!event.key.repeat && k==SDLK_q){title_page::answer(serial,"prev");return true;}
                if(!event.key.repeat && k==SDLK_e){title_page::answer(serial,"next");return true;}
            }
            if(event.key.repeat)return true;
            if(k==SDLK_RETURN || k==SDLK_z || k==SDLK_SPACE){title_page::answer(serial,"choose");return true;}
            if(k==SDLK_ESCAPE || k==SDLK_x){title_page::answer(serial,"back");return true;}
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
uint32_t touch_buttons() {
    const uint32_t linger=SDL_GetTicks64()<touch_linger_until.load()?touch_linger.load():(touch_linger=0,0u);
    return touch_held.load(std::memory_order_relaxed)|linger;
}
bool event(SDL_Event& e){auto lock=lock_ui();bool consumed=dispatch(e);if(e.type==SDL_TEXTEDITING_EXT)SDL_free(e.editExt.text);if(context){context->Update();input.flush_sdl();}return consumed;}
// Set while this present's first pass is in flight: its second pass is part of the same
// frame and must not wait for it (lock_ui waits for the previous frame's pages).
bool first_pass_drawn=false;
bool draw(plume::RenderCommandList* list,plume::RenderFramebuffer* framebuffer,bool name_cover,int layer){
    const bool second=layer==1 && first_pass_drawn;
    auto lock=second?std::unique_lock<std::mutex>(mutex):lock_ui();
    first_pass_drawn=false;if(!context || int(framebuffer->getWidth())!=pixels_w || int(framebuffer->getHeight())!=pixels_h)return false;
    // The guest cover is workload keyed. Do not put a newly-opened page over an
    // older game workload that preceded interception of the guest name grid.
    if(names::request().visible && !name_cover)return false;
    // No page shown: skip the renderer, whose start and end clear, resolve and copy a
    // window-sized MSAA target, and leave the next lock_ui free of this GPU frame.
    bool shown=false;
    for(int i=0;i<context->GetNumDocuments() && !shown;++i) {
        auto* doc=context->GetDocument(i);
        shown=doc->IsVisible() && (layer<0 || doc->HasAttribute("data-chrome")==(layer==1));
    }
    if(!shown)return false;
    layer_pass=layer;layer_now=0;
    renderer->start(list,pixels_w,pixels_h,second);list->setFramebuffer(framebuffer);
    list->setViewports(plume::RenderViewport{0,0,float(pixels_w),float(pixels_h)});context->Render();renderer->end(list,framebuffer);
    layer_pass=-1;
    first_pass_drawn=layer==0;
    in_flight=true;return true;
}
void presented(){std::lock_guard lock(mutex);in_flight=false;completed.notify_all();}
void render_shutdown(){auto lock=lock_ui();ready=false;if(initialized){name_page.reset();Rml::Shutdown();slant_instancer.reset();initialized=false;context=nullptr;settings_doc=link_doc=notice_doc=battle_doc=intermission_doc=upgrade_doc=parts_doc=ability_doc=swap_doc=title_doc=mini_doc=nullptr;}renderer.reset();}
void shutdown(){app_menu::shutdown();input.flush_sdl();SDL_StopTextInput();window=nullptr;names::window_claim_input(false);link_page::window_claim_input(false);intermission_page::window_claim_input(false);upgrade_page::window_claim_input(false);parts_page::window_claim_input(false);ability_page::window_claim_input(false);swap_page::window_claim_input(false);title_page::window_claim_input(false);battle_page::window_claim_input(false);}
json tree(){auto lock=lock_ui();require();json docs=json::array();for(int i=0;i<context->GetNumDocuments();++i)if(context->GetDocument(i)->IsVisible())docs.push_back(describe(context->GetDocument(i)));return {{"backend","SDL2/RmlUi"},{"windows",json::array({{{"number",SDL_GetWindowID(window)},{"title",SDL_GetWindowTitle(window)},{"game",true},{"scale",pixel_ratio},{"views",{{"class","RmlContext"},{"children",docs}}}}})}};}
json click(const json& p){auto lock=lock_ui();require();float x=0,y=0;
    if(p.contains("text") || p.contains("id")){
        const std::string text=p.value("id",p.value("text",std::string{}));Rml::Element* found=nullptr;
        const auto search=[&]{for(bool exact:{true,false}){for(int i=context->GetNumDocuments()-1;i>=0 && !found;--i)found=find(context->GetDocument(i),text,exact);if(found)break;}};
        search();
        // A settings control on another page: turn to that page first, as a player would.
        if(!found && p.contains("id") && settings_open && settings_doc) {
            const unsigned before=settings_page;
            for(unsigned page=0;page<std::size(settings_pages) && !found;++page)if(page!=before){settings_show(page,"");sync();search();}
            if(!found){settings_show(before,"");sync();}
        }
        if(!found)throw debug::RpcError(debug::InvalidParams,"no visible control: "+text);
        for(auto* parent=found;parent;parent=parent->GetParentNode())if(parent->GetTagName()=="button" || parent->GetTagName()=="input"){found=parent;break;}
        if(found->HasAttribute("disabled"))throw debug::RpcError(debug::InvalidParams,"control is disabled: "+text);
        // A control scrolled out of its page is scrolled into view first, as a player would.
        found->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));context->Update();
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
        if(app_menu::activate(wanted)){sync();return {{"pressed",wanted}};}
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
    // The touch controls (docs/design/touch-controls.md): the scene, whether they show, what they hold.
    result["touch"]={{"supported",ui::touch_supported()},{"shown",ui::touch_shown},{"active",ui::touch_active()},{"scene",int(ui::touch_scene_id)},
                     {"buttons",ui::touch_buttons()},{"stick",ui::touch_fingers.stick()!=nullptr}};
    return result;
}
nlohmann::json click(const nlohmann::json& p){return ui::click(p);}
nlohmann::json key(const nlohmann::json& p){return ui::key(p);}
nlohmann::json type(const nlohmann::json& p){return ui::type(p);}
nlohmann::json menu(const nlohmann::json& p){return ui::menu(p);}
nlohmann::json compose(const std::filesystem::path&){return {{"backend","SDL2/RmlUi"},{"composited",false},{"reason","UI is already in the GPU frame"}};}
nlohmann::json capture(const nlohmann::json&,const std::filesystem::path&){throw debug::RpcError(debug::InvalidParams,"Shared settings use the game surface; request a game screenshot");}
}
