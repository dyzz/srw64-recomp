#include "runtime.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <random>
#include <stdexcept>
#include <system_error>
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
bool safe_token(std::string_view value) {
    return !value.empty() && value.size()<=80 &&
        std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||c=='-';});
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
           "       [--new-game | --import-save SRAM] [--mute]\n"
           "Without --content, imports the matching ROM into a versioned local cache.\n"
           "--content keeps the developer-only prepared-content path.\n"
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
        else {
            if (i+1==args.size() || args[i+1].empty() || args[i+1].starts_with("--"))
                throw std::runtime_error("Missing value for "+std::string(key));
            const std::string value(args[++i]);
            if (key=="--rom") options.rom=utf8_path(value);
            else if (key=="--content") options.content=utf8_path(value);
            else if (key=="--user-dir") options.user_dir=utf8_path(value);
            else if (key=="--import-save") options.import_save=utf8_path(value);
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
    if (options.rom.empty()) throw std::runtime_error("--rom is required");
    if (options.new_game && !options.import_save.empty()) throw std::runtime_error("Choose --new-game or --import-save, not both");
    options.rom=fs::absolute(options.rom);
    if (!options.content.empty()) options.content=fs::absolute(options.content);
    if (!options.user_dir.empty()) options.user_dir=fs::absolute(options.user_dir);
    if (!options.import_save.empty()) options.import_save=fs::absolute(options.import_save);
    return options;
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
    for (const auto& key:keys) {
#ifdef _WIN32
        if (_putenv_s(key.c_str(),"")!=0) throw std::runtime_error("Cannot clear environment");
#else
        if (unsetenv(key.c_str())!=0) throw std::runtime_error("Cannot clear environment");
#endif
    }
}

struct Session::Lock {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
    explicit Lock(const fs::path& path) {
        handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                           nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (handle==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open play lock");
        OVERLAPPED offset{};
        if (!LockFileEx(handle,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&offset)) {
            CloseHandle(handle);handle=INVALID_HANDLE_VALUE;
            throw std::runtime_error("Another game is using this user directory (or locking failed)");
        }
    }
    ~Lock() { if(handle!=INVALID_HANDLE_VALUE){OVERLAPPED offset{};UnlockFileEx(handle,0,1,0,&offset);CloseHandle(handle);} }
#else
    int fd=-1;
    explicit Lock(const fs::path& path) {
        fd=open(path.c_str(),O_RDWR|O_CREAT|O_CLOEXEC,0600);
        if (fd<0) throw std::runtime_error("Cannot open play lock");
        if (flock(fd,LOCK_EX|LOCK_NB)!=0) { close(fd);fd=-1;throw std::runtime_error("Another game is using this user directory (or locking failed)"); }
    }
    ~Lock() { if(fd>=0)close(fd); }
#endif
};
Session::Session(const Options& options) {
    root=fs::absolute(options.user_dir.empty()?default_user_dir():options.user_dir);
    fs::create_directories(root);
    lock=std::make_unique<Lock>(root/"active.lock");
    std::optional<fs::path> source;
    std::string source_hash;
    if (!options.import_save.empty()) { require_save(options.import_save);source=options.import_save;source_hash=sha256_file(*source); }
    else if (!options.new_game && fs::exists(root/"last-session.txt")) {
        auto previous=read_text(root/"last-session.txt",128);
        if (!previous.empty() && previous.back()=='\n') previous.pop_back();
        if (!safe_token(previous)) throw std::runtime_error("Invalid last-session pointer; use --import-save to recover explicitly");
        const auto old=root/"sessions"/previous;
        if (fs::is_symlink(old)) throw std::runtime_error("Refusing a symlinked save session");
        source=old/"save.bin";require_save(*source);
        auto expected=read_text(old/"save.sha256",65);
        if (!expected.empty() && expected.back()=='\n')expected.pop_back();
        source_hash=sha256_file(*source);
        if (source_hash!=expected) throw std::runtime_error("Save integrity check failed; use --import-save to recover explicitly");
    }
    fs::create_directories(root/"sessions");
    for (unsigned attempt=0;attempt<16;++attempt) {
        id=token();directory=root/"sessions"/id;
        if (fs::create_directory(directory)) break;
        directory.clear();
    }
    if (directory.empty()) throw std::runtime_error("Cannot allocate a unique play session");
    if (source) {
        initial=directory/"initial.bin";
        fs::copy_file(*source,*initial);
        require_save(*initial);
        if (sha256_file(*initial)!=source_hash) throw std::runtime_error("Save changed during staging");
    }
}
Session::~Session()=default;
bool Session::commit_save(const fs::path& host_save) {
    if (published) throw std::runtime_error("Session already committed");
    if (!fs::exists(host_save)) return false;
    require_save(host_save);
    const auto save=directory/"save.bin";
    const auto expected=sha256_file(host_save);
    fs::copy_file(host_save,save);
    require_save(save);
    if (sha256_file(save)!=expected) throw std::runtime_error("Host save changed before publication");
    atomic_write(directory/"save.sha256",sha256_file(save)+"\n");
    atomic_write(root/"last-session.txt",id+"\n");
    published=true;
    return true;
}
}
