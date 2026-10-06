#include "name_page.hpp"
#include "ui_fonts.hpp"
#include "game_adapter/default_names.hpp"
#include <algorithm>
#include <stdexcept>

namespace srw64::ui {
std::string utf8(const std::u16string& value) {
    std::string result;
    for (size_t i=0;i<value.size();++i) {
        uint32_t cp=value[i];
        if(cp>=0xD800 && cp<=0xDBFF) {
            if(++i==value.size() || value[i]<0xDC00 || value[i]>0xDFFF)throw std::runtime_error("Invalid UTF-16 pair");
            cp=0x10000+((cp-0xD800)<<10)+(value[i]-0xDC00);
        } else if(cp>=0xDC00 && cp<=0xDFFF)throw std::runtime_error("Invalid UTF-16 surrogate");
        result+=Rml::StringUtilities::ToUTF8(static_cast<Rml::Character>(cp));
    }
    return result;
}
std::u16string utf16(const std::string& value) {
    std::u16string result;
    for(Rml::StringIteratorU8 it(value);it;++it) {
        auto v=static_cast<uint32_t>(*it);
        if(v>0xFFFF){v-=0x10000;result+=char16_t(0xD800+(v>>10));result+=char16_t(0xDC00+(v&1023));}
        else result+=char16_t(v);
    }
    return result;
}
NamePage::NamePage(Rml::Context& c,NameActions a):context(c),actions(std::move(a)){
    if(!actions.select || !actions.choose || !actions.review || !actions.image)throw std::invalid_argument("Incomplete name-page actions");
}
NamePage::~NamePage(){if(document){document->RemoveEventListener("click",this);document->Close();}}
std::string NamePage::label(const std::string& key) const {
    auto found=labels.find(key);return found==labels.end()?key:found->second;
}
void NamePage::sync(const names::Request& next,const std::map<std::string,std::string>& next_labels,const std::string& next_locale) {
    // The frontend syncs every frame; with no page up the request is the default one.
    if(next.visible && ((next.person!=names::Selection && next.person!=names::Review) || next.route>=4))
        throw std::invalid_argument("Invalid name-page request");
    // The layout is sized to its room in dp (a Steam Deck at Largest: 865 x 540; at 4:3 the
    // picture's 800 x 600), the whole window unless set_area says otherwise.
    const auto dims=context.GetDimensions();
    if(area_w<=0 || area_h<=0){area_x=area_y=0;area_w=float(dims.x);area_h=float(dims.y);}
    const float density=std::max(0.01f,context.GetDensityIndependentPixelRatio());
    const int w=int(area_w/density+.5f),h=int(area_h/density+.5f),ix=int(area_x),iy=int(area_y);
    const bool rebuild=art_changed || !document || next.serial!=request.serial || next.person!=request.person || next.visible!=request.visible || next_locale!=locale || w!=page_w || h!=page_h || ix!=inset_x || iy!=inset_y;
    if(next.serial!=request.serial || next.revision!=request.revision)waiting=false;
    request=next;labels=next_labels;locale=next_locale;art_changed=false;page_w=w;page_h=h;inset_x=ix;inset_y=iy;
    if(!request.visible){if(document && document->IsVisible())document->Hide();return;}
    if(rebuild)build();
    for(unsigned i=0;i<4;++i)if(auto* card=document->GetElementById("route"+std::to_string(i))){
        card->SetClass("selected",i==request.route);
        if(!request.active || waiting)card->SetAttribute("disabled","");else card->RemoveAttribute("disabled");
    }
    for(auto id:{"next","back"})if(auto* button=document->GetElementById(id)){
        if(!request.active || waiting)button->SetAttribute("disabled","");else button->RemoveAttribute("disabled");
    }
}
// The 2 x 2 card selection and the review with both starting units, laid out in dp for
// the current window (design: the SRW64 主角选择重设计 canvas, 方案 C). Every size that
// depends on the window is computed here; the rest is the style sheet below.
void NamePage::build() {
    if(document){document->RemoveEventListener("click",this);document->Close();context.Update();}
    auto escape=[](const std::string& s){return Rml::StringUtilities::EncodeRml(s);};
    auto t=[&](const char* key){return escape(label(key));};
    auto dp=[](float v){return std::to_string(int(v+.5f))+"dp";};
    const auto full=[&](const std::array<std::u16string,3>& person){return utf8(person[0])+names::separator(locale)+utf8(person[1]);};
    const bool selection=request.person==names::Selection;
    // Route colours: super routes warm, real routes cool, told apart by lightness too.
    const auto route_colour=[](unsigned route){return route<2?"#f0b060":"#86bcf7";};
    const auto route_tag=[&](unsigned route,const char* fill){
        return "<div class='tagrow'><div class='tag' style='decorator: slant("+std::string(fill)+" "+fill+" 0dp 0dp 0dp 0dp 0dp 10dp);'><span class='bar' style='decorator: slant("+
            std::string(route_colour(route))+" "+route_colour(route)+" 0dp 0dp 3dp 0dp 0dp 3dp);'></span><span style='color:"+route_colour(route)+";'>"+t(route<2?"select_super":"select_real")+"</span></div></div>";
    };
    // A square portrait frame; the image is resampled for its size.
    const auto portrait=[&](const std::array<std::string,2>& art,float size,const char* frame){
        const auto& path=art[hd && !art[1].empty()?1:0];
        std::string img=path.empty()?"":"<img src='"+escape(actions.image(path,int(size+.5f)))+"' style='width:"+dp(size)+"; height:"+dp(size)+";'/>";
        return "<div class='frame' style='width:"+dp(size)+"; height:"+dp(size)+"; border-color:"+frame+";'>"+img+"</div>";
    };
    // A unit pose fitted into a square box, keeping its 96 x 96 or 128 x 96 proportions.
    const auto unit_pose=[&](const names::Art& art,float box){
        const auto& path=hd && !art.hd.empty()?art.hd:art.original;
        std::string img;
        if(!path.empty() && art.width && art.height) {
            const float scale=std::min(box/float(art.width),box/float(art.height)),w=art.width*scale,h=art.height*scale;
            img="<img src='"+escape(actions.image(path,int(w+.5f)))+"' style='width:"+dp(w)+"; height:"+dp(h)+";'/>";
        }
        return "<div class='pose' style='width:"+dp(box)+"; height:"+dp(box)+";'>"+img+"</div>";
    };
    const float margin=std::clamp(page_w*0.0375f,24.f,48.f),gap=12,header=44,footer=64;
    std::string body="<div id='page' style='padding: 0 "+dp(margin)+";'><div id='top'><div id='brand'><span class='logo'>SRW64</span><span class='rule'></span>";
    if(selection)body+="<span class='title'>"+t("select_title")+"</span><span class='hint'>"+t("select_hint")+"</span>";
    else body+="<span class='title'>"+t("name_title")+"</span>";
    body+="</div><div id='steps'><span class='step "+std::string(selection?"on":"")+"'><span class='num'>1</span><span>"+t("name_step_select")+"</span></span><span class='line "+std::string(selection?"":"on")+"'></span>";
    body+="<span class='step "+std::string(selection?"":"on")+"'><span class='num'>2</span><span>"+t("name_step_review")+"</span></span></div></div><div id='main'>";
    if(selection){
        // Two rows of two cards; each card shows the protagonist and the partner alike.
        const float grid_h=std::max(200.f,page_h-header-footer-3*gap),card_h=(grid_h-gap)/2,card_w=(page_w-2*margin-gap)/2-1;   // a dp of slack so rounding never wraps a row
        const float face=std::max(56.f,std::min(card_h-30-16-24-20,(card_w-56)/2));
        body+="<div id='cards'>";
        for(unsigned i=0;i<4;++i){
            const auto& choice=request.choices[i];
            body+="<button class='card' id='route"+std::to_string(i)+"' style='width:"+dp(card_w)+"; height:"+dp(card_h)+"; margin:"+(i>=2?dp(gap):"0")+" "+(i%2?"0":dp(gap))+" 0 0;'>"+route_tag(i,"#1b3448")+"<div class='faces'>";
            for(unsigned p=0;p<2;++p){
                body+="<div class='face'><div class='eyebrow "+std::string(p?"":"lead")+"'>"+t(p?"name_step_partner":"name_step_player")+"</div>"+
                    portrait(choice.portraits[p],face,p?"#1e3446":"#2f5670")+"<div class='person "+std::string(p?"":"lead")+"'>"+escape(full({choice.names[p][0],choice.names[p][1],{}}))+"</div></div>";
            }
            body+="</div></button>";
        }
        body+="</div>";
    } else {
        const auto& choice=request.choices[request.route];
        const float content=std::max(220.f,page_h-header-footer-3*gap-58),pilot_h=float(int(content*0.55f)),unit_h=content-pilot_h-gap;
        const float face=pilot_h-30-14,box=unit_h-16,card_w=(page_w-2*margin-gap)/2-1;
        body+="<h1>"+t("name_review")+"</h1><p class='lede'>"+t("name_review_hint")+"</p><div id='review'>";
        for(unsigned p=0;p<2;++p){
            body+="<div class='pilot "+std::string(p?"":"lead")+"' style='width:"+dp(card_w)+"; height:"+dp(pilot_h)+"; margin-right:"+(p?"0":dp(gap))+";'>"+route_tag(request.route,p?"#152535":"#1b3448")+
                "<div class='row'>"+portrait(choice.portraits[p],face,p?"#2b4356":"#9be4f7")+"<div class='text'><div class='eyebrow "+std::string(p?"":"lead")+"'>"+t(p?"name_step_partner":"name_step_player")+"</div>"+
                "<p id='name"+std::to_string(p)+"'><span class='full'>"+escape(full(request.names[p]))+"</span><span class='nick'> / "+escape(utf8(request.names[p][2]))+"</span></p></div></div></div>";
        }
        body+="</div><div id='units'>";
        for(unsigned p=0;p<2;++p){
            body+="<div class='unit' style='width:"+dp(card_w)+"; height:"+dp(unit_h)+"; margin-right:"+(p?"0":dp(gap))+";'>"+unit_pose(choice.units[p],box)+"<div class='text'><div class='eyebrow "+std::string(p?"":"lead")+"'>"+t("name_unit")+"</div>"+
                "<div class='unitname'>"+escape(choice.unit_names[p])+"</div><div class='pilotname'>"+t("name_pilot")+" · "+escape(utf8(request.names[p][2]))+"</div></div></div>";
        }
        body+="</div>";
    }
    // The footer stays on screen below the part that scrolls (a 540 dp tall Steam Deck).
    body+="</div><div id='footer'>";
    if(!selection)body+="<button id='back'>"+t("name_cancel")+"</button>";
    body+="<div class='keys'>"+t(selection?"select_keyboard_hint":"review_keyboard_hint")+"</div>";
    body+="<button id='next'>"+t(selection?"select_confirm":"name_start")+"</button></div></div>";
    const std::string style=R"(
layer-mark{display:block;width:0;height:0;} layer-mark.layer-end{position:absolute;left:0;top:0;z-index:2000000000;}
scrollbarvertical { width: 12dp; } scrollbarhorizontal { height: 12dp; }
scrollbarvertical slidertrack, scrollbarhorizontal slidertrack { background-color: #122131; }
scrollbarvertical sliderbar, scrollbarhorizontal sliderbar { background-color: #506d81; min-height: 16dp; min-width: 12dp; }
body { display: block; width: 100%; height: 100%; font-family: srw64-ui; font-size: 16dp; color: #eaf2f7; background-color: #0a141e; margin: 0; }
div, h1, h2, p { display: block; }
#page { display: flex; flex-direction: column; box-sizing: border-box; height: 100%; width: 100%; }
#main { flex: 1 1 auto; min-height: 0; overflow-y: auto; }
#top { display: flex; flex: none; height: 44dp; align-items: center; justify-content: space-between; }
#brand { display: flex; flex: 1 1 auto; min-width: 0; align-items: baseline; margin-right: 16dp; font-size: 20dp; font-weight: bold; color: #8fddf2; white-space: nowrap; overflow: hidden; }
#brand span { flex: none; }
#brand .rule { display: inline-block; width: 1dp; height: 18dp; background-color: #2b4356; margin: 0 12dp; }
#brand .title { font-size: 19dp; color: #eaf2f7; }
#brand .hint { font-size: 13dp; font-weight: normal; color: #8fa5b5; margin-left: 12dp; }
#steps { display: flex; flex: none; align-items: center; margin-left: auto; font-size: 15dp; color: #6f8595; white-space: nowrap; }
#steps .step { display: flex; align-items: center; }
#steps .step.on { color: #eaf2f7; font-weight: bold; }
#steps .num { display: inline-block; width: 22dp; height: 22dp; line-height: 22dp; text-align: center; font-size: 12dp; font-weight: bold; border-radius: 11dp; border: 2dp #2b4356; color: #6f8595; margin-right: 6dp; box-sizing: border-box; }
#steps .step.on .num { background-color: #8fddf2; border-color: #8fddf2; color: #0a141e; }
#steps .line { display: inline-block; width: 32dp; height: 2dp; background-color: #1e3446; margin: 0 10dp; }
#steps .line.on { background-color: #8fddf2; }
h1 { font-size: 24dp; font-weight: bold; margin: 8dp 0 0; }
p.lede { font-size: 13dp; color: #8fa5b5; margin: 2dp 0 10dp; }
#cards { display: flex; flex-wrap: wrap; margin-top: 12dp; }
button { display: block; box-sizing: border-box; padding: 0; background-color: #101e2c; color: #eaf2f7; border: 2dp #1e3446; border-radius: 12dp; cursor: pointer; tab-index: auto; overflow: hidden; }
button:hover, button:focus { border-color: #4f7d94; background-color: #15283a; }
button:disabled { opacity: 0.45; }
.card.selected { border-color: #8fddf2; background-color: #15283a; }
.tagrow { display: block; flex: none; height: 30dp; padding-left: 10dp; white-space: nowrap; }   /* RmlUi clips to a rectangle: start the tag past the 12dp corner radius so its square corner never covers the rounded border */
.tag { display: inline-block; height: 30dp; line-height: 30dp; padding: 0 20dp 0 12dp; font-size: 13dp; font-weight: bold; white-space: nowrap; }
.tag .bar { display: inline-block; width: 5dp; height: 14dp; margin-right: 8dp; vertical-align: -3dp; }
.faces { display: flex; justify-content: center; padding: 0 12dp; }
.face { display: flex; flex-direction: column; align-items: center; flex: 1 1 0; min-width: 0; }
.eyebrow { font-size: 12dp; color: #8fa5b5; line-height: 16dp; white-space: nowrap; }
.eyebrow.lead { color: #c9d6df; }
.card.selected .eyebrow.lead, .card.selected .person.lead { color: #8fddf2; }
.card.selected .face .frame { border-color: #8fddf2; }
.frame { box-sizing: border-box; margin: 4dp 0; background-color: #0a141e; border: 2dp #1e3446; border-radius: 9dp; overflow: hidden; }
.frame img { display: block; }
.person { width: 100%; text-align: center; font-size: 17dp; font-weight: bold; line-height: 24dp; white-space: nowrap; overflow: hidden; }
#review, #units { display: flex; }
#units { margin-top: 12dp; }
.pilot, .unit { display: flex; flex-direction: column; box-sizing: border-box; flex: none; background-color: #101e2c; border: 1dp #2b4356; border-radius: 12dp; overflow: hidden; }
.pilot.lead { background-color: #15283a; border: 2dp #8fddf2; }
.pilot .row { display: flex; align-items: center; padding: 0 16dp 8dp; }
.pilot .frame { flex: none; margin: 0 16dp 0 0; }
.text { display: flex; flex: 1 1 auto; flex-direction: column; justify-content: center; min-width: 0; overflow: hidden; }
.text .eyebrow { border-bottom: 1dp #2b4356; padding-bottom: 4dp; margin-bottom: 6dp; }
#review p { margin: 0; white-space: nowrap; overflow: hidden; }
#review .full { font-size: 23dp; font-weight: bold; }
#review .nick { display: block; font-size: 17dp; color: #c9d6df; margin-top: 2dp; }
.unit { flex-direction: row; align-items: center; padding: 8dp 16dp 8dp 8dp; }
.pose { display: flex; flex: none; align-items: center; justify-content: center; margin-right: 12dp; }
.pose img { display: block; }
.unitname { font-size: 21dp; font-weight: bold; line-height: 28dp; white-space: nowrap; overflow: hidden; }
.pilotname { font-size: 13dp; color: #8fa5b5; border-top: 1dp #2b4356; padding-top: 6dp; margin-top: 6dp; white-space: nowrap; overflow: hidden; }
#footer { display: flex; flex: none; align-items: center; height: 64dp; }
#footer button { flex: none; height: 44dp; line-height: 40dp; padding: 0 18dp; font-size: 17dp; font-weight: bold; white-space: nowrap; }
#footer .keys { flex: 1 1 auto; min-width: 0; margin: 0 16dp; font-size: 15dp; color: #a9bccb; white-space: nowrap; overflow: hidden; }   /* the pre-battle page's hint size; 12dp was too small (user, 2026-09-30) */
#back { border-color: #2b4356; font-weight: normal; }
#next { background-color: #8fddf2; border-color: #8fddf2; color: #0a141e; }
#next:hover, #next:focus { background-color: #b9ecf9; border-color: #b9ecf9; }
)";
    document=context.LoadDocumentFromMemory("<rml><head><style>"+style+locale_font_css(locale)+"</style></head><body style='box-sizing: border-box; border-width: "+std::to_string(inset_y)+"px "+std::to_string(inset_x)+"px; border-color: #00000000;'><layer-mark/>"+body+"<layer-mark class='layer-end'/></body></rml>");
    if(!document)throw std::runtime_error("Cannot build name page");
    document->AddEventListener("click",this);document->Show();context.Update();
}
void NamePage::advance(){
    if(!request.active || waiting)return;
    if(request.person==names::Selection)actions.choose(request.serial,request.route);
    else actions.review(request.serial,true);
    waiting=true;
}
void NamePage::action(const std::string& id){
    if(!request.visible || !request.active || waiting)return;
    if(id=="next")advance();
    else if(id=="back" && request.person==names::Review){actions.review(request.serial,false);waiting=true;}
    else if(id.size()==6 && id.starts_with("route") && id[5]>='0' && id[5]<='3' && request.person==names::Selection){
        request.route=unsigned(id[5]-'0');actions.select(request.serial,request.route);
    }
}
void NamePage::ProcessEvent(Rml::Event& e){
    for(auto* el=e.GetTargetElement();el && el!=document;el=el->GetParentNode())if(el->GetTagName()=="button"){action(el->GetId());break;}
}
bool NamePage::event(SDL_Event& event){
    if(!request.visible)return false;
    if(event.type==SDL_KEYDOWN && !event.key.repeat){
        const auto key=event.key.keysym.sym;
        if(key==SDLK_RETURN || key==SDLK_KP_ENTER || key==SDLK_z){advance();return true;}
        if(key==SDLK_ESCAPE || key==SDLK_x){action("back");return true;}
        if(request.person==names::Selection){
            // Cards sit two by two: left and right walk the route order as the original
            // (801C34E4), up and down swap rows.
            if(key==SDLK_LEFT || key==SDLK_RIGHT){action("route"+std::to_string((request.route+(key==SDLK_LEFT?3:1))%4));return true;}
            if(key==SDLK_UP || key==SDLK_DOWN){action("route"+std::to_string(request.route^2));return true;}
        }
    }
    return false;
}
}
