#include "name_page.hpp"
#include "probe_surface.hpp"
#include "ui_renderer.h"
#include "RmlUi_Platform_SDL.h"
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include "game_adapter/name_codec.hpp"
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
// changes SRAM. Name validation can use the real glyph map from local content.
struct Fixture {
    srw64::names::Request request;
    srw64::names::Codec codec;
    unsigned submissions{},starts{};
    Fixture(){
        request.serial=1;request.visible=request.active=true;request.person=srw64::names::Selection;
        for(unsigned i=0;i<4;++i)request.choices[i].names={{{u"アキラ",utf16("Test"+std::to_string(i+1))},{u"ユウ",u"Test"}}};
        unsigned glyph=1;
        for(char16_t c:u"アキラユウナテスト名前漢字")codec.add(glyph++,std::u16string(1,c));
        for(char16_t c=0xFF01;c<=0xFF5E;++c)codec.add(glyph++,std::u16string(1,c));
    }
    void open(unsigned person){request.person=person;++request.serial;request.active=true;request.pending=false;request.error.clear();
        if(person<2)request.values=request.names[person];}
    NameActions actions(){return {
        [this](uint64_t serial,unsigned route){if(serial==request.serial && route<4)request.route=route;},
        [this](uint64_t serial,unsigned route){if(serial!=request.serial || route>=4)return;request.route=route;
            for(unsigned p=0;p<2;++p)request.names[p]={request.choices[route].names[p][0],request.choices[route].names[p][1],request.choices[route].names[p][0]};
            request.portraits={request.choices[route].portraits[0],request.choices[route].portraits[1]};open(0);},
        [this](uint64_t serial,const std::array<std::u16string,3>& values,bool cancel){
            if(serial!=request.serial || request.person>1)return;
            if(cancel){open(srw64::names::Selection);return;}
            ++submissions;request.names[request.person]=values;open(request.person+1);},
        [this](uint64_t serial,bool confirm){if(serial!=request.serial)return;if(confirm){++starts;request.visible=false;}else open(0);},
        [this](const std::u16string& value,unsigned field){std::vector<uint16_t> encoded;return codec.encode(value,field,encoded);}
    };}
};
}
int main(int argc,char** argv){try{
    std::map<std::string,std::string> options;
    for(int i=1;i<argc;++i){std::string key=argv[i];if(key=="--help"){
        std::cout<<"srw64-ui-probe --catalog-dir content/locales --font local.ttf --output NEW_DIR [--script actions.json] [--dialogue local/dialogue.json] [--language ja|zh-Hans|en]\n";return 0;}
        if(i+1==argc || !key.starts_with("--") || options.contains(key))throw std::runtime_error("Invalid probe arguments");options[key]=argv[++i];}
    for(const auto& [key,value]:options)if(key!="--catalog-dir" && key!="--font" && key!="--output" && key!="--script" && key!="--dialogue" && key!="--language")throw std::runtime_error("Unknown option: "+key);
    for(auto key:{"--catalog-dir","--font","--output"})if(!options.contains(key))throw std::runtime_error(std::string("Required: ")+key);
    fs::path output=fs::absolute(options.at("--output"));if(!fs::create_directory(output))throw std::runtime_error("Output must be a new directory");
    std::map<std::string,std::map<std::string,std::string>> catalogs;
    for(auto language:{"ja","zh-Hans","en"})catalogs[language]=load(fs::path(options.at("--catalog-dir"))/(std::string(language)+".json")).at("ui");
    std::string locale=options.contains("--language")?options.at("--language"):"en";
    if(!catalogs.contains(locale))throw std::runtime_error("Unknown locale");
    json script=json::array();if(options.contains("--script")){
        const auto data=load(options.at("--script"));if(data.at("schema")!="srw64.ui-probe-script.v1")throw std::runtime_error("Invalid script schema");script=data.at("actions");}
    Fixture fixture;
    std::vector<std::pair<std::string,std::vector<char>>> portraits;
    if(options.contains("--dialogue")){
        const auto data=load(options.at("--dialogue"));
        fixture.codec={};for(auto& [code,value]:data.at("glyphs").items())fixture.codec.add(std::stoul(code),utf16(value.get<std::string>()));
        unsigned i=0;for(auto& [id,face]:data.at("name_entry_assets").at("portraits").items()){
            if(i==8)break;fs::path path=face.at("original").get<std::string>();if(path.is_relative())path=fs::path(options.at("--dialogue")).parent_path()/path;
            std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Missing fixture portrait");
            std::string name="fixture-face-"+id;portraits.push_back({name,{std::istreambuf_iterator<char>(in),{}}});fixture.request.choices[i/2].portraits[i%2][0]=name;++i;
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
    TextInput input;
    if(!Rml::Initialise())throw std::runtime_error("RmlUi initialization failed");
    struct RmlCleanup{bool active=true;~RmlCleanup(){if(active)Rml::Shutdown();}} rml_cleanup;
    if(!Rml::LoadFontFace(font,"srw64-ui",Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal,true))throw std::runtime_error("Cannot load font");
    auto* context=Rml::CreateContext("name-probe",{1100,760},nullptr,&input);if(!context)throw std::runtime_error("Cannot create RmlUi context");
    input.bind(*context);
    bool running=true;unsigned frame=0;size_t next_action=0;std::ofstream log(output/"events.jsonl");
    {
    NamePage page(*context,input,fixture.actions());
    auto sync=[&]{page.sync(fixture.request,catalogs.at(locale),locale);context->Update();input.update_rectangle();};sync();
    auto snapshot=[&]{json values=json::array();for(auto& value:page.values())values.push_back(utf8(value));
        json state={{"schema","srw64.ui-probe-state.v1"},{"frame",frame},{"locale",locale},{"serial",fixture.request.serial},
            {"person",fixture.request.person},{"route",fixture.request.route},{"visible",fixture.request.visible},{"values",values},
            {"composition",input.has_composition()},{"pending",page.pending()},{"submissions",fixture.submissions},{"starts",fixture.starts}};
        if(auto* focused=context->GetFocusElement())state["focus"]=focused->GetId();
        if(auto* view=page.view())state["error"]=view->GetElementById("error")->GetInnerRML();return state;};
    auto event=[&](SDL_Event& e){
        if(e.type==SDL_QUIT){running=false;return;}
        if(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_F7 && !e.key.repeat){
            locale=locale=="ja"?"zh-Hans":locale=="zh-Hans"?"en":"ja";sync();return;}
        if(!page.event(e))RmlSDL::InputEventHandler(context,e);
        // This modal page consumes all input. The real game adapter remains
        // responsible for its existing release-after-close gate.
        if(e.type==SDL_TEXTEDITING_EXT)SDL_free(e.editExt.text);
    };
    while(running){
        SDL_Event e;while(SDL_PollEvent(&e))event(e);
        sync();fs::path shot;
        if(next_action<script.size() && frame>=script[next_action].value("frame",unsigned(next_action*3+3))){
            const auto action=script[next_action++];const auto op=action.at("op").get<std::string>();
            if(op=="click")page.action(action.at("id"));
            else if(op=="focus"){
                auto* field=page.view()->GetElementById(action.at("id").get<std::string>());if(!field)throw std::runtime_error("Unknown focus target");field->Focus();
            }else if(op=="select_all"){
                auto* field=dynamic_cast<Rml::ElementFormControlInput*>(context->GetFocusElement());if(!field)throw std::runtime_error("No focused input");field->Select();
            }else if(op=="text" || op=="preedit"){
                const std::string text=action.at("text");SDL_Event injected{};injected.type=op=="text"?SDL_TEXTINPUT:SDL_TEXTEDITING;
                if(text.size()>=sizeof(injected.text.text))throw std::runtime_error("Script text exceeds SDL event capacity");
                if(op=="text")SDL_strlcpy(injected.text.text,text.c_str(),sizeof(injected.text.text));
                else{SDL_strlcpy(injected.edit.text,text.c_str(),sizeof(injected.edit.text));injected.edit.start=action.value("start",0);injected.edit.length=action.value("length",0);}event(injected);
            }else if(op=="key"){
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
        const auto width=swapchain->getWidth(),height=swapchain->getHeight();context->SetDimensions({int(width),int(height)});context->Update();input.update_rectangle();
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
        {"scope","Synthetic name-page peer; no game execution, SRAM, or OS-IME acceptance"}}).dump(2)<<'\n';
    }
    Rml::RemoveContext("name-probe");Rml::Shutdown();rml_cleanup.active=false;renderer.reset();
    std::cout<<"Name-page probe completed: "<<output<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"UI probe failed: "<<e.what()<<'\n';return 1;}}
