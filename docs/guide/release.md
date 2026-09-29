# 发布构建

公开发布有两个下载文件：应用本体（原版画面），以及单独下载的 HD 图片包。
两者都由 `tools/release/build_release.py` 从一个提交构建出来，不受共用工作区里其他会话未提交改动的影响。
脚本只负责构建，不会发布；发布是另外手动执行的一步。

## 一次构建

```sh
.venv/bin/python tools/release/build_release.py --commit HEAD --version 0.3.1
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
  --binary build/recomp/macos14-app-build/srw64-gfx-host --output "../app-internal/SRW64 Recompiled.app" \
  --version <版本> --minimum-macos 14.0 --search-dir ../../../macos-deps/14.0-arm64/prefix/lib \
  --runtime-library ../../../macos-deps/14.0-arm64/prefix/lib/libSDL3.dylib \
  --fonts build/fonts --dialogue content/dialogue --hd ../pack/hd --rom rom.z64
```

- `--rom` 先按 `config/recomp/rom-variants.json` 核对 SHA-256，只收日版 Rev 0；ROM 放进 `Contents/Resources/rom.z64`，`Distribution.txt` 第一行写明内部测试版、含 ROM、不得外传。
- 启动时（`src/host/host.cpp`）打包的 ROM 顶替第一次弹出的 ROM 选择框：这台 Mac 记得别的 ROM 时照旧用记住的；按住 Option 或 `--choose-rom` 仍然弹选择框。
- 这个应用不进 `release.json`，也不压缩成发布文件；`build_release.py` 本身从不带 ROM。

## 发布

确认 `release-notes.md` 与两个文件后，执行 `release.json` 里 `publish` 记下的 `gh release create` 命令。
标签用 `--target` 钉在构建时的提交上。GitHub 单个附件上限 2 GB，两个文件都远小于此。

## HD 包的内容与约束

- 美术来自 `content/art/stage1-hd.json` 引用的本机素材；舰船与标记包由提交里的脚本重建，不含 ROM 字节。
- 包里的 AI 图不写入 AIGC 元数据，只在包内说明和发布说明里用文字声明。
- 公开 HD 包就是完整的 HD 文件夹，与自用的相同（2026-09-28 用户定）：由原版画面衍生的机体立绘、地图机体图标和
  HD 战术地图（含色号图）都在包里，NOTICE 和发布说明写明它们由原版像素放大而来。此前公开包删掉战术地图和图标（`ROM_DERIVED*`），已取消。
- HD 窗口边框和标题图里的 BANPRESTO 标志、GAME OVER 由游戏在运行时从玩家自己的 ROM 生成（99d5cb2、7b0d7f5），
  美术清单里没有它们。
- 玩家把解压出的 `hd` 文件夹放进用户目录（macOS 为 `~/Library/Application Support/SRW64Recomp/hd`），
  启动器即以 HD 开局；文件夹不完整或版本不对时报错，不会悄悄退回原版（`src/native/app/launch.cpp`）。
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
