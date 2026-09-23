#include "name_page.hpp"
#include "ui_fonts.hpp"
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
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
NamePage::NamePage(Rml::Context& c,TextInput& i,NameActions a):context(c),text_input(i),actions(std::move(a)){
    if(!actions.select || !actions.choose || !actions.submit || !actions.review || !actions.validate)
        throw std::invalid_argument("Incomplete name-page actions");
}
NamePage::~NamePage(){if(document){text_input.clear();document->RemoveEventListener("click",this);document->RemoveEventListener("change",this);document->Close();}}
std::string NamePage::label(const std::string& key) const {
    auto found=labels.find(key);return found==labels.end()?key:found->second;
}
std::array<std::u16string,3> NamePage::values() const {
    auto result=request.values;
    if(document)for(unsigned i=0;i<3;++i)if(auto* field=document->GetElementById("field"+std::to_string(i)))
        result[i]=utf16(field->GetAttribute<Rml::String>("value",""));
    return result;
}
void NamePage::sync(const names::Request& next,const std::map<std::string,std::string>& next_labels,const std::string& next_locale) {
    if(next.person>names::Selection || next.route>=4)throw std::invalid_argument("Invalid name-page request");
    bool rebuild=art_changed || !document || next.serial!=request.serial || next.person!=request.person || next.visible!=request.visible || next_locale!=locale;
    auto edited=values();
    bool retain=document && next.serial==request.serial && next.person==request.person;
    bool new_request=next.serial!=request.serial || next.revision!=request.revision || next.error!=request.error;
    request=next;labels=next_labels;locale=next_locale;art_changed=false;
    if(new_request){waiting=false;local_error.clear();}
    if(!request.visible){if(document && document->IsVisible()){text_input.clear();document->Hide();}return;}
    if(rebuild){
        const std::string focus=context.GetFocusElement()?context.GetFocusElement()->GetId():"";
        // Cancel pending preedit before reading values when changing locale.
        if(retain){text_input.clear();edited=values();}
        build();
        if(retain)for(unsigned i=0;i<3;++i)if(auto* field=document->GetElementById("field"+std::to_string(i)))field->SetAttribute("value",utf8(edited[i]));
        if(retain && !focus.empty())if(auto* field=document->GetElementById(focus)){context.Update();field->Focus();}
    }
    for(unsigned i=0;i<4;++i)if(auto* card=document->GetElementById("route"+std::to_string(i))){
        card->SetClass("selected",i==request.route);
        if(!request.active || waiting)card->SetAttribute("disabled","");else card->RemoveAttribute("disabled");
    }
    if(auto* error=document->GetElementById("error")){
        const auto key=request.error.empty()?local_error:request.error;
        error->SetInnerRML(Rml::StringUtilities::EncodeRml(key.empty()?"":label(key)));
    }
    for(auto id:{"next","back","reset"})if(auto* button=document->GetElementById(id)){
        if(!request.active || waiting)button->SetAttribute("disabled","");else button->RemoveAttribute("disabled");
    }
}
void NamePage::build() {
    if(document){text_input.clear();document->RemoveEventListener("click",this);document->RemoveEventListener("change",this);document->Close();context.Update();}
    auto escape=[](const std::string& s){return Rml::StringUtilities::EncodeRml(s);};
    auto t=[&](const char* key){return escape(label(key));};
    std::string body="<div id='page'><div id='top'>SRW64 <span>"+t("name_title")+"</span></div><div id='steps'>";
    unsigned step=0;
    for(auto key:{"name_step_select","name_step_player","name_step_partner","name_step_review"}){
        body+="<span class='"+std::string(step==(request.person==names::Selection?0:request.person+1)?"active":"")+"'>"+t(key)+"</span>";++step;
    }
    body+="</div>";
    if(request.person==names::Selection){
        body+="<h1>"+t("select_title")+"</h1><p>"+t("select_hint")+"</p><div id='cards'>";
        for(unsigned i=0;i<4;++i){
            const auto& choice=request.choices[i];
            body+="<button class='card' id='route"+std::to_string(i)+"'><div class='eyebrow'>"+t(i<2?"select_super":"select_real")+"</div>";
            for(unsigned p=0;p<2;++p){
                if(!choice.portraits[p][hd && !choice.portraits[p][1].empty()?1:0].empty())body+="<img src='"+escape(choice.portraits[p][hd && !choice.portraits[p][1].empty()?1:0])+"'/>";
                body+="<div class='person'>"+escape(utf8(choice.names[p][0])+" "+utf8(choice.names[p][1]))+"</div>";
            }
            body+="</button>";
        }
        body+="</div>";
    } else if(request.person==names::Review){
        body+="<h1>"+t("name_review")+"</h1><p>"+t("name_review_hint")+"</p><div id='review'>";
        for(unsigned p=0;p<2;++p)body+="<h2>"+t(p?"name_step_partner":"name_step_player")+"</h2><p>"+escape(utf8(request.names[p][0])+" "+utf8(request.names[p][1])+" / "+utf8(request.names[p][2]))+"</p>";
        body+="</div>";
    } else {
        body+="<h1>"+t(request.person==names::Player?"name_player":"name_partner")+"</h1><p>"+t("name_page_hint")+"</p><div id='editor'>";
        if(!request.portraits[request.person][hd && !request.portraits[request.person][1].empty()?1:0].empty())body+="<img class='portrait' src='"+escape(request.portraits[request.person][hd && !request.portraits[request.person][1].empty()?1:0])+"'/>";
        body+="<div id='fields'>";
        const char* keys[]={"name_given","name_family","name_nickname"};
        for(unsigned i=0;i<3;++i)body+="<label>"+t(keys[i])+"</label><input type='text' id='field"+std::to_string(i)+"'/>";
        body+="</div></div>";
    }
    body+="<p id='error'></p><div id='footer'>";
    if(request.person!=names::Selection)body+="<button id='back'>"+t("name_back")+"</button>";
    if(request.person<names::Review)body+="<button id='reset'>"+t("name_default")+"</button>";
    const char* next=request.person==names::Selection?"select_confirm":request.person==names::Player?"name_to_partner":request.person==names::Partner?"name_to_review":"name_start";
    body+="<button id='next'>"+t(next)+"</button></div></div>";
    const std::string style=R"(
scrollbarvertical { width: 12dp; } scrollbarhorizontal { height: 12dp; }
scrollbarvertical slidertrack, scrollbarhorizontal slidertrack { background-color: #122131; }
scrollbarvertical sliderbar, scrollbarhorizontal sliderbar { background-color: #506d81; min-height: 16dp; min-width: 12dp; }
body { display: block; width: 100%; height: 100%; font-family: srw64-ui; font-size: 18dp; color: #d6e2ef; background-color: #0b1421; margin: 0; }
div, h1, h2, p, label { display: block; }
#page { height: 94%; overflow-y: auto; width: 90%; max-width: 1100dp; margin: 24dp auto; }
#top { font-size: 25dp; font-weight: bold; color: #9be4f7; margin-bottom: 18dp; }
#top span { font-size: 18dp; margin-left: 24dp; color: #8b9eb4; }
#steps { display: flex; padding-bottom: 14dp; border-bottom: 1dp #294154; }
#steps span { width: 25%; color: #8b9eb4; }
#steps span.active { color: #9be4f7; }
h1 { font-size: 30dp; margin: 24dp 0 8dp; } h2 { font-size: 22dp; }
p { margin: 8dp 0 18dp; color: #9eafc3; }
#cards { display: flex; margin-top: 24dp; }
button { display: inline-block; padding: 12dp 18dp; background-color: #152436; color: #d6e2ef; border: 1dp #304859; border-radius: 7dp; cursor: pointer; tab-index: auto; }
button:hover, button:focus { border-color: #9be4f7; background-color: #22394e; }
button:disabled { opacity: 0.45; }
.card { width: 21%; margin-right: 1%; padding: 12dp 1%; }
.card.selected { border-color: #9be4f7; background-color: #1e3548; }
.card img { width: 72dp; height: 72dp; display: block; margin: 12dp auto 6dp; }
.person { text-align: center; font-size: 16dp; margin-bottom: 16dp; }
.eyebrow { color: #9be4f7; font-size: 15dp; }
#editor { display: flex; margin-top: 28dp; } .portrait { width: 160dp; height: 160dp; margin: 20dp 40dp 0 0; }
#fields { width: 70%; max-width: 620dp; } label { display: block; color: #9eafc3; margin: 12dp 0 6dp; }
input { display: block; width: 95%; height: 30dp; font-size: 22dp; padding: 10dp; border: 1dp #304859; border-radius: 5dp; background-color: #142334; color: #edf4fa; tab-index: auto; }
input:focus { border-color: #9be4f7; } input selection { color: #0b1421; background-color: #9be4f7; }
#footer { display: flex; margin-top: 20dp; } #footer button { margin-right: 12dp; }
#next { background-color: #9be4f7; color: #0b2737; margin-left: auto; }
#error { color: #ffaaa0; min-height: 24dp; } #review { min-height: 240dp; }
)";
    document=context.LoadDocumentFromMemory("<rml><head><style>"+style+locale_font_css(locale)+"</style></head><body>"+body+"</body></rml>");
    if(!document)throw std::runtime_error("Cannot build name page");
    for(unsigned i=0;i<3;++i)if(auto* field=document->GetElementById("field"+std::to_string(i)))field->SetAttribute("value",utf8(request.values[i]));
    document->AddEventListener("click",this);document->AddEventListener("change",this);document->Show();context.Update();
    if(auto* field=document->GetElementById("field0"))field->Focus();
}
void NamePage::advance(){
    if(!request.active || waiting || !text_input.accepts_submit())return;
    if(request.person==names::Selection){actions.choose(request.serial,request.route);waiting=true;}
    else if(request.person==names::Review){actions.review(request.serial,true);waiting=true;}
    else{
        const auto edited=values();
        for(unsigned i=0;i<3;++i)if(auto error=actions.validate(edited[i],i);!error.empty()){
            local_error=error;document->GetElementById("error")->SetInnerRML(Rml::StringUtilities::EncodeRml(label(error)));return;
        }
        actions.submit(request.serial,edited,false);waiting=true;
    }
}
void NamePage::action(const std::string& id){
    if(!request.visible || !request.active || waiting)return;
    if(id=="next")advance();
    else if(id=="back"){
        if(text_input.has_composition()){text_input.clear();return;}
        if(request.person==names::Review)actions.review(request.serial,false);
        else if(request.person!=names::Selection)actions.submit(request.serial,values(),true);
        else return;
        waiting=true;
    } else if(id=="reset"){
        text_input.clear();for(unsigned i=0;i<3;++i)if(auto* field=document->GetElementById("field"+std::to_string(i)))field->SetAttribute("value",utf8(request.values[i]));
    } else if(id.size()==6 && id.starts_with("route") && id[5]>='0' && id[5]<='3' && request.person==names::Selection){
        request.route=unsigned(id[5]-'0');actions.select(request.serial,request.route);
    }
}
void NamePage::ProcessEvent(Rml::Event& e){
    if(e.GetType()=="change"){local_error.clear();return;}
    for(auto* el=e.GetTargetElement();el && el!=document;el=el->GetParentNode())if(el->GetTagName()=="button"){action(el->GetId());break;}
}
bool NamePage::event(SDL_Event& event){
    if(!request.visible)return false;
    if(text_input.event(event))return true;
    if(event.type==SDL_KEYDOWN && !event.key.repeat){
        const auto key=event.key.keysym.sym;
        if(key==SDLK_RETURN || key==SDLK_KP_ENTER){advance();return true;}
        if(key==SDLK_ESCAPE){action("back");return true;}
        if(request.person==names::Selection){
            if(key==SDLK_LEFT || key==SDLK_RIGHT){action("route"+std::to_string((request.route+(key==SDLK_LEFT?3:1))%4));return true;}
            if(key==SDLK_z){advance();return true;}
        }
    }
    return false;
}
}
