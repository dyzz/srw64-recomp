#include "name_page.hpp"
#include "slant_decorator.hpp"
#include "text/button_prompts.hpp"
#include "probe_surface.hpp"
#include "ui_renderer.h"
#include "RmlUi_Platform_SDL.h"
#include "json/json.hpp"
#include <fstream>
#include <iostream>

using json=nlohmann::json;
namespace fs=std::filesystem;
using namespace srw64::ui;
using namespace plume;
namespace {
json load(const fs::path& path){std::ifstream in(path);if(!in)throw std::runtime_error("Cannot open "+path.string());return json::parse(in);}
std::vector<Rml::byte> bytes(const fs::path& path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot open font");return {std::istreambuf_iterator<char>(in),{}};}
// Synthetic peer of the game adapter. It deliberately never loads a ROM or
// changes SRAM. Choosing a route commits its names and opens the review.
struct Fixture {
    srw64::names::Request request;
    unsigned starts{},backs{};
    // The ROM's default names and starting units per route, protagonist then partner
    // (tests/test_protagonist_select.py, native_name_entry.cpp route_units); the terms
    // catalog translates them the way the host's DefaultNames and text table do.
    static constexpr const char* full_names[4][2]={{"ブラッド・スカイウィンド","カーツ・フォルネウス"},{"マナミ・ハミル","アイシャ・リッジモンド"},{"アークライト・ブルー","エルリッヒ・シュターゼン"},{"セレイン・メネス","リッシュ・グリスウェル"}};
    static constexpr const char* nicknames[4][2]={{"ブラッド","カーツ"},{"マナミ","アイシャ"},{"アーク","エルリッヒ"},{"セレイン","リッシュ"}};
    static constexpr const char* unit_names[4][2]={{"アースゲイン","ヴァイローズ"},{"スイームルグ","エルブルス"},{"ソルデファー","ノウルーズ"},{"スヴァンヒルド","シグルーン"}};
    std::map<std::string,json> terms;   // locale -> terms sections (none for ja)
    Fixture(){
        request.serial=1;request.visible=request.active=true;request.person=srw64::names::Selection;
        localize("ja");
    }
    // Names in the reading language: the terms catalog's default_names and units, split
    // at the language's separator into given and family name as the host shows them.
    void localize(const std::string& locale){
        auto term=[&](const char* section,const std::string& ja){
            const auto found=terms.find(locale);
            if(found==terms.end() || !found->second.contains(section))return ja;
            return found->second.at(section).value(ja,ja);
        };
        const std::string sep=srw64::names::separator(locale);
        auto split=[&](const std::string& full){
            const auto at=full.find(sep);
            return std::array<std::u16string,2>{utf16(at==std::string::npos?full:full.substr(0,at)),utf16(at==std::string::npos?"":full.substr(at+sep.size()))};
        };
        for(unsigned r=0;r<4;++r)for(unsigned p=0;p<2;++p){
            request.choices[r].names[p]=split(term("default_names",full_names[r][p]));
            request.choices[r].unit_names[p]=term("units",unit_names[r][p]);
        }
        for(unsigned p=0;p<2;++p){
            const auto name=split(term("default_names",full_names[request.route][p]));
            request.names[p]={name[0],name[1],utf16(term("default_names",nicknames[request.route][p]))};
        }
    }
    void open(unsigned person){request.person=person;++request.serial;request.active=true;request.pending=false;}
    NameActions actions(){return {
        [this](uint64_t serial,unsigned route){if(serial==request.serial && route<4)request.route=route;},
        [this](uint64_t serial,unsigned route){if(serial!=request.serial || route>=4)return;request.route=route;open(srw64::names::Review);},
        [this](uint64_t serial,bool confirm){if(serial!=request.serial)return;
            if(confirm){++starts;request.visible=false;}else{++backs;open(srw64::names::Selection);}},
        // Fixture images are registered by name; the page's requested width is ignored.
        [](const std::string& path,int){return path;}
    };}
};
}
int main(int argc,char** argv){try{
    std::map<std::string,std::string> options;
    for(int i=1;i<argc;++i){std::string key=argv[i];if(key=="--help"){
        std::cout<<"srw64-ui-probe --catalog-dir content/locales --font local.ttf --output NEW_DIR [--script actions.json] [--dialogue local/dialogue.json] [--language ja|zh-Hans|en] [--density 1.48] [--pad deck|xbox|playstation|nintendo]\n";return 0;}
        if(i+1==argc || !key.starts_with("--") || options.contains(key))throw std::runtime_error("Invalid probe arguments");options[key]=argv[++i];}
    for(const auto& [key,value]:options)if(key!="--catalog-dir" && key!="--font" && key!="--output" && key!="--script" && key!="--dialogue" && key!="--language" && key!="--density" && key!="--pad")throw std::runtime_error("Unknown option: "+key);
    for(auto key:{"--catalog-dir","--font","--output"})if(!options.contains(key))throw std::runtime_error(std::string("Required: ")+key);
    fs::path output=fs::absolute(options.at("--output"));if(!fs::create_directory(output))throw std::runtime_error("Output must be a new directory");
    std::map<std::string,std::map<std::string,std::string>> catalogs;
    for(auto language:{"ja","zh-Hans","en"})catalogs[language]=load(fs::path(options.at("--catalog-dir"))/(std::string(language)+".json")).at("ui");
    std::string locale=options.contains("--language")?options.at("--language"):"en";
    if(!catalogs.contains(locale))throw std::runtime_error("Unknown locale");
    // Button tokens become PromptFont icons as in the host (frontend.cpp sync): keyboard
    // hints, or with --pad the controller hints ("_pad" labels) of that controller family.
    srw64::text::PromptContext prompts;
    if(options.contains("--pad")){
        const auto family=options.at("--pad");
        prompts.pad=true;
        prompts.family=family=="deck"?srw64::text::PadFamily::Deck:family=="playstation"?srw64::text::PadFamily::PlayStation:family=="nintendo"?srw64::text::PadFamily::Nintendo:srw64::text::PadFamily::Xbox;
    }
    const auto labels=[&](const std::string& language){
        auto result=catalogs.at(language);
        if(prompts.pad)for(auto& [key,value]:result)if(const auto pad=result.find(key+"_pad");pad!=result.end())value=pad->second;
        for(auto& [key,value]:result)value=srw64::text::expand_prompts(value,prompts);
        return result;
    };
    json script=json::array();if(options.contains("--script")){
        const auto data=load(options.at("--script"));if(data.at("schema")!="srw64.ui-probe-script.v1")throw std::runtime_error("Invalid script schema");script=data.at("actions");}
    Fixture fixture;
    for(auto language:{"zh-Hans","en"}){
        const auto path=fs::path(options.at("--catalog-dir"))/"terms"/(std::string(language)+".json");
        if(fs::exists(path))fixture.terms[language]=load(path).at("sections");
    }
    std::vector<std::pair<std::string,std::vector<char>>> portraits;
    if(options.contains("--dialogue")){
        const auto data=load(options.at("--dialogue"));
        // The eight portraits by route (route_faces: protagonist then partner, four routes each).
        const auto& assets=data.at("name_entry_assets");
        for(unsigned p=0;p<2;++p)for(unsigned r=0;r<4;++r){
            const auto id=std::to_string(assets.at("route_faces").at(p).at(r).get<unsigned>());
            fs::path path=assets.at("portraits").at(id).at("original").get<std::string>();if(path.is_relative())path=fs::path(options.at("--dialogue")).parent_path()/path;
            std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Missing fixture portrait");
            std::string name="fixture-face-"+id;portraits.push_back({name,{std::istreambuf_iterator<char>(in),{}}});fixture.request.choices[r].portraits[p][0]=name;
        }
        // The starting units' poses (native_name_entry.cpp route_units), when the content has them.
        if(data.contains("battle_assets") && data.at("battle_assets").contains("units")){
            const unsigned route_units[4][2]={{34,35},{36,37},{30,327},{32,328}};
            const auto& units=data.at("battle_assets").at("units");
            for(unsigned r=0;r<4;++r)for(unsigned p=0;p<2;++p)if(const auto row=units.find(std::to_string(route_units[r][p]));row!=units.end()){
                fs::path path=row->at("path").get<std::string>();if(path.is_relative())path=fs::path(options.at("--dialogue")).parent_path()/path;
                std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Missing fixture unit pose");
                std::string name="fixture-unit-"+std::to_string(route_units[r][p]);portraits.push_back({name,{std::istreambuf_iterator<char>(in),{}}});
                fixture.request.choices[r].units[p]={name,"",row->value("width",96u),row->value("height",96u)};
            }
        }
    }
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());
    struct QuitSDL{~QuitSDL(){SDL_Quit();}} quit_sdl;
    ProbeSurface surface;
    auto device=surface.interface->createDevice();if(!device)throw std::runtime_error("No render device");
    auto queue=device->createCommandQueue(RenderCommandListType::DIRECT);
    auto fence=device->createCommandFence();auto acquire=device->createCommandSemaphore();
    auto swapchain=queue->createSwapChain(RenderSwapChainDesc(surface.handle,RenderFormat::B8G8R8A8_UNORM,2));
    if(!swapchain || !swapchain->resize())throw std::runtime_error("Cannot create swapchain");
    auto commands=queue->createCommandList();
    std::vector<std::unique_ptr<RenderFramebuffer>> frames;
    std::vector<std::unique_ptr<RenderCommandSemaphore>> release;
    auto rebuild=[&]{frames.clear();release.clear();for(unsigned i=0;i<swapchain->getTextureCount();++i){
        const auto* texture=swapchain->getTexture(i);RenderFramebufferDesc desc{};desc.colorAttachments=&texture;desc.colorAttachmentsCount=1;
        frames.push_back(device->createFramebuffer(desc));release.push_back(device->createCommandSemaphore());}};rebuild();
    recompui::RmlRenderInterface_RT64 renderer;renderer.init(surface.interface.get(),device.get());
    for(auto& [name,data]:portraits)renderer.queue_image_from_bytes_file(name,data);
    SystemInterface_SDL system;system.SetWindow(surface.window);Rml::SetSystemInterface(&system);Rml::SetRenderInterface(renderer.get_rml_interface());
    auto font=bytes(options.at("--font"));
    if(!Rml::Initialise())throw std::runtime_error("RmlUi initialization failed");
    struct RmlCleanup{bool active=true;~RmlCleanup(){if(active)Rml::Shutdown();}} rml_cleanup;
    if(!Rml::LoadFontFace(font,"srw64-ui",Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal,true))throw std::runtime_error("Cannot load font");
    // The button icons, a fallback face beside the font as in the host.
    std::vector<Rml::byte> prompt_font;
    if(const auto path=fs::path(options.at("--font")).parent_path()/"SRW64Prompts.ttf";fs::exists(path)){
        prompt_font=bytes(path);
        if(!Rml::LoadFontFace(prompt_font,"srw64-ui-prompts",Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal,true))throw std::runtime_error("Cannot load prompt font");
    }
    SlantInstancer slant_instancer;Rml::Factory::RegisterDecoratorInstancer("slant",&slant_instancer);   // the route tags (frontend.cpp registers it too)
    auto* context=Rml::CreateContext("name-probe",{1100,760});if(!context)throw std::runtime_error("Cannot create RmlUi context");
    // The host's dp ratio (frontend.cpp sync): a Steam Deck at Largest is 1.48.
    if(options.contains("--density"))context->SetDensityIndependentPixelRatio(std::stof(options.at("--density")));
    context->SetDimensions({int(swapchain->getWidth()),int(swapchain->getHeight())});   // the window's size before the first layout, as the host
    bool running=true;unsigned frame=0;size_t next_action=0;std::ofstream log(output/"events.jsonl");
    {
    NamePage page(*context,fixture.actions());
    page.sync({},labels(locale),locale);   // the host syncs every frame, also with no page up
    auto sync=[&]{fixture.localize(locale);page.sync(fixture.request,labels(locale),locale);context->Update();};sync();
    auto snapshot=[&]{
        json state={{"schema","srw64.ui-probe-state.v1"},{"frame",frame},{"locale",locale},{"serial",fixture.request.serial},
            {"person",fixture.request.person},{"route",fixture.request.route},{"visible",fixture.request.visible},
            {"pending",page.pending()},{"starts",fixture.starts},{"backs",fixture.backs}};
        if(auto* focused=context->GetFocusElement())state["focus"]=focused->GetId();
        return state;};
    auto event=[&](SDL_Event& e){
        if(e.type==SDL_QUIT){running=false;return;}
        if(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_F7 && !e.key.repeat){
            locale=locale=="ja"?"zh-Hans":locale=="zh-Hans"?"en":"ja";sync();return;}
        // This modal page consumes all input. The real game adapter remains
        // responsible for its existing release-after-close gate.
        if(!page.event(e))RmlSDL::InputEventHandler(context,e);
    };
    while(running){
        SDL_Event e;while(SDL_PollEvent(&e))event(e);
        sync();fs::path shot;
        if(next_action<script.size() && frame>=script[next_action].value("frame",unsigned(next_action*3+3))){
            const auto action=script[next_action++];const auto op=action.at("op").get<std::string>();
            if(op=="click")page.action(action.at("id"));
            else if(op=="key"){
                SDL_Event injected{};injected.type=action.value("up",false)?SDL_KEYUP:SDL_KEYDOWN;injected.key.keysym.sym=SDL_GetKeyFromName(action.at("key").get<std::string>().c_str());
                if(!injected.key.keysym.sym)throw std::runtime_error("Unknown SDL key");event(injected);
            }else if(op=="language"){locale=action.at("locale");if(!catalogs.contains(locale))throw std::runtime_error("Unknown locale");}
            else if(op=="resize")SDL_SetWindowSize(surface.window,action.at("width"),action.at("height"));
            else if(op=="capture"){
                const auto name=action.at("name").get<std::string>();if(fs::path(name).filename()!=name || fs::path(name).extension()!=".png")throw std::runtime_error("Invalid capture name");shot=output/name;
            }else if(op=="expect"){
                const auto state=snapshot();for(auto& [key,value]:action.at("state").items())if(!state.contains(key)||state.at(key)!=value)
                    throw std::runtime_error("Expectation failed: "+key+" expected="+value.dump()+" actual="+state.value(key,json()).dump());
            }else if(op=="quit")running=false;
            else throw std::runtime_error("Unknown script operation: "+op);
            sync();log<<json({{"action",action},{"state",snapshot()}}).dump()<<'\n';log.flush();
        }
        if(!running)break;
        if(swapchain->needsResize()){
            frames.clear();if(!swapchain->resize())continue;rebuild();
        }
        unsigned index=0;if(!swapchain->acquireTexture(acquire.get(),&index)){SDL_Delay(10);continue;}
        const auto width=swapchain->getWidth(),height=swapchain->getHeight();context->SetDimensions({int(width),int(height)});context->Update();
        commands->begin();auto* texture=swapchain->getTexture(index);
        commands->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(texture,RenderTextureLayout::COLOR_WRITE));commands->setFramebuffer(frames[index].get());
        commands->setViewports(RenderViewport(0,0,float(width),float(height)));commands->setScissors(RenderRect(0,0,width,height));commands->clearColor(0,RenderColor(.03,.05,.08,1));
        renderer.start(commands.get(),width,height);context->Render();renderer.end(commands.get(),frames[index].get());
        std::function<void()> finish;if(!shot.empty())finish=surface.capture(device.get(),commands.get(),texture,width,height,shot);
        commands->barriers(RenderBarrierStage::NONE,RenderTextureBarrier(texture,RenderTextureLayout::PRESENT));commands->end();
        const auto* cmd=commands.get();auto* wait=acquire.get();auto* signal=release[index].get();
        queue->executeCommandLists(&cmd,1,&wait,1,&signal,1,fence.get());swapchain->present(index,&signal,1);queue->waitForCommandFence(fence.get());
        if(finish){finish();std::ofstream(shot.string()+".json")<<snapshot().dump(2)<<'\n';}
        ++frame;
        if(options.contains("--script") && next_action==script.size())running=false;
        SDL_Delay(8);
    }
    std::ofstream(output/"result.json")<<json({{"schema","srw64.ui-probe-result.v1"},{"status","passed"},{"state",snapshot()},
        {"scope","Synthetic name-page peer; no game execution or SRAM"}}).dump(2)<<'\n';
    }
    Rml::RemoveContext("name-probe");Rml::Shutdown();rml_cleanup.active=false;renderer.reset();
    std::cout<<"Name-page probe completed: "<<output<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"UI probe failed: "<<e.what()<<'\n';return 1;}}
