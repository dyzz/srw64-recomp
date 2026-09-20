# 跨平台发布改造：计划与第一批实施

基线：`22706a4294f7e0ddee40563e7c6e4972376811f9`（2026-09-19）。

**本批是独立运行入口与可移植应用层，不是 Windows/Linux 游戏移植完成。**
图形宿主仍使用 Metal、Core Text；默认游戏页面已换成 SDL/RmlUi，桌面 ROM 选择器仍使用 AppKit；`src/host/CMakeLists.txt` 的
非 Apple 平台拒绝条件有意保留。公共三平台 CI 测试的是应用启动/存档逻辑，
不是原游戏、GPU、输入法或完整关卡。

## 目标与边界

最终玩家流程：下载对应平台的程序 → 首次选择自己持有的匹配 ROM → 本地导入 → 开始游戏。
玩家不安装 Python、Git、CMake、Ninja、编译器或重编译工具。Python 保留在开发、
ROM 分析、代码生成、翻译编译、美术处理与 QA 中；不重写这整套工具。

保留原游戏执行、游戏性修正、TextKey/语言目录、阅读状态与已验证的线程退出行为。
不要通过删除原生对白、姓名页、Link Battler 页或设置来宣布跨平台完成。
现有 `.command`/Python 试玩、探针、MCP 和 `build/recomp/profile-play` 存档不自动迁移、不删除。

## 第一批：已实现的代码

| 文件 | 责任 |
| --- | --- |
| `src/native/app/runtime.*` | 参数、用户数据目录、Windows/POSIX 文件锁、独立会话、存档复制与完整性检查 |
| `src/native/app/sha256.hpp` | 流式 ROM/内容/SRAM 摘要；摘要不等于资源来源可信或授权 |
| `src/native/app/launch.*` | 读取可迁移内容目录、恢复语言/规则、清除开发环境变量、直接调用已编译宿主 |
| `src/host/host.cpp` | 新增 `--play`；原 positional probe ABI 保留；不更改游戏循环 |
| `tools/release/export_content.py` | 将已有 prepared profile 导出为**本地自用**的相对路径内容目录 |
| 根 `CMakeLists.txt` | 不依赖 ROM、SDL、RT64 renderer、Python 的基础测试入口；不是游戏构建入口 |
| `.github/workflows/native-app.yml` | Windows、Linux、macOS 的 ROM-free 原生应用测试和内容导出测试 |

`srw64_app` 仅依赖 C++20 标准库与少量 OS 文件锁 API。
`srw64_launch` 复用已固定 RT64 里的 JSON 单头文件，不链接 RT64 renderer。
应用层不包含 Metal/AppKit/CoreText 头文件，也不执行外部进程。
游戏规则目录由宿主传入，避免在新入口复制一套会漂移的规则 ID/默认值。

### 开发者试用新入口（当前仍只适用于 macOS 图形宿主）

在已有 `rom.z64` 与开发环境的仓库根目录执行：

```sh
# 原有构建入口保留。构建过程中仍然会使用 Python。
make

# 一次性本地内容准备；两个输出目录均须不存在。
.venv/bin/python tools/content/compile_profile.py \
  --images original --output build/standalone-prepared
.venv/bin/python tools/release/export_content.py \
  --prepared build/standalone-prepared --output build/standalone-content

# 运行阶段直接调用 native binary，不经过 Python/build/probe launcher。
./build/recomp/gfx-build/srw64-gfx-host --play \
  --rom "$PWD/rom.z64" --content "$PWD/build/standalone-content" \
  --language zh-Hans
```

`--user-dir PATH` 指定独立用户目录；`--new-game` 不读取旧存档；
`--import-save PATH` 显式导入一份 32 KiB 原版 SRAM（复制而非修改来源）；
`--mute` 静音；`--rules original|fixed|all` 选择并记住规则预设；
`--resolution-scale 1..8` 指定本次分辨率。语言可在游戏内切换并在下次恢复。

ROM、内容和二进制的路径可以在源码目录之外。新入口本身不查询源码、`.git`、
生成代码或工具链报告；但这不等于已完成 macOS `.app` 的动态库收集、签名与分发。
第一批没有文件选择器或双击启动 UI。没有准备内容目录时会明确报错，不暗中调用 Python。

### 内容与存档

导出目录包含原 ROM 派生的日文文本/头像，**不得作为本项目公共 release artifact 上传**。
当前导出只覆盖 Original 模式、原生多语言与姓名/联动头像；HD 的图像和模型包迁移留在后续阶段。
原有试玩入口的 HD 功能不受本批影响。导出器不复制 ROM、字体、源码、存档或整个 `assets/`。
manifest 列出允许读取的文件及 SHA-256；运行时拒绝路径越界、外部 symlink 和摘要不符。

第一批仍需要开发工具进行一次内容准备。**“原 ROM 首次导入也不需要 Python”尚未实现**，
必须在面向普通玩家发布前完成原生导入器，不能把本批的本地内容目录当作可公共分发的替代品。

默认用户目录：

| 平台 | 路径 |
| --- | --- |
| Windows | `%LOCALAPPDATA%/SRW64Recomp` |
| macOS | `~/Library/Application Support/SRW64Recomp` |
| Linux | `$XDG_DATA_HOME/srw64-recomp`；未设置有效绝对路径时为 `~/.local/share/srw64-recomp` |

每次运行占一个独立会话，宿主只修改本次 SRAM 副本。正常返回后才复制最终 SRAM、
写摘要并原子替换 `last-session.txt`。无存档、错误返回、损坏摘要或异常终止不会自动
把未完成会话作为下次恢复源。文件完整性检查不证明游戏内存档槽位有效。

历史快照保留在 `sessions/<id>/save.bin`。需要恢复旧会话时用 `--import-save` 指向该文件；
导入原 Python 试玩历史时，显式选择那次会话的 `runtime-data/saves/*.bin`。
自动恢复损坏时不悄悄回退或开新档。`--new-game`/`--import-save` 是显式恢复通道。

没有自动清理历史；本次宿主的日志与 runtime-data 也会保留，可能包括缓存 ROM。
这些目录是私有运行数据，不是可分享的错误报告包。未来增加共享 ROM/cache 目录、日志脱敏
和可配置保留策略。当前原子替换防止读到半写的指针，不声称具备断电级事务持久性。

## 后续实施顺序及验收门槛

### P1：原生首次导入与完整 macOS 分发

将 `source_catalog`、原 ROM 解码/字形映射、姓名与联动头像提取需要的**运行时子集**迁入 C++。
Python 实现保留为 oracle，通过固定 ROM 与合成 fixture 做逐字节/逐记录对照。
导入缓存用 `(ROM hash, importer version, locale/content schema)` 标识；导入成功后原子发布，
失败不破坏已有缓存。不在第一次启动时编译 C/C++。

增加文件选择、清晰的错误提示、独立只读资源/可写用户目录，以及 macOS bundle 的动态库收集。
二进制、原生 UI 字体方案、语言包与可分发资源需要明确清单。

**完成条件：** 干净 macOS 用户环境，无 Python/Git/Homebrew/Xcode，程序与 ROM 均位于源码目录外；
首次导入、日中英切换、正常保存/退出/重启、坏 ROM、缺资源、只读应用目录均通过。

### P2：图形与文字后端解耦

将 `graphics.cpp`、`native_dialogue_text.cpp`、`native_marker.cpp` 中直接依赖
`MTL::*`、`plume::Metal*`、`SDL_MetalView` 的部分移出共用代码。
保留 RT64/Plume：macOS Metal，Linux Vulkan，Windows 先选一个支持路径（Vulkan 或 D3D12）跑通。
截图/readback、命名页遮挡和 present completion 也要抽象，不能只更换窗口初始化。

对白 model、分页/逐字/阅读状态与 render snapshot 保留；文字 shaping/raster 与 GPU 合成分层。
评估共享字体栅格化方案及可再分发字体，不在 Windows/Linux 硬编码 macOS PostScript 字体名。

**不可破坏：** UI 必须匹配正在呈现的 workload，不能读“最新 RDRAM”覆盖旧帧；
resize、异步 GPU 完成回调、字体 metrics、行尾禁则与语言切换不能推进原脚本。

**完成条件：** 每个平台至少通过开场→姓名→第一话→存档冷启动，含窗口缩放、F6/F7、
对白遮挡/闪烁与安全退出；通过前保持平台 gate，不宣称完整支持。

### P3：共享 UI 与输入

2026-09-20 默认游戏页面已切换为 [SDL/RmlUi](../native/shared-game-ui.md)：主角选择、姓名、
确认、设置、Link Battler、通知及调试 UI 共用事件和渲染路径，不再编译原 AppKit 游戏页面。
保留[独立姓名页探针](../native/shared-name-page-probe.md)供组字与布局回归。
真实游戏命名与写回已验证；OS 输入法候选窗、手柄、跨平台 surface 和第一话／存档冷启动仍待验收。
旧 AppKit 源码暂作回归参考；CoreText 对白层属于尚未完成的 P2。

连接手柄与可配置按键；MCP/QA 使用语义动作，避免把 Cocoa 控件路径当作公共协议。
调试 socket 必须可在 release 构建时排除，而不只是运行时关闭。

### P4：发行构建与自动化

首批目标固定为 Windows x64、Linux x64、macOS arm64。Intel macOS、Windows ARM64、
Universal binary 与更多 Linux 发行格式单独验收，不用 `*-latest` 标签暗中扩大承诺。

公共 PR CI 只使用合成输入和可公开源码；持有 ROM 的受信构建/实机验证与不受信 PR 隔离。
代码生成与三平台编译分开，生成物记录 ROM/工具链/patch/schema 摘要。
发布时只按清单收集二进制、必要依赖和可分发资源，绝不打包整个 `build/` 或用户目录。
源码/生成代码/二进制、翻译、美术与字体分别核对授权边界；不以“不带 ROM”代替该检查。

先采用 Windows zip、Linux 明确 glibc 基线的 tar.gz、macOS `.app` zip；
动态库闭包、Mac 签名/notarization、Windows 签名与干净机器测试完成后再提供普通玩家下载。
AppImage/Flatpak、安装器与自动更新不是第一批的前置任务。

## 验证入口与证据范围

```sh
# 无 ROM、无 Python、无 renderer 依赖的原生基础测试。
cmake -S . -B build/native-app -DCMAKE_BUILD_TYPE=Release
cmake --build build/native-app --config Release
ctest --test-dir build/native-app -C Release --output-on-failure

# 加入完整 bootstrap 的合成内容 + fake-host 测试；只需要已固定的 JSON 头文件。
cmake -S . -B build/native-app \
  -DSRW64_APP_JSON_INCLUDE_DIR="$PWD/build/recomp/upstream/RT64/src/contrib"
cmake --build build/native-app --config Release
ctest --test-dir build/native-app -C Release --output-on-failure

python -m unittest discover -s tests -p test_release_content.py -v
```

基础测试覆盖参数/UTF-8 路径、各平台目录策略、文件锁、SHA-256 已知向量与分块读取、
存档隔离、损坏拒绝、显式恢复、异常退出不提交、路径越界与环境变量清理。
完整 bootstrap 测试使用合成 ROM/头像和内存中的 fake host，覆盖内容搬迁、任意 cwd、
语言/规则恢复与错误返回；**不能代替真实宿主构建或 GPU/游戏验证**。

合并第一批前须在开发者的 macOS + 匹配 ROM 环境构建新增入口，运行上述真实试玩流程并检查
旧入口无回归。之后才进入原生导入器和平台图形/UI 迁移。
