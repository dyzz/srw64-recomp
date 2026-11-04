#include "mod_library.hpp"
#include "miniz.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <random>

namespace srw64::mods {
namespace {
std::recursive_mutex mutex;
std::atomic<unsigned> changes{1};
std::atomic<uint64_t> game_packed{0};

// Archive limits: a music pack is large, but nothing a player would make comes near these.
constexpr uint64_t max_total=uint64_t(4)<<30;
constexpr unsigned max_entries=20000;

std::string stamp() {
    const auto now=std::chrono::system_clock::now().time_since_epoch();
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}
fs::path state_file(){return directory()/"mods.json";}
json read_json(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)return json();
    auto data=json::parse(in,nullptr,false);
    return data.is_discarded()?json():data;
}
State read_state(){return State::parse(read_json(state_file()));}
bool write_state(const State& state) {
    std::error_code error;
    fs::create_directories(directory(),error);
    const auto path=state_file(),temporary=fs::path(path.string()+".tmp");
    {
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
        if(!out)return false;
        out<<state.to_json().dump(1)<<'\n';
    }
    fs::rename(temporary,path,error);
    return !error;
}
// Keeps a folder out of the way rather than deleting it: removed and replaced packages go
// to <mods>/.trash, where the player can take them back.
bool to_trash(const fs::path& folder,const std::string& name) {
    std::error_code error;
    const auto trash=directory()/".trash";
    fs::create_directories(trash,error);
    fs::rename(folder,trash/(name+"-"+stamp()),error);
    return !error;
}
// The unpacking of the files placed in the folder (unpack_loose): bytes of the package
// files done out of all of them (each entry's packed bytes as it is written, each file's
// whole size once it is through), told on in whole percents.
void (*unpack_report)(unsigned)=nullptr;
uint64_t unpack_total=0,unpack_done=0;
unsigned unpack_told=0;
void unpack_reach(uint64_t done) {
    if(!unpack_report || !unpack_total)return;
    unpack_done=std::max(unpack_done,done);
    const unsigned percent=unsigned(std::min<uint64_t>(100,unpack_done*100/unpack_total));
    if(percent>unpack_told)unpack_report(unpack_told=percent);
}
void unpack_advance(uint64_t bytes){unpack_reach(unpack_done+bytes);}
// A fresh folder inside <mods> to unpack into, renamed into place once complete.
fs::path staging_folder() {
    std::random_device random;
    return directory()/(".installing-"+stamp()+"-"+std::to_string(random()%100000));
}

// Reads the manifest of an archive and unpacks the package into `target`.
InstallResult unpack(const fs::path& archive,const fs::path& target) {
    InstallResult result;
    mz_zip_archive zip{};
    // miniz opens by narrow name; read the file ourselves so any path works on Windows.
    std::ifstream in(archive,std::ios::binary);
    if(!in){result.problem={"open",archive.filename().string()};return result;}
    // In one read: a character at a time took half a minute for a voice package on the Switch.
    std::error_code size_error;
    std::vector<char> bytes(size_t(fs::file_size(archive,size_error)));
    if(size_error || !in.read(bytes.data(),std::streamsize(bytes.size()))){result.problem={"open",archive.filename().string()};return result;}
    if(!mz_zip_reader_init_mem(&zip,bytes.data(),bytes.size(),0)){result.problem={"archive",archive.filename().string()};return result;}
    struct Closer{mz_zip_archive* z;~Closer(){mz_zip_reader_end(z);}} closer{&zip};
    const unsigned count=mz_zip_reader_get_num_files(&zip);
    if(count>max_entries){result.problem={"archive_size",""};return result;}
    struct Entry{unsigned index;std::string name;bool folder;uint64_t size,packed;};
    std::vector<Entry> entries;
    uint64_t total=0;
    for(unsigned i=0;i<count;++i) {
        mz_zip_archive_file_stat stat;
        if(!mz_zip_reader_file_stat(&zip,i,&stat)){result.problem={"archive",""};return result;}
        std::string name=stat.m_filename;
        if(name.starts_with("__MACOSX/") || name.ends_with(".DS_Store"))continue;
        const auto safe=safe_entry(name);
        if(!safe){result.problem={"archive_path",name};return result;}
        total+=stat.m_uncomp_size;
        entries.push_back({i,*safe,bool(stat.m_is_directory),stat.m_uncomp_size,stat.m_comp_size});
    }
    if(total>max_total){result.problem={"archive_size",""};return result;}
    // mod.json at the top, or inside the one folder everything sits in.
    std::string prefix;
    const auto has=[&](const std::string& name){return std::any_of(entries.begin(),entries.end(),[&](const Entry& e){return !e.folder && e.name==name;});};
    if(!has("mod.json")) {
        std::set<std::string> tops;
        for(const auto& e:entries)tops.insert(e.name.substr(0,e.name.find('/')));
        if(tops.size()==1 && has(*tops.begin()+"/mod.json"))prefix=*tops.begin()+"/";
        else{result.problem={"no_manifest",""};return result;}
    }
    // The manifest first, before anything is written.
    for(const auto& e:entries) {
        if(e.folder || e.name!=prefix+"mod.json")continue;
        size_t size=0;
        void* data=mz_zip_reader_extract_to_heap(&zip,e.index,&size,0);
        if(!data){result.problem={"archive",""};return result;}
        const auto text=std::string(static_cast<char*>(data),size);
        mz_free(data);
        const auto parsed=parse_manifest(json::parse(text,nullptr,false));
        if(const auto* problem=std::get_if<Problem>(&parsed)){result.problem=*problem;return result;}
        const auto& manifest=std::get<Manifest>(parsed);
        result.id=manifest.id;result.version=manifest.version;
    }
    std::error_code error;
    fs::create_directories(target,error);
    for(const auto& e:entries) {
        if(!e.name.starts_with(prefix))continue;
        const auto relative=e.name.substr(prefix.size());
        if(relative.empty())continue;
        const auto out=target/fs::path(relative);
        if(e.folder){fs::create_directories(out,error);continue;}
        fs::create_directories(out.parent_path(),error);
        std::ofstream file(out,std::ios::binary|std::ios::trunc);
        if(!file){result.problem={"write",relative};return result;}
        const auto write=[](void* user,mz_uint64,const void* buffer,size_t n)->size_t{
            static_cast<std::ofstream*>(user)->write(static_cast<const char*>(buffer),std::streamsize(n));
            return n;
        };
        if(!mz_zip_reader_extract_to_callback(&zip,e.index,write,&file,0) || !file){result.problem={"write",relative};return result;}
        unpack_advance(e.packed);
    }
    result.ok=true;
    return result;
}
// Copies a package folder into `target`.
InstallResult copy_folder(const fs::path& source,const fs::path& target) {
    InstallResult result;
    const auto parsed=parse_manifest(read_json(source/"mod.json"));
    if(const auto* problem=std::get_if<Problem>(&parsed)){result.problem=*problem;return result;}
    result.id=std::get<Manifest>(parsed).id;result.version=std::get<Manifest>(parsed).version;
    std::error_code error;
    fs::copy(source,target,fs::copy_options::recursive,error);
    if(error){result.problem={"write",error.message()};return result;}
    result.ok=true;
    return result;
}
bool is_package_file(const fs::path& path) {
    auto ext=path.extension().string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    return ext==package_extension || ext==".zip";
}
// Packages placed in the folder as files are unpacked; the file is then kept in .trash.
// A file that does not install stays, and the next listing tries it again; what each
// listing found is kept for the MOD page (loose_results).
std::vector<LooseResult> loose;
void unpack_loose() {
    std::error_code error;
    std::vector<fs::path> files;
    for(const auto& item:fs::directory_iterator(directory(),error))if(item.is_regular_file(error) && is_package_file(item.path()))files.push_back(item.path());
    std::sort(files.begin(),files.end());
    loose.clear();
    unpack_total=unpack_done=0;unpack_told=0;
    for(const auto& file:files)unpack_total+=fs::file_size(file,error);
    if(unpack_report && unpack_total)unpack_report(0);
    uint64_t passed=0;
    for(const auto& file:files) {
        const uint64_t size=fs::file_size(file,error);
        LooseResult r{file,install(file)};
        unpack_reach(passed+=size);   // the archive's own headers too, or one that failed
        if(r.result.ok) {
            const auto trash=directory()/".trash";
            fs::create_directories(trash,error);
            fs::rename(file,trash/(file.filename().string()+"-"+stamp()),error);
        }
        loose.push_back(std::move(r));
    }
    unpack_total=0;
}
// A package folder put in the folder under another name, as a package file extracted by hand
// comes out ("srw64.space-a-1.0.2/mod.json", or one level deeper when the extractor made a
// folder of the same name inside), takes its id's name, the package of that id already there
// going to .trash as an install's would. Folders already named after their id are left as they
// are; anything else is the listing's problem.
void adopt_folders() {
    std::error_code error;
    std::vector<fs::path> folders;
    for(const auto& item:fs::directory_iterator(directory(),error))
        if(item.is_directory(error) && !item.path().filename().string().starts_with("."))folders.push_back(item.path());
    std::sort(folders.begin(),folders.end());
    for(const auto& folder:folders) {
        fs::path source=folder;
        if(!fs::is_regular_file(folder/"mod.json",error)) {
            std::vector<fs::path> inner;
            for(const auto& item:fs::directory_iterator(folder,error))
                if(item.is_directory(error) && item.path().filename()!="__MACOSX")inner.push_back(item.path());
            if(inner.size()!=1 || !fs::is_regular_file(inner.front()/"mod.json",error))continue;
            source=inner.front();
        }
        const auto parsed=parse_manifest(read_json(source/"mod.json"));
        const auto* manifest=std::get_if<Manifest>(&parsed);
        if(!manifest || (source==folder && folder.filename()==manifest->id))continue;
        // As an install: one with the id already there (an older version) goes to .trash.
        const auto target=directory()/manifest->id;
        if(fs::exists(target,error) && !to_trash(target,manifest->id))continue;
        fs::rename(source,target,error);
        if(error)continue;
        if(source!=folder)to_trash(folder,folder.filename().string());   // the shell left around it, with __MACOSX
        ++changes;
    }
}
}

std::vector<LooseResult> loose_results() {
    std::lock_guard lock(mutex);
    return loose;
}

fs::path directory() {
    const char* dir=std::getenv("SRW64_MODS_DIR");
    return dir && *dir?fs::path(dir):fs::path{};
}

std::vector<Package> installed(const Version& game) {
    std::lock_guard lock(mutex);
    if(directory().empty())return {};
    std::error_code error;
    if(!fs::is_directory(directory(),error))return {};
    unpack_loose();
    adopt_folders();
    std::vector<fs::path> folders;
    for(const auto& item:fs::directory_iterator(directory(),error)) {
        const auto name=item.path().filename().string();
        if(item.is_directory(error) && !name.starts_with("."))folders.push_back(item.path());
    }
    std::sort(folders.begin(),folders.end());
    std::vector<Package> packages;
    for(const auto& folder:folders) {
        Package p;
        p.folder=folder;
        if(!fs::is_regular_file(folder/"mod.json",error))p.problem={"no_manifest",""};
        else {
            const auto parsed=parse_manifest(read_json(folder/"mod.json"));
            if(const auto* problem=std::get_if<Problem>(&parsed))p.problem=*problem;
            else {
                p.manifest=std::get<Manifest>(parsed);
                // The folder is named after the id, so a package is found by its id.
                if(folder.filename().string()!=p.manifest->id)p.problem={"folder",p.manifest->id};
            }
        }
        p.contents=summarize(folder);
        packages.push_back(std::move(p));
    }
    return resolve(std::move(packages),read_state(),game);
}

InstallResult install(const fs::path& source) {
    std::lock_guard lock(mutex);
    InstallResult result;
    if(directory().empty()){result.problem={"no_folder",""};return result;}
    std::error_code error;
    fs::create_directories(directory(),error);
    const auto staging=staging_folder();
    if(fs::is_directory(source,error)) {
        if(fs::equivalent(source.parent_path(),directory(),error)){result.problem={"already",source.filename().string()};return result;}
        result=copy_folder(source,staging);
    }
    else if(is_package_file(source))result=unpack(source,staging);
    else{result.problem={"not_package",source.filename().string()};return result;}
    if(!result.ok){fs::remove_all(staging,error);return result;}
    const auto target=directory()/result.id;
    if(fs::exists(target,error)) {
        const auto old=parse_manifest(read_json(target/"mod.json"));
        if(const auto* m=std::get_if<Manifest>(&old))result.replaced=m->version;
        if(!to_trash(target,result.id)){fs::remove_all(staging,error);result.ok=false;result.problem={"write",target.filename().string()};return result;}
    }
    fs::rename(staging,target,error);
    if(error){fs::remove_all(staging,error);result.ok=false;result.problem={"write",error.message()};return result;}
    // A freshly installed package is on, and goes on top unless it was already placed.
    auto state=read_state();
    state.disabled.erase(result.id);
    if(std::find(state.order.begin(),state.order.end(),result.id)==state.order.end())state.order.push_back(result.id);
    write_state(state);
    ++changes;
    return result;
}

bool set_enabled(const std::string& id,bool on) {
    std::lock_guard lock(mutex);
    if(directory().empty())return false;
    auto state=read_state();
    if(on)state.disabled.erase(id);else state.disabled.insert(id);
    if(std::find(state.order.begin(),state.order.end(),id)==state.order.end())state.order.push_back(id);
    ++changes;
    return write_state(state);
}

bool remove(const std::string& id) {
    std::lock_guard lock(mutex);
    if(directory().empty() || !campaign_library::valid_id(id))return false;
    std::error_code error;
    const auto folder=directory()/id;
    if(!fs::is_directory(folder,error))return false;
    if(!to_trash(folder,id))return false;
    auto state=read_state();
    state.disabled.erase(id);
    std::erase(state.order,id);
    ++changes;
    return write_state(state);
}
void set_unpack_progress(void (*report)(unsigned percent)){std::lock_guard lock(mutex);unpack_report=report;}
unsigned revision(){return changes.load();}
std::mutex builtin_mutex;
std::map<std::string,Version> builtins;   // the usable ones
void set_builtin(const std::string& id,std::optional<Version> version) {
    std::lock_guard lock(builtin_mutex);
    const auto f=builtins.find(id);
    const bool same=version?(f!=builtins.end() && f->second==*version):f==builtins.end();
    if(same)return;
    if(version)builtins[id]=*version;else builtins.erase(id);
    ++changes;
}
std::optional<Version> builtin(const std::string& id) {
    std::lock_guard lock(builtin_mutex);
    const auto f=builtins.find(id);
    return f==builtins.end()?std::nullopt:std::optional<Version>(f->second);
}
void set_game_version(const Version& v){game_packed=(uint64_t(v.major)<<40)|(uint64_t(v.minor)<<20)|uint64_t(v.patch);}
Version game_version() {
    const uint64_t p=game_packed.load();
    return {int(p>>40),int((p>>20)&0xFFFFF),int(p&0xFFFFF)};
}
}
