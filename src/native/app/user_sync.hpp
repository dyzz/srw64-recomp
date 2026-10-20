#pragma once
#include "runtime.hpp"

namespace srw64::app {
using SyncConfirm=std::function<bool(const std::string&,const std::string&,const std::string&)>;
// Only absent top-level groups are imported. Existing local settings and save trees win.
// Both operations take the real user-directory locks and reject symlinked data trees.
void import_missing_user_data(const fs::path& local,const fs::path& appdata);
// Mirror present groups, including deletions within a save tree. Returns false on refusal.
// Different existing saves require confirmation; every replaced group is retained in
// the system directory under sync-backups/<id>. Settings, saves and campaigns only: no ROMs, caches or locks.
bool sync_user_data(const fs::path& local,const fs::path& appdata,
                    const std::string& locale,const SyncConfirm& confirm);
}
