# Windows/Linux portable data / 桌面便携数据

This change addresses [Issue #8](https://github.com/dyzz/srw64-recomp/issues/8).
It requires a rebuilt executable as well as the updated launcher scripts.

## Using it

Close Marchwind64, then create an empty folder named `user-data` beside
`Marchwind64.exe` (Windows) or `srw64` (Linux). Launch the executable or its
`Marchwind64.cmd` / `marchwind64.sh` script. Move the **whole game
folder**, including `user-data` and your ROM, to another PC or drive letter.
The folder is relative to the executable, not the terminal or shortcut's working
directory. Release packages must **not** include an empty `user-data` folder by
default: its presence is the player's opt-in.

| Situation | Behavior |
| --- | --- |
| No `user-data` folder | Existing system user-data directory; first launch in Simplified Chinese |
| `user-data` exists, including an empty folder | Local data takes precedence; missing settings and entire missing save groups are imported from the system user-data directory |
| Neither location has language settings | First portable launch uses English |
| Language settings already exist locally or are imported | Restore that language; do not force English or Chinese |
| Explicit `--language en` / `ja` / `zh-Hans` | Apply the requested language, retaining the other valid settings |
| Explicit `--user-dir DIR` | Use only DIR; no automatic import or synchronization |
| Save-only `--import-save` / `--export-save` | Retain their original system-directory default; add `--user-dir user-data` to operate on portable data |

No `portable.txt` marker is required. Existing local files win even when the system user-data directory
has a newer modification time. Missing settings are imported individually;
`saves` and `campaigns` are imported as complete groups **only when that group is
absent locally**. An existing empty local save directory counts as a local
choice and is not merged with another computer's slots. Legacy session-only saves in the system directory use the existing checksum and SRAM checks and are migrated into
the local save library without modifying their original copy.

## System directories and Linux dialogs

| Platform | System user-data directory |
| --- | --- |
| Windows | `%LOCALAPPDATA%\SRW64Recomp` |
| Linux / Steam Deck | `$XDG_DATA_HOME/srw64-recomp` when XDG_DATA_HOME is absolute; otherwise `$HOME/.local/share/srw64-recomp` |

Windows and Linux share the same selection, language, ROM-memory and sync code.
Linux preserves the existing XDG/HOME rules; an intentional system-directory
alias is resolved before locking. macOS and Android keep their existing launch
and data-directory behavior.

Linux uses the existing optional `kdialog` or `zenity` desktop utilities for ROM
selection, errors and confirmation. Arguments are passed with `posix_spawnp`,
never interpolated into a shell. If neither utility is usable, fixed-name ROM
lookup and explicit `--rom` still work; a replacement requiring confirmation is
refused and reported on stderr / console.log. Install either utility for the
complete interactive workflow. Steam Deck Game Mode dialog focus still needs
real-device testing. The shell keeps the Steam on-screen keyboard environment
setting. The add-to-Steam helper now follows local/imported language settings
when choosing the shortcut name and artwork.

## Synchronization and backups

After a normal game exit, mirror these present local groups to this computer's
system user-data directory:

- `presentation.json`, `rules.json`, `input.json`;
- the complete `saves` tree (including extended slots and autosaves);
- the complete `campaigns` tree (campaign files and their saves).

If existing system-directory saves differ, the launcher asks before replacing them.
Cancel keeps both copies and skips the entire synchronization, including
settings. Identical groups cause neither a prompt nor a backup. Deleted files
inside a present local save tree stay deleted in the synchronized copy; absent
top-level groups do not erase the system user-data directory groups.

Before replacing any group, keep its complete previous copy under
`<system user-data directory>/sync-backups/<id>`. Backups include replaced settings
as well as saves. To restore, close the game and copy the selected backup's
settings and save directories back into the intended user directory. Copy
`saves` as a complete directory rather than mixing its individual files.
For an older installation that only has session saves, differing local
saves still require confirmation; its backup retains `last-session.txt` and the
referenced session's save and digest. To restore this older layout, first move
the current `saves` directory aside, then restore those backed-up session files
and pointer; the next launch migrates that card using the existing integrity
checks. Backups are retained until the player removes them.

Both data directories are locked during import or synchronization. Copies are
staged and verified by digest before destination groups are renamed; ordinary
I/O failures roll back installed groups. This is not a power-loss transaction
across all groups: after an interruption, the backup retains the prior data for
manual recovery. Symlinks in synchronized trees are rejected. A failed launch
or nonzero host exit does not synchronize. A synchronization failure reports
where the local saves remain and does not reset the game's committed progress.
If the system user-data directory is unavailable, local play still works and exit reports the skipped
synchronization. An unavailable local folder never silently falls back to
another save library.

ROMs, `last-rom.txt`, sessions, import caches, locks, debug tokens, update state,
HD assets and dialogue overrides are **not** synchronized. ROM selection uses
`--rom`, then `SRW64_ROM`, then `rom.z64`, `rom.n64`, `rom.v64` in the selected
user directory and game folder, then a remembered path or the file picker.
Portable mode never searches the system user-data directory for a ROM. ROMs inside the game tree are
remembered relative to the user directory using a versioned preference. Existing
absolute preferences still load; ROMs outside the game tree stay absolute and
must be selected again if that external location changes. Keep your ROM inside
the moved game tree for portability.

## 中文使用说明

Windows 和 Linux 都是在游戏程序旁新建 `user-data` 文件夹后使用便携数据。
第一次从系统用户目录补入本地缺少的设置和存档组，本地已有数据优先。
把对应平台的游戏程序、ROM 和 `user-data` 整个目录一起移动即可；ROM 相对路径
采用通用 `/` 分隔符，Linux 也能读取早期 Windows 写出的 `\` 相对路径。

正常退出后，同步回本机 AppData（Windows）或 XDG/HOME 用户数据目录（Linux）。
不同存档覆盖前询问，确认后备份旧数据；取消则两边都保留，不同步。
同步失败不影响游戏目录中已经保存的进度。Linux 交互窗口需要 kdialog 或 zenity，
缺少这些工具时不会自动批准覆盖。没有 `user-data` 的普通安装保持原行为；显式
`--user-dir` 不参与自动同步。便携全新数据默认英语，已有或导入的语言照常恢复。

## Automated regression checks

The ROM-free foundation build runs the real C++ controller, settings loader,
Session locks and save-staging code with synthetic ROM/content/host fixtures:

```sh
cmake -S . -B build/portable-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/portable-tests --parallel
ctest --test-dir build/portable-tests --output-on-failure
```

For the actual settings/SRAM bootstrap tests, also pass
`-DSRW64_APP_JSON_INCLUDE_DIR=/path/to/RT64/src/contrib` when configuring, using
the pinned RT64 JSON header from `config/recomp/toolchain.json`. This adds the
launch and importer tests without building the renderer or requiring a ROM.
On a Windows multi-configuration generator, build and test with `--config Debug`
and `ctest -C Debug`, respectively. Python 3.11+ is needed for the integration
tests.

`tests/native_windows_portable.cpp` covers directory precedence, isolated custom
user directories, first-language policy, moving a complete tree, relative and
legacy absolute ROM preferences, missing-data imports, conflict refusal and
acceptance, full backups, deletion propagation, campaigns, busy-directory locks,
backup failures and skipping sync after a failed host. `tests/native_launch.cpp`
also exercises first English/Chinese boots, imported and saved language
restoration, other settings and actual SRAM restoration after relocation.

On Windows, CTest additionally runs `tests/test_windows_launcher.py` with its
native probe. It executes the actual CMD under a path containing spaces and
`&`, checks argument/exit-code forwarding, then uses two unused `SUBST` drive
letters to seed and resume the same portable tree after changing its letter.
Plain Python discovery explicitly skips these two Windows tests when there is
no native probe or Windows environment; this is not evidence of a Windows pass.

Linux additionally runs `tests/native_linux_portable.cpp` for XDG/HOME selection,
custom-directory isolation, imports, conflict choices and backups, moving a tree,
and Windows-relative preference compatibility. `tests/test_linux_launcher.py`
runs the actual shell script and the compiled Linux dialog adapter with fake
kdialog/zenity executables: argument boundaries, Unicode paths, cancel/fallback,
missing utilities, malformed picker output, exit codes and the Steam keyboard
environment. CTest passes the native probe; plain unittest discovery skips the
probe-dependent integration cases. No display server is required for these tests.

## Validation boundary (2026-10-19)

Linux component and launcher-adapter results and a baseline comparison are
supplied with the patch. The GUI-helper tests use deterministic helper processes;
they do not establish real desktop dialog behavior or rendered-game acceptance.
A real Linux desktop and Steam Deck still need UI/focus, physical drive moves,
protected-folder and real-ROM/save acceptance. The Windows-only CMD/SUBST drive
integration tests are provided but have not been executed on Windows here; the
rebuilt Windows executable also needs real-machine acceptance. No game binary
or ROM is included. Known unrelated baseline test failures are listed separately
in the delivery notes rather than counted as a complete suite pass.
