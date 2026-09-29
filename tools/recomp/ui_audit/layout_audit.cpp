// Offline layout audit of the shared UI (src/native/ui/frontend.cpp) with recorded page
// states: no ROM, no game thread. Every page adapter below is a stand-in that returns
// the fixture's state. For each fixture the page is laid out at the requested window
// size, saved as a screenshot, and text that runs out of its box is reported.
// Built and driven by run_audit.py (docs/guide/ui-layout-audit.md).
#include "frontend.hpp"
#include "probe_surface.hpp"
#include "localization/catalog.hpp"
#include "presentation_settings.hpp"
#include "native_name_entry.hpp"
#include "link_page.hpp"
#include "battle_page.hpp"
#include "intermission_page.hpp"
#include "upgrade_page.hpp"
#include "parts_page.hpp"
#include "ability_page.hpp"
#include "swap_page.hpp"
#include "save_page.hpp"
#include "title_page.hpp"
#include "settings_window.hpp"
#include "debug_ui.hpp"
#include "app_menu.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>

using json=nlohmann::json;
namespace fs=std::filesystem;

// ---- stand-ins for the game-side modules ------------------------------------------
namespace fixture {
std::map<std::string,json> states;
json get(const std::string& page){auto it=states.find(page);return it==states.end()?json{{"visible",false}}:it->second;}
}
#define PAGE(ns) namespace srw64::ns { json state(){return fixture::get(#ns);} void answer(uint64_t,const std::string&){} \
    bool owns_input(){return false;} void window_claim_input(bool){} }
PAGE(battle_page) PAGE(intermission_page) PAGE(upgrade_page) PAGE(parts_page) PAGE(ability_page) PAGE(swap_page) PAGE(save_page) PAGE(title_page)
namespace srw64::link_page {
Request request() {
    Request r;const auto s=fixture::get("link_page");r.visible=s.value("visible",false);r.serial=1;
    for(unsigned i=0;i<3;++i){r.joined[i]=s.value("joined",json::array({true,true,true}))[i];r.scheduled[i]=s.value("scheduled",json::array({false,false,false}))[i];}
    return r;
}
void answer(uint64_t,unsigned,bool){} bool owns_input(){return false;} void window_claim_input(bool){}
}
namespace srw64::names {
DefaultNames& default_names(){static DefaultNames names;return names;}
Request request(){return {};} bool owns_input(){return false;} void window_claim_input(bool){}
void select(uint64_t,unsigned){} void choose(uint64_t,unsigned){} void review(uint64_t,bool){}
}
namespace srw64::settings {
BattleUi battle_value=BattleUi::Native;UiSize size_value=UiSize::Largest;std::string page="general";
bool intermission=true,name_entry=true,title=true,wide=true;
BattleUi battle_ui(){return battle_value;}
BattleUi battle_ui_from(std::string_view v){return v=="hd"?BattleUi::HD:v=="original"?BattleUi::Original:BattleUi::Native;}
const char* battle_ui_name(BattleUi ui){return ui==BattleUi::HD?"hd":ui==BattleUi::Original?"original":"native";}
bool failed(){return false;} bool owns_input(){return false;}
std::string key_display_name(int key){return SDL_GetScancodeName(SDL_Scancode(key));}
bool native_intermission_ui(){return intermission;} bool native_name_entry_ui(){return name_entry;} bool native_title_ui(){return title;}
void request_locale(const std::string& locale){if(auto c=localization::find(locale))localization::activate(c);}
void set_battle_ui(BattleUi v){battle_value=v;} void set_native_intermission_ui(bool v){intermission=v;}
void set_native_name_entry_ui(bool v){name_entry=v;} void set_native_title_ui(bool v){title=v;}
void set_settings_page(const std::string& v){page=v;} std::string settings_page(){return page;}
void set_ui_size(UiSize v){size_value=v;} UiSize ui_size(){return size_value;}
float ui_scale(UiSize s){return s==UiSize::Largest?1.5f:s==UiSize::Large?1.25f:1.f;}
const char* ui_size_name(UiSize s){return s==UiSize::Largest?"largest":s==UiSize::Large?"large":"standard";}
void set_wide_picture(bool v){wide=v;} bool wide_picture(){return wide;}
}
namespace srw64::app_menu {
void update(const Labels&,const WindowState&){} bool available(){return false;} bool activate_settings(){return false;}
bool activate(const std::string&){return false;} bool take_settings_request(){return false;} bool take_reload_request(){return false;}
bool take_fullscreen_request(){return false;} int take_scale_request(){return 0;} void shutdown(){}
}
namespace srw64::dialogue { void request_reload(){} }
namespace srw64::intro { int title_major(){return 0;} }
uint32_t srw64_pad_state(){return 0;}
uint32_t srw64_keyboard_state(){return 0;}
std::string srw64_pad_name(){return "Steam Deck Controller";}
uint64_t srw64_current_vi(){return 1000;}

// ---- overflow scan ---------------------------------------------------------------
namespace {
constexpr float slack=1.2f;   // dp: less is sub-pixel rounding
bool inline_box(Rml::Element* e){const auto d=e->GetDisplay();return d==Rml::Style::Display::Inline || d==Rml::Style::Display::InlineBlock;}
bool shown(Rml::Element* e){for(;e;e=e->GetParentNode())if(!e->IsVisible())return false;return true;}
std::string path_of(Rml::Element* e) {
    std::string path;
    for(int depth=0;e && depth<4;e=e->GetParentNode(),++depth) {
        std::string part=e->GetTagName();
        if(!e->GetId().empty())part+="#"+e->GetId();else if(!e->GetClassNames().empty())part+="."+e->GetClassNames();
        path=part+(path.empty()?"":" > "+path);
    }
    return path;
}
float right_edge(Rml::Element* e,Rml::BoxArea area){return e->GetAbsoluteOffset(area).x+e->GetBox().GetSize(area).x;}
// One line of text as drawn: its span, and the nearest ancestor that clips it (a panel).
struct Drawn {float x0,x1,y0,y1;Rml::Element* clip;Rml::Element* block;std::string text,path;};
Rml::Element* clipper(Rml::Element* e) {
    for(e=e->GetParentNode();e;e=e->GetParentNode())if(e->GetComputedValues().overflow_x()!=Rml::Style::Overflow::Visible)return e;
    return nullptr;
}
// RmlUi breaks lines only at white space, so a run between spaces wider than its block
// runs out of it; text that does not wrap must fit from where it starts. A bordered box
// must stay inside its parent's frame.
void scan(Rml::Element* e,json& out,float ratio,std::vector<Drawn>& drawn) {
    if(!shown(e))return;
    const float limit=slack*ratio;   // the scan measures pixels
    if(auto* t=dynamic_cast<Rml::ElementText*>(e)) {
        const auto text=t->GetText();
        if(text.find_first_not_of(" \t\n")==std::string::npos)return;
        Rml::Element* owner=t->GetParentNode();Rml::Element* block=owner;
        while(block && inline_box(block) && block->GetParentNode())block=block->GetParentNode();
        const auto space=owner->GetComputedValues().white_space();
        const bool nowrap=space==Rml::Style::WhiteSpace::Nowrap || space==Rml::Style::WhiteSpace::Pre;
        const float x0=t->GetAbsoluteOffset(Rml::BoxArea::Border).x,width=float(Rml::ElementUtilities::GetStringWidth(t,text));
        if(nowrap) {
            if(const float over=x0+width-right_edge(block,Rml::BoxArea::Padding);over>limit)
                out.push_back({{"kind","text-past-box"},{"text",text},{"over_dp",over/ratio},{"path",path_of(owner)}});
            // An inline-block or flex item of its own: the text past its edge.
            if(owner!=block && owner->GetDisplay()!=Rml::Style::Display::Inline)
                if(const float over=x0+width-right_edge(owner,Rml::BoxArea::Padding);over>limit)
                    out.push_back({{"kind","text-past-item"},{"text",text},{"over_dp",over/ratio},{"path",path_of(owner)}});
            // The block's own box gives the line's height and place (a text node's offset is its baseline).
            Rml::Element* box=owner;while(box->GetDisplay()==Rml::Style::Display::Inline && box->GetParentNode())box=box->GetParentNode();
            const float y0=box->GetAbsoluteOffset(Rml::BoxArea::Border).y,line=box->GetBox().GetSize(Rml::BoxArea::Border).y;
            bool transformed=false;   // a squeezed line (transform: scale) is narrower than measured
            for(auto* up=owner;up;up=up->GetParentNode())transformed|=up->GetTransformState()!=nullptr;
            // bh-num: the original's number pool, one glyph per 8-pixel cell as the original lays it.
            if(!transformed && !owner->IsClassSet("bh-num"))drawn.push_back({x0,x0+width,y0,y0+line,clipper(t),block,text,path_of(owner)});
        } else {
            float widest=0;std::string run;
            for(size_t at=0;at<text.size();) {
                size_t end=text.find(' ',at);if(end==std::string::npos)end=text.size();
                const auto piece=text.substr(at,end-at);
                if(const float w=float(Rml::ElementUtilities::GetStringWidth(t,piece));w>widest){widest=w;run=piece;}
                at=end+1;
            }
            if(const float over=widest-block->GetBox().GetSize(Rml::BoxArea::Padding).x;over>limit)
                out.push_back({{"kind","run-wider-than-block"},{"text",text},{"run",run},{"over_dp",over/ratio},{"path",path_of(owner)}});
        }
        return;
    }
    if(auto* parent=e->GetParentNode();parent && parent->GetParentNode()) {
        const auto& box=e->GetBox();bool bordered=false;
        for(auto edge:{Rml::BoxEdge::Top,Rml::BoxEdge::Right,Rml::BoxEdge::Bottom,Rml::BoxEdge::Left})bordered|=box.GetEdge(Rml::BoxArea::Border,edge)>0;
        if(bordered) {
            const float past=std::max(right_edge(e,Rml::BoxArea::Border)-right_edge(parent,Rml::BoxArea::Border),
                                      parent->GetAbsoluteOffset(Rml::BoxArea::Border).x-e->GetAbsoluteOffset(Rml::BoxArea::Border).x);
            if(past>limit)out.push_back({{"kind","box-past-parent"},{"over_dp",past/ratio},{"path",path_of(e)}});
        }
    }
    for(int i=0;i<e->GetNumChildren();++i)scan(e->GetChild(i),out,ratio,drawn);
}
// Lines cut off by the panel that holds them, and lines of one panel running into each other
// (absolutely placed cells size to their text, so neither overflows its own box).
void collide(const std::vector<Drawn>& drawn,json& out,float ratio) {
    const float limit=slack*ratio;
    for(size_t i=0;i<drawn.size();++i) {
        const auto& a=drawn[i];
        if(a.clip)if(const float over=a.x1-right_edge(a.clip,Rml::BoxArea::Padding);over>limit)
            out.push_back({{"kind","text-clipped"},{"text",a.text},{"over_dp",over/ratio},{"path",a.path}});
        // A line mostly below the panel that clips it is lost (scrolling lists excepted).
        if(a.clip && a.clip->GetComputedValues().overflow_y()==Rml::Style::Overflow::Hidden) {
            const float bottom=a.clip->GetAbsoluteOffset(Rml::BoxArea::Padding).y+a.clip->GetBox().GetSize(Rml::BoxArea::Padding).y;
            if(const float below=a.y1-bottom;below>.5f*(a.y1-a.y0))
                out.push_back({{"kind","text-cut-below"},{"text",a.text},{"over_dp",below/ratio},{"path",a.path}});
        }
        for(size_t j=i+1;j<drawn.size();++j) {
            const auto& b=drawn[j];
            if(a.clip!=b.clip || a.block==b.block)continue;   // pieces of one block flow, they cannot collide
            const float rows=std::min(a.y1,b.y1)-std::max(a.y0,b.y0),cols=std::min(a.x1,b.x1)-std::max(a.x0,b.x0);
            // Glyph edges of neighbouring cells touch by a dp or two; more is a collision.
            if(rows>.5f*std::min(a.y1-a.y0,b.y1-b.y0) && cols>3*ratio)
                out.push_back({{"kind","text-overlap"},{"text",a.text+" | "+b.text},{"over_dp",cols/ratio},{"path",a.path}});
        }
    }
}
json load(const fs::path& p){std::ifstream in(p);if(!in)throw std::runtime_error("Cannot open "+p.string());return json::parse(in);}
}

int main(int argc,char** argv){try{
    if(argc!=6){std::cerr<<"srw64-ui-audit LOCALE_DIR FIXTURES.json OUTPUT_DIR WIDTH HEIGHT\n";return 2;}
    const fs::path locales=argv[1],output=fs::absolute(argv[3]);const auto fixtures=load(argv[2]);
    const int width=std::stoi(argv[4]),height=std::stoi(argv[5]);
    fs::create_directories(output);
    // The catalogs as the host registers them; the pages only read their UI labels.
    json data={{"schema","srw64.native-dialogue-data.v2"},{"config",{{"locale","zh-Hans"},{"font","-"}}},{"entries",json::object()},
        {"source_entries",json::object()},{"ui",json::object()},{"locale_catalogs",json::object()},{"locale_options",json::array()}};
    for(const auto* locale:{"ja","zh-Hans","en"}) {
        const auto doc=load(locales/(std::string(locale)+".json"));
        data["locale_catalogs"][locale]={{"config",{{"locale",locale},{"font","-"}}},{"ui",doc.at("ui")}};
        data["locale_options"].push_back({{"locale",locale},{"label",doc.value("display_name",locale)}});
    }
    srw64::localization::initialize(data);
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());
    srw64::ui::ProbeSurface surface(width,height,true);SDL_SetWindowTitle(surface.window,"SRW64 UI layout audit");
    using namespace plume;
    auto device=surface.interface->createDevice();auto queue=device->createCommandQueue(RenderCommandListType::DIRECT);
    auto fence=device->createCommandFence();auto acquire=device->createCommandSemaphore();
    auto swapchain=queue->createSwapChain(RenderSwapChainDesc(surface.handle,RenderFormat::B8G8R8A8_UNORM,2));
    if(!swapchain || !swapchain->resize())throw std::runtime_error("Cannot create swapchain");
    auto commands=queue->createCommandList();
    std::vector<std::unique_ptr<RenderFramebuffer>> frames;std::vector<std::unique_ptr<RenderCommandSemaphore>> release;
    auto rebuild=[&]{frames.clear();release.clear();for(unsigned i=0;i<swapchain->getTextureCount();++i){
        const auto* texture=swapchain->getTexture(i);RenderFramebufferDesc desc{};desc.colorAttachments=&texture;desc.colorAttachmentsCount=1;
        frames.push_back(device->createFramebuffer(desc));release.push_back(device->createCommandSemaphore());}};
    rebuild();
    srw64::ui::window_init(surface.window,output);srw64::ui::render_init(surface.interface.get(),device.get());
    const auto frame=[&](const fs::path& shot) {
        SDL_Event e;while(SDL_PollEvent(&e)){}
        srw64::ui::update();
        if(swapchain->needsResize()){frames.clear();swapchain->resize();rebuild();}
        unsigned index=0;if(!swapchain->acquireTexture(acquire.get(),&index))throw std::runtime_error("Cannot acquire a swapchain image");
        const auto w=swapchain->getWidth(),h=swapchain->getHeight();
        commands->begin();auto* texture=swapchain->getTexture(index);
        commands->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(texture,RenderTextureLayout::COLOR_WRITE));commands->setFramebuffer(frames[index].get());
        commands->setViewports(RenderViewport(0,0,float(w),float(h)));commands->setScissors(RenderRect(0,0,w,h));commands->clearColor(0,RenderColor(.18,.22,.16,1));
        srw64::ui::draw(commands.get(),frames[index].get(),false);
        std::function<void()> finish;if(!shot.empty())finish=surface.capture(device.get(),commands.get(),texture,w,h,shot);
        commands->barriers(RenderBarrierStage::NONE,RenderTextureBarrier(texture,RenderTextureLayout::PRESENT));commands->end();
        const auto* list=commands.get();auto* wait=acquire.get();auto* signal=release[index].get();
        queue->executeCommandLists(&list,1,&wait,1,&signal,1,fence.get());swapchain->present(index,&signal,1);queue->waitForCommandFence(fence.get());
        srw64::ui::presented();if(finish)finish();
    };
    json report=json::array();
    for(const auto& f:fixtures) {
        const auto name=f.at("name").get<std::string>();
        try {
            fixture::states.clear();
            const json pages=f.value("pages",json::object());
            for(const auto& [page,state]:pages.items())fixture::states[page]=state;
            srw64::settings::request_locale(f.value("locale","zh-Hans"));
            srw64::settings::battle_value=srw64::settings::battle_ui_from(f.value("battle_ui","native"));
            const auto size=f.value("ui_size","largest");
            srw64::settings::size_value=size=="standard"?srw64::settings::UiSize::Standard:size=="large"?srw64::settings::UiSize::Large:srw64::settings::UiSize::Largest;
            srw64::settings_window::close();frame({});
            if(f.contains("settings")){srw64::settings::page=f.at("settings");srw64::settings_window::open();}
            for(int i=0;i<4;++i)frame({});
            frame(output/(name+".png"));
            json issues=json::array();float ratio=1;
            if(auto* context=Rml::GetContext("game-ui")) {
                ratio=context->GetDensityIndependentPixelRatio();
                for(int i=0;i<context->GetNumDocuments();++i)if(context->GetDocument(i)->IsVisible()) {
                    std::vector<Drawn> drawn;scan(context->GetDocument(i),issues,ratio,drawn);collide(drawn,issues,ratio);
                }
            }
            report.push_back({{"name",name},{"issues",issues}});
            std::cout<<name<<": "<<issues.size()<<" issue(s)\n";
        } catch(const std::exception& e) {
            report.push_back({{"name",name},{"error",e.what()}});std::cout<<name<<": ERROR "<<e.what()<<"\n";
            fixture::states.clear();try{frame({});}catch(...){}
        }
    }
    std::ofstream(output/"audit.json")<<report.dump(2)<<'\n';
    srw64::ui::render_shutdown();srw64::ui::shutdown();
    return 0;
}catch(const std::exception& e){std::cerr<<"UI audit failed: "<<e.what()<<'\n';return 1;}}
