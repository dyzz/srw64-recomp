// Story MODs (src/host/story_mod.hpp, docs/design/mod-packages.md §7): the compiled
// image's rules, the triggers against a guest-memory picture, the persuasion mark, the
// events after the end event, what has fired across suspend records, and the lines
// served as text records.
#include "story_mod.hpp"
#include "miniz.h"
#include <cstdio>
#include <cstdlib>
uint64_t srw64_current_vi(){return 0;}
using namespace srw64;
using json=nlohmann::json;
namespace fs=std::filesystem;
namespace story=srw64::story_mod;

static int failures=0;
static void check(bool ok,const std::string& what){if(!ok){std::fprintf(stderr,"FAIL %s\n",what.c_str());++failures;}}
static std::string fails(const json& image) {
    story::Library library;
    try {story::add_image(library,"test",image);} catch(const std::exception& e){return e.what();}
    return {};
}
static json event(const std::string& id,const std::string& trigger,json params,json words={0x3D4E,0xFFFF}) {
    return {{"id",id},{"scene",129},{"trigger",trigger},{"params",params},{"words",words}};
}

int main(int argc,char** argv) {
    if(argc!=3){std::fprintf(stderr,"usage: test <scratch folder> <compiled example image>\n");return 2;}
    const fs::path dir=argv[1];
    fs::remove_all(dir);fs::create_directories(dir);
    std::vector<uint8_t> ram(0x800000);
    using guest::read;using guest::write16;using guest::write32;using guest::write8;
    const uint32_t e=story::engine;

    // --- The image -----------------------------------------------------------------
    json image={{"schema",story::image_schema},{"texts",json::array({{{"speaker",4},{"lines",{{"ja","テスト"},{"zh-Hans","测试"}}}}})},
        {"events",json::array({event("a","turn",{1,1},{0x3D3E,0xFFFE,0xFFFF})})}};
    image["events"][0]["texts"]=json::array({json::array({1,0})});
    check(fails(image).empty(),"a small image: "+fails(image));
    {
        auto bad=image;bad["events"][0]["trigger"]="nope";check(!fails(bad).empty(),"an unknown trigger");
        bad=image;bad["events"][0]["words"]={0x3D4E};check(!fails(bad).empty(),"words without FFFF");
        bad=image;bad["events"][0]["texts"]=json::array({json::array({1,5})});check(!fails(bad).empty(),"a line out of range");
        bad=image;bad["texts"][0]["lines"].erase("ja");check(!fails(bad).empty(),"a line without ja");
        bad=image;bad["events"][0]["fired"]={"b"};check(!fails(bad).empty(),"fired names an unknown event");
        bad=image;bad["events"].push_back(bad["events"][0]);check(!fails(bad).empty(),"duplicate ids");
        bad=image;bad["events"][0]["trigger"]="after_end";bad["events"][0]["repeat"]=true;check(!fails(bad).empty(),"after_end repeating");
        bad=image;bad["events"][0]["vars"]={{"131",4}};check(!fails(bad).empty(),"a variable value past 3");
        bad=image;bad["events"][0]["kills"]={{"4",0}};check(!fails(bad).empty(),"kills of 0");
        bad=image;bad["events"][0]["kills"]={{"400",5}};check(!fails(bad).empty(),"kills of a pilot past 360");
    }
    // A stage information entry granted again in a later playthrough (legacy.cpp).
    {
        auto with=image;
        with["stage_info"]=json::parse(R"([{"id":"kai","scene":40,"title":{"ja":"a"},"text":{"ja":"b"},"done":{"pilots":[18],"units":[27]},
            "inherit":{"after":[40],"commands":[["3D5A",18,{"like":4},27,500],["3D5A",18,-2,30,27],["3D6F",874]],"crew":[{"unit":27,"pilots":[18,4]}]}}])");
        story::Library library;
        story::add_image(library,"p",with);
        const auto& info=library.infos.at(0);
        check(info.id=="kai" && info.inherit && info.inherit->after==std::vector<int>{40} && info.inherit->before.empty(),"an entry's grant");
        check(info.inherit->commands==json::parse(R"([["3D5A",18,0,27,500],["3D5A",18,-2,30,27],["3D6F",874]])") &&
              info.inherit->like==std::vector<std::pair<size_t,unsigned>>{{0,4}},"its commands, a level like Domon's");
        check(info.inherit->crew.size()==1 && info.inherit->crew[0].second==std::vector<unsigned>{18,4},"its crew");
        auto bad=with;bad["stage_info"][0].erase("id");check(!fails(bad).empty(),"a grant without an id");
        bad=with;bad["stage_info"][0].erase("done");check(!fails(bad).empty(),"a grant without done");
        bad=with;bad["stage_info"][0]["inherit"].erase("after");check(!fails(bad).empty(),"a grant at no stage");
        bad=with;bad["stage_info"][0]["inherit"]["commands"].push_back(json::parse(R"(["3E13",5,0])"));check(!fails(bad).empty(),"a grant setting a variable");
        bad=with;bad["stage_info"].push_back(bad["stage_info"][0]);check(!fails(bad).empty(),"two entries of one id");
    }
    // Two packages: their lines are numbered on from each other.
    story::Library two;
    story::add_image(two,"p1",image);story::add_image(two,"p2",image);
    check(two.events[0].words[1]==story::text_first && two.events[1].words[1]==story::text_first+1,"lines numbered across packages");

    // The example package, compiled by tools/mods/story.py.
    std::ifstream example(argv[2]);
    story::Library master;
    story::add_image(master,"srw64.master-asia",json::parse(example));
    check(master.events.size()==8 && master.events[0].trigger==story::Trigger::persuade && master.events[0].a==4 && master.events[0].b==18,"the example's persuasion");
    check(master.events[0].vars==std::map<unsigned,unsigned>{{131,0}},"after the talk");
    check(master.events[0].kills==std::map<unsigned,unsigned>{{4,50}},"Domon at 50 kills");
    check(master.events[1].trigger==story::Trigger::persuade && master.events[1].kills_below==std::map<unsigned,unsigned>{{4,50}} &&
          !master.events[1].repeat && master.events[0].not_fired==std::vector<std::string>{"refused"},"refused under 50 kills, once: then no persuasion");
    check(master.events[2].trigger==story::Trigger::after_end && master.events[2].fired==std::vector<std::string>{"persuade"},"joins after the end event");
    const auto& joins=master.events[2].words;
    check(std::vector<uint16_t>(joins.end()-6,joins.end())==std::vector<uint16_t>{0x3D5A,18,0,27,500,0xFFFF},"3D5A 18,0,27,500 at its end");

    // --- A package's own deployment groups ---------------------------------------------
    {
        auto with=image;
        with["deployments"]=json::parse(R"([{"id":"wave","scene":129,"records":[{"x":10,"y":5,"pilot":289,"unit":21,"side":1,"level":2,"upgrade":1},
            {"x":11,"y":5,"pilot":290,"unit":22,"side":4,"level":0,"upgrade":0}]}])");
        with["events"].push_back(event("w","turn",{1,1},{0x3D45,0xFFFD,0x3D46,0xFFFD,1,0xFFFF}));
        with["events"][1]["groups"]=json::parse(R"([[1,"wave",false],[3,"wave",true]])");
        story::Library library;
        story::add_image(library,"p",with);
        check(library.deployments.size()==1 && library.deployments[0].records.size()==2 && library.events[1].groups.size()==2,"a package's group and its references");
        check(library.deployments[0].records[0]==std::array<uint16_t,14>{0,10,5,289,2,21,1,0,0,0xFFFF,1,0,0,0},"the record as the ROM's");
        auto bad=with;bad["events"][1]["groups"]=json::parse(R"([[1,"nope",false]])");check(!fails(bad).empty(),"an unknown group");
        bad=with;bad["deployments"][0]["records"][0]["side"]=3;check(!fails(bad).empty(),"side 3");
        bad=with;bad["deployments"][0]["scene"]=130;check(!fails(bad).empty(),"a group of another scene");
        // The scene's block: groups 0, 1 and 3, then 999.
        auto& st=story::state();
        const auto saved=st.library;st.library=library;st.scene=129;st.appended.clear();st.appended_at=0;
        const uint32_t block=0x80199400;
        write32(ram.data(),e,block);
        const auto rom=[&]{for(unsigned i=0;i<0x2000;i+=2)write16(ram.data(),block+i,0x5555);
            const uint16_t groups[]={0,1,3};
            for(unsigned k=0;k<3;++k)write16(ram.data(),block+k*28,groups[k]);
            write16(ram.data(),block+3*28,999);};
        rom();
        story::assign_groups(ram.data());
        check(st.groups==std::map<std::string,unsigned>{{"p/wave",2u}},"the first group the block leaves free");
        check(read(ram.data(),block+3*28,2)==2 && read(ram.data(),block+3*28+6,2)==289 && read(ram.data(),block+4*28,2)==2 &&
              read(ram.data(),block+4*28+20,2)==4 && read(ram.data(),block+5*28,2)==999,"appended before a new 999");
        story::assign_groups(ram.data());
        check(st.groups==std::map<std::string,unsigned>{{"p/wave",2u}} && read(ram.data(),block+5*28,2)==999 && read(ram.data(),block+6*28,2)==0x5555,"registered again: known as ours, not twice");
        rom();write16(ram.data(),block+2*28,2);   // copied again, its third record now group 2: 0, 1, 2 used
        story::assign_groups(ram.data());
        check(st.groups==std::map<std::string,unsigned>{{"p/wave",3u}},"a fresh copy numbered afresh");
        story::place(ram.data(),library.events[1]);
        check(read(ram.data(),story::region+(story::header_words+1)*2,2)==3 && read(ram.data(),story::region+(story::header_words+3)*2,2)==503,"3D45 3, 3D46 503");
        write16(ram.data(),block,0x5555);for(unsigned i=0;i<0x2000;i+=28)write16(ram.data(),block+i,7);
        st.appended.clear();story::assign_groups(ram.data());
        check(st.groups.empty(),"no 999: nothing appended");
        story::View none;
        check(!story::eligible(library.events[1],{},none),"an event whose group found no room does not fire");
        st.library=saved;st.groups.clear();st.appended.clear();st.appended_at=0;
    }

    // --- Triggers --------------------------------------------------------------------
    auto& s=story::state();
    s.library=master;
    s.library.events.push_back(story::Event{.package="x",.id="turn2",.scene=129,.trigger=story::Trigger::turn,.a=1,.b=1,.words={0x3D4E,0xFFFF}});
    s.library.events.push_back(story::Event{.package="x",.id="gone",.scene=129,.trigger=story::Trigger::defeated,.a=248,.words={0x3D4E,0xFFFF}});
    s.scene=129;s.fired.clear();
    {std::lock_guard lock(s.texts_mutex);s.texts=std::make_shared<std::vector<story::Text>>(s.library.texts);}
    std::vector<uint32_t> started;
    auto start=[&](uint32_t owner){started.push_back(owner);};
    story::View v;v.side=1;v.turn=0;v.variable=[&](unsigned n){return story::variable(ram.data(),n);};
    v.hp=[](unsigned who){return who==248?0.0:50.0;};
    write16(ram.data(),e+story::f_status,0x80);
    write16(ram.data(),e+story::f_count,7);
    // Phase 0 scans [0, 2): turn 1 (0-based) is not reached at turn 0.
    check(!story::scan(ram.data(),0,2,v,start),"turn 2 not yet");
    v.turn=1;
    check(story::scan(ram.data(),0,2,v,start) && started==std::vector<uint32_t>{story::owner},"turn 2, player phase");
    check(read(ram.data(),e+story::f_status,2)==0x2000 && read(ram.data(),e+story::f_count,2)==8,"started like the original: status, counted in");
    check(read(ram.data(),story::region,2)==0 && read(ram.data(),story::region+10,2)==0x3D4E && read(ram.data(),story::region+12,2)==0xFFFF,"a type-0 header, then its words");
    check(!story::scan(ram.data(),0,2,v,start),"nothing while an event runs");
    write16(ram.data(),e+story::f_status,0x80);story::stepped(0x80);
    check(!s.running && !story::scan(ram.data(),0,2,v,start),"fired once");
    // Defeat is scanned in [2, 4) with the original's status.
    check(story::scan(ram.data(),2,4,v,start) && read(ram.data(),e+story::f_status,2)==0x2002,"defeat, status 2002");
    write16(ram.data(),e+story::f_status,0x80);

    // --- Persuasion --------------------------------------------------------------------
    // Domon at (5,5), Master Asia at (6,5); Master's unit names pilot 18.
    const uint32_t domon=0x80300000,master_unit=0x80300100,record=0x80300200,crew=0x80300300;
    write8(ram.data(),domon+4,5);write8(ram.data(),domon+5,5);
    write8(ram.data(),master_unit+4,6);write8(ram.data(),master_unit+5,5);
    write32(ram.data(),master_unit+0xC,record);write32(ram.data(),record+0x38,crew);write16(ram.data(),crew+2,18);
    auto unit=[&](unsigned who)->uint32_t{return who==4?domon:who==18?master_unit:0;};
    write16(ram.data(),e+story::f_persuader,4);
    // 131 is 3 until they talk: no persuasion.
    write16(ram.data(),0x8015E818+2*(131/8),uint16_t(3<<(2*(131%8))));
    check(story::persuasion(ram.data(),v,unit)==-1,"not before the talk");
    write16(ram.data(),0x8015E818+2*(131/8),0);
    // Domon needs 50 kills: his record (pilot 4) is the second of ours.
    v.kills=[&](unsigned pilot){return story::kills(ram.data(),pilot);};
    check(story::persuasion(ram.data(),v,unit)==-1,"not without Domon on the roster");
    const uint32_t record_domon=story::pilot_records+story::pilot_record_size;
    write8(ram.data(),record_domon,1);write16(ram.data(),record_domon+2,4);write16(ram.data(),record_domon+story::pilot_kills,49);
    check(story::kills(ram.data(),4)==49u && !story::kills(ram.data(),5),"kills read from our records");
    // Under 50 the persuasion is offered all the same: the refusal (event 1), once.
    check(story::persuasion(ram.data(),v,unit)==18 && read(ram.data(),e+story::f_persuade,2)==story::persuade_mark+1,"at 49 kills: the refusal");
    write16(ram.data(),e+story::f_phase,6);
    check(story::persuaded(ram.data(),start) && read(ram.data(),story::region+12,2)==story::text_first+7,"the refusal's first line");
    check(s.fired.count("srw64.master-asia/refused")==1 && !s.fired.count("srw64.master-asia/persuade"),"refused, not persuaded");
    write16(ram.data(),e+story::f_status,0x80);write16(ram.data(),e+story::f_phase,0);
    write16(ram.data(),e+story::f_persuade,0xFFFF);
    check(story::persuasion(ram.data(),v,unit)==-1,"refused once: no second try");
    write16(ram.data(),record_domon+story::pilot_kills,50);
    check(story::persuasion(ram.data(),v,unit)==-1,"nor at 50 kills, once refused");
    // As in a stage where he was never refused.
    s.fired.erase("srw64.master-asia/refused");
    check(story::persuasion(ram.data(),v,unit)==18,"after the talk, at 50 kills: the target's pilot");
    check(read(ram.data(),e+story::f_persuade,2)==story::persuade_mark,"+0x992 holds our mark");
    write8(ram.data(),master_unit+4,8);
    check(story::persuasion(ram.data(),v,unit)==-1,"not when apart");
    write8(ram.data(),master_unit+4,6);
    write16(ram.data(),e+story::f_persuader,7);
    check(story::persuasion(ram.data(),v,unit)==-1,"not by someone else");
    write16(ram.data(),e+story::f_persuader,4);
    story::persuasion(ram.data(),v,unit);
    started.clear();
    write16(ram.data(),e+story::f_phase,0);
    check(!story::persuaded(ram.data(),start),"only in the persuasion phase");
    write16(ram.data(),e+story::f_phase,6);
    check(story::persuaded(ram.data(),start) && started.size()==1,"started in phase 6");
    check(read(ram.data(),e+story::f_persuade,2)==0xFFFF && read(ram.data(),e+story::f_status,2)==0x2000,"mark cleared, status 2000");
    check(read(ram.data(),story::region,2)==9 && read(ram.data(),story::region+10,2)==0x3D3E && read(ram.data(),story::region+12,2)==story::text_first,"a type-9 header, then the first line");
    check(s.fired.count("srw64.master-asia/persuade")==1,"fired");
    check(!s.flags.count("srw64.master-asia/persuaded"),"its flag only once it has run");
    write16(ram.data(),e+story::f_status,0x80);
    story::stepped(0x80);
    check(s.flags==std::set<std::string>{"srw64.master-asia/persuaded"},"run to its end: the long-term flag");
    check(story::persuasion(ram.data(),v,unit)==-1,"once only, and no refusal at 50");

    // --- After the end event ---------------------------------------------------------------
    write16(ram.data(),e+story::f_mode,0xC3);
    write32(ram.data(),e+0x10,0x8019B500);
    check(!story::special(ram.data(),v),"not while the original's end event waits");
    write32(ram.data(),e+0x10,0);
    check(story::special(ram.data(),v) && read(ram.data(),e+0x10,4)==story::region,"ours into the end event's pointer");
    check(s.fired.count("srw64.master-asia/joins")==1,"joins fired");
    // Its 3D5A joins at Domon's level: base level 1 here and Domon 1, so +0.
    const uint32_t level_operand=story::region+uint32_t((story::header_words+12)*2);
    check(read(ram.data(),level_operand-4,2)==0x3D5A && read(ram.data(),level_operand,2)==0,"join at Domon's level: +0 at base 1");
    write8(ram.data(),story::base_level,30);write8(ram.data(),record_domon+story::pilot_level,38);
    story::place(ram.data(),s.library.events[2]);
    check(read(ram.data(),level_operand,2)==8,"base 30, Domon 38: +8");
    write8(ram.data(),record_domon+story::pilot_level,28);
    story::place(ram.data(),s.library.events[2]);
    check(read(ram.data(),level_operand,2)==0xFFFE,"Domon 28: -2");
    write32(ram.data(),e+0x10,0);
    check(!story::special(ram.data(),v),"then the original goes on");
    // Without the persuasion it does not join.
    s.fired.clear();
    check(!story::special(ram.data(),v),"no join without the persuasion");

    // --- Suspend records ---------------------------------------------------------------------
    s.keep=[&]{return dir;};
    s.fired={"srw64.master-asia/persuade"};
    for(uint32_t i=0;i<story::suspend_size;i+=4)write32(ram.data(),story::suspend_buffer+i,i*2654435761u);
    story::serialized(ram.data(),true);
    s.fired.clear();s.snapshots.clear();
    story::restored(ram.data(),true);
    check(s.fired==std::set<std::string>{"srw64.master-asia/persuade"},"back from the sidecar file");
    s.fired={"srw64.master-asia/persuade","srw64.master-asia/joins"};
    write32(ram.data(),story::suspend_buffer+0x100,1);
    story::serialized(ram.data(),false);
    s.fired.clear();
    story::restored(ram.data(),false);
    check(s.fired.size()==2,"back from memory (the buffer's snapshot)");
    write32(ram.data(),story::suspend_buffer+0x100,2);
    story::restored(ram.data(),true);
    check(s.fired.empty(),"a record never seen: nothing has fired");

    // --- Across stages ---------------------------------------------------------------------
    // Scene 81's word from Master Asia: on our roster and persuaded.
    s.scene=81;s.fired.clear();s.flags.clear();
    v.has_pilot=[&](unsigned pilot){return story::pilot_record(ram.data(),pilot).has_value();};
    v.has_unit=[&](unsigned unit){return story::has_unit(ram.data(),unit);};
    write16(ram.data(),e+story::f_mode,0xC1);write32(ram.data(),e+0x8,0);write16(ram.data(),e+story::f_status,0x80);
    check(!story::special(ram.data(),v),"scene 81: not without him nor the flag");
    const uint32_t record_master=story::pilot_records+2*story::pilot_record_size;
    write8(ram.data(),record_master,1);write16(ram.data(),record_master+2,18);
    check(!story::special(ram.data(),v),"scene 81: on the roster, but no flag");
    s.flags={"srw64.master-asia/persuaded"};
    check(story::special(ram.data(),v) && s.fired.count("srw64.master-asia/comes-along"),"scene 81: on the roster and persuaded");
    check(!story::has_unit(ram.data(),27),"no Master Gundam yet");
    write8(ram.data(),story::unit_instances+5*story::unit_instance_size,1);write16(ram.data(),story::unit_instances+5*story::unit_instance_size+2,27);
    check(story::has_unit(ram.data(),27),"a unit on the roster");
    // A slot record keeps the flags (not what fired in a stage); a new game drops them.
    for(uint32_t i=0;i<story::slot_size;i+=4)write32(ram.data(),story::slot_buffer+i,i*40503u+7);
    story::serialized(ram.data(),true,story::Record::slot);
    write16(ram.data(),story::slot_buffer,0x8123);   // the checksum word, written later, is not hashed
    s.flags.clear();s.snapshots.clear();
    story::restored(ram.data(),true,story::Record::slot);
    check(s.flags==std::set<std::string>{"srw64.master-asia/persuaded"} && s.fired.empty(),"a slot record: flags back from the sidecar");
    story::new_game();
    check(s.flags.empty() && s.fired.empty(),"a new game: no flags");
    check(story::snapshot().value("scene",0)==81,"the status copy");

    // --- Lines ------------------------------------------------------------------------------
    uint32_t offset{},size{};
    // The first line's Japanese has 38 code points (with its quotes): three rows, 14 + 14 + 10.
    check(story::descriptor(0,story::text_first,offset,size) && size==8+38*2+2*2+2,"a line's descriptor: "+std::to_string(size));
    check(!story::descriptor(0,story::text_first+99,offset,size) && !story::descriptor(1,story::text_first,offset,size),"only ours, table 0");
    check(story::read_text(story::text_table+offset,ram.data(),0x80310000,size),"served");
    std::string header;for(uint32_t i=0;i<8;++i)header+=char(read(ram.data(),0x80310000+i,1));
    check(header=="00400004" && read(ram.data(),0x80310008,2)==0 && read(ram.data(),0x80310008+14*2,2)==0xFFFE &&
          read(ram.data(),0x80310000+size-2,2)==0xFFFF,"header 004 0 0004, a page of blanks: "+header);
    check(story::read_text(story::text_table+offset,ram.data(),0x80310000,4),"the header alone");
    check(story::line(story::text_first,"zh-Hans").value_or("").find("师父")!=std::string::npos,"the Chinese line");
    check(story::line(story::text_first,"ko")==story::line(story::text_first,"ja"),"Japanese otherwise");
    // A package's battle quote (mod_quotes.hpp) is served the same way, with a battle quote's
    // header: it closes on its own.
    srw64::mod_quotes::publish({{430,{{"ja","「行くぞ!」"},{"zh-Hans","「上了！」"}},"pkg","hero.ogg"}});
    const uint16_t quote=srw64::mod_quotes::text_first;
    check(story::descriptor(0,quote,offset,size) && size==8+6*2+2,"a battle quote's descriptor");
    check(story::read_text(story::text_table+offset,ram.data(),0x80310000,size),"a battle quote served");
    header.clear();for(uint32_t i=0;i<8;++i)header+=char(read(ram.data(),0x80310000+i,1));
    check(header=="43020032","header 430 20 032: "+header);
    check(story::line(quote,"zh-Hans")==std::optional<std::string>("「上了！」"),"its Chinese line");
    // A routine that shows the lines after it (194: +1 to +5) gets a blank line of the same
    // speaker past the last one, not the ROM's empty entry.
    check(story::descriptor(0,quote+5,offset,size) && size==8+2+2 && story::line(quote+5,"ja")==std::optional<std::string>(""),"a blank line after the last");
    check(story::read_text(story::text_table+offset,ram.data(),0x80310000,size),"the blank line served");
    header.clear();for(uint32_t i=0;i<8;++i)header+=char(read(ram.data(),0x80310000+i,1));
    check(header=="43020032","the blank line's speaker: "+header);
    srw64::mod_quotes::publish({});
    check(!story::descriptor(0,quote,offset,size),"none without package lines");

    // --- A custom campaign's stages (tools/mods/stage.py) ---------------------------------
    {
        // Its story image beside its campaign image: an event of scene 2, a line of the
        // opening event at offset 12 of the scene's event block.
        json campaign_story={{"schema",story::image_schema},{"campaign","c.one"},
            {"texts",json::array({{{"speaker",4},{"lines",{{"ja","キャンペーン"}}}}})},
            {"events",json::array({{{"id","two.turn-1"},{"scene",2},{"trigger","turn"},{"params",{0,1}},{"words",{0x3D4A,0xFFFF}}}})},
            {"patches",json::array({{{"scene",2},{"at",12},{"text",0}}})}};
        std::ofstream(dir/"story.json")<<campaign_story.dump();
        check(!fails(campaign_story).empty(),"patches outside a campaign's image");
        auto bad=campaign_story;bad["patches"][0]["text"]=3;
        {story::Library l;bool threw=false;try{story::add_image(l,"c.one",bad,"c.one");}catch(const std::exception&){threw=true;}check(threw,"a patch naming no line");}
        auto library=story::collect({},"c.one",dir/"story.json");
        check(library.problems.empty() && library.events.size()==1 && library.events[0].campaign=="c.one" && library.patches.size()==1 &&
              library.patches[0].text==story::text_first,"the campaign's story image: "+(library.problems.empty()?std::string():library.problems[0]));
        check(story::collect({},"c.two",dir/"story.json").events.empty(),"another campaign's story image is refused");
        check(story::collect({},"",dir/"story.json").events.empty(),"no campaign, no campaign events");
        const auto archive=dir/"c.one.srw64mod";
        const auto serialized=campaign_story.dump();
        mz_zip_archive zip{};
        const bool opened=mz_zip_writer_init_file(&zip,archive.string().c_str(),0);
        check(opened,"open the campaign archive fixture");
        if(opened) {
            check(mz_zip_writer_add_mem(&zip,"mod.json","{}",2,MZ_DEFAULT_COMPRESSION) &&
                  mz_zip_writer_add_mem(&zip,"campaign/story.json",serialized.data(),serialized.size(),MZ_DEFAULT_COMPRESSION) &&
                  mz_zip_writer_finalize_archive(&zip),"write the campaign archive fixture");
            mz_zip_writer_end(&zip);
        }
        const auto packed=story::collect({},"c.one",archive/"campaign"/"story.json");
        check(packed.problems.empty() && packed.events.size()==1 && packed.events[0].id=="two.turn-1" &&
              packed.patches.size()==1 && packed.patches[0].text==story::text_first,"the archived campaign reads its event and opening line");
        // A main story event of the scene the stage borrows, beside the campaign's.
        library.events.push_back(story::Event{.package="x",.id="main",.scene=2,.trigger=story::Trigger::turn,.a=0,.b=1,.words={0x3D4E,0xFFFF}});
        s.library=library;s.loaded=true;s.revision=mods::revision();s.game=mods::game_version();
        story::View none;none.side=1;
        std::vector<std::string> ran;
        const auto first=[&] {
            write16(ram.data(),e+story::f_status,0x80);write16(ram.data(),e+story::f_turn,0);write16(ram.data(),e+story::f_side,1);
            const auto before=s.fired;
            story::scan(ram.data(),0,1,none,[](uint32_t){});
            for(const auto& key:s.fired)if(!before.count(key) && key[0]!='#')return key;
            return std::string();
        };
        write8(ram.data(),story::scene_byte,2);
        write16(ram.data(),story::event_block+12,story::text_placeholder);
        story::set_campaign("c.one",dir/"story.json");s.loaded_story=dir/"story.json";
        story::registered(ram.data(),0);
        check(read(ram.data(),story::event_block+12,2)==story::text_first,"the opening's line gets its number");
        check(first()=="c.one/two.turn-1","in the campaign its own event runs");
        check(first().empty(),"and the main story's of the borrowed scene does not");
        check(story::infos()->empty(),"nor its stage information");
        story::set_campaign("",{});s.loaded_story.clear();
        write16(ram.data(),story::event_block+12,story::text_placeholder);
        story::registered(ram.data(),0);
        check(read(ram.data(),story::event_block+12,2)==story::text_placeholder,"the main story's block is left alone");
        check(first()=="x/main","in the main story the campaign's event stays out");
    }

    // --- Conditions (level-editor.md §5.3) ------------------------------------------------
    {
        json conditions={{"schema",story::image_schema},{"texts",json::array()},{"events",json::array({
            event("reach","reach",{28,-1,7,2,9,4}),
            event("down2","downed",{0,-1,-1,2}),
            event("wave","group_gone",{3}),
            event("by","defeated",{298,28}),
            event("few","remaining",{2,1,4}),
            {{"id","tested"},{"scene",129},{"trigger","turn"},{"params",{0,1}},{"words",{0x3D4E,0xFFFF}},
             {"test",{{"all",{{{"turn",{0,4}}},{{"status",{18,"on_map"}}},{{"any",{{{"route",{0,1}}},{{"not",{{"fired","reach"}}}}}}},
                              {{"count",{1,0,3}}},{{"funds",{5000,99999999}}},{{"choice",2}}}}}}},
            {{"id","step"},{"scene",129},{"trigger","turn"},{"params",{0,1}},{"words",{0x3E14,0xFFFC,0x3E08,1,0x3D4E,0x3E1D,0xFFFF}},
             {"checks",{{1,{{"in_area",{-1,1,1,1,3,3}}}}}}}})}};
        check(fails(conditions).empty(),"the condition image: "+fails(conditions));
        auto bad=conditions;bad["events"][0]["params"]={28,-1,7};check(!fails(bad).empty(),"reach without its area");
        bad=conditions;bad["events"][5]["test"]={{"weather",1}};check(!fails(bad).empty(),"an unknown condition");
        bad=conditions;bad["events"][5]["test"]={{"turn",{1}}};check(!fails(bad).empty(),"a condition with too few numbers");
        bad=conditions;bad["events"][6]["checks"]={{2,{{"choice",1}}}};check(!fails(bad).empty(),"a step's condition not at 3E14's operand");
        story::Library l;story::add_image(l,"p",conditions);
        s.library=l;s.campaign.clear();s.scene=129;s.fired.clear();s.flags.clear();s.running.reset();s.seen={};s.primed=false;
        write16(ram.data(),e+story::f_status,0x80);
        auto map=[&](std::vector<story::MapUnit> units){story::View w;w.side=1;w.turn=1;w.units=std::move(units);
            w.group_of=[](const story::MapUnit& u){return u.pilot==298?3:u.side==0?1:0;};w.hp=[](unsigned){return 50.0;};
            w.funds=8000;w.route=2;w.choice=2;return w;};
        auto fires=[&](const story::View& w,unsigned from,unsigned to) {
            write16(ram.data(),e+story::f_status,0x80);s.running.reset();
            const auto before=s.fired;
            story::scan(ram.data(),from,to,w,[](uint32_t){});
            for(const auto& key:s.fired)if(!before.count(key) && key[0]!='#')return key;
            return std::string();
        };
        const std::vector<story::MapUnit> start_units={{0,0,28,36,10,10},{0,1,29,37,11,10},{1,0,298,239,1,1},{1,1,298,239,2,1},{1,2,18,5,20,20}};
        auto w=map(start_units);
        story::observe(w,false);   // the first look only records
        check(story::downs(-1,-1,-1)==0 && story::group_on_map(3),"the first look records, groups from the deployment records");
        check(fires(w,8,10).empty(),"nobody in the area yet");
        w=map({{0,0,28,36,8,3},start_units[1],start_units[2],start_units[3],start_units[4]});
        story::observe(w,false);
        check(fires(w,8,10)=="p/reach","28 walks into [7,2]-[9,4]");
        // One of the two 298s leaves in an event: not a defeat. The other falls in battle.
        w=map({start_units[0],start_units[1],start_units[3],start_units[4]});
        story::observe(w,true);
        check(story::downs(-1,-1,-1)==0,"leaving in an event is not a defeat");
        check(fires(w,2,4).empty(),"group 3 still has one on the map");
        w=map({start_units[0],start_units[1],start_units[4]});w.battle_a=28;w.battle_b=298;
        w.hp=[](unsigned who){return who==298?0.0:50.0;};
        story::observe(w,false);
        check(story::downs(1,3,298)==1 && !story::group_on_map(3),"the other falls: a defeat of group 3");
        const auto a=fires(w,2,4),b=fires(w,2,4);
        check(a=="p/wave" && b=="p/by","group 3 gone, then 298 defeated by 28: "+a+" "+b);
        w.battle_a=29;s.fired.erase("p/by");
        check(fires(w,2,4).empty(),"defeated by someone else: not \"by 28\"");
        check(fires(w,6,8).empty(),"two of ours on the map: more than one");
        w=map({start_units[0],start_units[4]});story::observe(w,false);
        check(fires(w,6,8)=="p/few" && fires(w,2,4).empty(),"one of ours left: remaining; one defeat is not two");
        w=map({start_units[4]});story::observe(w,false);
        check(fires(w,2,4)=="p/down2","two of ours defeated");
        // The tested event: turn 1-5, 18 on the map, route 0/1 or reach not fired... (it has).
        s.fired.insert("p/step");   // out of the way: it has the same trigger
        check(fires(w,0,2).empty(),"route 2 and reach fired: the any fails");
        s.fired.erase("p/reach");
        check(fires(w,0,2)=="p/tested","with reach not fired the any holds");
        // A step's condition: written where 3E14 reads it, when the event starts and each step.
        s.fired.clear();s.seen={};s.primed=false;
        auto inside=map({{1,0,298,239,2,2}}),outside=map({{1,0,298,239,9,9}});
        s.fired.insert("p/tested");
        check(fires(inside,0,2)=="p/step","the step's event starts");
        check(read(ram.data(),story::region+(story::header_words+1)*2,2)==1,"an enemy in [1,1]-[3,3]: 1");
        story::stepped(0x2000,ram.data(),&outside);
        check(read(ram.data(),story::region+(story::header_words+1)*2,2)==0,"after a step, nobody there: 0");
    }

    // --- A choice's options, a stage's own objective ----------------------------------
    {
        story::glyph_of()=[](const std::string& c)->std::optional<uint16_t> {
            if(c=="は")return 0x0120;if(c=="い")return 0x0121;if(c=="・")return 0x0050;return std::nullopt;
        };
        story::Text choice{0,{{"ja","はい<BR>いいえ"}},story::TextKind::choice};
        const auto bytes=story::text_record(choice);
        const std::vector<uint8_t> want={'0','2','5','0','0','0','0','4', 0,0, 0x01,0x20,0x01,0x21, 0xFF,0xFE, 0,0, 0x01,0x21,0x01,0x21,0x00,0x50, 0xFF,0xFF};
        check(bytes==want,"a choice: a row per option, each after a blank, ROM glyphs, ・ for one the font lacks");
        story::Text goal{0,{{"ja","はい"}},story::TextKind::objective};
        const auto g=story::text_record(goal);
        check(std::string(g.begin(),g.begin()+8)=="00040000" && g.size()==8+4+2,"an objective: one row, the original's header");
        json image={{"schema",story::image_schema},{"campaign","c.goal"},{"texts",json::array({
            {{"speaker",0},{"kind","objective"},{"lines",{{"ja","勝つ"}}}},{{"speaker",0},{"kind","objective"},{"lines",{{"ja","負け"}}}}})},
            {"events",json::array()},{"objectives",json::array({{{"scene",7},{"win",0},{"lose",1}}})}};
        check(!fails(image).empty(),"objectives outside a campaign's image");
        story::Library l;story::add_image(l,"c.goal",image,"c.goal");
        s.library=l;s.campaign="c.goal";
        write8(ram.data(),story::scene_byte,7);
        check(story::objective_text(ram.data(),story::objective_win)==story::text_first && story::objective_text(ram.data(),story::objective_lose)==story::text_first+1,
              "on the stage, 5694 / 5720 are its own texts");
        check(story::objective_text(ram.data(),5584)==5584,"another objective text stays");
        write8(ram.data(),story::scene_byte,8);
        check(story::objective_text(ram.data(),story::objective_win)==story::objective_win,"another scene keeps 5694");
        s.campaign.clear();
        auto bad=image;bad["texts"][0]["kind"]="poem";check(!fails(bad).empty(),"an unknown kind of text");
    }

    // --- Battle lines ---------------------------------------------------------------------
    {
        json image={{"schema",story::image_schema},{"campaign","c.q"},{"texts",json::array({
            {{"speaker",28},{"kind","battle"},{"lines",{{"ja","「行くわよ！」"}}}},{{"speaker",287},{"kind","battle"},{"lines",{{"ja","「馬鹿な」"}}}}})},
            {"events",json::array()},{"quotes",json::array({
                {{"id","duel"},{"scene",9},{"who",28},{"against",287},{"moments",{0,5}},{"text",0},{"once",true}},
                {{"id","down"},{"scene",9},{"who",287},{"moments",{1}},{"text",1},{"test",{{"turn",{0,2}}}}}})}};
        story::Library l;story::add_image(l,"c.q",image,"c.q");
        check(l.quotes.size()==2 && l.quotes[0].moments==0x21 && l.quotes[0].text==story::text_first,"battle lines loaded");
        const auto record=story::text_record(l.texts[0]);
        check(std::string(record.begin(),record.begin()+8)=="02820032","a battle line's record: the speaker and the battle quotes' header");
        s.library=l;s.campaign="c.q";s.scene=9;s.fired.clear();
        write16(ram.data(),story::voice_table+28*2,40);write16(ram.data(),story::voice_table+287*2,77);
        story::View w;w.turn=1;w.battle_a=28;w.battle_b=287;
        check(story::battle_quote(ram.data(),40,0,w)==story::text_first,"28 attacking 287: her line");
        check(!story::battle_quote(ram.data(),40,5,w),"once: not again when she evades");
        check(!story::battle_quote(ram.data(),40,2,w),"not at a moment it does not name");
        check(story::battle_quote(ram.data(),77,1,w)==story::text_first+1,"287 defeated by turn 3: his line");
        w.turn=4;check(!story::battle_quote(ram.data(),77,1,w),"its condition: not after turn 3");
        s.fired.clear();w.turn=1;w.battle_b=300;
        check(!story::battle_quote(ram.data(),40,0,w),"against someone else: no line");
        s.scene=10;w.battle_b=287;check(!story::battle_quote(ram.data(),40,0,w),"another scene: no line");
        s.campaign.clear();
    }

    // --- Some turns after an event; levels; the opponent -------------------------------
    {
        json image={{"schema",story::image_schema},{"texts",json::array()},{"events",json::array({
            event("start","turn",{0,1}),
            {{"id","later"},{"scene",129},{"trigger","turns_after"},{"params",{2,1}},{"after","start"},{"words",{0x3D4E,0xFFFF}}},
            {{"id","stronger"},{"scene",129},{"trigger","turn"},{"params",{0,2}},{"words",{0x3D4E,0xFFFF}},
             {"test",{{"all",{{{"level",{4,18}}},{{"opponent",{4,18}}},{{"we_struck",1}}}}}}}})}};
        check(fails(image).empty(),"turns_after and the new conditions load: "+fails(image));
        auto bad=image;bad["events"][1].erase("after");check(!fails(bad).empty(),"turns_after without its event");
        story::Library l;story::add_image(l,"p",image);
        s.library=l;s.campaign.clear();s.scene=129;s.fired.clear();s.running.reset();
        auto fires=[&](story::View w,unsigned from,unsigned to) {
            write16(ram.data(),e+story::f_status,0x80);s.running.reset();
            write16(ram.data(),e+story::f_turn,uint16_t(w.turn));
            const auto before=s.fired;
            story::scan(ram.data(),from,to,w,[](uint32_t){});
            for(const auto& key:s.fired)if(!before.count(key) && key[0]!='#')return key;
            return std::string();
        };
        story::View w;w.side=1;w.turn=1;
        check(fires(w,0,2)=="p/start" && story::started_turn("p/start")==1u,"the start event, on turn 2 (1 0-based)");
        check(fires(w,0,2).empty(),"not yet the later one");
        w.turn=2;check(fires(w,0,2).empty(),"one turn on: not yet");
        w.turn=3;w.side=2;check(fires(w,0,2).empty(),"two turns on, the enemy's phase: not yet");
        w.side=1;check(fires(w,0,2)=="p/later","two turns on, ours: now");
        story::View b;b.side=2;b.turn=0;b.battle_a=18;b.battle_b=4;b.we_struck=false;
        b.level=[](unsigned p)->std::optional<unsigned>{return p==4?std::optional<unsigned>(30):p==18?std::optional<unsigned>(28):std::nullopt;};
        check(fires(b,0,2).empty(),"ours defended: no");
        b.we_struck=true;check(fires(b,0,2)=="p/stronger","4 at level 30 over 18's 28, fighting 18, struck");
        s.fired.erase("p/stronger");b.level=[](unsigned)->std::optional<unsigned>{return 10;};b.battle_b=5;
        check(fires(b,0,2).empty(),"another opponent: no");
    }

    if(failures)std::fprintf(stderr,"%d failure(s)\n",failures);
    else std::printf("story MOD tests passed\n");
    return failures?1:0;
}
