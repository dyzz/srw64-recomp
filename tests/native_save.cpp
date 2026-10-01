#include "app/runtime.hpp"
#include "app/save_library.hpp"
#include "app/sram.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace srw64::app;
namespace {
unsigned checks{};
void check(bool condition,const char* message) { ++checks;if(!condition)throw std::runtime_error(message); }
template<class F> void rejects(F f,const char* message) {
    ++checks;try{f();}catch(const std::exception&){return;}throw std::runtime_error(message);
}
struct Temporary {
    fs::path path;
    Temporary() {
        const auto base=fs::temp_directory_path();
        for(unsigned i=0;i<100;++i) {
            path=base/("srw64-save-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+'-'+std::to_string(i));
            if(fs::create_directory(path))return;
        }
        throw std::runtime_error("No temporary directory");
    }
    ~Temporary(){std::error_code ignored;fs::remove_all(path,ignored);}
};
void seal(std::span<uint8_t> record){record[0]=0x80;record[1]=sram::checksum(record);}
// A formatted card: slot `n` (0/1) holds `tag`, its header fields set; a suspend
// record when `suspended`; one seen bit per tag.
sram::Bytes card(uint8_t tag,unsigned n=0,bool suspended=false) {
    sram::Bytes bytes(sram::size,0);
    std::copy(sram::magic.begin(),sram::magic.end(),bytes.begin());
    auto record=sram::slot(std::span(bytes),n);
    std::fill_n(record.begin()+0x100,0x40,tag);
    record[0x4C]=0x01;record[0x4D]=0x23;record[0x4F]=5;record[0x51]=7;
    record[0x54]=0;record[0x55]=0x01;record[0x56]=0x86;record[0x57]=0xA0;
    seal(record);
    if(suspended) {
        auto suspend=std::span(bytes).subspan(sram::suspend_offset,sram::suspend_size);
        std::fill_n(suspend.begin()+2,0x3000,uint8_t(tag+1));
        seal(suspend);
    }
    bytes[sram::seen_offset+tag%0xA0]|=1;
    return bytes;
}
sram::Bytes read(const fs::path& path) {
    const auto text=read_text(path,sram::retroarch_size);
    return {text.begin(),text.end()};
}
void write(const fs::path& path,std::span<const uint8_t> bytes) {
    fs::create_directories(path.parent_path());
    atomic_write(path,{reinterpret_cast<const char*>(bytes.data()),bytes.size()});
}
bool same(std::span<const uint8_t> a,std::span<const uint8_t> b){return std::equal(a.begin(),a.end(),b.begin(),b.end());}

void formats() {
    const auto big=card(0x41,0,true);
    check(sram::problems(big).empty(),"test card is not usable");
    for(const auto format:{sram::Format::ares,sram::Format::project64,sram::Format::mupen64plus,sram::Format::retroarch}) {
        const auto file=sram::from_cartridge(big,format);
        const auto found=sram::detect(file);
        check(found.has_value(),"exported file not recognised");
        check(found->retroarch==(format==sram::Format::retroarch),"container misread");
        check(found->order==(format==sram::Format::ares?sram::Order::big:sram::Order::word),"byte order misread");
        check(same(sram::to_cartridge(file),big),"round trip changed the card");
    }
    // Project64 and mupen64plus keep each 32-bit word reversed.
    const auto swapped=sram::from_cartridge(big,sram::Format::project64);
    check(swapped[0]=='6' && swapped[1]=='W' && swapped[2]=='R' && swapped[3]=='S',"word order");
    sram::Bytes half(big);
    for(size_t i=0;i<half.size();i+=2)std::swap(half[i],half[i+1]);
    check(sram::detect(half)->order==sram::Order::half && same(sram::to_cartridge(half),big),"16-bit order");
    // A RetroArch container keeps its other saves.
    sram::Bytes container(sram::retroarch_size,0x5A);
    const auto merged=sram::from_cartridge(big,sram::Format::retroarch,container);
    check(merged[0]==0x5A && merged[sram::retroarch_sram-1]==0x5A && merged[sram::retroarch_sram+sram::size]==0x5A,"container contents lost");
    check(same(sram::to_cartridge(merged),big),"container card");
    rejects([&]{sram::from_cartridge(big,sram::Format::retroarch,std::span(container).first(100));},"short container accepted");
    rejects([&]{sram::to_cartridge(sram::Bytes(sram::size,0));},"blank file recognised");
    rejects([&]{sram::to_cartridge(sram::Bytes(sram::size+4,0));},"odd size recognised");
    check(sram::format_for(".SRA")==sram::Format::project64 && sram::format_for(".ram")==sram::Format::ares &&
          sram::format_for(".srm")==sram::Format::retroarch && !sram::format_for(".bin"),"extensions");
}
void records() {
    auto bytes=card(0x41,1,true);
    const auto slot=sram::slot(std::span<const uint8_t>(bytes),1);
    check(sram::used(slot) && sram::intact(slot) && !sram::used(sram::slot(std::span<const uint8_t>(bytes),0)),"slot flags");
    const auto shown=sram::summary(slot);
    check(shown.turns==0x123 && shown.episode==5 && shown.title==7 && shown.funds==100000,"summary fields");
    // The sum covers 0x1F00 bytes from +2: for a slot the last two lie past the
    // record and count as the intermission overlay's 00 00.
    sram::Bytes edge(sram::slot_size,0);edge[sram::slot_size-1]=9;
    check(sram::checksum(edge)==9,"slot sum misses its last byte");
    sram::Bytes longer(sram::suspend_size,0);longer[2+sram::slot_size]=9;
    check(sram::checksum(longer)==0,"suspend sum runs past 0x1F00 bytes");
    auto broken=bytes;broken[sram::slot_offsets[1]+0x200]^=1;
    check(sram::problems(broken)==std::vector<std::string>{"slot 2 checksum"},"slot damage not reported");
    auto suspended=bytes;suspended[sram::suspend_offset+0x200]^=1;
    check(sram::problems(suspended)==std::vector<std::string>{"suspend checksum"},"suspend damage not reported");
    auto unformatted=bytes;unformatted[0]='X';
    check(!sram::problems(unformatted).empty(),"missing magic accepted");
    auto into=card(0x01);const auto from=card(0x02);
    sram::merge_seen(into,from);
    check(into[sram::seen_offset+1]==1 && into[sram::seen_offset+2]==1,"seen bits not merged");
}
void library() {
    Temporary temp;
    SaveLibrary saves(temp.path/"saves");
    check(!saves.read_cartridge(),"empty library has a card");
    rejects([&]{saves.export_cartridge(temp.path/"x.ram",sram::Format::ares);},"exported without a card");
    const auto first=card(0x41);
    saves.publish_cartridge(first);
    check(same(*saves.read_cartridge(),first) && !fs::exists(temp.path/"saves/cartridge.sram.prev"),"first card");
    saves.publish_cartridge(first);
    check(!fs::exists(temp.path/"saves/cartridge.sram.prev"),"identical card replaced the previous one");
    const auto second=card(0x42);
    saves.publish_cartridge(second);
    check(same(read(temp.path/"saves/cartridge.sram.prev"),first),"previous card not kept");
    rejects([&]{saves.publish_cartridge(sram::Bytes(sram::size,0));},"unformatted card published");
    check(same(*saves.read_cartridge(),second),"refused card replaced the old one");

    // Import from Project64: the old card's slot becomes slot 3, its file goes to
    // imports/, and the seen bitmaps merge.
    const auto emulator=card(0x43,1);
    write(temp.path/"pj64.sra",sram::from_cartridge(emulator,sram::Format::project64));
    const auto imported=saves.import_cartridge(temp.path/"pj64.sra");
    check(imported.detected.order==sram::Order::word && !imported.detected.retroarch,"import detection");
    check(imported.kept==std::vector<unsigned>{3} && same(saves.read_slot(3),sram::slot(std::span<const uint8_t>(second),0)),"old slot not kept");
    check(fs::exists(imported.backup) && same(read(imported.backup),second),"old card not backed up");
    const auto now=*saves.read_cartridge();
    check(same(sram::slot(std::span<const uint8_t>(now),1),sram::slot(std::span<const uint8_t>(emulator),1)),"imported slot changed");
    check(now[sram::seen_offset+0x42]==1 && now[sram::seen_offset+0x43]==1,"seen bits lost on import");
    check(saves.import_cartridge(temp.path/"pj64.sra").kept.empty(),"re-import duplicated slots");
    check(saves.slots()==std::vector<unsigned>{3},"slot list");
    write(temp.path/"junk.sra",sram::Bytes(sram::size,0x11));
    rejects([&]{saves.import_cartridge(temp.path/"junk.sra");},"junk imported");
    auto damaged=sram::from_cartridge(card(0x44),sram::Format::ares);damaged[0x200]^=1;
    write(temp.path/"damaged.ram",damaged);
    rejects([&]{saves.import_cartridge(temp.path/"damaged.ram");},"damaged card imported");
    check(same(*saves.read_cartridge(),now),"failed import changed the card");

    // One slot from a RetroArch .srm; the same record again is not duplicated.
    write(temp.path/"ra.srm",sram::from_cartridge(card(0x45),sram::Format::retroarch));
    check(saves.import_slot(temp.path/"ra.srm",0)==4,"slot import number");
    check(saves.import_slot(temp.path/"ra.srm",0)==4,"slot import duplicated");
    rejects([&]{saves.import_slot(temp.path/"ra.srm",1);},"empty slot imported");
    rejects([&]{saves.write_slot(5,sram::Bytes(sram::slot_size,0));},"empty record stored");
    rejects([&]{saves.slot_path(2);},"slot 2 is the cartridge's");
    rejects([&]{saves.slot_path(100);},"slot past 99");

    // Export: an existing file is copied aside; a RetroArch .srm keeps its other saves.
    sram::Bytes container(sram::retroarch_size,0x5A);
    write(temp.path/"game.srm",container);
    saves.export_cartridge(temp.path/"game.srm",sram::Format::retroarch);
    const auto exported=read(temp.path/"game.srm");
    check(same(sram::to_cartridge(exported),now) && exported[0]==0x5A,"RetroArch export");
    check(same(read(temp.path/"game.srm.before-srw64"),container),"existing .srm not kept");
    saves.export_cartridge(temp.path/"game.srm",sram::Format::retroarch);
    check(same(read(temp.path/"game.srm.before-srw64"),container),"second export replaced the first backup");
    saves.export_cartridge(temp.path/"card.ram",sram::Format::ares);
    check(same(read(temp.path/"card.ram"),now),"ares export is the card itself");
    write(temp.path/"big.bin",sram::Bytes(sram::retroarch_size+1,0));
    rejects([&]{saves.export_cartridge(temp.path/"big.bin",sram::Format::ares);},"overwrote an unrelated large file");

    // Damage on disk is refused rather than played.
    auto broken=now;broken[sram::slot_offsets[1]+0x300]^=1;
    write(saves.cartridge(),broken);
    rejects([&]{saves.read_cartridge();},"damaged card read");

    // Autosaves: one sequence for both kinds, newest first, pruned per kind with notes.
    SaveLibrary autos(temp.path/"autos");
    using Kind=SaveLibrary::AutoKind;
    const auto slot_record=sram::slot(std::span<const uint8_t>(first),0);
    const auto suspend_record=sram::suspend(std::span<const uint8_t>(card(0x46,0,true)));
    for(unsigned n=0;n<4;++n) {
        const auto path=autos.next_autosave(n%2?Kind::turn:Kind::intermission);
        if(n%2)SaveLibrary::write_record(path,suspend_record,sram::suspend_size);
        else SaveLibrary::write_record(path,slot_record,sram::slot_size);
        auto note=path;note.replace_extension(".json");atomic_write(note,"{}");
    }
    auto saved=autos.autosaves();
    check(saved.size()==4 && saved[0].sequence==4 && saved[0].kind==Kind::turn && saved[3].sequence==1 &&
          saved[0].record.filename()=="turn-000004.sus" && saved[3].record.filename()=="inter-000001.rec","autosave list");
    autos.prune_autosaves(Kind::intermission,1);
    saved=autos.autosaves();
    check(saved.size()==3 && !fs::exists(temp.path/"autos/auto/inter-000001.rec") && !fs::exists(temp.path/"autos/auto/inter-000001.json")
          && fs::exists(temp.path/"autos/auto/inter-000003.rec"),"autosave pruning");
    check(autos.next_autosave(Kind::intermission).filename()=="inter-000005.rec","autosave numbering");
    rejects([&]{SaveLibrary::write_record(temp.path/"x.sus",slot_record,sram::suspend_size);},"short suspend record");
    // Deleting moves the record and its note to trash/.
    const auto moved=autos.trash(saved[0].record);
    check(!fs::exists(saved[0].record) && fs::exists(moved) && moved.parent_path().filename()=="trash" &&
          fs::exists(fs::path(moved).replace_extension(".json")),"trash");
    // One damaged slot: refused, then taken in with its sum redone; scan reports both slots.
    auto mixed=card(0x47,0);
    auto other=sram::slot(std::span<uint8_t>(mixed),1);std::copy(slot_record.begin(),slot_record.end(),other.begin());
    other[0x300]^=1;
    write(temp.path/"mixed.sra",sram::from_cartridge(mixed,sram::Format::project64));
    const auto scanned=SaveLibrary::scan(temp.path/"mixed.sra");
    check(scanned.detected.order==sram::Order::word && scanned.slots[0].intact && scanned.slots[1].used && !scanned.slots[1].intact && !scanned.suspend.used,"scan");
    rejects([&]{autos.import_slot(temp.path/"mixed.sra",1);},"damaged slot imported without repair");
    const auto repaired=autos.import_slot(temp.path/"mixed.sra",1,true);
    check(sram::intact(autos.read_slot(repaired)),"repaired slot");

    // Slots 3..99, then full.
    SaveLibrary many(temp.path/"many");
    const auto record=sram::slot(std::span<const uint8_t>(first),0);
    for(unsigned n=SaveLibrary::first_slot;n<=SaveLibrary::last_slot;++n)many.write_slot(n,record);
    rejects([&]{many.next_free_slot();},"a 98th extended slot");
}
}
int main() {
    try { formats();records();library();std::cout<<checks<<" checks passed\n";return 0; }
    catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}
}
