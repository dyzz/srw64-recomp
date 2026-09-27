# 发布构建

公开发布有两个下载文件：应用本体（原版画面），以及单独下载的 HD 图片包。
两者都由 `tools/release/build_release.py` 从一个提交构建出来，不受共用工作区里其他会话未提交改动的影响。
脚本只负责构建，不会发布；发布是另外手动执行的一步。

## 一次构建

```sh
.venv/bin/python tools/release/build_release.py --commit HEAD --version 0.2.0
```

- 输出目录默认是 `build/release/<版本>-<短提交号>`，已存在时拒绝覆盖。可以用 `--output` 指定，用 `--keep-source` 保留检出目录与编译产物。
- 前提：主工作区已有 `rom.z64`、`assets/`、`make all` 生成的 `build/recomp/{upstream,tool-build,cpu-scan}`，以及
  [macOS 依赖](../native/macos-release.md)（`build/macos-deps/14.0-arm64/prefix`）。

步骤：

1. 用 `git worktree add --detach` 把提交检出到 `输出目录/src`。
2. 把 ROM 链接进去；`assets/` 里的 `hd-ai`、`fonts`、`models` 用写时复制克隆（美术编译不接受指向检出目录外的路径），其余链接。
3. 克隆固定工具链的上游源码与已编好的工具，然后只用这个提交的代码重新生成：CPU 代码、RT64 补丁、前端适配层、音频微码、字体。
4. 跑全部 Python 测试。
5. 按 macOS 14 / arm64 从零编译，打出应用包。
6. 重建舰船模型包和 5600 标记包，打 HD 包，并附上 `tools/release/hd-notice.txt`（安装方法、AI 生成来源、非官方声明）。
7. 两个文件用 `ditto --norsrc --noextattr` 压缩：不带 `__MACOSX` 和隔离、来源等扩展属性，应用签名解压后仍然有效。
8. 写出 `release.json`（提交、版本、两个文件的大小与 SHA-256、HD 包的 `hd.json`、发布命令）
   和按 `tools/release/release-notes.md` 填好的发布说明。

整个过程在本机约 3 分钟：从零编译 942 步不到一分钟，测试约 40 秒。

## 发布

确认 `release-notes.md` 与两个文件后，执行 `release.json` 里 `publish` 记下的 `gh release create` 命令。
标签用 `--target` 钉在构建时的提交上。GitHub 单个附件上限 2 GB，两个文件都远小于此。

## HD 包的内容与约束

- 美术来自 `content/art/stage1-hd.json` 引用的本机素材；舰船与标记包由提交里的脚本重建，不含 ROM 字节。
- 包里的 AI 图不写入 AIGC 元数据，只在包内说明和发布说明里用文字声明。
- HD 战术地图（`art/maps`）含 ROM 派生的色号图，公开包里删掉，游戏画原版地图；自用全 HD 包保留。
- HD 窗口边框和标题图里的 BANPRESTO 标志、GAME OVER 由游戏在运行时从玩家自己的 ROM 生成（99d5cb2、7b0d7f5），
  美术清单里没有它们。`build_release.py` 仍会删掉包里的 `art/frames` 和这两张图（`ROM_DERIVED*`），只作保险：
  以后有人把 ROM 派生图放回清单，也不会进公开包。
- 玩家把解压出的 `hd` 文件夹放进用户目录（macOS 为 `~/Library/Application Support/SRW64Recomp/hd`），
  启动器即以 HD 开局；文件夹不完整或版本不对时报错，不会悄悄退回原版（`src/native/app/launch.cpp`）。
- HD 包只配同一版本的应用。`hd.json` 的 schema 或 HD 数据格式变了，要同时发新应用和新 HD 包。
