#include "save_library.hpp"
#include "runtime.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <stdexcept>

namespace srw64::app {
namespace {
sram::Bytes read_bytes(const fs::path& path,size_t limit) {
    if(!fs::is_regular_file(path) || fs::is_symlink(path) || fs::file_size(path)>limit)
        throw std::runtime_error("Not a readable save file: "+path.string());
    std::ifstream file(path,std::ios::binary);
    sram::Bytes data((std::istreambuf_iterator<char>(file)),{});
    if(!file && !file.eof())throw std::runtime_error("Cannot read: "+path.string());
    return data;
}
void write_bytes(const fs::path& path,std::span<const uint8_t> bytes) {
    fs::create_directories(path.parent_path());
    atomic_write(path,{reinterpret_cast<const char*>(bytes.data()),bytes.size()});
}
// 20261001T031500Z, then -2, -3 ... when that name is taken.
fs::path unused_stamped(const fs::path& directory,const std::string& suffix) {
    const auto now=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc,&now);
#else
    gmtime_r(&now,&utc);
#endif
    char stamp[32];std::strftime(stamp,sizeof stamp,"%Y%m%dT%H%M%SZ",&utc);
    for(unsigned n=1;n<1000;++n) {
        auto path=directory/(std::string(stamp)+(n>1?"-"+std::to_string(n):"")+suffix);
        if(!fs::exists(path))return path;
    }
    throw std::runtime_error("No free name in "+directory.string());
}
void require_usable(std::span<const uint8_t> card,const std::string& what,const std::string& hint={}) {
    const auto found=sram::problems(card);
    if(found.empty())return;
    std::string list;
    for(const auto& problem:found)list+=(list.empty()?"":", ")+problem;
    throw std::runtime_error(what+" is not a usable SRW64 save ("+list+")"+hint);
}
constexpr const char* recover="; use --import-save to recover explicitly";
bool session_token(std::string_view value) {
    return !value.empty() && value.size()<=80 &&
        std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||c=='-';});
}
}

SaveLibrary::SaveLibrary(fs::path directory):root(std::move(directory)) {}

std::optional<sram::Bytes> SaveLibrary::read_cartridge() const {
    if(!fs::exists(cartridge()))return std::nullopt;
    auto card=read_bytes(cartridge(),sram::size);
    require_usable(card,cartridge().string(),"; restore cartridge.sram.prev or another save with --import-save");
    return card;
}
void SaveLibrary::publish_cartridge(std::span<const uint8_t> card) {
    require_usable(card,"The new cartridge");
    if(fs::exists(cartridge())) {
        const auto old=read_bytes(cartridge(),sram::size);
        if(std::equal(old.begin(),old.end(),card.begin(),card.end()))return;
        write_bytes(fs::path(cartridge())+=".prev",old);
    }
    write_bytes(cartridge(),card);
}

fs::path SaveLibrary::slot_path(unsigned number) const {
    if(number<first_slot || number>last_slot)throw std::runtime_error("Extended slots are numbered 3 to 99");
    char name[16];std::snprintf(name,sizeof name,"%03u.rec",number);
    return root/"slots"/name;
}
std::vector<unsigned> SaveLibrary::slots() const {
    std::vector<unsigned> found;
    for(unsigned n=first_slot;n<=last_slot;++n)if(fs::exists(slot_path(n)))found.push_back(n);
    return found;
}
sram::Bytes SaveLibrary::read_slot(unsigned number) const {
    auto record=read_bytes(slot_path(number),sram::slot_size);
    if(record.size()!=sram::slot_size || !sram::used(record))throw std::runtime_error("Not a slot record: "+slot_path(number).string());
    return record;
}
void SaveLibrary::write_slot(unsigned number,std::span<const uint8_t> record) {
    write_record(slot_path(number),record,sram::slot_size);
}
unsigned SaveLibrary::next_free_slot() const {
    for(unsigned n=first_slot;n<=last_slot;++n)if(!fs::exists(slot_path(n)))return n;
    throw std::runtime_error("All extended slots 3-99 are in use");
}
void SaveLibrary::write_record(const fs::path& path,std::span<const uint8_t> record,size_t size) {
    if(record.size()!=size || !sram::used(record) || !sram::intact(record))
        throw std::runtime_error("Refusing an empty or damaged record for "+path.string());
    write_bytes(path,record);
}
fs::path SaveLibrary::trash(const fs::path& record) {
    const auto target=unused_stamped(root/"trash","-"+record.filename().string());
    fs::create_directories(target.parent_path());
    auto note=record;note.replace_extension(".json");
    if(fs::exists(note)){auto moved=target;moved.replace_extension(".json");fs::rename(note,moved);}
    fs::rename(record,target);
    return target;
}
namespace {
constexpr std::pair<const char*,const char*> auto_names[]={{"inter-",".rec"},{"turn-",".sus"}};
}
std::vector<SaveLibrary::AutoSave> SaveLibrary::autosaves() const {
    std::vector<AutoSave> found;
    if(!fs::is_directory(root/"auto"))return found;
    for(const auto& entry:fs::directory_iterator(root/"auto")) {
        const auto name=entry.path().filename().string();
        for(unsigned kind=0;kind<2;++kind) {
            const auto& [prefix,suffix]=auto_names[kind];
            const std::string_view head(prefix),tail(suffix);
            if(name.size()!=head.size()+6+tail.size() || !name.starts_with(head) || !name.ends_with(tail))continue;
            const auto digits=name.substr(head.size(),6);
            if(!std::all_of(digits.begin(),digits.end(),[](char c){return c>='0' && c<='9';}))continue;
            found.push_back({AutoKind(kind),unsigned(std::stoul(digits)),entry.path()});
        }
    }
    std::sort(found.begin(),found.end(),[](const AutoSave& a,const AutoSave& b){return a.sequence>b.sequence;});
    return found;
}
fs::path SaveLibrary::next_autosave(AutoKind kind) const {
    const auto saved=autosaves();
    const unsigned next=saved.empty()?1:saved.front().sequence+1;
    if(next>999999)throw std::runtime_error("Autosave numbers are used up");
    char name[32];std::snprintf(name,sizeof name,"%s%06u%s",auto_names[unsigned(kind)].first,next,auto_names[unsigned(kind)].second);
    return root/"auto"/name;
}
void SaveLibrary::prune_autosaves(AutoKind kind,unsigned keep) {
    unsigned seen=0;
    for(const auto& save:autosaves()) {
        if(save.kind!=kind || ++seen<=keep)continue;
        auto note=save.record;note.replace_extension(".json");
        std::error_code ignored;fs::remove(note,ignored);fs::remove(save.record);
    }
}
unsigned SaveLibrary::keep(std::span<const uint8_t> record) {
    for(const auto n:slots()) {
        const auto held=read_bytes(slot_path(n),sram::slot_size);
        if(std::equal(held.begin(),held.end(),record.begin(),record.end()))return n;
    }
    const auto n=next_free_slot();
    write_slot(n,record);
    return n;
}

std::vector<unsigned> SaveLibrary::keep_cartridge_slots(std::span<const uint8_t> next) {
    std::vector<unsigned> kept;
    if(!fs::exists(cartridge()))return kept;
    const auto old=read_bytes(cartridge(),sram::size);
    if(!sram::formatted(old))return kept;
    const auto held=[&](std::span<const uint8_t> record) {
        for(unsigned n=0;n<2 && sram::formatted(next);++n)if(std::ranges::equal(record,sram::slot(next,n)))return true;
        return false;
    };
    for(unsigned n=0;n<2;++n)
        if(const auto record=sram::slot(old,n);sram::used(record) && sram::intact(record) && !held(record))kept.push_back(keep(record));
    return kept;
}
SaveLibrary::Imported SaveLibrary::import_cartridge(const fs::path& file) {
    const auto bytes=read_bytes(file,sram::retroarch_size);
    auto card=sram::to_cartridge(bytes);
    Imported result{*sram::detect(bytes),{},{}};
    require_usable(card,file.string());
    // The old card may be the damaged one being replaced: keep what is intact.
    if(fs::exists(cartridge())) {
        const auto old=read_bytes(cartridge(),sram::size);
        if(std::equal(old.begin(),old.end(),card.begin(),card.end()))return result;
        result.backup=unused_stamped(root/"imports",".sram");
        write_bytes(result.backup,old);
        result.kept=keep_cartridge_slots(card);
        if(sram::formatted(old))sram::merge_seen(card,old);
    }
    publish_cartridge(card);
    return result;
}
unsigned SaveLibrary::import_slot(const fs::path& file,unsigned index,bool repair) {
    auto card=sram::to_cartridge(read_bytes(file,sram::retroarch_size));
    if(!sram::formatted(card))throw std::runtime_error("No SRW64V3 header: "+file.string());
    const auto record=sram::slot(std::span<uint8_t>(card),index);
    if(!sram::used(record))throw std::runtime_error("Slot "+std::to_string(index+1)+" is empty in "+file.string());
    if(!sram::intact(record)) {
        if(!repair)throw std::runtime_error("Slot "+std::to_string(index+1)+" fails its checksum in "+file.string());
        record[1]=sram::checksum(record);
    }
    return keep(record);
}
SaveLibrary::Scan SaveLibrary::scan(const fs::path& file) {
    const auto bytes=read_bytes(file,sram::retroarch_size);
    const auto card=sram::to_cartridge(bytes);
    if(!sram::formatted(card))throw std::runtime_error("No SRW64V3 header: "+file.string());
    const auto state=[](std::span<const uint8_t> record){return Scan::Record{sram::used(record),sram::used(record) && sram::intact(record)};};
    return {*sram::detect(bytes),{state(sram::slot(card,0)),state(sram::slot(card,1))},state(sram::suspend(card))};
}
fs::path SaveLibrary::export_cartridge(const fs::path& destination,sram::Format format) const {
    const auto card=read_cartridge();
    if(!card)throw std::runtime_error("There is no saved cartridge to export yet");
    sram::Bytes container;
    if(fs::exists(destination)) {
        container=read_bytes(destination,sram::retroarch_size);
        auto backup=destination;backup+=".before-srw64";
        if(fs::exists(backup))backup=unused_stamped(destination.parent_path(),"-"+destination.filename().string()+".before-srw64");
        write_bytes(backup,container);
        if(format!=sram::Format::retroarch || container.size()!=sram::retroarch_size)container.clear();
    }
    write_bytes(destination,sram::from_cartridge(*card,format,container));
    return destination;
}

bool SaveLibrary::migrate_sessions(const fs::path& user_dir) {
    if(fs::exists(cartridge()) || !fs::exists(user_dir/"last-session.txt"))return false;
    auto previous=read_text(user_dir/"last-session.txt",128);
    if(!previous.empty() && previous.back()=='\n')previous.pop_back();
    if(!session_token(previous))throw std::runtime_error(std::string("Invalid last-session pointer")+recover);
    const auto old=user_dir/"sessions"/previous;
    if(fs::is_symlink(old))throw std::runtime_error("Refusing a symlinked save session");
    auto expected=read_text(old/"save.sha256",65);
    if(!expected.empty() && expected.back()=='\n')expected.pop_back();
    if(sha256_file(old/"save.bin")!=expected)throw std::runtime_error(std::string("Save integrity check failed")+recover);
    const auto card=read_bytes(old/"save.bin",sram::size);
    require_usable(card,(old/"save.bin").string(),recover);
    publish_cartridge(card);
    return true;
}
}
