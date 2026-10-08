#include "runtime.hpp"
#include "save_library.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <cctype>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <random>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
extern char** environ;
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace srw64::app {
namespace {
fs::path utf8_path(std::string_view value) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}
std::string token() {
    std::ostringstream text;
    text<<std::hex<<std::chrono::system_clock::now().time_since_epoch().count()<<'-'<<std::random_device{}();
    return text.str();
}
// The runtime's copy of the ROM in a run's data (recomp::register_config_path), about 32 MB.
void remove_rom_copies(const fs::path& run) {
    std::error_code error;
    for (fs::directory_iterator file(run/"runtime-data",error),end;!error && file!=end;file.increment(error))
        if (file->path().extension()==".z64" && file->is_regular_file(error) && !file->is_symlink(error))
            fs::remove(file->path(),error);
}
// A session directory's name: token(), the creation time in hex, '-', a random number.
std::optional<uint64_t> session_time(const std::string& name) {
    const auto dash=name.find('-');
    if (dash==0 || dash==std::string::npos || name.size()>80 ||
        !std::all_of(name.begin(),name.end(),[](char c){return std::isxdigit(uint8_t(c)) || c=='-';})) return {};
    uint64_t value=0;
    const auto [end,error]=std::from_chars(name.data(),name.data()+dash,value,16);
    if (error!=std::errc() || end!=name.data()+dash) return {};
    return value;
}
// Each run leaves its session (logs, the card it started from, the runtime's ROM copy):
// only the latest few stay, and the one last-session.txt names (the card from before
// saves/, which migrate_sessions reads). The ones kept lose their ROM copies.
void prune_sessions(const fs::path& user_dir, const fs::path& current) {
    constexpr size_t keep=3;
    std::string pointed;
    try { pointed=read_text(user_dir/"last-session.txt",128); } catch (const std::exception&) {}
    while (!pointed.empty() && (pointed.back()=='\n' || pointed.back()=='\r')) pointed.pop_back();
    std::vector<std::pair<uint64_t,fs::path>> older;
    std::error_code error;
    for (fs::directory_iterator entry(user_dir/"sessions",error),end;!error && entry!=end;entry.increment(error)) {
        const auto time=session_time(entry->path().filename().string());
        if (!time || entry->path()==current || entry->is_symlink(error) || !entry->is_directory(error)) continue;
        older.emplace_back(*time,entry->path());
    }
    std::sort(older.begin(),older.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    for (size_t i=0;i<older.size();++i) {
        const auto& path=older[i].second;
        if (i<keep || path.filename()==pointed) remove_rom_copies(path/"run");
        else fs::remove_all(path,error);
    }
}
void require_save(const fs::path& path) {
    if (!fs::is_regular_file(path) || fs::is_symlink(path) || fs::file_size(path)!=32768)
        throw std::runtime_error("SRAM must be a regular 32 KiB file: "+path.string());
}
void replace_file(const fs::path& source,const fs::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::system_error(GetLastError(),std::system_category(),"Cannot publish file");
#else
    fs::rename(source,destination);
#endif
}
}

std::string usage() {
    return "Usage: srw64-gfx-host --play --rom ROM [--content CONTENT_DIR] [--user-dir DIR]\n"
           "       [--language LOCALE] [--rules original|fixed|all] [--resolution-scale 1..8]\n"
           "       [--new-game | --import-save SAVE] [--mute] [--campaign CAMPAIGN.json] [--debug]\n"
           "       srw64-gfx-host --play --export-save FILE [--export-format FORMAT] [--user-dir DIR]\n"
           "       srw64-gfx-host --play --import-save SAVE [--user-dir DIR]\n"
           "--import-save takes a 32 KiB SRAM (ares .ram, Project64/mupen64plus .sra) or a\n"
           "RetroArch .srm; the old card's slots are kept as extended slots. Without --rom it\n"
           "only imports and starts nothing.\n"
           "--export-save writes the card as ares, project64, mupen64plus or retroarch\n"
           "(by default from the extension: .ram/.sav ares, .sra project64, .srm retroarch).\n"
           "Without --content, imports the matching ROM into a versioned local cache.\n"
           "--content keeps the developer-only prepared-content path.\n"
           "--campaign plays a compiled custom campaign (mini_stage.py campaign) with its own\n"
           "saves in DIR/campaigns/<id>/saves.\n"
           "--debug opens the debug interface for tools/recomp/debug (MCP) or tools/release/linux/attach.py:\n"
           "loopback TCP, with the port and token in debug.json in the session's run directory.\n"
           "This entry never runs Python, Git, CMake, Ninja or the recompilers.\n";
}
Options parse_options(std::span<const std::string_view> args) {
    Options options;
    std::vector<std::string_view> seen;
    for (size_t i=0;i<args.size();++i) {
        const auto key=args[i];
        if (std::find(seen.begin(),seen.end(),key)!=seen.end())
            throw std::runtime_error("Repeated option: "+std::string(key));
        seen.push_back(key);
        if (key=="--new-game") options.new_game=true;
        else if (key=="--mute") options.mute=true;
        else if (key=="--debug") options.debug=true;
        else {
            if (i+1==args.size() || args[i+1].empty() || args[i+1].starts_with("--"))
                throw std::runtime_error("Missing value for "+std::string(key));
            const std::string value(args[++i]);
            if (key=="--rom") options.rom=utf8_path(value);
            else if (key=="--content") options.content=utf8_path(value);
            else if (key=="--user-dir") options.user_dir=utf8_path(value);
            else if (key=="--import-save") options.import_save=utf8_path(value);
            else if (key=="--export-save") options.export_save=utf8_path(value);
            else if (key=="--campaign") options.campaign=utf8_path(value);
            else if (key=="--export-format") {
                if (value!="ares" && value!="project64" && value!="mupen64plus" && value!="retroarch")
                    throw std::runtime_error("Unknown save format: "+value);
                options.export_format=value;
            }
            else if (key=="--language") options.language=value;
            else if (key=="--rules") {
                if (value!="original" && value!="fixed" && value!="all")
                    throw std::runtime_error("Unknown rule preset: "+value);
                options.rules=value;
            } else if (key=="--resolution-scale") {
                const auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),options.resolution_scale);
                if (error!=std::errc{} || end!=value.data()+value.size() || options.resolution_scale<1 || options.resolution_scale>8)
                    throw std::runtime_error("Resolution scale must be in 1..8");
            } else throw std::runtime_error("Unknown option: "+std::string(key));
        }
    }
    if (!options.export_format.empty() && options.export_save.empty()) throw std::runtime_error("--export-format needs --export-save");
    if (!options.export_save.empty()) {
        if (!options.rom.empty() || !options.content.empty() || options.new_game || !options.import_save.empty() ||
            !options.language.empty() || options.rules || options.resolution_scale || options.mute || options.debug || !options.campaign.empty())
            throw std::runtime_error("--export-save only takes --export-format and --user-dir");
        options.export_save=fs::absolute(options.export_save);
        if (!options.user_dir.empty()) options.user_dir=fs::absolute(options.user_dir);
        return options;
    }
    // --import-save alone takes the card in and starts nothing.
    if (options.rom.empty() && !options.import_save.empty()) {
        if (!options.content.empty() || options.new_game || !options.language.empty() || options.rules || options.resolution_scale || options.mute ||
            options.debug || !options.campaign.empty())
            throw std::runtime_error("--import-save without --rom only takes --user-dir");
        options.import_save=fs::absolute(options.import_save);
        if (!options.user_dir.empty()) options.user_dir=fs::absolute(options.user_dir);
        return options;
    }
    if (options.rom.empty()) throw std::runtime_error("--rom is required");
    if (options.new_game && !options.import_save.empty()) throw std::runtime_error("Choose --new-game or --import-save, not both");
    if (!options.campaign.empty() && !options.import_save.empty()) throw std::runtime_error("A campaign keeps its own saves; --import-save is for the main game");
    options.rom=fs::absolute(options.rom);
    if (!options.campaign.empty()) options.campaign=fs::absolute(options.campaign);
    if (!options.content.empty()) options.content=fs::absolute(options.content);
    if (!options.user_dir.empty()) options.user_dir=fs::absolute(options.user_dir);
    if (!options.import_save.empty()) options.import_save=fs::absolute(options.import_save);
    return options;
}

std::string import_save(const Options& options) {
    const auto root=fs::absolute(options.user_dir.empty()?default_user_dir():options.user_dir);
    fs::create_directories(root);
    UserLock lock(root);
    const auto imported=SaveLibrary(root/"saves").import_cartridge(options.import_save);
    std::string kept;
    for (const auto n:imported.kept) kept+=(kept.empty()?"":",")+std::to_string(n);
    return std::string(sram::name(imported.detected.order))+(imported.detected.retroarch?" retroarch":"")+" kept_slots="+(kept.empty()?"none":kept);
}
fs::path export_save(const Options& options) {
    std::optional<sram::Format> format=sram::format_for(options.export_save.extension().string());
    for (const auto known:{sram::Format::ares,sram::Format::project64,sram::Format::mupen64plus,sram::Format::retroarch})
        if (options.export_format==sram::name(known)) format=known;
    if (!format) throw std::runtime_error("Name the emulator with --export-format: the extension does not say");
    // Reading needs no lock: the card is only ever replaced by a rename.
    return SaveLibrary((options.user_dir.empty()?default_user_dir():options.user_dir)/"saves").export_cartridge(options.export_save,*format);
}
fs::path default_user_dir(Platform platform,const EnvironmentLookup& lookup) {
    if (platform==Platform::Windows) {
        const auto local=lookup("LOCALAPPDATA");
        if (local.empty() || !utf8_path(local).is_absolute()) throw std::runtime_error("LOCALAPPDATA is unavailable; pass --user-dir");
        return utf8_path(local)/"SRW64Recomp";
    }
    if (platform==Platform::Linux) {
        const auto xdg=utf8_path(lookup("XDG_DATA_HOME"));
        if (!xdg.empty() && xdg.is_absolute()) return xdg/"srw64-recomp";
    }
    const auto home=utf8_path(lookup("HOME"));
    if (home.empty() || !home.is_absolute()) throw std::runtime_error("HOME is unavailable; pass --user-dir");
    return platform==Platform::MacOS ? home/"Library/Application Support/SRW64Recomp" : home/".local/share/srw64-recomp";
}
fs::path default_user_dir() {
#ifdef _WIN32
    constexpr auto platform=Platform::Windows;
#elif defined(__APPLE__)
    constexpr auto platform=Platform::MacOS;
#else
    constexpr auto platform=Platform::Linux;
#endif
    return default_user_dir(platform,[](const char* key){
#ifdef _WIN32
        const std::wstring name(key,key+std::char_traits<char>::length(key));
        const auto* value=_wgetenv(name.c_str());
        if(!value)return std::string{};
        const auto utf8=fs::path(value).u8string();
        return std::string(reinterpret_cast<const char*>(utf8.data()),utf8.size());
#else
        const auto* value=std::getenv(key);return value?std::string(value):std::string{};
#endif
    });
}
fs::path content_path(const fs::path& root,const std::string& relative) {
    const auto path=utf8_path(relative);
    // Reject Windows drive/UNC spelling on every OS as well as native traversal.
    if (relative.empty() || relative.find('\\')!=std::string::npos || relative.find(':')!=std::string::npos || path.is_absolute())
        throw std::runtime_error("Content paths must be portable and relative");
    for (const auto& part:path) if (part==".." || part==".") throw std::runtime_error("Content path traversal");
    const auto base=fs::canonical(root);
    const auto target=fs::canonical(base/path);
    const auto within=target.lexically_relative(base);
    if (within.empty() || *within.begin()==".." || !fs::is_regular_file(target))
        throw std::runtime_error("Content file escapes its directory or is not a file");
    return target;
}
std::string read_text(const fs::path& path,size_t limit) {
    if (!fs::is_regular_file(path) || fs::file_size(path)>limit) throw std::runtime_error("Invalid or oversized file: "+path.string());
    std::ifstream file(path,std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(file)),{});
    if (!file || data.size()>limit) throw std::runtime_error("Cannot read file: "+path.string());
    return data;
}
bool valid_campaign_id(const std::string& id) {
    return !id.empty() && id.size()<=64 && id!="." && id!=".." &&
        std::all_of(id.begin(),id.end(),[](char c){return std::isalnum(static_cast<unsigned char>(c)) || c=='.' || c=='_' || c=='-';});
}
void atomic_write(const fs::path& path,std::string_view text) {
    // Append an ASCII suffix to the native path, without converting a Windows
    // UTF-16 filename through the active ANSI code page.
    auto temporary=path;
    temporary += fs::path(".tmp-"+token());
    try {
        std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
        file.exceptions(std::ios::badbit|std::ios::failbit);
        file.write(text.data(),static_cast<std::streamsize>(text.size())); file.flush(); file.close();
        replace_file(temporary,path);
    } catch (...) { std::error_code ignored;fs::remove(temporary,ignored);throw; }
}
fs::path executable_path() {
#ifdef _WIN32
    std::wstring buffer(32768,L'\0');
    const auto size=GetModuleFileNameW(nullptr,buffer.data(),DWORD(buffer.size()));
    if(!size || size>=buffer.size())return {};
    buffer.resize(size);return fs::path(buffer);
#elif defined(__APPLE__)
    uint32_t size=0;_NSGetExecutablePath(nullptr,&size);
    std::string buffer(size,'\0');
    if(_NSGetExecutablePath(buffer.data(),&size)!=0)return {};
    std::error_code error;auto path=fs::canonical(buffer.c_str(),error);
    return error?fs::path{}:path;
#else
    std::error_code error;auto path=fs::read_symlink("/proc/self/exe",error);
    return error?fs::path{}:path;
#endif
}
fs::path bundled_resource(const std::string& name) {
#ifdef __ANDROID__
    // No program file beside its resources: the app unpacks them from the APK and names
    // the folder (tools/release/android, SRW64Activity).
    if (const char* root=std::getenv("SRW64_RESOURCE_DIR"); root && *root) {
        std::error_code error;
        const auto candidate=utf8_path(root)/name;
        return fs::exists(candidate,error)?candidate:fs::path{};
    }
#endif
    const auto executable=executable_path();
    if(executable.empty())return {};
    std::error_code error;
    // A macOS bundle keeps resources in Contents/Resources beside Contents/MacOS.
    for(const auto& candidate:{executable.parent_path().parent_path()/"Resources"/name,executable.parent_path()/name})
        if(fs::exists(candidate,error))return candidate;
    return {};
}
fs::path beside_game(const std::string& name) {
#ifdef __ANDROID__
    return {};
#else
    auto folder=executable_path().parent_path();
    if(folder.empty())return {};
#ifdef __APPLE__
    // <folder>/Marchwind64.app/Contents/MacOS/<executable>. A downloaded app that was
    // not moved runs translocated (a read-only copy elsewhere), with nothing beside it.
    if(folder.filename()=="MacOS" && folder.parent_path().filename()=="Contents"
       && folder.parent_path().parent_path().extension()==".app")
        folder=folder.parent_path().parent_path().parent_path();
#endif
    std::error_code error;
    const auto candidate=folder/name;
    return fs::exists(candidate,error)?candidate:fs::path{};
#endif
}
void set_environment(const std::string& key,const std::string& value) {
#ifdef _WIN32
    if (_putenv_s(key.c_str(),value.c_str())!=0) throw std::runtime_error("Cannot set environment: "+key);
#else
    if (setenv(key.c_str(),value.c_str(),1)!=0) throw std::runtime_error("Cannot set environment: "+key);
#endif
}
void clear_runtime_environment() {
    std::vector<std::string> keys;
#ifdef _WIN32
    char* block=GetEnvironmentStringsA();
    if (!block) throw std::runtime_error("Cannot enumerate environment");
    for (const char* p=block;*p;p+=std::char_traits<char>::length(p)+1) {
        const std::string value(p);
        auto key=value.substr(0,value.find('='));
        auto upper=key;
        for (auto& c:upper) if(c>='a' && c<='z')c=char(c-'a'+'A');
        if (upper.starts_with("SRW64_")) keys.push_back(key);
    }
    FreeEnvironmentStringsA(block);
#else
    for (auto** p=environ;*p;++p) {
        const std::string value(*p);
        if (value.starts_with("SRW64_")) keys.push_back(value.substr(0,value.find('=')));
    }
#endif
    // Set by the Android app for bundled_resource, not a development probe.
    std::erase(keys,std::string("SRW64_RESOURCE_DIR"));
#ifdef __ANDROID__
    // A development launch (--ez debug true) keeps the variables it set (--esa env).
    if (std::getenv("SRW64_DEBUG")) return;
#endif
    for (const auto& key:keys) {
#ifdef _WIN32
        if (_putenv_s(key.c_str(),"")!=0) throw std::runtime_error("Cannot clear environment");
#else
        if (unsetenv(key.c_str())!=0) throw std::runtime_error("Cannot clear environment");
#endif
    }
}

namespace {
// A game that was just closed can hold the lock for a few seconds more while it exits,
// so a quick relaunch waits for it before giving up.
template<class Try> bool wait_for_lock(Try&& attempt) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while (!attempt()) {
        if (std::chrono::steady_clock::now()>=deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return true;
}
constexpr const char* play_lock_held=
    "Marchwind64 is already running with this user directory, or a closed game has not finished exiting.\n"
    "Switch to the open game, or end Marchwind64 in Task Manager (Activity Monitor on a Mac) and start again.";
}
struct UserLock::Handle {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
    explicit Handle(const fs::path& path) {
        handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                           nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (handle==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open play lock");
        if (!wait_for_lock([&] { OVERLAPPED offset{};
                return LockFileEx(handle,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&offset)!=0; })) {
            CloseHandle(handle);handle=INVALID_HANDLE_VALUE;
            throw std::runtime_error(play_lock_held);
        }
    }
    ~Handle() { if(handle!=INVALID_HANDLE_VALUE){OVERLAPPED offset{};UnlockFileEx(handle,0,1,0,&offset);CloseHandle(handle);} }
#else
    int fd=-1;
    explicit Handle(const fs::path& path) {
        fd=open(path.c_str(),O_RDWR|O_CREAT|O_CLOEXEC,0600);
        if (fd<0) throw std::runtime_error("Cannot open play lock");
        if (!wait_for_lock([&] { return flock(fd,LOCK_EX|LOCK_NB)==0; })) { close(fd);fd=-1;throw std::runtime_error(play_lock_held); }
    }
    ~Handle() { if(fd>=0)close(fd); }
#endif
};
UserLock::UserLock(const fs::path& user_dir):handle(std::make_unique<Handle>(user_dir/"active.lock")) {}
UserLock::~UserLock()=default;
Session::Session(const Options& options) {
    root=fs::absolute(options.user_dir.empty()?default_user_dir():options.user_dir);
    fs::create_directories(root);
    lock=std::make_unique<UserLock>(root);
    // The card lives in saves/ (save_library.hpp); sessions/ keeps each run's copy. A
    // campaign's saves name scenes its stages borrow, so they live apart from the game's.
    if (!options.campaign.empty() && !valid_campaign_id(options.campaign_id))
        throw std::runtime_error("A campaign id is 1-64 letters, digits, '.', '_' or '-'");
    saves_root=options.campaign.empty()?root/"saves":root/"campaigns"/options.campaign_id/"saves";
    fs::create_directories(saves_root);
    SaveLibrary saves(saves_root);
    std::optional<fs::path> source;
    std::string source_hash;
    if (!options.import_save.empty()) {
        const auto imported=saves.import_cartridge(options.import_save);
        std::fprintf(stderr,"SRW64_SAVE_IMPORT %s%s kept_slots=%zu\n",std::string(sram::name(imported.detected.order)).c_str(),
                     imported.detected.retroarch?" retroarch":"",imported.kept.size());
    } else if (options.campaign.empty()) {   // old sessions hold the main game's card
        try { saves.migrate_sessions(root); }
        catch (...) { if (!options.new_game) throw; }  // A new game does not need the old card.
    }
    // A new game starts from a blank card, which the host publishes as soon as the game
    // formats it: the old card's slots become extended slots first.
    if (options.new_game) saves.keep_cartridge_slots({});
    if (!options.new_game && saves.read_cartridge()) { source=saves.cartridge();source_hash=sha256_file(*source); }
    fs::create_directories(root/"sessions");
    for (unsigned attempt=0;attempt<16;++attempt) {
        id=token();directory=root/"sessions"/id;
        if (fs::create_directory(directory)) break;
        directory.clear();
    }
    if (directory.empty()) throw std::runtime_error("Cannot allocate a unique play session");
    prune_sessions(root,directory);   // under the user lock: no other game uses them
    if (source) {
        initial=directory/"initial.bin";
        fs::copy_file(*source,*initial);
        require_save(*initial);
        if (sha256_file(*initial)!=source_hash) throw std::runtime_error("Save changed during staging");
    }
}
Session::~Session()=default;
void Session::release_run() const { remove_rom_copies(output_dir()); }
bool Session::commit_save(const fs::path& host_save) {
    if (published) throw std::runtime_error("Session already committed");
    if (!fs::exists(host_save)) return false;
    require_save(host_save);
    const auto bytes=read_text(host_save,32768);
    const std::span card{reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size()};
    if (!sram::problems(card).empty()) throw std::runtime_error("The host wrote an unusable save: "+host_save.string());
    Sha256 digest;digest.update(card);
    const auto expected=digest.finish();
    const auto save=directory/"save.bin";
    fs::copy_file(host_save,save);
    require_save(save);
    if (sha256_file(save)!=expected) throw std::runtime_error("Host save changed before publication");
    SaveLibrary(saves_root).publish_cartridge(card);
    atomic_write(directory/"save.sha256",sha256_file(save)+"\n");
    atomic_write(root/"last-session.txt",id+"\n");
    published=true;
    return true;
}
}
