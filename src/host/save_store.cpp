#include "save_store.hpp"
#include "app/runtime.hpp"
#include "app/save_library.hpp"
#include "guest_memory.hpp"
#include <algorithm>
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
std::recursive_mutex mutex;
std::optional<app::SaveLibrary> library;
// The cartridge as the game has written it, published whenever it is a usable card.
std::array<uint8_t,sram::size> card{};
std::array<std::optional<fs::path>,2> window;   // nothing: the cartridge's own slot
std::optional<fs::path> suspend_window,armed,armed_suspend_record;
Settings choices;
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
sram::Bytes read_file(const fs::path& path,size_t size) {
    sram::Bytes bytes(size,0);   // a missing record reads as empty
    if(std::ifstream file(path,std::ios::binary);file)file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size));
    return bytes;
}
json read_json(const fs::path& path) {
    std::ifstream file(path);
    auto value=json::parse(file,nullptr,false);
    return value.is_object()?value:json::object();
}
void write_json(const fs::path& path,const json& value) {
    app::atomic_write(path,value.dump(2)+"\n");
}
fs::path sidecar(fs::path record){record.replace_extension(".json");return record;}
// A redirected transfer: the record file stands in for the cartridge's area.
void redirect(uint8_t* ram,const fs::path& path,unsigned direction,uint32_t buffer,uint32_t length,size_t size) {
    try {
        if(direction==1) {
            app::SaveLibrary::write_record(path,from_guest(ram,buffer,length),size);
            record("record-write",{{"record",path.filename().string()}});
        } else {
            const auto bytes=read_file(path,size);
            to_guest(ram,buffer,std::span(bytes).first(length));
            record("record-read",{{"record",path.filename().string()},{"length",length},{"used",sram::used(bytes)}});
        }
    } catch(const std::exception& error) {
        // The game's read-back then finds the old record and reports the save as failed.
        record("record-error",{{"record",path.filename().string()},{"direction",direction},{"error",error.what()}});
    }
}
}

void configure(const fs::path& directory,const std::optional<fs::path>& initial) {
    const char* root=std::getenv("SRW64_SAVE_LIBRARY");
    if(!root || !*root)return;
    library.emplace(fs::path(root));
    if(initial) {
        std::ifstream file(*initial,std::ios::binary);
        file.read(reinterpret_cast<char*>(card.data()),std::streamsize(card.size()));
    }
    const auto saved=read_json(library->directory()/"settings.json");
    choices.autosave=saved.value("autosave",true);
    choices.intermission=std::clamp(saved.value("autosave_intermission",3u),1u,20u);
    choices.turn=std::clamp(saved.value("autosave_turn",5u),1u,20u);
    log.open(directory/"save-store-events.jsonl");
    record("open",{{"library",root},{"initial",initial.has_value()},{"extended",library->slots()}});
}
fs::path main_library(){const char* root=std::getenv("SRW64_SAVE_LIBRARY");return root && *root?fs::path(root):fs::path();}
void switch_library(const fs::path& root) {
    std::lock_guard lock(mutex);
    library.emplace(root);
    // The card as this library last published it; a new library starts blank, as the
    // game will format it.
    card.fill(0);
    if(std::ifstream file(library->cartridge(),std::ios::binary);file)file.read(reinterpret_cast<char*>(card.data()),std::streamsize(card.size()));
    window={};suspend_window.reset();armed.reset();armed_suspend_record.reset();
    const auto saved=read_json(library->directory()/"settings.json");
    choices.autosave=saved.value("autosave",true);
    choices.intermission=std::clamp(saved.value("autosave_intermission",3u),1u,20u);
    choices.turn=std::clamp(saved.value("autosave_turn",5u),1u,20u);
    record("switch",{{"library",root.string()},{"card",sram::problems(card).empty()},{"extended",library->slots()}});
}
fs::path cartridge_path(){std::lock_guard lock(mutex);return library?library->cartridge():fs::path();}
bool enabled(){return library.has_value();}
fs::path library_directory(){return library?library->directory():fs::path();}
bool transfer(uint8_t* ram,unsigned direction,uint32_t cart,uint32_t buffer,uint32_t length) {
    std::lock_guard lock(mutex);
    if(!library || cart<cart_base || cart-cart_base+length>sram::size)return false;
    const uint32_t offset=cart-cart_base;
    if(const auto index=slot_index(offset,length)) {
        std::optional<fs::path> target=window[*index];
        if(*index==0 && !target && armed && direction==0){target=armed;armed.reset();}
        if(target){redirect(ram,*target,direction,buffer,length,sram::slot_size);return true;}
    }
    if(offset==sram::suspend_offset && (length==sram::suspend_size || (length==0x20 && direction==0))) {
        const auto target=suspend_window?suspend_window:armed_suspend_record;
        if(target){redirect(ram,*target,direction,buffer,length,sram::suspend_size);return true;}
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
Window::Window(unsigned slot,const fs::path& path):index(slot){std::lock_guard lock(mutex);window.at(index)=path;}
Window::~Window(){std::lock_guard lock(mutex);window.at(index).reset();}
SuspendWindow::SuspendWindow(const fs::path& path){std::lock_guard lock(mutex);suspend_window=path;}
SuspendWindow::~SuspendWindow(){std::lock_guard lock(mutex);suspend_window.reset();}
void arm_load(const fs::path& path){std::lock_guard lock(mutex);armed=path;record("arm",{{"record",path.filename().string()}});}
void arm_suspend(const fs::path& path){std::lock_guard lock(mutex);armed_suspend_record=path;record("arm-suspend",{{"record",path.filename().string()}});}
void disarm_suspend(){std::lock_guard lock(mutex);if(armed_suspend_record){armed_suspend_record.reset();record("disarm-suspend");}}
std::optional<fs::path> armed_suspend(){std::lock_guard lock(mutex);return armed_suspend_record;}
void disarm(){std::lock_guard lock(mutex);if(armed){armed.reset();record("disarm");}}

Settings settings(){std::lock_guard lock(mutex);return choices;}
void set_settings(const Settings& next) {
    std::lock_guard lock(mutex);
    choices=next;
    if(!library)return;
    try{write_json(library->directory()/"settings.json",{{"schema","srw64.save-settings.v1"},{"autosave",next.autosave},
        {"autosave_intermission",next.intermission},{"autosave_turn",next.turn}});}
    catch(const std::exception& error){record("settings-error",{{"error",error.what()}});}
}
fs::path slot_path(unsigned number){std::lock_guard lock(mutex);return library?library->slot_path(number):fs::path();}
std::vector<unsigned> extended(){std::lock_guard lock(mutex);return library?library->slots():std::vector<unsigned>{};}
std::optional<unsigned> next_free() {
    std::lock_guard lock(mutex);
    if(!library)return std::nullopt;
    try{return library->next_free_slot();}catch(const std::exception&){return std::nullopt;}
}
std::vector<AutoSave> autosaves() {
    std::lock_guard lock(mutex);
    std::vector<AutoSave> found;
    if(!library)return found;
    for(const auto& save:library->autosaves())
        found.push_back({save.kind==app::SaveLibrary::AutoKind::turn,save.sequence,save.record,read_json(sidecar(save.record))});
    return found;
}
bool autosave(bool turn,unsigned keep,json& about,const std::function<void(const fs::path&)>& write) {
    std::lock_guard lock(mutex);
    if(!library)return false;
    const auto kind=turn?app::SaveLibrary::AutoKind::turn:app::SaveLibrary::AutoKind::intermission;
    try {
        const auto path=library->next_autosave(kind);
        fs::create_directories(path.parent_path());
        write(path);
        if(!fs::exists(path)){record("autosave-error",{{"turn",turn},{"error","nothing written"}});return false;}
        write_json(sidecar(path),about);
        library->prune_autosaves(kind,keep);
        record("autosave",{{"record",path.filename().string()},{"turn",turn}});
        return true;
    } catch(const std::exception& error) {
        record("autosave-error",{{"turn",turn},{"error",error.what()}});
        return false;
    }
}
bool trash(const fs::path& path) {
    std::lock_guard lock(mutex);
    if(!library || !fs::exists(path))return false;
    try{const auto moved=library->trash(path);record("trash",{{"record",path.filename().string()},{"to",moved.filename().string()}});return true;}
    catch(const std::exception& error){record("trash-error",{{"record",path.filename().string()},{"error",error.what()}});return false;}
}
json import_candidates() {
    std::lock_guard lock(mutex);
    json files=json::array();
    if(!library)return files;
    const auto folder=library->directory()/"import";
    std::error_code error;fs::create_directories(folder,error);
    std::vector<fs::path> paths;
    for(const auto& entry:fs::directory_iterator(folder,error))if(entry.is_regular_file())paths.push_back(entry.path());
    std::sort(paths.begin(),paths.end());
    for(const auto& path:paths) {
        json row={{"file",path.filename().string()}};
        try {
            const auto found=app::SaveLibrary::scan(path);
            // The emulators that keep a card this way, by name: no words to translate.
            const auto order=found.detected.order;
            row["format"]=found.detected.retroarch?"RetroArch":order==sram::Order::big?"ares":order==sram::Order::word?"Project64 / mupen64plus":"16-bit swapped";
            row["slots"]=json::array();
            for(const auto& slot:found.slots)row["slots"].push_back({{"used",slot.used},{"intact",slot.intact}});
        } catch(const std::exception&){row["unknown"]=true;}
        files.push_back(row);
    }
    return files;
}
std::optional<unsigned> import_slot(const std::string& file,unsigned index,bool repair,std::string& error) {
    std::lock_guard lock(mutex);
    if(!library)return std::nullopt;
    const auto path=library->directory()/"import"/fs::path(file).filename();
    try{const auto number=library->import_slot(path,index,repair);record("import",{{"file",file},{"slot",index+1},{"to",number},{"repair",repair}});return number;}
    catch(const std::exception& failure){error=failure.what();record("import-error",{{"file",file},{"error",error}});return std::nullopt;}
}
std::vector<fs::path> export_all(std::string& error) {
    std::lock_guard lock(mutex);
    std::vector<fs::path> written;
    if(!library)return written;
    const std::pair<sram::Format,const char*> targets[]={{sram::Format::ares,"srw64-ares.ram"},{sram::Format::project64,"srw64-project64.sra"},
        {sram::Format::mupen64plus,"srw64-mupen64plus.sra"},{sram::Format::retroarch,"srw64-retroarch.srm"}};
    try {
        // export/ holds only the game's own files: each export replaces the last.
        fs::create_directories(library->directory()/"export");
        for(const auto& [format,name]:targets) {
            const auto path=library->directory()/"export"/name;
            fs::remove(path);
            written.push_back(library->export_cartridge(path,format));
        }
        record("export",{{"files",written.size()}});
    } catch(const std::exception& failure){error=failure.what();record("export-error",{{"error",error}});}
    return written;
}
}
