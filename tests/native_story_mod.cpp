// Story MODs (src/host/story_mod.hpp, docs/design/mod-packages.md §7): the compiled
// image's rules, the triggers against a guest-memory picture, the persuasion mark, the
// events after the end event, what has fired across suspend records, and the lines
// served as text records.
#include "story_mod.hpp"
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
    }
    // Two packages: their lines are numbered on from each other.
    story::Library two;
    story::add_image(two,"p1",image);story::add_image(two,"p2",image);
    check(two.events[0].words[1]==story::text_first && two.events[1].words[1]==story::text_first+1,"lines numbered across packages");

    // The example package, compiled by tools/mods/story.py.
    std::ifstream example(argv[2]);
    story::Library master;
    story::add_image(master,"srw64.master-asia",json::parse(example));
    check(master.events.size()==2 && master.events[0].trigger==story::Trigger::persuade && master.events[0].a==4 && master.events[0].b==18,"the example's persuasion");
    check(master.events[0].vars==std::map<unsigned,unsigned>{{131,0}},"after the talk");
    check(master.events[1].trigger==story::Trigger::after_end && master.events[1].fired==std::vector<std::string>{"persuade"},"joins after the end event");
    const auto& joins=master.events[1].words;
    check(std::vector<uint16_t>(joins.end()-6,joins.end())==std::vector<uint16_t>{0x3D5A,18,0,27,500,0xFFFF},"3D5A 18,0,27,500 at its end");

    // --- Triggers --------------------------------------------------------------------
    auto& s=story::state();
    s.library=master;
    s.library.events.push_back(story::Event{"x","turn2",129,story::Trigger::turn,1,1,0,{},{},{},false,{0x3D4E,0xFFFF}});
    s.library.events.push_back(story::Event{"x","gone",129,story::Trigger::defeated,248,0,0,{},{},{},false,{0x3D4E,0xFFFF}});
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
    check(story::persuasion(ram.data(),v,unit)==18,"after the talk: the target's pilot");
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
    write16(ram.data(),e+story::f_status,0x80);
    check(story::persuasion(ram.data(),v,unit)==-1,"once only");

    // --- After the end event ---------------------------------------------------------------
    write16(ram.data(),e+story::f_mode,0xC3);
    write32(ram.data(),e+0x10,0x8019B500);
    check(!story::special(ram.data(),v),"not while the original's end event waits");
    write32(ram.data(),e+0x10,0);
    check(story::special(ram.data(),v) && read(ram.data(),e+0x10,4)==story::region,"ours into the end event's pointer");
    check(s.fired.count("srw64.master-asia/joins")==1,"joins fired");
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

    // --- Lines ------------------------------------------------------------------------------
    uint32_t offset{},size{};
    // The first line's Japanese has 35 code points: three rows of blanks, 14 + 14 + 7.
    check(story::descriptor(0,story::text_first,offset,size) && size==8+35*2+2*2+2,"a line's descriptor: "+std::to_string(size));
    check(!story::descriptor(0,story::text_first+99,offset,size) && !story::descriptor(1,story::text_first,offset,size),"only ours, table 0");
    check(story::read_text(story::text_table+offset,ram.data(),0x80310000,size),"served");
    std::string header;for(uint32_t i=0;i<8;++i)header+=char(read(ram.data(),0x80310000+i,1));
    check(header=="00400004" && read(ram.data(),0x80310008,2)==0 && read(ram.data(),0x80310008+14*2,2)==0xFFFE &&
          read(ram.data(),0x80310000+size-2,2)==0xFFFF,"header 004 0 0004, a page of blanks: "+header);
    check(story::read_text(story::text_table+offset,ram.data(),0x80310000,4),"the header alone");
    check(story::line(story::text_first,"zh-Hans").value_or("").find("师父")!=std::string::npos,"the Chinese line");
    check(story::line(story::text_first,"ko")==story::line(story::text_first,"ja"),"Japanese otherwise");

    if(failures)std::fprintf(stderr,"%d failure(s)\n",failures);
    else std::printf("story MOD tests passed\n");
    return failures?1:0;
}
