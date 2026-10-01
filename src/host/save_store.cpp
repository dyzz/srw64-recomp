#include "save_store.hpp"
#include "app/save_library.hpp"
#include "guest_memory.hpp"
#include "json/json.hpp"
#include <array>
#include <cstdlib>
#include <fstream>
#include <mutex>

uint64_t srw64_current_vi();
namespace srw64::save_store {
namespace {
namespace sram=app::sram;
using json=nlohmann::json;
constexpr uint32_t cart_base=0x08000000;
std::mutex mutex;
std::optional<app::SaveLibrary> library;
// The cartridge as the game has written it, published whenever it is a usable card.
std::array<uint8_t,sram::size> card{};
std::array<unsigned,2> window{};   // 0: the cartridge's own slot
std::optional<unsigned> armed;
std::ofstream log;

void record(const char* kind,json extra=json::object()) {
    if(!log.is_open())return;
    json row={{"schema","srw64.save-store-event.v1"},{"kind",kind},{"vi",srw64_current_vi()}};
    row.update(extra);log<<row.dump()<<'\n';log.flush();
}
std::optional<unsigned> slot_index(uint32_t offset,uint32_t length) {
    for(unsigned n=0;n<2;++n)if(offset==sram::slot_offsets[n] && length==sram::slot_size)return n;
    return std::nullopt;
}
sram::Bytes from_guest(const uint8_t* ram,uint32_t buffer,uint32_t length) {
    sram::Bytes bytes(length);
    for(uint32_t i=0;i<length;++i)bytes[i]=uint8_t(guest::read(ram,buffer+i,1));
    return bytes;
}
void to_guest(uint8_t* ram,uint32_t buffer,std::span<const uint8_t> bytes) {
    for(size_t i=0;i<bytes.size();++i)guest::write8(ram,buffer+uint32_t(i),bytes[i]);
}
}

void configure(const std::filesystem::path& directory,const std::optional<std::filesystem::path>& initial) {
    const char* root=std::getenv("SRW64_SAVE_LIBRARY");
    if(!root || !*root)return;
    library.emplace(std::filesystem::path(root));
    if(initial) {
        std::ifstream file(*initial,std::ios::binary);
        file.read(reinterpret_cast<char*>(card.data()),std::streamsize(card.size()));
    }
    log.open(directory/"save-store-events.jsonl");
    record("open",{{"library",root},{"initial",initial.has_value()},{"extended",library->slots()}});
}
bool enabled(){return library.has_value();}
bool transfer(uint8_t* ram,unsigned direction,uint32_t cart,uint32_t buffer,uint32_t length) {
    std::lock_guard lock(mutex);
    if(!library || cart<cart_base || cart-cart_base+length>sram::size)return false;
    const uint32_t offset=cart-cart_base;
    const auto index=slot_index(offset,length);
    unsigned number=index?window[*index]:0;
    if(index==0u && !number && armed && direction==0){number=*armed;armed.reset();}
    if(number) {
        // An extended slot: the record file stands in for the cartridge's slot.
        try {
            if(direction==1) {
                library->write_slot(number,from_guest(ram,buffer,length));
                record("slot-write",{{"slot",number}});
            } else {
                sram::Bytes record_bytes(sram::slot_size,0);   // a missing slot reads as empty
                if(std::filesystem::exists(library->slot_path(number)))record_bytes=library->read_slot(number);
                to_guest(ram,buffer,record_bytes);
                record("slot-read",{{"slot",number},{"used",sram::used(record_bytes)}});
            }
        } catch(const std::exception& error) {
            // The game's read-back then finds the old record and reports the save as failed.
            record("slot-error",{{"slot",number},{"direction",direction},{"error",error.what()}});
        }
        return true;
    }
    if(direction!=1)return false;
    const auto bytes=from_guest(ram,buffer,length);
    std::copy(bytes.begin(),bytes.end(),card.begin()+offset);
    // The seen bitmaps go first in every save, the record right after: publish once, with
    // the record. A card mid-format (no header yet) is not published.
    if(offset==sram::seen_offset || !sram::problems(card).empty())return false;
    try{library->publish_cartridge(card);record("publish",{{"offset",offset},{"length",length}});}
    catch(const std::exception& error){record("publish-error",{{"error",error.what()}});}
    return false;
}
Window::Window(unsigned slot,unsigned number):index(slot) {
    std::lock_guard lock(mutex);window.at(index)=number;
}
Window::~Window() {
    std::lock_guard lock(mutex);window.at(index)=0;
}
void arm_load(unsigned number){std::lock_guard lock(mutex);armed=number;record("arm",{{"slot",number}});}
void disarm(){std::lock_guard lock(mutex);if(armed){armed.reset();record("disarm");}}
std::vector<unsigned> extended() {
    std::lock_guard lock(mutex);
    return library?library->slots():std::vector<unsigned>{};
}
std::optional<unsigned> next_free() {
    std::lock_guard lock(mutex);
    if(!library)return std::nullopt;
    try{return library->next_free_slot();}catch(const std::exception&){return std::nullopt;}
}
}
