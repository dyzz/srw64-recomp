# 发布构建

公开发布有两个下载文件：应用本体（原版画面），以及单独下载的 HD 图片包。
两者都由 `tools/release/build_release.py` 从一个提交构建出来，不受共用工作区里其他会话未提交改动的影响。
脚本只负责构建，不会发布；发布是另外手动执行的一步。

## 一次构建

```sh
.venv/bin/python tools/release/build_release.py --commit HEAD --version 0.5.0
```

- 输出目录默认是 `build/release/<版本>-<短提交号>`，已存在时拒绝覆盖。可以用 `--output` 指定，用 `--keep-source` 保留检出目录与编译产物。
- 前提：主工作区已有 `rom.z64`、`assets/`、`make all` 生成的 `build/recomp/{upstream,tool-build,cpu-scan}`，以及
  [macOS 依赖](../native/macos-release.md)（`build/macos-deps/14.0-arm64/prefix`）。

步骤：

1. 用 `git worktree add --detach` 把提交检出到 `输出目录/src`。
2. 把 ROM 链接进去；`assets/` 里的 `hd-ai`、`fonts`、`models` 用写时复制克隆（美术编译不接受指向检出目录外的路径），其余链接。
3. 克隆固定工具链的上游源码与已编好的工具，把 RT64（含子模块）还原成锁定的原样（主工作区里别的会话未提交的 RT64 改动不会混进来，也不会让 `prepare_rt64.py` 拒绝），然后只用这个提交的代码重新生成：CPU 代码、RT64 补丁、前端适配层、音频微码、字体。
4. 跑全部 Python 测试。
5. 按 macOS 14 / arm64 从零编译，打出应用包。
6. 重建舰船模型包和 5600 标记包，打 HD 包，并附上 `tools/release/hd-notice.txt`（安装方法、AI 生成来源、非官方声明）。
7. 两个文件用 `ditto --norsrc --noextattr` 压缩：不带 `__MACOSX` 和隔离、来源等扩展属性，应用签名解压后仍然有效。
8. 写出 `release.json`（提交、版本、两个文件的大小与 SHA-256、HD 包的 `hd.json`、发布命令）
   和按 `tools/release/release-notes.md` 填好的发布说明。

整个过程在本机约 3 分钟：从零编译 942 步不到一分钟，测试约 40 秒。

### 内部测试版（带 ROM 与 HD，不外传）

2026-09-29 用户要一个装好 ROM 的内部测试版。先按上面加 `--keep-source` 构建，再把编好的程序、`pack/hd` 和 ROM 一起打进应用：

```sh
cd build/release/<版本>-<提交>/src
PYTHONPATH=src:tools ../../../../.venv/bin/python tools/release/package_macos.py \
  --binary build/recomp/macos14-app-build/srw64-gfx-host --output "../app-internal/Marchwind64.app" \
  --version <版本> --minimum-macos 14.0 --search-dir ../../../macos-deps/14.0-arm64/prefix/lib \
  --runtime-library ../../../macos-deps/14.0-arm64/prefix/lib/libSDL3.dylib \
  --fonts build/fonts --dialogue content/dialogue --hd ../pack/hd --rom rom.z64
```

- `--rom` 先按 `config/recomp/rom-variants.json` 核对 SHA-256，只收日版 Rev 0；ROM 放进 `Contents/Resources/rom.z64`，`Distribution.txt` 第一行写明内部测试版、含 ROM、不得外传。
- 启动时（`src/host/host.cpp`）打包的 ROM 顶替第一次弹出的 ROM 选择框：这台 Mac 记得别的 ROM 时照旧用记住的；按住 Option 或 `--choose-rom` 仍然弹选择框。
- 这个应用不进 `release.json`，也不压缩成发布文件；`build_release.py` 本身从不带 ROM。

### 附上其他平台的包

Linux 包和 Windows 包由 GitHub Actions 的 `build` 工作流构建（`.github/workflows/build.yml`；推送 main、推送 `v*` 标签或手动触发）：
Ubuntu 上从私有仓库 `dyzz/srw64-ci-inputs` 的 ROM 生成游戏代码，加密后交给 Windows（clang-cl）和 Linux（与 `tools/release/linux/build.sh`
同一个 Ubuntu 22.04 容器）两个任务；成品也加密上传（公开仓库的产物谁都能下载），用本地 `~/.config/srw64/ci-artifact-key` 解开。只改 `web/`、`docs/` 或根目录 `.md` 的推送不触发构建（标签推送照常构建）。三个平台各有编译缓存（ccache，含编译后的游戏代码，所以同样加密后存进 Actions 缓存，每次运行存一份、下次从最新的一份开始；`ccache -s` 的输出在编译步骤末尾），Linux 的 librashader 和另外两个平台一样缓存成品。Windows 的 vcpkg 固定在发布版 2026.07.29（工作流的 `VCPKG_COMMIT`），不用 runner 镜像自带的那份：各镜像的 vcpkg 不同，同一个 ICU 算出不同的 ABI，半数运行要重编 ICU 14 分钟；改这个提交号会让 ICU 重编一次：

```sh
gh run download <运行号> --repo dyzz/srw64-recomp -n Marchwind64-linux-x64-encrypted
openssl enc -d -aes-256-cbc -pbkdf2 -iter 200000 -pass file:$HOME/.config/srw64/ci-artifact-key -in Marchwind64-linux-x64.tar.gz.enc -out Marchwind64-linux-x64.tar.gz
```

Windows 包同样下载 `Marchwind64-windows-x64-encrypted`，解开后是 `Marchwind64-windows-x64` 文件夹，压成 zip 再附上。**压的是这个文件夹本身**（在它的上一级 `zip -r … Marchwind64-windows-x64`），zip 顶层必须是 `Marchwind64-windows-x64/` 一层，附上前 `unzip -l … | head` 核对：安装页写的就是这个文件夹，历代包都如此；0.5.1 曾压成文件直接在根上，玩家按老办法解压覆盖时新文件落在旧 exe 的上一层，游戏仍是旧版、标题一直提示新版本（已发布的资产不再动，从下一版起注意）。
安卓包下载 `SRW64-android-arm64-encrypted`，解开后就是 APK（CI 用仓库里的发布密钥签名，能覆盖安装旧版并保留存档）；
`android:versionName` 与 `versionCode` 在 `tools/release/android/app/AndroidManifest.xml`，发版时与根目录 `CMakeLists.txt` 一起改
（versionCode＝主版本×10000＋次版本×100＋修订号，0.4.0 为 400，只能变大；`tests/test_release_version.py` 核对三处一致）。Steam 封面图在仓库的 `tools/release/linux/steam-art/`
（`steam_art.py` 由 HD 标题图生成，改了标题图再重新生成并提交），CI 构建的 Linux 包也带封面。

Linux 包也仍可在本机构建（`tools/release/linux/build.sh` 或干净检出里的 `build_linux.py`），Windows 包只由 Actions 构建，
用 `--attach` 交给同一次 `build_release.py`，与 Mac 包和 HD 包放进同一个发布：

```sh
.venv/bin/python tools/release/build_release.py --commit HEAD --version 0.5.0 \
  --attach linux=build/deck/<提交>/src/build/linux-x64/Marchwind64-SteamDeck-0.5.1-<日期>-<提交>.tar.gz \
  --attach windows=build/windows/<提交>/Marchwind64-0.5.0-windows-x64.zip \
  --attach android=build/android/<提交>/SRW64-android-arm64-<提交>.apk
```

附件改名为 `Marchwind64-<版本>-linux-x64.tar.gz`、`Marchwind64-<版本>-windows-x64.zip`、`Marchwind64-<版本>-android-arm64.apk`，下载表、HD 包的三个平台安装位置、校验值和
`release.json` 的发布命令都按附上的包生成。三个平台共用同一个 HD 包；它只能在本机构建（素材不进仓库），所以总是由这一步打出。
附上的包要来自同一个提交：HD 包只配同一版本的应用。

### RecompLauncher 的 `recomplauncher.json`

[RecompLauncher](https://recomplauncher.com)（与 RecompStore 同一个开发者中心）按 GitHub Release 安装、更新重编译游戏；发布包顶层、程序旁边的
`recomplauncher.json` 告诉它程序在包里的路径、设置文件在哪、哪些设置放到它的页面上，以及退出码的含义。文件是 `tools/release/recomplauncher.json`，
三个桌面包都带：Windows 包由 CI 的打包步骤复制进 `Marchwind64-windows-x64/`，Linux 包由 `build_linux.py` 放进 `Marchwind64-linux-x64/`，
Mac 包由 `build_release.py` 放在 `Marchwind64.app` 旁边（所以 Mac zip 的顶层是应用加这个文件，不是只有应用）。`tests/test_recomplauncher.py`
核对它写的程序路径、设置键值和存档目录与打包脚本、`presentation.json` 的实际写法一致。

- 它没有 N64 ROM 这种单文件的 `game.kind`（只有 iso/folder），所以文件里不写 `game`：ROM 仍由程序自己的选择框要，点 Play 第一次会弹。
- 设置文件按平台列出 `presentation.json` 的几个位置（程序旁 `user-data/` 的便携模式优先），存档目录同样。启动器可能在第一次启动前就写入一两个键，
  这种只有部分键、没有 schema 的文件 `launch.cpp` 当作部分设置接受，语言仍按系统（`tests/native_launch.cpp` 的 `launcher_partial_settings`）。
- 它每个系统只装 Release 里的一个文件（按文件名里的 `windows-x64`、`linux-x64` 挑），HD 包这种第二个下载它不装，也不会误认（`Marchwind64-HD-<版本>.zip` 不带系统名）。
  从启动器装的玩家要默认有 HD（2026-10-28 用户定，只限这条渠道；官网、网盘照旧自己下载），所以文件里写了 `prepare`：安装画面的「Getting the extras」一步
  无窗口跑 `Marchwind64 --prepare`（Linux 经 `marchwind64.sh` 变成 `srw64 --play --prepare`，入口两种写法都认），程序读官网 `/latest.json` 的 `hd`，
  把包下载到用户目录 `downloads/`、核对 SHA-256、解压到暂存文件夹再改名成 `hd/`，所以失败不会留下半个包；用户目录（或游戏旁、应用内）已有同版本就直接结束。
  进度按启动器的格式打印 `progress <0..1> <文字>`；失败退出码 3（`exit_codes` 里有说明），玩家重跑安装即可，游戏照常以原版画面运行。
  实现在 `src/host/hd_prepare.{hpp,cpp}`，下载走 `update_http.cpp`（Windows WinHTTP、Linux 运行时 libcurl）和 `macos/update_http_macos.mm`（NSURLSession）的
  `http_download`，空闲 60 秒无数据才算超时。`make recomp-hd-prepare-test` 测解压与替换（含符号链接的 `hd/`、不完整的包、越界路径）；联网部分手动验证：
  `SRW64_UPDATE_URL=file:///…/latest.json srw64-gfx-host --prepare --user-dir DIR`，`latest.json` 的 `hd.url` 可指向本地 `file://` 的 zip。
  「打开数据文件夹」的按钮（`folders.data`＋`actions`）说明 HD 包装在这里、删掉就回原版。
- 它的 MOD 页只接 Thunderstore 和 GameBanana，我们的 `.srw64mod` 照旧走官网，文件里不写 `mods`。
- 提交游戏在开发者中心的 Submit a game 填 GitHub 仓库；页面图（图标、背景、logo、截图）在本地 `local/store-pictures/`。

## 发布

确认 `release-notes.md` 与两个文件后，执行 `release.json` 里 `publish` 记下的 `gh release create` 命令。
发布说明英文在前、中文在后（2026-10-16 用户定）；HD 包不重复上传，说明顶部和两张下载表都直接链到它自己发布页上的 zip（`{hd_download}`），校验里也列出它。
发布后要改说明：改模板，`build_release.py --render-notes build/release/<目录>` 按该目录的 `release.json` 重新生成 `release-notes.md`（不构建），再 `gh release edit v<版本> --notes-file …`。
标签用 `--target` 钉在构建时的提交上。GitHub 单个附件上限 2 GB，两个文件都远小于此。

## 只发文本

文本修正（台词、数据文本词条、已有界面文字、图鉴名）不必发新版程序，见 [文本单独更新](../design/text-updates.md)。

1. 照平常改文本并提交：`renames.json`、`make_zh_hant.py`、`apply_terms` 跑过，`check_dialogue.py` 与 `run_audit.py` 无问题。
2. 写一句更新说明 `notes.json`：`{"zh": "…", "tw": "…", "en": "…", "ja": "…"}`。
3. `.venv/bin/python tools/release/build_text_update.py --notes notes.json`：把全部文本按文件列进清单；先跑同游戏一样的检查，有问题不出清单。`--dry-run` 只检查并报文件数。
   写出 `web/public/text/text-r<N>.json`、`web/public/text/files/<hash>.z` 和 `web/src/data/text-update.json`（官网只留最近两份清单）。
4. 提交这三处，`bash web/deploy/deploy.sh` 上线；核对 `https://srw64.dreamquest.club/latest.json` 的 `text` 段。

一份全量清单给所有版本：开着「启动时检查更新」的玩家下次启动（一天一次）、或手动检查时，只下载和自己已装的不一样的文件。**每次发新版程序后也要跑一次**：清单的 `release` 是提交里最新的发布标签，比它新的程序不用这份清单。

## HD 包的内容与约束

- 美术来自 `content/art/stage1-hd.json` 引用的本机素材；舰船与标记包由提交里的脚本重建，不含 ROM 字节。
- 包里的 AI 图不写入 AIGC 元数据，只在包内说明和发布说明里用文字声明。
- 公开 HD 包就是完整的 HD 文件夹，与自用的相同（2026-09-28 用户定）：由原版画面衍生的机体立绘、地图机体图标和
  HD 战术地图（含色号图）都在包里，NOTICE 和发布说明写明它们由原版像素放大而来。此前公开包删掉战术地图和图标（`ROM_DERIVED*`），已取消。
- HD 窗口边框和标题图里的 BANPRESTO 标志、GAME OVER 由游戏在运行时从玩家自己的 ROM 生成（99d5cb2、7b0d7f5），
  美术清单里没有它们。
- 玩家把解压出的 `hd` 文件夹放进用户目录（macOS 为 `~/Library/Application Support/SRW64Recomp/hd`），
  或者放在游戏所在的文件夹里（`Marchwind64.app`、`marchwind64.sh`、`Marchwind64.exe` 旁边，`beside_game`），
  启动器即以 HD 开局。查找顺序：用户目录 → 游戏旁边 → 应用自带（`Contents/Resources/hd`）；
  文件夹不完整或版本不对时报错，不会悄悄退回原版（`src/native/app/launch.cpp`）。macOS 上未在访达里移动过的
  下载应用会被系统换到只读副本里运行（App Translocation），这时旁边的 `hd` 看不到；安卓没有游戏文件夹，只认用户目录。
- HD 包只配同一版本的应用。`hd.json` 的 schema 或 HD 数据格式变了，要同时发新应用和新 HD 包。
- 体积（`tools/release/compress_hd.py`，2026-09-28）：整图都存 JPEG 质量 92、4:2:0，要透明的另存一张灰度加透明的 PNG。
  机体立绘从 8 倍缩到 6 倍 ROM 像素，长边不超过 1024 px；战前确认页最大只显示约 830 px（密度 2）或 980 px（密度 3）。
  头像保持 768 px，战术地图保持 4 倍，因为全屏时它们本来就不够大。RT64 贴图只能是 PNG 或 DDS，
  所以只把不透明的去掉透明通道；BC7 会把世界地图的近景放大出块状，没有采用。效果：完整 HD 从 708 MB 降到 409 MB（zip 后约 400 MB），
  公开包现在与它相同；抽样 PSNR 为立绘 44、头像 42、地图 41 dB。
- **掌机版 HD 包：研究过，暂不做**（2026-09-29 用户：“留作研究吧，暂时还是全高清”）。设想是给 Steam Deck 这类 1280×800 掌机另出一个低分辨率包、桌面保持现状。
  按 0.3.0 的 HD 包（385 MB zip）逐类抽样重压估算，只能省约 30%，到约 265 MB：

  | 部分 | 现在 | 掌机版估计 | 说明 |
  | --- | --- | --- | --- |
  | 战术地图 | 219 MB | 约 140 MB | 本来只有原版 4 倍，Deck 上显示 3.33 倍，缩尺寸省得少，主要靠 JPEG 92→85；色号图约 39 MB 不能动（缩放要用最近邻，线性插值反而变大） |
  | 世界地图（RT64 512² 贴图） | 83 MB | 83 MB | Deck 上近景要 10–17 倍，8 倍都不够，不能缩 |
  | 头像 | 38 MB | 约 13 MB | 768→384 px；Deck 对白头像约 320 px 高 |
  | 机体立绘 | 26 MB | 约 14 MB | 576→384 px；Deck 战前页最大约 300 px |
  | 背景 | 6 MB | 约 3 MB | 1920×1440→1280×960 |
  | 其他 | 13 MB | 13 MB | 剧情图、模型、图标 |

  代价：Deck 接电视或显示器时掌机版明显发虚（1080p 下战术地图要约 4.5 倍）；多一个包要构建、测试和写说明。
  真要做，只是 `compress_hd.py` 加一个目标档位（尺寸上限与 JPEG 质量），加载端不用改，约半天工作量。
