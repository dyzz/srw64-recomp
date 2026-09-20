#include "frontend.hpp"
#include "name_page.hpp"
#include "ui_renderer.h"
#include "RmlUi_Platform_SDL.h"
#include "native_dialogue.hpp"
#include "link_page.hpp"
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
Rml::ElementDocument *settings_doc{}, *link_doc{}, *toolbar{}, *notice_doc{};
std::string settings_stamp, link_stamp, toolbar_locale, notice_stamp;
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
#settings-open {tab-index: none; pointer-events: auto; position: absolute; right: 6dp; top: 4dp; font-size: 12dp; padding: 6dp;}
#notices {width: 86%; margin: 8dp auto; text-align: center;} .banner {padding: 12dp; background-color: #122131ed; border: 1dp #9be4f7; margin-bottom: 6dp;}
)";
void document_close(Rml::ElementDocument*& doc) {
    if(doc){doc->Close();doc=nullptr;}
}
bool held();
void choose(const std::string& id);
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
    const auto stamp=localization::catalog().locale+std::to_string(rules::active_fixes())+std::to_string(presentation::image_mode.requested())+
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
    body+="<p>"+label("settings_images_note")+"</p><h2>"+label("rules_menu")+"</h2>";
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
void choose(const std::string& id) {
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
    } catch(const std::exception& error){notices::post("settings-error",error.what());}
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
    link_sync();
    if(!toolbar || toolbar_locale!=language->locale){document_close(toolbar);toolbar_locale=language->locale;toolbar=document(button("settings-open",label("settings_title")),false);}
    toolbar->PullToFront();settings_sync();notices_sync();context->Update();input.update_rectangle();
    names::window_claim_input(request.visible || (names::owns_input() && held()));
    link_page::window_claim_input(link_request.visible || (link_page::owns_input() && held()));
}
bool dispatch(SDL_Event& event) {
    if(!context)return false;
    if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_CLOSE)return false;
    if(input.event(event))return true;
    if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_F7 && !input.has_composition() &&
       !(event.key.keysym.mod&(KMOD_GUI|KMOD_ALT|KMOD_CTRL|KMOD_SHIFT))){
        if(!event.key.repeat)settings::request_locale(localization::next_locale(localization::catalog().locale));return true;
    }
    if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_COMMA && (event.key.keysym.mod&(KMOD_CTRL|KMOD_GUI))){choose("settings-open");return true;}
    // A closed modal can restore focus to the toolbar. Game keys must not
    // activate that stale focus (Return used to reopen Options after naming).
    if(!settings_open && !names::request().visible && !link_request.visible &&
       (event.type==SDL_KEYDOWN || event.type==SDL_KEYUP))return false;
    if(settings_open){
        if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE){choose("settings-close");return true;}
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
    return consumed || settings_open || names::request().visible || link_request.visible;
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
void render_shutdown(){auto lock=lock_ui();ready=false;if(initialized){name_page.reset();Rml::Shutdown();initialized=false;context=nullptr;settings_doc=link_doc=toolbar=notice_doc=nullptr;}renderer.reset();}
void shutdown(){input.flush_sdl();SDL_StopTextInput();window=nullptr;names::window_claim_input(false);link_page::window_claim_input(false);}
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
        if(wanted==localization::catalog().ui("settings_open") || wanted==localization::catalog().ui("settings_title")){choose("settings-open");sync();return {{"opened","settings"}};}
        for(const auto& entry:rules::catalog)if(wanted==localization::catalog().ui(rules::ui_key(entry.id))){choose("settings-open");choose("rule:"+std::string(entry.id));sync();return {{"pressed",wanted}};}
        for(const auto& preset:rules::presets)if(wanted==localization::catalog().ui(std::string(preset.key))){choose("settings-open");choose("preset:"+std::string(preset.key));sync();return {{"pressed",wanted}};}
    }
    return {{"backend","SDL2/RmlUi"},{"settings",localization::catalog().ui("settings_open")},{"shortcut","Ctrl/Cmd+,"}};
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
    result["input_owners"]={{"names",names::owns_input()},{"link",link_page::owns_input()},{"settings",settings_window::owns_input()},{"locale",settings::owns_input()}};
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
