# 安卓移植方案（调研）

2026-10-01。接续[三平台移植计划](three-platform-port.md)。本页是调研结论和建议方案，依据有三部分：

- 对本仓库宿主代码的静态审计，行号以 main `c480cfe` 为准；
- 对 `config/recomp/toolchain.json` 固定的 RT64（含 plume 子模块）、N64ModernRuntime、Zelda64Recomp 提交的源码审计，并用固定版本的 DXC 编译了 RT64 的全部 SPIR-V 着色器；
- 社区已有的 N64Recomp 安卓移植：读了源码和文档，没有编译或运行。

**没有用 NDK 构建过本项目，也没有在安卓设备上运行过。** 文中标“推断”的地方是工程判断，不是实测。

## 结论

- **可行，大部分问题已有人解决过。** 社区至少有五个 N64Recomp 游戏的安卓移植，都是“构建机生成代码 → APK 侧载 → 玩家自己选 ROM”，渲染都用打了补丁的 RT64 Vulkan。上游 RT64、N64Recomp 和 Zelda64Recomp 都没有官方安卓支持，社区的补丁也没有合回上游。
- **路线：** SDL + RT64 Vulkan，只做 arm64-v8a。HD 图层、对白合成和 RmlUi 已经走 plume，并嵌入了 SPIR-V（三平台计划 X1），所以安卓与 Linux 共用同一套图形代码，不另写后端。
- **先做安卓掌机，再做手机触屏。** 掌机有手柄、屏幕是 16:9、多为高通 Adreno GPU。现有的 Deck 手柄键位、按键提示和 16:9 画面可以直接用，触屏和手机界面另作一个阶段。这与“先上 Deck”的顺序一致。
- **分发：** APK 里含由 ROM 生成的已编译代码，与 Deck 包性质相同。所以只做自用侧载包，不上 Google Play。
- **主要工作量（推断）：**
  1. RT64/plume/N64ModernRuntime 的安卓补丁，约十项，社区 fork 有可参照的实现；
  2. 宿主的应用生命周期，主要是进程在后台被杀时不能丢档；
  3. 交叉编译构建；
  4. 触屏与手机界面。

## 目标与范围

| 项 | 选择 | 理由 |
| --- | --- | --- |
| ABI | 只做 arm64-v8a | 生成代码与 macOS arm64 是同一种 ABI 形态：小端、LP64，RSP 向量单元经 sse2neon（`src/host/CMakeLists.txt:7`） |
| 图形 | Vulkan，经 plume | RT64 没有 GLES 后端。libultraship 系移植（Ship of Harkinian 等）用 GLES，与本项目无关 |
| 窗口、输入、音频 | SDL，经 SDLActivity | 宿主全程用 SDL。改用 AGDK GameActivity 要重写这一层，不采用 |
| minSdk | 暂定 28（Android 9） | 与 Goemon64Recomp-Android 相同。ICU 随包，不受 NDK 系统 ICU 要求 API 31 的限制，见下文。实际门槛由 GPU 特性决定 |
| 首批验收设备 | 高通 Adreno 的安卓掌机（如 AYN Odin、Retroid Pocket 系列） | 手柄、16:9、Adreno 驱动的社区经验最多 |
| 分发 | 自用侧载 APK | 见[分发](#分发与-rom-派生代码) |

## 已有先例

以下项目都是社区非官方移植，2026-10-01 核查：

| 项目 | 要点 |
| --- | --- |
| [Goemon64Recomp-Android](https://github.com/ogdanimal/Goemon64Recomp-Android) | **参照价值最大。** v1.0.7，最后提交 2026-09-01，minSdk 28，NDK 27.1，按 16 KB 页对齐。fork 了 RT64、plume 和运行库，下文多数阻塞项都在这些 fork 里能找到对应修复。ROM 用存储访问框架（SAF）选择后复制进应用私有目录。可选用 libadrenotools 加载 Turnip 驱动 |
| [Zelda64Recomp-Android](https://github.com/linkzenic/Zelda64Recomp-Android) | minSdk 24，NDK 26。用 `MANAGE_EXTERNAL_STORAGE` 读写公共目录，不符合 Play 的存储政策。主要在 Adreno 上测试；说明里写三星设备不行 |
| [HarvestMoon64Recomp](https://github.com/igawa6/HarvestMoon64Recomp) | Android 9+，Vulkan 1.1，ROM 复制进私有目录，不申请权限 |
| [dk64-recomp-android](https://github.com/deivid22srk/dk64-recomp-android) | 报告 Adreno 619 在 `vkGetRefreshCycleDurationGOOGLE` 里段错误。上游 RT64 每次呈现都会调用它 |
| [BanjoRecomp-Android](https://github.com/AurelioB/BanjoRecomp-Android) | 在 AYN Thor 掌机上测试 |

共同做法：

- CPU 代码在构建机上由 ROM 生成，APK 里是编译好的代码，不带 ROM 数据；
- 都用 SDL2 2.32 自带的安卓胶水，没有一个用 SDL3 或 sdl2-compat。

**Goemon 在骁龙 865（Adreno 650）上的性能实测**（见该仓库的 Android 性能记录）：

- 4 倍分辨率、关 MSAA 时，游戏按原生 30 FPS 运行；8 倍分辨率只有 30 FPS，4 倍是 52 FPS；MSAA 约损失 20%。
- 最差的菜单约 14 FPS。原因是每帧 20 个 fence，GPU 频率被调度压在 305–400 MHz。
- 结论：安卓版默认 4 倍分辨率、关 MSAA。

官方态度：Zelda64Recomp 的 [#45 “Android support?”](https://github.com/Mr-Wiseguy/Zelda64Recomp/issues/45) 以“不计划”关闭。没有找到 RT64 或 N64Recomp 维护者关于安卓的表态。

## 上游现状（固定提交）

### RT64 与 plume

plume 留着一套安卓骨架：

- `VK_USE_PLATFORM_ANDROID_KHR`；
- `RenderWindow` 定义为 `ANativeWindow*`；
- 用 `vkCreateAndroidSurfaceKHR` 创建表面。

这套代码来自 RT64（2024-04）和 plume（2025-02）的初始提交，此后没有提交再提到安卓。**照原样编译不过，也画不对：**

| 问题 | 位置（上游） | 后果 |
| --- | --- | --- |
| `static_assert(false && "Android unimplemented")` | `rt64_application_window.cpp:107,151` | 编译失败 |
| nativefiledialog 无条件链接；它的 CMake 把安卓当 Linux | RT64 `CMakeLists.txt:74,444` | 配置期要 GTK3，CMake 失败 |
| DXC 和 `file_to_c` 按目标平台选择 | RT64 `CMakeLists.txt:61-65,72` | 交叉编译时在构建机上运行 arm64 程序 |
| 交换链只认 `B8G8R8A8_UNORM` | `rt64_application.cpp:328`、`plume_vulkan.cpp:2207-2209` | 安卓表面一般只提供 `R8G8B8A8`，创建交换链失败 |
| 表面只创建一次 | plume Vulkan 后端 | 切到后台时安卓会销毁 `ANativeWindow`，回到前台无法重建 |
| `preTransform` 固定为 IDENTITY | `plume_vulkan.cpp:2343` | 屏幕原生竖放的设备每帧多一次系统旋转，约 1–3 ms，并返回 SUBOPTIMAL（[Android 预旋转](https://developer.android.com/games/optimize/vulkan-prerotation)） |
| 混合固定用 `SRC1_ALPHA`（dualSrcBlend），不检查设备是否支持 | `rt64_raster_shader.cpp:336-337` | **所有 Mali 都没有这个特性**，驱动仍返回成功，画面全白（Goemon 在 Mali-G57 上实测） |
| `preferHDR`：设备本地内存大于 512 MB 时成立 | `plume_vulkan.cpp:4136` | 手机是统一内存，一律走 RGBA16 渲染目标，在分块 GPU 上更费带宽（推断） |
| 空闲线程约每 1 ms 派发一次计算任务，用来保持 GPU 频率 | `rt64_workload_queue.cpp:1179-1222` | 耗电、发热（推断） |
| 没有 `VkPipelineCache`，也没有磁盘缓存 | `plume_vulkan.cpp:1393,1652` | 每次启动都重新编译管线 |

**RT64 实际依赖、但没有检查的 Vulkan 特性：**

- **descriptor indexing：** 8192 项的纹理表要用到 non-uniform 采样索引、update-after-bind、partially bound、variable count 和 runtime array。
- **dualSrcBlend。**
- **depthClamp。**

安卓的 Vulkan 档案 2022、15、16 都不要求这三项。只有 Android 17 档案（VRA17，Vulkan 1.4）要求 dualSrcBlend 和 descriptor indexing，而且只约束以 Android 17 首发的芯片。

**不需要的：** timeline semaphore、dynamic rendering、Int64、Float16、几何着色器、sample-rate shading。编出的 SPIR-V 只有 Shader、Sampled1D、SampledBuffer、ImageBuffer 和 non-uniform 索引几项能力。

**着色器：** 构建期用 DXC 把 HLSL 编成 SPIR-V 再嵌进程序，非 Windows 平台运行时不编译 HLSL。x86 专属代码只有 hlsl++，它在 arm64 上自动改用 NEON。

### N64ModernRuntime

- **窗口句柄类型对不上：** `renderer_context.hpp` 在安卓上把 `WindowHandle` 定义为 `SDL_Window*`，旁边留着 TODO，而 plume 安卓路径要的是 `ANativeWindow*`。
- **线程：** 优先级在 Linux 和安卓上没有实现（TODO）。
- **轮询：** 图形线程和主循环都每 1 ms 轮询一次。linkzenic 的 fork 把安卓的主循环间隔改成 16 ms。
- **前后台：** 没有暂停机制。Goemon 加了 `set_app_paused` 和 VI 线程的暂停闸门。
- **内存：** 先保留 4 GiB `PROT_NONE`，再把 512 MiB 设为可读写，没有写死页大小，16 KB 页没有问题。macOS arm64 本身就是 16 KB 页。
- **ROM 与存档：** `select_rom` 只接受文件系统路径，并把 ROM 复制进配置目录；存档用临时文件加改名写入。`create_directories` 可能抛异常，Goemon 在 SD 卡被拔出时因此崩溃。
- **错误提示：** 错误框只打到 stderr，而安卓会丢弃 stderr。

## 设备门槛（推断）

| GPU | 判断 |
| --- | --- |
| Adreno 6xx 及以上（骁龙 845 起） | 首选。dualSrcBlend 支持。Goemon 遇到的官方驱动问题：Adreno 6xx 着色器链接失败（驱动 0746），Adreno 630 在帧缓冲拷贝的计算派发里空指针。都有绕过办法 |
| Mali G7x 及以上 | 需要 dualSrcBlend 的回退方案（Goemon 1.0.3 用单源混合近似）。nullDescriptor 也可能缺 |
| PowerVR、三星 Xclipse | 没有数据 |

启动时应检查 descriptor indexing 和 depthClamp，不满足就用对话框说明，不要白屏或崩溃。逐型号核对以后用 gpuinfo.org 做，**本次没有做**。

## 分发与 ROM 派生代码

APK 的 `libmain.so` 必须链接两份由 ROM 生成的代码：`build/recomp/cpu-bound/generated/funcs_*.c` 和 RSPRecomp 生成的 `build/recomp/audio-probe/audio.cpp`。原生导入器（[P1](native-rom-importer.md)）在运行时只生成文本、头像和战斗美术，生成不了这两份代码。

| 方案 | 说明 | 结论 |
| --- | --- | --- |
| a. 构建机出 APK，侧载 | 与 Deck 包相同的“自用、勿再分发”定位 | **建议采用。** 也是全部社区先例的做法 |
| b. 玩家自己构建 | 在电脑上 `make`，再跑安卓构建，最后 `adb install` | 与 P0“玩家不装 Python／编译器”的目标冲突 |
| c. 不含 ROM 派生代码的 APK，在设备上生成代码 | 可能的途径是 N64Recomp 的实时重编译器（sljit，支持 ARM64） | 不在本计划内。`generate_cpu.py` 对生成代码的改写（约 150 个 `NATIVE_HOOKS` 改名、overlay 查找、地图宽度字面量）和 RSPRecomp 音频都要改成运行时实现，工作量很大 |

需要注意两点：

- **项目对二进制分发的定位前后不一。** `tools/release/build_release.py` 按 GitHub 公开发布准备 macOS 包，Linux 包却写着“自用”。APK 跟随维护者的最终决定，见“待定”。
- **安卓开发者验证。** 2026-09-30 起在巴西、印尼、新加坡、泰国先行，2027 年推广到全球。届时侧载 APK 要由经过验证的开发者签名，否则只能走“高级流程”，或用最多 20 台设备的有限分发（[说明](https://developer.android.com/developer-verification)）。这会影响侧载方式，需要持续关注。

安卓构建机与 Linux 构建一样，只需要四样输入，见三平台计划的“构建步骤在哪台机器做”：

- `build/recomp/cpu-bound/`
- `build/recomp/audio-probe/audio.cpp`
- 打过补丁的 `build/recomp/upstream/`
- `build/fonts/`

## 改动清单

严重程度：**阻塞**表示编译不过或不能运行；**必改**表示能运行但在安卓上是错的或不能用；**次要**是性能、体验或只影响开发功能。

### 1. 构建（阻塞）

- **CMake 闸门：** `src/host/CMakeLists.txt:62-64` 只放行 Apple 和 Linux，安卓的 `CMAKE_SYSTEM_NAME` 是 `Android`。
- **产物形态：** 游戏要改成 `add_library(main SHARED …)`，加上 `SDL_main`，并链接 `android` 和 `log`。`srw64-frame-host` 和各测试程序不进安卓构建。
- **构建机工具：** `cmake/NativeGpu.cmake`、`cmake/PixelCompositor.cmake`、`src/native/ui/CMakeLists.txt` 和 RT64 都按目标平台挑 DXC、编译 `file_to_c`。改为：
  - 按 `CMAKE_HOST_SYSTEM_NAME`/`CMAKE_HOST_SYSTEM_PROCESSOR` 选 DXC；
  - `file_to_c` 在构建机上单独编译后导入。Goemon 的做法是传 `-DRT64_FILE_TO_C`。
- **崩溃调用栈：** `src/host/host.cpp:511-535` 在 `__linux__` 下用 `execinfo.h`，安卓也定义 `__linux__`。bionic 从 API 33 才有 `backtrace`，而且替换信号处理会挡住系统 debuggerd 的 tombstone。条件改为排除 `__ANDROID__`。
- **依赖：**
  - FreeType、HarfBuzz、ICU 改为静态链接，因为 APK 放不下 `libicuuc.so.78` 这样带版本号的共享库。
  - ICU 用数据过滤，只保留断行、双向文本和文字系统数据，约 1–3 MB；全量数据约 30 MB。只有 `src/native/text/portable_text.cpp` 用 ICU，没有用 i18n 库的接口，`cmake/PortableText.cmake` 可以去掉 `ICU::i18n`。
  - HarfBuzz 的 pkg-config 回退在交叉编译时会找到构建机的库，安卓上要禁用。
  - SDL 的选择见“待定”。
- **NDK 与页大小：** 用 NDK r28 及以上，默认按 16 KB 页对齐。
- **构建脚本：** 新建安卓构建目录和 `build_android.py`（待写），结构照 `tools/release/build_linux.py` 和 `tools/release/linux/Dockerfile`：容器里装 NDK、JDK 和 Gradle。输入摘要检查可以沿用，glibc 和 `$ORIGIN` 相关的检查不适用。

### 2. 上游补丁（阻塞）

沿用 `tools/recomp/toolchain/prepare_rt64.py` 的白名单字符串替换，不 fork。下面的每一项都能在 Goemon 的 fork 里找到参照；RT64 是 MIT 许可，采纳时在 `docs/guide/provenance.md` 记下来源。

1. **能编译：** 去掉 `static_assert`；安卓上不构建 nativefiledialog；处理好 DXC 和 `file_to_c` 的构建机工具问题（见上节）。
2. **交换链格式：**
   - 安卓上用 `R8G8B8A8`，并经现有的 `GetRenderHookSwapChainTexture` 把实际格式交给宿主。
   - 本仓库有两处写死了 BGRA：截图读回（`src/host/graphics.cpp:209,239`）和对白合成管线（`src/host/dialogue_plume.cpp:39-41`）。
   - 还要核对 RecompFrontend 的界面渲染器。
3. **窗口与表面：**
   - `src/host/graphics.cpp` 加安卓分支，从 SDL 取得 `ANativeWindow*`；
   - 统一 ultramodern 的 `WindowHandle` 类型；
   - plume 加 `setRenderWindow`，回到前台时重建表面和交换链（Goemon 4c087b7、c69ce04）。
4. **GPU 兼容：**
   - Mali 的 dualSrcBlend 回退；
   - 启动时的特性检查与报错；
   - 安卓上关掉 `preferHDR` 和空闲计算线程；
   - 防护 `vkGetRefreshCycleDurationGOOGLE`；
   - Adreno 6xx 着色器链接问题的处理（Goemon 25fa568：spirv-opt 内联，改写 `EndianSwapUINT16`）；
   - “关闭帧缓冲效果”的开关，绕过 Adreno 630 的问题。
5. **运行库：**
   - VI 线程加前后台暂停闸门，放进 `tools/recomp/toolchain/prepare_runtime_lifecycle.py` 生成的改写版；
   - `create_directories` 改为不抛异常；
   - 错误框改用 `SDL_ShowSimpleMessageBox`；
   - 主循环在安卓上放宽轮询间隔。
6. **以后再做：** 预旋转、`VkPipelineCache` 磁盘缓存。

### 3. 宿主：入口、路径、资源（阻塞）

- **用户目录：** `default_user_dir`（`src/native/app/runtime.cpp:103-123`）在安卓上落进 Linux 分支，要 `HOME` 或 `XDG_DATA_HOME`，两者在安卓上都不可用。加 `Platform::Android`，用 SDL 提供的应用内部存储路径。
- **随包资源：** `bundled_resource` 读 `/proc/self/exe`（`runtime.cpp:180-194`），在安卓上得到的是 `app_process64`。于是字体、对白文本和 HD 都找不到，共享界面会抛出“Shared UI needs a CJK font”。
  - **做法：** `fonts/`、`dialogue/`（786 个 txt，约 21 MB）和 `licenses/` 放进 APK 资源，第一次启动时按 versionCode 解出到私有目录，再把资源根目录指向那里。
  - **为什么要解出：** 对白加载用 `recursive_directory_iterator` 遍历目录，安卓的 `AAssetDir` 不能列出子目录。
- **启动入口：**
  - 现在只有 Apple 有无参数的图形入口（`src/host/host.cpp`）。安卓入口由 Java 端用 SAF（`ACTION_OPEN_DOCUMENT`）选 ROM，复制成私有目录里的 `rom.z64`，再把路径交给 C++ 组装 `Options`。
  - 必须复制，因为 `select_rom`、`sha256_file` 和 `fs::canonical` 都要真实路径。
  - `srw64.sh` 的逻辑（找 ROM、首次启动的语言）一并移进 C++，Windows 原本就计划这样做。
- **HD 包：**
  - 约 700 MB 的目录，不进 APK。宿主在用户目录 `files/user/hd` 找它（`launch.cpp`），和电脑上同一个 `SRW64-<版本>-HD.zip`。
  - 各 HD 图层用 `directory_iterator` 读目录，所以必须是解开的目录。
  - 已做（2026-10-05，`SetupActivity.importHd`）：启动页用 SAF 读 zip，只解 `hd/` 下的条目到 `files/user/hd.new`，有 `hd.json` 才与旧目录对换，失败或中断不动已装的包；按压缩字节显示百分比，先查剩余空间。三个入口：首次选完 ROM 问一次；长按图标的静态快捷方式「导入 HD 包」（`res/xml/shortcuts.xml`，动作 `org.srw64.game.IMPORT_HD`）；在文件管理器或浏览器里用本应用打开／分享 zip。游戏在运行时不导入（宿主不可重入，`SRW64Activity.running`），提示先关掉游戏。开发时仍可 `adb push` + `run-as` 放进去。
  - 尚未在真机上走过导入流程。
- **日志与报错：**
  - stderr 转到 logcat，同时写一份会话日志文件。
  - 面向玩家的 `std::abort()`（`graphics.cpp`、`src/host/audio.cpp`、`host.cpp`）改成先弹 `SDL_ShowSimpleMessageBox`。

### 4. 生命周期与存档（阻塞，最重要）

- **存档提交时机：**
  - 现在只有宿主返回 0 后才提交存档（`src/native/app/launch.cpp:204-205`）。安卓经常在后台直接杀掉进程（低内存或玩家划掉），整局进度不会成为“上次会话”，下次启动会悄悄接着更早的存档。
  - 要改成可以重复提交的原子检查点：进后台（`SDL_APP_WILLENTERBACKGROUND`）、`SDL_APP_TERMINATING`，以及每次 SRAM 落盘之后都提交一次。
  - 这会改动 P0 “异常终止不作为恢复源”的规则，需要维护者确认。
- **前后台切换：** 现在完全没有处理 `SDL_APP_*` 事件。
  - 进后台：暂停 guest 和 VI，暂停音频，停止 RT64 呈现，提交存档。
  - 回前台：重建表面和交换链，然后恢复。
- **卡死风险：** `src/native/ui/frontend.cpp` 的 `lock_ui()` 要等呈现回调清掉 `in_flight` 才返回，`apply_images` 也会一直等。表面丢失导致呈现停住时，主线程会永久阻塞，安卓报 ANR。改为限时等待，表面丢失时主动清除。
- **重复进入 `SDL_main`：** 运行库和宿主都不可重入。Activity 销毁时直接结束进程，清单里声明 `configChanges` 并固定横屏。
- **占用越来越多：** 每次启动新建一个会话目录，带一份 `dialogue.json`；还有前 30 秒的音频录制和每秒改写的 live JSON。安卓上关掉录制和 live JSON，并清理旧会话。`content-cache/`、`sessions/` 和 ROM 排除在安卓自动备份之外：它们是 ROM 派生内容，体积也超出备份配额。

### 5. 画面、内存与性能（必改）

- **画质默认值：** 现在默认 4 倍 MSAA 加 4 倍分辨率。安卓默认关 MSAA、3–4 倍分辨率，参照 Goemon 的实测。
- **HD 内存：**
  - 全部是未压缩 RGBA8，带完整 mip 链，背景最大 1920×1440。
  - RmlUi 的图片缓存从不淘汰（`frontend.cpp`），要加 LRU。
  - 20.6 MB 的 HarmonyOS SC 字体，每个 `FontSet` 各读一份，最坏五到十份，要改成共享一份缓冲。
  - 收到 `SDL_APP_LOWMEMORY` 时释放缓存。
- **诊断级别：** 原先未设置环境变量时默认是完整诊断（定期转储 RDRAM、读回整帧并编码 PNG）；完整诊断已从宿主删除，安卓入口不用再设置。
- **刷新率：** 90/120 Hz 的手机上请求 60 Hz，避免帧时间抖动。

### 6. 输入与界面

**掌机阶段（必改，量小）：**

- 手柄这一套原样可用：绑定、改键、手柄→页面按键的桥接、PromptFont 提示。
- 截获安卓返回键，映射为 B。
- 在 `src/host/steam_deck.hpp` 旁边加一个“掌机”判断，供设置页隐藏窗口大小、全屏这些行。

**手机阶段（必改，量大）：**

- **触屏虚拟手柄：** 覆盖 N64 键和宿主键（L2、R2、视图键等）。按键经调试接口的输入注入口（`src/host/debug_protocol.hpp`）送进去，页面和按键提示就会把它当成手柄。
- **界面密度：**
  - `frontend.cpp` 的 `sync()` 把一个 dp 当作一个 SDL 点。安卓上像素比是 1，在 2400×1080 的手机上 17 dp 的字只有约 1.5 mm 高。
  - 要改用显示器的 DPI 或缩放系数。
  - 手机横屏只有约 360–430 dp 高，而现有页面按 540–720 dp 设计，需要手机版布局或最小物理尺寸。
- **安全区：**
  - 游戏画面按 `src/host/game_frame.hpp` 最宽到 16:9，在 20:9 的屏幕上两侧留黑，本身不受刘海影响。
  - 铺满整窗的 RmlUi 页面要避开刘海和手势条。
- **触屏细节：**
  - 加触屏用的提示文字；
  - 原生页面加返回和翻页按钮；
  - 支持拖动滚动；
  - 解决触摸后 hover 高亮不消失；
  - 合并触摸移动事件，现在每个事件都要等一次 `lock_ui()`。
- **输入法：** 资金输入框用数字键盘。`src/native/ui/text_input.cpp` 每帧调用一次 `SDL_SetTextInputRect`，在安卓上每次都是一趟 JNI 调用，改为位置变化时才调用。

### 7. 调试接口（必改）

- 现在的 AF_UNIX 套接字放在应用私有目录里，`adb forward` 访问不到，路径长度也接近 `sun_path` 的 108 字节上限（`src/host/debug_server.cpp`）。
- 改用三平台计划 X0 已经规划的回环 TCP 加令牌，再经 `adb forward tcp:` 连入。`srw64ctl` 和 MCP 不需要为安卓另写。
- 实际做法（已实现）：安卓用抽象 socket `@srw64-debug`，`adb forward tcp:0 localabstract:srw64-debug` 连入，不带令牌。桌面三平台 2026-10-06 改成了回环 TCP 加令牌，见[调试接口 · 连接方式](../guide/debug-interface.md#连接方式)。
- release 构建要能编译时去掉调试接口。

### 已经可移植、不用改

- **图形：** HD 图层、对白合成、RmlUi 都走 plume 并嵌入 SPIR-V。`native_gpu` 的环形常量缓冲、16 字节 push constant、按格式区分的管线都适合移动 GPU。
- **ARM 正确性：** 弱内存序、16 KB 页和小端假设，都已经在 macOS arm64 上验证过。
- **文字与美术：** 文字栈全是 C 接口，字体从内存加载。画面比例从 4:3 到 16:9 自适应。
- **应用层的 POSIX 部分：** `flock`、原子改名和 `std::filesystem` 在 bionic 上都能用。
- **调试工具：** 各类 QA 和探针工具都由环境变量启用，默认不生效。

## 阶段

### A0 可行性验证（不需要 ROM）

- 用 NDK 交叉编译根目录 CMake 的应用层和导入器测试，用 `adb shell` 在设备上运行。
- 给 RT64 和 plume 打补丁 1，编出安卓版；SDL 建窗，经 plume 清屏出一帧。
- 定下 SDL 路线（待定 1）。

**验收：** 设备上测试全部通过；Adreno 设备上能稳定显示清屏颜色，切后台再回来不崩溃。

### A1 安卓掌机，原版画面

- 安卓构建脚本、Gradle 工程和 `libmain.so`；
- 安卓入口、资源解出、SAF 选 ROM；
- 上游补丁 2、3、5，以及补丁 4 里的特性检查；
- 前后台暂停与存档检查点；
- 日志转到 logcat。

**验收（Adreno 掌机，只用手柄）：**

- 开场 → 姓名 → 第一话 → 存档；
- 切到后台 5 分钟再回来能继续；
- 划掉进程后冷启动，能从后台检查点恢复；
- L3/R3 切换语言与画面；
- 设置窗口能打开。

### A2 GPU 兼容与 HD

- Mali 回退、各项 Adreno 绕过、画质默认值；
- 导入 HD 包，并按安卓收紧内存上限；
- 可选：预旋转。

**验收：**

- 一台 Adreno 手机加一台 Mali 手机，接手柄，开 HD 跑同一流程；
- 连续 30 分钟内存不持续上涨；
- 记录帧率和发热。

### A3 手机触屏

- 触屏虚拟手柄、DPI、安全区、手机版布局、触屏提示与返回键。

**验收：** 20:9 手机只用触屏，完成开场 → 第一话 → 一场战斗 → 存档。

### A4 打包

- **签名与文件名：** 用自签名密钥，文件名照 Deck 包的规则：`SRW64-Android-<版本>-<提交日期>-<提交>.apk`。
- **许可文件补全：** Linux 包现在也漏了 RT64、N64ModernRuntime、RmlUi、RecompFrontend、volk 等的许可。
- **HarmonyOS 字体：** 许可只允许随软件不经修改地分发，所以不能做子集化来缩小 APK。
- **说明：** 写 README 和侧载说明。

## 待定

1. **SDL 路线：**
   - a. 与桌面一样用 SDL3 + sdl2-compat。版本锁相同，SDL3 的 Java 胶水自带 SAF 和 `content://` 支持，但还没有任何移植验证过 sdl2-compat 在安卓上能用。
   - b. 用 SDL2 2.32 自带的安卓胶水。全部社区先例都这样做，代价是多出第二个 SDL 版本来源。
   - c. 宿主直接改用 SDL3 接口。还要处理 RT64 自己对 SDL2 的依赖，改动最大。

   建议在 A0 先试 a，不行就退到 b。
2. **安卓上的存档规则：** 后台检查点是否算正常提交（见第 4 节）。
3. **分发定位：** 安卓与 Linux 一样定为自用侧载，还是跟随 macOS 的公开发布。这是全项目的问题，不只关系到安卓。
4. **验收设备：** 手上有没有安卓掌机或手机。A1 起至少需要一台 Adreno 设备，A2 还需要一台 Mali 设备。
5. **minSdk：** 定 28 还是更高。
6. **libadrenotools / Turnip：** 建议首批不做，以后作为可选项。

## 不在本计划内

- 上架 Google Play；
- 不含 ROM 派生代码的 APK（在设备上重编译）；
- x86_64 和 32 位 ABI；
- 竖屏布局；
- 云存档。
