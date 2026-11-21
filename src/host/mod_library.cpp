#include "mod_library.hpp"
#include "miniz.h"
#include "../native/presentation/file_source.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
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
// One file read into memory: a whole song decodes from one (music.cpp), fonts are 32 MB at most.
constexpr uint64_t max_read=uint64_t(512)<<20;

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
// A zip file read in place: only its directory comes into memory.
struct Archive {
    FILE* file{};
    mz_zip_archive zip{};
    std::mutex mutex;   // miniz seeks and reads the one FILE
    // miniz opens by narrow name; open it ourselves so any path works on Windows.
    bool open_file(const fs::path& path) {
#ifdef _WIN32
        file=_wfopen(path.c_str(),L"rb");
#else
        file=std::fopen(path.c_str(),"rb");
#endif
        return file;
    }
    bool read_directory(){return mz_zip_reader_init_cfile(&zip,file,0,0);}
    void close() {
        if(zip.m_pState)mz_zip_reader_end(&zip);
        if(file)std::fclose(file);
        file=nullptr;zip={};
    }
    ~Archive(){close();}
};
// A package file's entries: the name as it stands in the file, and made safe.
struct Entry{unsigned index;std::string stored,name;bool folder;uint64_t size,packed;};
// Every entry but the ones a Mac's archiver adds; false, and the bad name, for one that
// could go outside the package.
bool read_entries(mz_zip_archive& zip,std::vector<Entry>& entries,Problem& problem) {
    const unsigned count=mz_zip_reader_get_num_files(&zip);
    for(unsigned i=0;i<count;++i) {
        mz_zip_archive_file_stat stat;
        if(!mz_zip_reader_file_stat(&zip,i,&stat)){problem={"archive",""};return false;}
        std::string name=stat.m_filename;
        if(name.starts_with("__MACOSX/") || name.ends_with(".DS_Store"))continue;
        const auto safe=safe_entry(name);
        if(!safe){problem={"archive_path",name};return false;}
        entries.push_back({i,name,*safe,bool(stat.m_is_directory),stat.m_uncomp_size,stat.m_comp_size});
    }
    return true;
}
// Where the package sits in the file: mod.json at the top, or inside the one folder
// everything is in. nullopt without one.
std::optional<std::string> package_prefix(const std::vector<Entry>& entries) {
    const auto has=[&](const std::string& name){return std::any_of(entries.begin(),entries.end(),[&](const Entry& e){return !e.folder && e.name==name;});};
    if(has("mod.json"))return "";
    std::set<std::string> tops;
    for(const auto& e:entries)tops.insert(e.name.substr(0,e.name.find('/')));
    if(tops.size()==1 && has(*tops.begin()+"/mod.json"))return *tops.begin()+"/";
    return std::nullopt;
}
// The package files being read in place, open from the first read to the package's move:
// Windows does not rename or move a file that is open.
std::mutex archives_mutex;
std::map<fs::path,std::shared_ptr<Archive>> archives;
std::shared_ptr<Archive> archive_at(const fs::path& path) {
    std::lock_guard lock(archives_mutex);
    if(const auto found=archives.find(path);found!=archives.end())return found->second;
    auto opened=std::make_shared<Archive>();
    if(!opened->open_file(path) || !opened->read_directory())return nullptr;
    archives[path]=opened;
    return opened;
}
void close_archive(const fs::path& path) {
    std::shared_ptr<Archive> open;
    {
        std::lock_guard lock(archives_mutex);
        const auto found=archives.find(path);
        if(found==archives.end())return;
        open=std::move(found->second);
        archives.erase(found);
    }
    std::lock_guard lock(open->mutex);   // a line being read finishes first
    open->close();
}
// A .srw64mod read in place: its directory in memory (miniz's, and the names as the package
// sees them), each file taken out as asked.
class ArchiveFiles:public Files {
public:
    // `under`: only the entries below this folder of the package ("audio/voice/").
    explicit ArchiveFiles(fs::path path,std::string under={}):path_(std::move(path)) {
        archive_=archive_at(path_);
        if(!archive_)return;
        std::lock_guard lock(archive_->mutex);
        if(!archive_->file || mz_zip_reader_get_num_files(&archive_->zip)>max_entries)return;
        std::vector<Entry> entries;
        Problem problem;
        if(!read_entries(archive_->zip,entries,problem))return;
        const auto prefix=package_prefix(entries);
        if(!prefix)return;
        for(const auto& e:entries) {
            if(e.folder || !e.name.starts_with(*prefix))continue;
            auto name=e.name.substr(prefix->size());
            if(name.empty() || !name.starts_with(under))continue;
            index_.emplace(name,e.index);
            files_.push_back(std::move(name));
        }
        std::sort(files_.begin(),files_.end());
        files_.erase(std::unique(files_.begin(),files_.end()),files_.end());
    }
    const std::vector<std::string>& all() const override {return files_;}
    std::optional<std::vector<unsigned char>> read(const std::string& path) const override {
        const auto found=index_.find(path);
        if(found==index_.end() || !archive_)return std::nullopt;
        std::lock_guard lock(archive_->mutex);
        if(!archive_->file)return std::nullopt;   // closed while this waited: the package moved
        mz_zip_archive_file_stat stat;
        if(!mz_zip_reader_file_stat(&archive_->zip,found->second,&stat) || stat.m_uncomp_size>max_read)return std::nullopt;
        std::vector<unsigned char> bytes(size_t(stat.m_uncomp_size));
        if(!bytes.empty() && !mz_zip_reader_extract_to_mem(&archive_->zip,found->second,bytes.data(),bytes.size(),0))return std::nullopt;
        return bytes;
    }
    fs::path where() const override {return path_;}
private:
    fs::path path_;
    std::shared_ptr<Archive> archive_;
    std::vector<std::string> files_;
    std::map<std::string,unsigned> index_;
};
// A folder the releases before 0.5.4 unpacked, its voices still in voice.srw64mod: they show
// as its own audio/voice/ files, the folder's file winning where both have one.
class FolderWithVoices:public FolderFiles {
public:
    explicit FolderWithVoices(const fs::path& folder):FolderFiles(folder),voices_(folder/voice_archive,"audio/voice/") {
        for(const auto& name:voices_.all())if(!std::binary_search(files_.begin(),files_.end(),name))merged_.push_back(name);
        merged_.insert(merged_.end(),files_.begin(),files_.end());
        std::erase(merged_,std::string(voice_archive));
        std::sort(merged_.begin(),merged_.end());
    }
    const std::vector<std::string>& all() const override {return merged_;}
    std::optional<std::vector<unsigned char>> read(const std::string& path) const override {
        if(std::binary_search(files_.begin(),files_.end(),path))return FolderFiles::read(path);
        return voices_.read(path);
    }
private:
    ArchiveFiles voices_;
    std::vector<std::string> merged_;
};
// Each package file's Files, made once: its directory is read at the first ask, and a voice
// package has thousands of entries. A package's move (to_trash) drops it.
std::mutex package_files_mutex;
std::map<fs::path,std::shared_ptr<const ArchiveFiles>> package_files;
std::shared_ptr<const ArchiveFiles> archive_files(const fs::path& path) {
    std::lock_guard lock(package_files_mutex);
    if(const auto found=package_files.find(path);found!=package_files.end())return found->second;
    auto made=std::make_shared<const ArchiveFiles>(path);
    package_files[path]=made;
    return made;
}
void forget_files(const fs::path& path) {
    std::lock_guard lock(package_files_mutex);
    package_files.erase(path);
}
// A path into a package file: "<mods>/x.srw64mod/art/a.png" is ("<mods>/x.srw64mod", "art/a.png").
std::optional<std::pair<fs::path,std::string>> split_package_path(const fs::path& path) {
    fs::path head;
    std::string rest;
    bool inside=false;
    for(const auto& part:path) {
        if(inside){rest+=(rest.empty()?"":"/")+part.generic_string();continue;}
        head/=part;
        std::error_code error;
        if(head.extension()==package_extension && fs::is_regular_file(head,error))inside=true;
    }
    if(!inside || rest.empty())return std::nullopt;
    return std::pair{head,rest};
}
std::optional<std::vector<unsigned char>> source_read(const fs::path& path) {
    if(const auto split=split_package_path(path))return archive_files(split->first)->read(split->second);
    std::ifstream in(path,std::ios::binary);
    if(!in)return std::nullopt;
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),{});
}
bool source_exists(const fs::path& path) {
    if(const auto split=split_package_path(path))return archive_files(split->first)->exists(split->second);
    std::error_code error;
    return fs::is_regular_file(path,error);
}
std::vector<fs::path> source_list(const fs::path& folder) {
    if(const auto split=split_package_path(folder/"_")) {   // the folder's own path may end in the package
        auto under=split->second.substr(0,split->second.size()-1);   // "dialogue/ko/"
        std::vector<fs::path> out;
        const auto files=archive_files(split->first);
        for(const auto& name:files->all())if(name.starts_with(under))out.push_back(split->first/fs::path(name));
        return out;
    }
    std::vector<fs::path> out;
    std::error_code error;
    if(!fs::is_directory(folder,error))return out;
    for(auto it=fs::recursive_directory_iterator(folder,fs::directory_options::skip_permission_denied,error);!error && it!=fs::recursive_directory_iterator();it.increment(error))
        if(it->is_regular_file(error))out.push_back(it->path());
    std::sort(out.begin(),out.end());
    return out;
}
// Every path the game names is read through this from here on (file_source.hpp).
const presentation::FileSource package_source{source_read,source_exists,source_list};
[[maybe_unused]] const bool package_source_set=(presentation::file_source()=&package_source,true);
// The last listing's packages, before the player's choices: reading each manifest and counting
// each package's files took 300-800 ms on a Switch's SD card, once per reader (the music, the
// Library, the names and the rest) every time anything moved. folder_stamp says when to look again.
std::optional<std::vector<Package>> listed;
std::string listed_stamp;
// Keeps a folder out of the way rather than deleting it: removed and replaced packages go
// to <mods>/.trash, where the player can take them back.
bool to_trash(const fs::path& folder,const std::string& name) {
    forget_files(folder);
    close_archive(folder);
    close_archive(folder/voice_archive);
    listed.reset();
    std::error_code error;
    const auto trash=directory()/".trash";
    fs::create_directories(trash,error);
    // A replace and a removal in the same millisecond would name the same folder.
    auto kept=trash/(name+"-"+stamp());
    for(unsigned n=2;fs::exists(kept,error);++n)kept=trash/(name+"-"+stamp()+"-"+std::to_string(n));
    fs::rename(folder,kept,error);
    return !error;
}
// The installing of the files placed in the folder (place_loose): bytes of the package
// files done out of all of them (each file's whole size once it is in place; a rename takes
// no time, a bundle's packages are copied out), told on in whole percents.
void (*unpack_report)(unsigned)=nullptr;
uint64_t unpack_total=0,unpack_done=0;
unsigned unpack_told=0;
void unpack_reach(uint64_t done) {
    if(!unpack_report || !unpack_total)return;
    unpack_done=std::max(unpack_done,done);
    const unsigned percent=unsigned(std::min<uint64_t>(100,unpack_done*100/unpack_total));
    if(percent>unpack_told)unpack_report(unpack_told=percent);
}
// A fresh folder inside <mods> to unpack into, renamed into place once complete.
fs::path staging_folder() {
    std::random_device random;
    return directory()/(".installing-"+stamp()+"-"+std::to_string(random()%100000));
}

// Checks a package file before it is kept: its directory, every name in it, the sizes, and its
// manifest. The game then reads it in place.
InstallResult check(const fs::path& archive) {
    InstallResult result;
    Archive package;
    if(!package.open_file(archive)){result.problem={"open",archive.filename().string()};return result;}
    if(!package.read_directory()){result.problem={"archive",archive.filename().string()};return result;}
    auto& zip=package.zip;
    if(mz_zip_reader_get_num_files(&zip)>max_entries){result.problem={"archive_size",""};return result;}
    std::vector<Entry> entries;
    if(!read_entries(zip,entries,result.problem))return result;
    uint64_t total=0;
    for(const auto& e:entries)total+=e.size;
    if(total>max_total){result.problem={"archive_size",""};return result;}
    const auto prefix=package_prefix(entries);
    if(!prefix){result.problem={"no_manifest",""};return result;}
    for(const auto& e:entries) {
        if(e.folder || e.name!=*prefix+"mod.json")continue;
        size_t size=0;
        void* data=mz_zip_reader_extract_to_heap(&zip,e.index,&size,0);
        if(!data){result.problem={"archive",""};return result;}
        const auto text=std::string(static_cast<char*>(data),size);
        mz_free(data);
        const auto parsed=parse_manifest(json::parse(text,nullptr,false));
        if(const auto* problem=std::get_if<Problem>(&parsed)){result.problem=*problem;return result;}
        result.id=std::get<Manifest>(parsed).id;result.version=std::get<Manifest>(parsed).version;
    }
    result.ok=true;
    return result;
}
// The package files inside a zip that is not a package itself, as a Thunderstore package is
// (its manifest.json, icon.png and README.md beside the .srw64mod, which is left as made so its
// SHA-256 holds): their entries, in the file's order. Empty for a package, or a zip without any.
std::vector<Entry> bundled_packages(mz_zip_archive& zip) {
    std::vector<Entry> entries,inner;
    Problem ignored;
    if(mz_zip_reader_get_num_files(&zip)>max_entries || !read_entries(zip,entries,ignored) || package_prefix(entries))return {};
    for(const auto& e:entries) {
        auto name=e.name;
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(!e.folder && name.ends_with(package_extension) && e.size<=max_total)inner.push_back(e);
    }
    return inner;
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
// What a listing depends on, cheaply: the folder's package files (with their sizes and times)
// and folders (with when each manifest was written). A file added inside a package folder by
// hand shows at the next start.
std::string folder_stamp() {
    std::vector<std::string> names;
    std::error_code error;
    for(const auto& item:fs::directory_iterator(directory(),error)) {
        auto name=item.path().filename().string();
        if(name.starts_with("."))continue;
        if(item.is_directory(error))names.push_back(name+"/"+std::to_string((long long)fs::last_write_time(item.path()/"mod.json",error).time_since_epoch().count()));
        else if(is_package_file(item.path()))
            names.push_back(name+"|"+std::to_string(item.file_size(error))+"|"+std::to_string((long long)item.last_write_time(error).time_since_epoch().count()));
    }
    std::sort(names.begin(),names.end());
    std::string stamp;
    for(const auto& name:names)stamp+=name+"\n";
    return stamp;
}
// A package file copied in, a MiB at a time: fs::copy_file's 8 KiB writes took minutes for
// 34 MB on a Switch's SD card.
bool copy_package(const fs::path& from,const fs::path& to) {
    std::ifstream in(from,std::ios::binary);
    std::ofstream out(to,std::ios::binary|std::ios::trunc);
    std::vector<char> buffer(1u<<20);
    while(in && out) {
        in.read(buffer.data(),std::streamsize(buffer.size()));
        out.write(buffer.data(),in.gcount());
    }
    out.flush();
    return in.eof() && out.good();
}
// A package file in the folder that is not installed as it stands: a .zip, or a .srw64mod not
// named <its id>.srw64mod ("srw64.space-a-1.0.2.srw64mod", as a download is called).
bool loose_file(const fs::path& file) {
    if(file.extension()!=package_extension)return true;
    const auto name=file.stem().string();
    if(!campaign_library::valid_id(name))return true;
    const auto r=check(file);
    return !r.ok || r.id!=name;
}
// Package files placed in the folder are installed: renamed to <id>.srw64mod (a Thunderstore
// zip's packages copied out of it, the zip then kept in .trash). A file that does not install
// stays, and the next listing tries it again; what each listing found is kept for the MOD page
// (loose_results).
std::vector<LooseResult> loose;
void place_loose() {
    std::error_code error;
    std::vector<fs::path> files;
    for(const auto& item:fs::directory_iterator(directory(),error))
        if(item.is_regular_file(error) && is_package_file(item.path()) && loose_file(item.path()))files.push_back(item.path());
    std::sort(files.begin(),files.end());
    loose.clear();
    unpack_total=unpack_done=0;unpack_told=0;
    for(const auto& file:files)unpack_total+=fs::file_size(file,error);
    if(unpack_report && unpack_total)unpack_report(0);
    uint64_t passed=0;
    for(const auto& file:files) {
        const uint64_t size=fs::file_size(file,error);
        LooseResult r{file,install(file)};
        unpack_reach(passed+=size);
        // A zip of packages stays after its packages are copied out: kept in .trash.
        if(r.result.ok && fs::exists(file,error)) {
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
        if(const auto file=directory()/(manifest->id+package_extension);fs::exists(file,error))to_trash(file,file.filename().string());
        fs::rename(source,target,error);
        if(error)continue;
        if(source!=folder)to_trash(folder,folder.filename().string());   // the shell left around it, with __MACOSX
        ++changes;
    }
}
}

fs::path local_file(const fs::path& path) {
    const auto split=split_package_path(path);
    if(!split)return path;
    std::error_code error;
    const auto& [package,entry]=*split;
    const auto stem=package.stem().string();
    const auto cache=directory()/".cache";
    const auto folder=cache/(stem+"-"+std::to_string(fs::file_size(package,error))+"-"+
                             std::to_string((long long)fs::last_write_time(package,error).time_since_epoch().count()));
    const auto out=folder/fs::path(entry);
    if(fs::is_regular_file(out,error))return out;
    const auto bytes=archive_files(package)->read(entry);
    if(!bytes)return {};
    // An older copy of this package's files goes.
    for(const auto& item:fs::directory_iterator(cache,error))
        if(item.path()!=folder && item.path().filename().string().starts_with(stem+"-"))fs::remove_all(item.path(),error);
    fs::create_directories(out.parent_path(),error);
    const auto part=fs::path(out.string()+".part");
    {
        std::ofstream file(part,std::ios::binary|std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes->data()),std::streamsize(bytes->size()));
        if(!file)return {};
    }
    fs::rename(part,out,error);
    return error?fs::path():out;
}

fs::file_time_type file_time(const fs::path& path) {
    std::error_code error;
    const auto split=split_package_path(path);
    return fs::last_write_time(split?split->first:path,error);
}

std::shared_ptr<const Files> open_files(const fs::path& where) {
    std::error_code error;
    if(fs::is_directory(where,error)) {
        if(fs::is_regular_file(where/voice_archive,error))return std::make_shared<FolderWithVoices>(where);
        return std::make_shared<FolderFiles>(where);
    }
    return archive_files(where);
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
    if(listed && folder_stamp()==listed_stamp) {
        // Nothing placed since: a file that did not install stays reported, and is not tried again.
        std::erase_if(loose,[](const LooseResult& r){return r.result.ok;});
        return resolve(*listed,read_state(),game);
    }
    place_loose();
    adopt_folders();
    // <id>/ and <id>.srw64mod; a folder takes the place of a file of the same name. A file that
    // did not install (a bad zip, or something else given the extension) is reported by
    // loose_results, not listed as a package too.
    std::set<fs::path> refused;
    for(const auto& r:loose)if(!r.result.ok)refused.insert(r.file);
    std::map<std::string,fs::path> found;
    for(const auto& item:fs::directory_iterator(directory(),error)) {
        const auto path=item.path();
        const auto name=path.filename().string();
        if(name.starts_with("."))continue;
        if(item.is_directory(error))found[name]=path;
        else if(item.is_regular_file(error) && path.extension()==package_extension && !refused.contains(path) && !found.contains(path.stem().string()))
            found.emplace(path.stem().string(),path);
    }
    std::vector<Package> packages;
    for(const auto& [name,where]:found) {
        Package p;
        p.folder=where;
        p.files=open_files(where);
        if(!p.files->exists("mod.json"))p.problem={"no_manifest",""};
        else {
            const auto parsed=parse_manifest(p.files->read_json("mod.json"));
            if(const auto* problem=std::get_if<Problem>(&parsed))p.problem=*problem;
            else {
                p.manifest=std::get<Manifest>(parsed);
                // Named after the id, so a package is found by its id.
                if(name!=p.manifest->id)p.problem={"folder",p.manifest->id};
            }
        }
        p.contents=summarize(*p.files);
        packages.push_back(std::move(p));
    }
    listed=packages;listed_stamp=folder_stamp();
    return resolve(std::move(packages),read_state(),game);
}

InstallResult install(const fs::path& source) {
    // Copying takes seconds on an SD card, and the window thread lists the packages meanwhile:
    // holding the lock through it stopped its frames for the stall watchdog. A copy goes to a
    // name of its own first; the lock is for the swap, and for a file placed in the folder,
    // which a listing would pick up too.
    const bool placed=directory()/source.filename()==source;
    std::unique_lock lock(mutex,std::defer_lock);
    if(placed)lock.lock();
    InstallResult result;
    if(directory().empty()){result.problem={"no_folder",""};return result;}
    std::error_code error;
    fs::create_directories(directory(),error);
    const auto staging=staging_folder();
    // A zip of package files: each is taken out on its own and installs as a file chosen from
    // elsewhere would. The result is the first that failed, or the last that installed.
    if(!fs::is_directory(source,error) && is_package_file(source)) {
        Archive outer;
        std::vector<Entry> inner;
        if(outer.open_file(source) && outer.read_directory())inner=bundled_packages(outer.zip);
        if(!inner.empty()) {
            fs::create_directories(staging,error);
            std::optional<InstallResult> failed;
            for(const auto& e:inner) {
                const auto out=staging/fs::path(e.name).filename();
                bool written=false;
                {
                    std::ofstream file(out,std::ios::binary|std::ios::trunc);
                    const auto write=[](void* user,mz_uint64,const void* buffer,size_t n)->size_t{
                        static_cast<std::ofstream*>(user)->write(static_cast<const char*>(buffer),std::streamsize(n));
                        return n;
                    };
                    written=file && mz_zip_reader_extract_to_callback(&outer.zip,e.index,write,&file,0) && file.flush();
                }
                if(!written) {
                    if(!failed){failed=InstallResult{};failed->problem={"archive",e.name};}
                    continue;
                }
                auto one=install(out);
                fs::remove(out,error);
                if(!one.ok && !failed)failed=one;
                if(one.ok)result=std::move(one);
            }
            outer.close();
            fs::remove_all(staging,error);
            if(failed)return *failed;
            return result;
        }
    }
    const bool folder=fs::is_directory(source,error);
    if(folder) {
        if(fs::equivalent(source.parent_path(),directory(),error)){result.problem={"already",source.filename().string()};return result;}
        result=copy_folder(source,staging);
    }
    else if(is_package_file(source))result=check(source);
    else{result.problem={"not_package",source.filename().string()};return result;}
    if(!result.ok){fs::remove_all(staging,error);return result;}
    const auto target=directory()/(folder?result.id:result.id+package_extension);
    // A file from elsewhere is copied in first, under a name no listing takes for a package.
    fs::path from=folder?staging:source;
    if(!folder && !placed) {
        from=fs::path(staging.string()+package_extension+".part");
        if(!copy_package(source,from)){fs::remove(from,error);result.ok=false;result.problem={"write",target.filename().string()};return result;}
    }
    const auto abandon=[&](std::string problem) {
        std::error_code ignored;
        if(from!=source)fs::remove_all(from,ignored);
        result.ok=false;result.problem={"write",std::move(problem)};
        return result;
    };
    if(!placed)lock.lock();
    // The package of that id as it was, a folder or a file or both, goes to .trash.
    if(from!=target) {
        for(const auto& old:{directory()/result.id,directory()/(result.id+package_extension)}) {
            if(!fs::exists(old,error))continue;
            if(!result.replaced) {
                const auto was=parse_manifest(open_files(old)->read_json("mod.json"));
                if(const auto* m=std::get_if<Manifest>(&was))result.replaced=m->version;
            }
            if(!to_trash(old,old.filename().string()))return abandon(old.filename().string());
        }
        fs::rename(from,target,error);
        if(error)return abandon(error.message());
    }
    listed.reset();
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

bool remove_failed(const std::string& file_name) {
    std::lock_guard lock(mutex);
    if(directory().empty())return false;
    // Only a name the last listing reported as failed, so no path reaches outside the folder.
    const auto it=std::find_if(loose.begin(),loose.end(),[&](const LooseResult& r){return !r.result.ok && r.file.filename().string()==file_name;});
    if(it==loose.end())return false;
    std::error_code error;
    if(fs::exists(it->file,error) && !to_trash(it->file,file_name))return false;
    loose.erase(it);
    ++changes;
    return true;
}

bool remove(const std::string& id) {
    std::lock_guard lock(mutex);
    if(directory().empty() || !campaign_library::valid_id(id))return false;
    std::error_code error;
    bool any=false;
    for(const auto& where:{directory()/id,directory()/(id+package_extension)}) {
        if(!fs::exists(where,error))continue;
        if(!to_trash(where,where.filename().string()))return false;
        any=true;
    }
    if(!any)return false;
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
