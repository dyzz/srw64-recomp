#include "user_sync.hpp"
#include "save_library.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <map>

namespace srw64::app {
namespace {
constexpr std::array groups={"presentation.json","rules.json","input.json","saves","campaigns"};
using Inventory=std::map<fs::path,std::string>;
void safe_root(const fs::path& root) {
    // Check ancestors too: directory_iterator does not treat a symlinked parent as an entry.
    for(auto at=fs::absolute(root).lexically_normal();!at.empty();) {
        if(fs::is_symlink(at))throw std::runtime_error("User-data synchronization refuses symlinks: "+at.string());
        const auto parent=at.parent_path();if(parent==at)break;at=parent;
    }
}
Inventory inventory(const fs::path& path) {
    Inventory result;
    const auto add=[&](const fs::path& entry,const fs::path& relative) {
        const auto status=fs::symlink_status(entry);
        if(fs::is_symlink(status))throw std::runtime_error("User-data synchronization refuses symlinks: "+entry.string());
        if(fs::is_directory(status))result[relative]="directory";
        else if(fs::is_regular_file(status))result[relative]=sha256_file(entry);
        else throw std::runtime_error("Unsupported user-data entry: "+entry.string());
    };
    if(!fs::exists(path) && !fs::is_symlink(path))return result;
    add(path,{});
    if(fs::is_directory(path))
        for(const auto& entry:fs::recursive_directory_iterator(path))add(entry.path(),entry.path().lexically_relative(path));
    return result;
}
void copy_tree(const fs::path& source,const fs::path& destination) {
    for(const auto& [relative,digest]:inventory(source)) {
        const auto target=relative.empty()?destination:destination/relative;
        if(digest=="directory")fs::create_directories(target);
        else {
            fs::create_directories(target.parent_path());
            fs::copy_file(relative.empty()?source:source/relative,target);
            if(sha256_file(target)!=digest)throw std::runtime_error("User-data changed while staging: "+source.string());
        }
    }
}
struct Locks {
    std::unique_ptr<UserLock> first,second;
    Locks(const fs::path& local,const fs::path& appdata) {
        safe_root(local);safe_root(appdata);
        fs::create_directories(local);fs::create_directories(appdata);
        const auto a=fs::canonical(local),b=fs::canonical(appdata);
        // Aliases/nesting would cause recursive backups or sync into the live directory.
        for(const auto& pair:{std::pair(a,b),std::pair(b,a)}) {
            const auto relative=pair.first.lexically_relative(pair.second);
            if(!relative.empty() && !relative.has_root_path() && *relative.begin()!="..")
                throw std::runtime_error("Local and system user-data directories must be separate");
        }
        first=std::make_unique<UserLock>(std::min(a,b),std::chrono::milliseconds(0));
        second=std::make_unique<UserLock>(std::max(a,b),std::chrono::milliseconds(0));
    }
};
fs::path unique_folder(const fs::path& parent) {
    safe_root(parent);fs::create_directories(parent);
    const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<100;++n) {
        const auto path=parent/(std::to_string(stamp)+"-"+std::to_string(n));
        if(fs::create_directory(path))return path;
    }
    throw std::runtime_error("Cannot allocate synchronization backup");
}
fs::path legacy_session(const fs::path& root) {
    inventory(root/"last-session.txt");
    auto pointer=read_text(root/"last-session.txt",128);
    if(!pointer.empty() && pointer.back()=='\n')pointer.pop_back();
    if(pointer.empty() || pointer.find_first_not_of("0123456789abcdef-")!=std::string::npos)
        throw std::runtime_error("Invalid legacy save pointer");
    const auto old=root/"sessions"/pointer;
    safe_root(old);inventory(old);
    auto digest=read_text(old/"save.sha256",65);
    if(!digest.empty() && digest.back()=='\n')digest.pop_back();
    if(sha256_file(old/"save.bin")!=digest)throw std::runtime_error("Legacy save integrity check failed");
    return old;
}
struct Change {std::string group;bool existed{},moved{},installed{};};
// Copy first; then rename complete groups on the destination volume. If a rename fails,
// roll back all groups already installed. Originals remain available after a successful sync.
void install(const fs::path& source,const fs::path& destination,std::vector<Change> changes,
             const fs::path& legacy = {}) {
    if(changes.empty())return;
    const auto stage=unique_folder(destination/"sync-staging");
    fs::path backup;
    try {
        for(const auto& change:changes)copy_tree(source/change.group,stage/change.group);
        backup=unique_folder(destination/"sync-backups");
        atomic_write(backup/"README.txt","Marchwind64 user-data backup before synchronization.\n"
            "Close the game before restoring the files and directories in this folder.\n");
        if(!legacy.empty()) {
            copy_tree(destination/"last-session.txt",backup/"last-session.txt");
            for(const auto* name:{"save.bin","save.sha256"})
                copy_tree(legacy/name,backup/"sessions"/legacy.filename()/name);
        }
        for(auto& change:changes) {
            if(change.existed){fs::rename(destination/change.group,backup/change.group);change.moved=true;}
            fs::rename(stage/change.group,destination/change.group);change.installed=true;
        }
        fs::remove_all(stage);
    } catch(...) {
        const auto failure=std::current_exception();
        try {
            for(auto it=changes.rbegin();it!=changes.rend();++it) {
                if(it->installed)fs::remove_all(destination/it->group);
                if(it->moved)fs::rename(backup/it->group,destination/it->group);
            }
            fs::remove_all(stage);
        } catch(...) {
            throw std::runtime_error("Synchronization rollback could not finish. Recover from: "+backup.string());
        }
        std::rethrow_exception(failure);
    }
}
std::string question(const std::string& locale,const fs::path& destination) {
    const auto raw=destination.u8string();const std::string path(reinterpret_cast<const char*>(raw.data()),raw.size());
    if(locale=="zh-Hans")return "游戏目录与系统用户目录的存档不同。是否用游戏目录的存档覆盖系统用户目录？\n"
        "覆盖前会备份系统用户目录中被替换的数据。取消则保留两份数据，不进行同步。\nSystem user data: "+path;
    if(locale=="ja")return "ゲームフォルダーとシステムのユーザーフォルダーのセーブデータが異なります。\n"
        "ゲームフォルダーのデータでシステム側を置き換えますか？置き換える前にバックアップします。\n"
        "キャンセルすると両方のデータを保持し、同期しません。\nSystem user data: "+path;
    return "Game-folder saves differ from the system user-data directory. Replace its saves with the game-folder data?\n"
        "The replaced system user-data files will be backed up first. Cancel keeps both copies and skips synchronization.\nSystem user data: "+path;
}
}
void import_missing_user_data(const fs::path& local,const fs::path& appdata) {
    if(!fs::exists(appdata))return;
    Locks locks(local,appdata);
    // Legacy saves were checked by Session before the current save library existed.
    // Migrate on the local side only, leaving the original AppData completely intact.
    std::vector<Change> changes;
    for(const auto* group:groups) {
        const auto from=inventory(appdata/group),to=inventory(local/group);
        if(to.empty() && !from.empty())changes.push_back({group});
    }
    if(inventory(local/"saves").empty() && inventory(appdata/"saves").empty() &&
       fs::exists(appdata/"last-session.txt")) {
        // Reuse the existing integrity checks without copying caches or all old sessions.
        const auto old=legacy_session(appdata);
        const auto pointer=old.filename().string();
        const auto temporary=unique_folder(local/"sync-staging");
        try {
            fs::create_directories(temporary/"sessions"/pointer);
            for(const auto* name:{"save.bin","save.sha256"})fs::copy_file(old/name,temporary/"sessions"/pointer/name);
            atomic_write(temporary/"last-session.txt",pointer+"\n");
            SaveLibrary(temporary/"saves").migrate_sessions(temporary);
            install(temporary,local,{{"saves"}});
            fs::remove_all(temporary);
        } catch(...) {std::error_code error;fs::remove_all(temporary,error);throw;}
    }
    install(appdata,local,std::move(changes));
}
bool sync_user_data(const fs::path& local,const fs::path& appdata,const std::string& locale,const SyncConfirm& confirm) {
    Locks locks(local,appdata);
    std::vector<Change> changes;
    bool save_conflict=false;
    fs::path legacy;
    for(const auto* group:groups) {
        const auto from=inventory(local/group),to=inventory(appdata/group);
        if(from.empty() || from==to)continue;
        changes.push_back({group,!to.empty()});
        if(std::string_view(group)=="saves" && to.empty() && fs::exists(appdata/"last-session.txt")) {
            legacy=legacy_session(appdata);save_conflict=true;
        }
        if((std::string_view(group)=="saves" || std::string_view(group)=="campaigns") &&
           std::any_of(to.begin(),to.end(),[](const auto& entry){return entry.second!="directory";}))save_conflict=true;
    }
    if(save_conflict && (!confirm || !confirm(question(locale,appdata),"Back up and replace","Keep both")))return false;
    install(local,appdata,std::move(changes),legacy);
    return true;
}
}
