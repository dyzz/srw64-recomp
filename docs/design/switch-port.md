# Nintendo Switch 移植方案（调研）

2026-10-17。接续[PS Vita 移植方案](psv-port.md)、[安卓移植方案](android-port.md)与[三平台移植计划](three-platform-port.md)。本页是可行性调研和建议方案，依据有三部分：

- 本仓库的静态审计：main `68d49cec`；`vita` 分支 `a2b7fbc6`（GL 渲染路径与 `srw64-gl-host`）；固定提交的上游检出（N64ModernRuntime、N64Recomp、RT64、RecompFrontend，见 `build/recomp/upstream/`）；HD 包 1.1 的实测体积；
- Switch 自制平台的公开资料：devkitPro 的包仓库与 libnx/newlib 源码、switchbrew 示例、Atmosphère 源码与发布页、NH Switch Guide；
- 社区先例：libultraship 系的 Switch 构建，以及 2026 年 9–10 月刚出现的几个静态重编译 Switch 移植（其中两个就是 N64Recomp）。读了源码、README 和发布页，没有编译或运行。

**本机没有装 devkitPro，没有为 Switch 编译或运行过本项目，也没有 Switch 实机。** 标注约定：**已查证**＝在一手来源（仓库文件、发布页、官方文档）里读到，附链接与日期；**二手**＝只在别的项目文档、论坛或新闻里看到；**推断**＝工程判断，没有实测。

## 结论

- **可行，而且比 Vita 容易。** 这次不是从零开始：`vita` 分支已经有一条不依赖 RT64/plume 的渲染路径（自写 F3DEX2 子集解释器 + GL 2.1/GLES 2.0 渲染器 + RmlUi GL 后端 + 原生对白与宿主标签绘制的 GL 版），并且产出了不链接 RT64 的 `srw64-gl-host`，27 个检查脚本在桌面上通过。Switch 的 GPU 能力远高于 Vita，devkitPro 的 Mesa 提供 GL 4.3 与 GLES 3.x，这套代码**几乎可以原样搬过去**。CPU 侧是 AArch64，安卓 arm64 已经跑通，Vita 那一整类 32 位问题都不存在。
- **RT64/plume 用不上（已查证）。** devkitPro 的 Switch 包里没有 Vulkan，只有 Mesa 20.1（nouveau，GL/GLES）和 deko3d（底层 API，着色器要离线编译）。官方 SDK 的 NVN 与 Vulkan 自制系统拿不到。2026 年 9 月起社区有几个 Mesa NVK（Vulkan）移植，已有一个 Xbox 360 重编译游戏在上面发布，但都只有一两个月历史、不在 devkitPro 里，**本页不把它当主路线**，只列为 S5 实验。
- **路线：** devkitA64 + libnx + devkitPro 的 SDL2 2.28.5 + Mesa（EGL 建 GLES 2/3 或 GL 兼容上下文）。N64 画面、界面、对白全部走 `vita` 分支的 `src/gl`；Switch 专用代码只有平台层（入口、小程序模式检查、底座切换、时钟、存储路径、Joy-Con）和运行库的几处 libnx 缺口补丁。
- **范围比 Vita 大：** Switch 应用模式有约 3.2 GB 内存，HD 包按场景懒加载是放得下的（推断）。但现有 HD 图层都是 plume 着色器和 RT64 贴图替换，GL 路径上要重做。所以分两步：先发「原版画面 + 三语 + 现代页面」（与 Vita 同一套），再把 HD 图层移到 GL 4.3 上（S3）。
- **主要工作量（推断）：**
  1. 平台层与打包（NRO、romfs、路径、小程序模式、底座 720p/1080p 切换、Joy-Con 与触屏）：量不大，有成熟配方可抄；
  2. 运行库 libnx 补丁：`pthread_detach`、`mmap`、线程命名/优先级、`copy_file`，比 Vita 少一半；
  3. GL 路径的宽屏（与 Vita V3 共用）；
  4. HD 图层的 GL 版（S3），是最大的一块，也是唯一 Vita 用不上的一块。
- **验收必须靠实机。** Mac 上的 Switch 模拟器（Ryubing、Eden）能加载 NRO，但用 Mesa 渲染的自制程序在模拟器上的表现没有找到可靠报告，性能和内存更不能当依据。用户准备买未修补的初代机（Erista）+ 短接器进 RCM + Hekate/Atmosphère + 离线 emuMMC，这正是 NH Switch Guide 的标准路线，也是先例项目们的测试环境。

## 目标与范围

| 项 | 选择 | 理由 |
| --- | --- | --- |
| 硬件 | 所有能跑 Atmosphère 的 Switch（初代 Erista 未修补机、改芯片的 Mariko/Lite/OLED）。Tegra X1，4×Cortex-A57，游戏用 1020 MHz；GPU Maxwell GM20B；掌机 1280×720，底座 1920×1080 | 只有一种 SoC，各机型默认频率相同。Switch 2 不在范围内（自制系统尚未成熟，见「不在本计划内」） |
| 工具链 | devkitA64（GCC 15.2，r29.1，2026-10-11）+ libnx 4.12.0，用 devkitPro 官方 Docker 镜像 `devkitpro/devkita64` 在 Mac 上交叉编译 | 与 Vita 用 VitaSDK 镜像同一方式；先例项目的 CI 都这样做 |
| 图形 | `vita` 分支的 `src/gl` 路径，跑在 devkitPro 的 Mesa 20.1（nouveau）上；HD 阶段用 GL 4.3 | RT64/plume 不可用；Mesa 是 devkitPro 唯一的 GL；deko3d 留作性能后备 |
| 窗口、输入、音频 | devkitPro 的 SDL2 2.28.5（`switch-sdl2`） | 宿主只用 SDL2 API（经 sdl2-compat）；SDL3 的 Switch 后端只对签了 NDA 的开发者开放 |
| 内存 | 只支持应用模式（「按住 R 启动任意游戏」或 NSP 前导），约 3.2 GB | 小程序（相簿）模式只有约 0.5 GB 共享池；所有先例都要求应用模式 |
| 分发 | NRO 侧载，玩家把 `rom.z64` 放进 `sdmc:/switch/Marchwind64/` | 与安卓、Vita 相同：包里含 ROM 派生代码，只做侧载；不发官方 NSP |
| 验收 | 先 Docker 编译 + 模拟器冒烟，再实机 | 模拟器只能证明“能加载、大体能画”；性能、内存、时序只认实机 |

## 平台事实

### 图形（2026-10-17 查证）

- **没有 Vulkan（已查证）。** devkitPro 的 `switch/` 包目录有 `mesa`、`glad`、`libdrm_nouveau`、`glu`、`SDL2*`、`glfw`，没有 vulkan、sdl3（[devkitPro/pacman-packages `switch/`](https://github.com/devkitPro/pacman-packages/tree/master/switch)，2026-10-17 列目录）。Switch 官方 SDK 2016 年就通过了 Vulkan 1.0、GL 4.5、GLES 3.2 认证（二手：[Phoronix](https://www.phoronix.com/news/Nintendo-Switch-Vulkan-Conform)，2016-12），但那是 NVN 之上的官方驱动，自制系统用不了。Zelda64Recomp 维护者 2024 年明确说过不打算支持 Switch，因为 RT64 要有 deko3d 后端（已查证：[Zelda64Recomp#334](https://github.com/Zelda64Recomp/Zelda64Recomp/issues/334)，2024-06-01；[#442](https://github.com/Zelda64Recomp/Zelda64Recomp/issues/442)，2024-07-26）。
- **Mesa（已查证）。** `switch-mesa` 是 Mesa 20.1.0 加补丁，pkgrel 5，fincs 2025-07-04 最后改动（[PKGBUILD](https://github.com/devkitPro/pacman-packages/blob/master/switch/mesa/PKGBUILD)）。补丁（`switch-mesa-20.1.0-5.patch`）的要点：
  - 只编 gallium `nouveau` 驱动，加一个 `switch` EGL 平台，不带 LLVM；强制 TGSI、关掉 NIR。
  - EGL 配置声明 `EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT | ES2 | ES3`，只有窗口表面，带 `KHR_create_context`。
  - 请求版本 ≥3.2 且要 core 时才建核心上下文；兼容档或低于 3.2 的请求走 Mesa 的默认（兼容/传统）上下文，所以 **GL 2.1 兼容上下文按代码看是拿得到的**（读了代码路径，没运行）。GLES 1/2/3 都可用。
  - 上游 Mesa 20.1 的 `nvc0_screen.c` 报 `GLSL_FEATURE_LEVEL` 430（兼容档同样 430），补丁没改：**上限是 GL 4.3（核心/兼容）、GLSL 4.30、GLES 3.2**。
  - **没有着色器磁盘缓存**：Meson 把 `horizon` 当 Windows 处理关掉了 shader cache。每次启动所有着色器重新编译。
  - switchbrew 官方示例（[switch-examples `graphics/opengl`](https://github.com/switchbrew/switch-examples/tree/master/graphics/opengl)，最后提交 2026-02-01）统一用 `eglBindAPI(EGL_OPENGL_API)`、4.3 核心档、RGBA8/D24S8，着色器 `#version 330 core`（`textured_cube` 用 `320 es`）；`es2gears` 用 `EGL_CONTEXT_CLIENT_VERSION 2`。链接 `-lglad -lEGL -lglapi -ldrm_nouveau -lnx`。
- **编译开销（二手）。** SwitchWakerHD 在自己加了磁盘缓存的 Mesa 上测得首次每个程序链接约 180 ms，命中缓存约 13 ms（[SwitchWakerHD `docs/switch-port.md`](https://github.com/centollOS/SwitchWakerHD/blob/main/docs/switch-port.md)，2026-10-16）。DK64Recomp-NX 记录首次见到房间时贴图与着色器造成 33–72 ms 的卡顿，启动黑屏约 10 秒（[DK64Recomp-NX README](https://github.com/Trinalord777/DK64Recomp-NX)，v1.1.2，2026-10-11）。
- **deko3d（已查证）。** devkitPro 的底层 API（[devkitPro/deko3d](https://github.com/devkitPro/deko3d)，v0.5.0 2023-11-26，最后推送 2026-02-18），没有运行时 GLSL 编译器，着色器用 [uam](https://github.com/devkitPro/uam)（基于 Mesa 19 的 GLSL/TGSI + nv50_ir）离线编译。SwitchWakerHD 2026-10-07 从 Mesa GL 换到 deko3d，报告帧率持平或更好、少一个 Mesa GL 线程（二手，同上）。
- **社区 NVK（已查证仓库存在，成熟度未知）。** [danfromtico/mesa-switch](https://github.com/danfromtico/mesa-switch)（2026-09-10 建，2026-10-13 推送，无许可证文件）：Mesa 26.2 + 对 libnx 写的 Horizon 后端，静态链接的 NVK（README 称 Vulkan 1.3）与 NVC0/Zink 上的 GL/GLES。自述限制里有「不能创建固定地址或保留的 CPU 映射」、没有 `VK_EXT_map_memory_placed`，EGL 配置标为非一致。同类还有 NaGaa95/mesa-switch、HayatoG/switch-nvk、PalindromicBreadLoaf/nxvk。已发布的使用者：[StevensND/nfsmw-nx](https://github.com/StevensND/nfsmw-nx)（Xbox 360 静态重编译，2026-09-27 建，229★，二手：默认频率 30 帧，管线缓存存 SD 卡，「第一场比赛会卡几秒」）。

### SDL（已查证）

- `switch-sdl2` 2.28.5-4（[PKGBUILD](https://github.com/devkitPro/pacman-packages/blob/master/switch/SDL2/PKGBUILD)，2024-03-24）：同时定义 `SDL_VIDEO_OPENGL_EGL`、`SDL_VIDEO_OPENGL`、`SDL_VIDEO_OPENGL_ES2`，默认 ES 2.0，桌面 GL 走 SDL 的通用 EGL 路径，建窗口前设 `SDL_GL_CONTEXT_PROFILE_MASK` 即可（读代码，推断可用）。libultraship 的 Switch 分支就是 SDL2 + glad + 请求 GL 2.1 上下文。手柄走 libnx HID（Joy-Con 双持/掌机/Pro，带震动），触屏 `hidGetTouchScreenStates`，音频 audren，pthread 开、filesystem 模块关。
- SDL3：官方 Switch/Switch 2 支持在**只对 NDA 开发者开放的单独仓库**（[SDL `docs/README-switch.md`](https://github.com/libsdl-org/SDL/blob/main/docs/README-switch.md)，2025-10-14）。devkitPro 自己有未打包的 libnx SDL3 分支 `switch-sdl-3.4`（最后提交 2026-02-01），不能指望。本项目桌面用 SDL3 + sdl2-compat，宿主只调 SDL2 API，Switch 直接链 SDL2 即可。

### CPU、线程与内存映射（已查证，除特别标注）

- **频率。** 官方掌机/底座 CPU 都是 1020 MHz；sys-clk 可调到 1785 MHz，GPU 超过 768 MHz 要原装充电器（[sys-clk](https://github.com/retronx-team/sys-clk)）。SwitchWakerHD 报告一台 Erista 在 1224 MHz 下无法唤醒，删掉了应用内调频（二手）。
- **核心。** hbloader 的 NPDM 允许 CPU 0–2、优先级 28–59（[nx-hbloader `hbl.json`](https://github.com/switchbrew/nx-hbloader/blob/master/hbl.json)），**自制程序可用 3 个核**，核 3 归系统。
- **线程。** devkitA64 的 GCC 以 `thread_file='posix'` 构建，`std::thread` 可用（[buildscripts gcc 补丁](https://github.com/devkitPro/buildscripts)，2026-06-10）。libnx `newlib.c`（2026-02-02）创建 pthread 时**默认栈 128 KiB**，统一优先级 0x3B、跑在进程默认核；newlib 的 `pthread_attr_setschedparam` 只存不用（[devkitPro/newlib `pthread.c`](https://github.com/devkitPro/newlib/blob/devkitPro/libgloss/libsysbase/pthread.c)，2026-01-24）。要设核心或优先级得直接调 `svcSetThreadCoreMask`/`svcSetThreadPriority`。**`pthread_detach` 不支持**（libnx 里注释为 Unsupported），`std::thread::detach()` 会失败；DK64Recomp-NX 实机上因此启动即中止，用 `--wrap=pthread_detach` 空实现解决（已查证其 README）。
- **没有 `mmap`。** newlib 与 libnx 都没有 `sys/mman.h`。libnx 有 `virtmemFindAslr`/`virtmemFindCodeMemory`/`virtmemAddReservation`（只是 libnx 自己的记账）和 `svcMapProcessCodeMemory` 等特权调用。SwitchWakerHD 用后者保留 4 GiB 再分段映射堆内存（二手）；DK64Recomp-NX 的 `mman_switch.c` 干脆把 `mmap` 实现成 `aligned_alloc` + 清零（已查证源码）。
- **JIT。** libnx `jitCreate` 在 Atmosphère（Mesosphère）下可用，与启动模式无关。本项目不需要（见运行库一节）。
- **宏（已查证）。** 编译器只定义 `__DEVKITPRO__`、`__DEVKITA64__` 和标准 aarch64 宏；**`__SWITCH__` 要由构建脚本 `-D` 加上**（switch-examples 的 Makefile 和 SDL2 都这样做）。目标是 `aarch64-none-elf`，**没有 `__unix__`、`__linux__`**。
- **标准库。** libstdc++ 带 `std::filesystem`（HarbourMasters 的 Ghostship 不加守卫地用，并有 Switch CI）；POSIX 信号量 `sem_init/sem_wait/sem_post` 于 2026-01-24 在 newlib 实现（是否已进 r29.1 未核实）；newlib 声明并编了 `quick_exit`，libstdc++ 是否导出 `std::quick_exit` 未核实。`std::filesystem::copy_file` 最后会调 `fchmod`，SD 卡驱动拒绝，DK64Recomp-NX 实机上存档写失败（已查证其 README）。
- **网络。** libnx 的 BSD socket（`socketInitializeDefault`，POSIX `socket/poll/select` 经 newlib devoptab）足够跑本项目的 TCP 调试接口。`nxlinkStdio()` 把 stdout/stderr 接回发送 NRO 的电脑（TCP 28771；上传服务端口 28280）。

### 内存（已查证）

- Atmosphère 内核为 4 GB 机型划的池：**应用 3285 MB，小程序 507 MB**；固件 23.0.0 起 Atmosphère 还从小程序池里拿走 40 MB（[Mesosphère `kern_k_system_control.cpp`](https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libmesosphere/source/board/nintendo/nx/kern_k_system_control.cpp)；Atmosphère 1.12.1，2026-10-16）。
- hbloader 给 NRO 的堆＝池总量 − 已用 − 2 MiB；开着自动录像时再少 96 MiB。应用模式实测日志「3281 MB used of 3285 MB」（二手：[deceptus_engine `switch_build.md`](https://github.com/varnholt/deceptus_engine/blob/master/doc/switch_build.md)，2026-08-22）。常被引用的「小程序模式约 442 MB」没有找到一手出处，上限就是 507 MB 减去系统占用。
- **GPU 内存不另算**：Mesa 的缓冲与纹理都从进程堆里经 nvdrv 映射（推断，与 DK64Recomp-NX 的记录一致）。DK64Recomp-NX 另记录 nvdrv 的传输内存默认 8 MiB，GLideN64 超过约 5,000 张纹理后 `GL_OUT_OF_MEMORY`，调到 32 MiB 才好（已查证其 README）。
- 进入应用模式的两种方式：按住 R 启动任意已装游戏（Atmosphère 的 `override_any_app_key`），或安装 NSP 前导。

## 已有先例

| 项目 | 要点 |
| --- | --- |
| **[DK64Recomp-NX](https://github.com/Trinalord777/DK64Recomp-NX)**（N64Recomp，GPL-3，2026-10-06 建，v1.1.2 2026-10-11） | **最直接的先例。** N64ModernRuntime 跑在 libnx 上，RT64 换成 Mesa 上的 GLideN64，接到运行库的 `RendererContext`，自己解码 RT64 扩展 GBI（0x64）做宽屏对齐。补了 `pthread_detach`、`mmap`、线程栈与核心、崩溃处理。实机踩坑表很有用：`copy_file` 的 `fchmod`、图形线程呈现时阻塞在垂直同步把 VI 饿死、SDL 音频缓冲 256 帧太小（改 1024）、DK64 依赖 RDP 冻结的帧序、nvdrv 传输内存。性能：底座默认 30 帧；60 帧插帧时一帧两趟 9.35+8.79 ms（1020 MHz），1581 MHz 下 6.82+5.90 ms，CPU 是瓶颈。要求应用模式启动 |
| **[BM64Switch](https://github.com/paulodiasred/BM64Switch)**（N64Recomp，GPL-3，v1.0.0 2026-10-17） | N64Recomp + N64ModernRuntime，渲染用 libultraship 的 Fast3D（OpenGL）。同时发 `.nro` 与 `.nsp`（NSP 用玩家自己的 `prod.keys` 生成，要 sigpatches）。ROM 放 `sdmc:/switch/bm64recomp/` 等处，首次运行校验并复制 |
| [SwitchWakerHD](https://github.com/centollOS/SwitchWakerHD)（Wii U 静态重编译，MPL-2.0，2026-10-04 建） | deko3d 渲染、uam 离线编译着色器；4 GiB 地址保留 + 按段映射；`svcSetThreadCoreMask` 绑核；`pthread_detach` 空实现。迁 deko3d 前用过 Mesa GL，文档里有两者对比 |
| [nfsmw-nx](https://github.com/StevensND/nfsmw-nx)（Xbox 360 静态重编译，GPL-3，2026-09-27 建） | 跑在社区 NVK（Vulkan）上，管线缓存落盘；要求 39 位地址空间的前导（二手） |
| libultraship 系：Ship of Harkinian、2Ship2Harkinian、Starship、SpaghettiKart、Ghostship | 上游 libultraship 2024-04-29 删掉了 Switch 代码（[PR #531](https://github.com/Kenix3/libultraship/pull/531)），现在维护在 [Net64DD/libultraship](https://github.com/Net64DD/libultraship/tree/main-nx-nightly) 的 `main-nx` 分支：SDL2 + glad + Mesa，`gfx_opengl` 请求 GL 2.1、`#version 110/130` 着色器，没有 deko3d 后端。`SwitchImpl.cpp`：`appletGetAppletType()` 不是应用就弹错误「请按住 R 打开任意游戏」；按 `appletGetOperationMode` 选 1080p/720p；默认把 CPU 拉到 1785 MHz（超频），失焦降回 1020。打包是 `nx_create_nro`（NACP + 图标，不用 romfs），资源包放在 NRO 旁边。各项目最后的 Switch 发布：SoH 8.0.6-1（独立仓库 Shipwright-Switch，2025-11-29；主仓库 9.3.0 已无 Switch 版）、Starship v2.0.0（2025-05-25）、SpaghettiKart 0.9.9.1（2025-11-15）、Ghostship 2.0.0（2026-05-29，3.0.0 停发）、2Ship 只有非官方分支（4.0.0，2026-01-30）。**Switch 版普遍跟不上主线**，主因是上游迁 SDL3 而 Switch 停在 SDL2（推断） |
| 固件 21 的 TLS ABI 变化 | libnx 4.10.0 之前编的自制程序在固件 21 上崩溃，Atmosphère 1.10.0（2025-11-15）发布说明提到；SoH、SpaghettiKart 都因此重编。**结论：始终用当前 libnx 编，固件升级后要回归** |

## 本仓库现状与阻塞项

### 渲染：`vita` 分支的 GL 路径能复用多少

GL 路径的代码量（`vita` 分支 `a2b7fbc6`）：`src/gl/display_list.cpp` 825 行、`gl_renderer.cpp` 564 行、头文件约 230 行；宿主侧 `native_gl.cpp` 160、`dialogue_gl.cpp` 118、`src/native/ui/ui_renderer_gl.cpp` 222、`gl_only_stubs.cpp` 34、`native_gpu_cpu.hpp` 50；`graphics.cpp` 里 13 处 `SRW64_GL_ONLY` 分支。

| 组件 | 现状（`vita` 分支） | Switch 要改的 |
| --- | --- | --- |
| GL 头文件 `src/gl/gl_api.hpp` | `__vita__` → vitaGL，`__APPLE__` → 系统 GL，否则 Mesa 的 `GL/gl.h` + `GL_GLEXT_PROTOTYPES` | 加 `__SWITCH__` 分支：glad（`switch-glad`）或 `GLES2/gl2.h`。若走 GLES 上下文就定义 `SRW64_GLES` |
| 着色器方言 | 四个模块（解释器、RmlUi、对白、宿主标签绘制）都按 `SRW64_GLES` 选 `#version 100` + `precision`，否则 `#version 120`；用 `attribute/varying/texture2D/gl_FragColor` | **不用改。** Mesa 20.1 的 GLES 2/3 上下文认 GLSL ES 1.00，兼容上下文认 1.20（推断，按补丁代码与 Mesa 能力） |
| 上下文与顶点数据 | `graphics.cpp:1127-1129` 请求 GL 2.1；`gl_renderer.cpp` 等处有客户端顶点数组（如呈现用的 `glVertexAttribPointer(..., corners)`），没有 VAO | 这两点决定**不能用示例里的 4.3 核心档**（核心档禁止客户端数组、必须绑 VAO）。用 **GLES 2.0/3.x 上下文**（与 Vita 同一条 `SRW64_GLES` 路径，最省事）或 GL 2.1 兼容上下文。S0 两种都试，取能建出来、驱动开销小的那个 |
| 解释器 `display_list.cpp` | 纯 CPU，F3DEX2 子集、TMEM、组合器键、扩展 GBI 0x64、原生命令 | 不改。64 位下没有 Vita 的指针宽度问题 |
| 渲染器 `gl_renderer.cpp` | 每个颜色缓冲一个 FBO（共享深度）、组合器键各一个程序、1024² 图集页 6 张 | 不改。着色器按组合器键惰性编译：Switch Mesa 没有磁盘缓存，要在启动时预热已知的键（V0 统计：7 种组合器、35 种绘制状态，见[统计](../data/gbi-usage.md)，在 `vita` 分支）。按每个约 180 ms 估，预热几秒（推断） |
| RmlUi GL 后端 | `RenderInterfaceCompatibility`，GL 调用推迟到图形线程 | 不改 |
| 呈现 | `SDL_GL_SwapWindow`，交换间隔 0，4:3 居中 | 掌机 1280×720、底座 1920×1080 随 `appletGetOperationMode`/SDL 窗口事件切换（本项目已有窗口尺寸变化处理）。注意 DK64Recomp-NX 的教训：图形线程在呈现里阻塞于垂直同步会把 VI 生产者饿死；保持交换间隔 0、由现有 VI 节拍配速 |
| 宽屏 | GL 下扩展指令已按原点换算，呈现与宿主图层仍按 4:3（`vita` V1「还没做」） | 与 Vita V3 同一件事；Switch 屏幕是 16:9，S2 必做 |
| 战斗残影等反馈效果 | 解释器把「从颜色缓冲装贴图」改读那张画面 | 不改 |
| HD 图层 | `gl_only_stubs.cpp` 给空入口，原画面照旧 | 首发不带；S3 移植（见下） |

**结论：Switch 版首发可以直接用 `srw64-gl-host` 的源码，渲染代码只加一个头文件分支和上下文属性。** 这也意味着 Switch 和 Vita 共用 GL 路径的全部后续工作（宽屏、字体共享、新状态补表），任一平台修了另一平台受益。

### HD 图层在 GL 上的做法（S3）

HD 包 1.1：zip 533 MB，3,646 张图（1,815 PNG + 1,831 JPG）。全部解码成 RGBA8 约 **12.4 GiB**（不含 mip）：地图 7.3 GiB（132 组，每组中位数 62 MiB、最大 88 MiB）、头像 1.3 GiB、背景 1.0 GiB、战斗精灵 1.0 GiB、地图机体图标 0.9 GiB、机体额外图 0.5 GiB（2026-10-17 用本机 `Marchwind64-HD-1.1.zip` 实测）。全常驻不可能，但现有实现本来就按场景懒加载、带淘汰（HD 地图有淘汰逻辑）。

现有 HD 分两类，GL 上做法不同：

| 类别 | 现状 | GL 上的做法（推断） |
| --- | --- | --- |
| 宿主自绘层：地图、背景、头像、剧情图片、5600 标记、世界地图模型、战斗精灵 | `srw64::gpu`（`native_gpu.hpp/cpp`）经 plume 画，HLSL 由 DXC 编成 SPIR-V/DXIL/MSL 嵌入 | GL 4.3 有 `gl_VertexID`、SSBO、整数纹理，HLSL 用到的特性都有。两条路：用 SPIRV-Cross 把已有的 SPIR-V 转成 GLSL 4.30 嵌入（构建期一步，源码不分叉）；再给 `srw64::gpu` 写一个 GL 实现（557 行接口）。这要求 HD 阶段换成 **GL 4.3 核心上下文**（或 GLES 3.1，带 SSBO），并把 `src/gl` 的客户端数组改成 VBO + VAO（改动集中在几个绘制函数） |
| RT64 贴图替换：地图机体图标、战斗地面、世界地图分块 | RT64 按贴图内容哈希替换（`rt64.json`、`replacementBlockedHashes`） | 解释器已经按内容哈希缓存解码结果。要做的是用 RT64 同一种哈希算法算键、查同一份 `rt64.json`，命中时换成 HD 纹理并按 UV 缩放。要先确认 RT64 哈希的精确定义（TMEM 范围、调色板是否计入） |

内存：Tegra X1 硬件支持 ASTC 与 BC1–7，但 Mesa 20.1 的 ASTC 是 CPU 解码（见下文 S3 实际做法）。HD 纹理在打包时转成 BC7（8 bpp，RGBA8 的 1/4）是 Switch 版控制内存的主要手段，也能缩短加载（推断）。按每场景「一组地图 + 画面上的头像与背景 + 当场战斗精灵」估，RGBA8 带 mip 的工作集约 300–600 MiB，压缩后约 100–150 MiB（推断，S3 实测）。应用模式 3.2 GB 放得下；小程序模式放不下。

### 运行库：libnx 上的阻塞项

Vita 的补丁走 `tools/recomp/toolchain/prepare_vita_runtime.py`（`vita` 分支，生成替身到 `build/recomp/vita-runtime/`，不改上游检出）。Switch 照同一机制写 `prepare_switch_runtime.py`，大部分条目比 Vita 少：

| 项 | 位置 | Switch 做法 |
| --- | --- | --- |
| `__int128`（`DMULT/DMULTU`） | `N64Recomp/include/recomp.h:42,49` | **不用改**，AArch64 GCC 有 `__int128` |
| RSP 向量单元 | `librecomp/include/librecomp/rsp_vu.hpp:23-31` | **不用改**，`__aarch64__` 走 sse2neon，与安卓、macOS arm64 相同 |
| 指针宽度、MOD 的 `static_assert(sizeof(size_t)==8)` | `mods.hpp:54` | **不用改**，LP64 |
| RDRAM 保留：`mmap` 4 GiB `PROT_NONE` + `mprotect` 512 MiB | `runtime-lifecycle/recomp.cpp:976-995` | 没有 `mmap`。照 Vita：直接 `aligned_alloc` 16 MiB（游戏只用 8 MiB 内）。代价是越界访问不再落到保护页上报错，而是写坏堆；正常运行无影响。若要保留保护页语义，可照 SwitchWakerHD 用 `virtmemFindCodeMemory` + `svcMapProcessCodeMemory`，S0 不必 |
| MOD 系统：`dlopen`、`mprotect(PROT_EXEC)` 改生成代码、sljit（LiveRecomp） | `librecomp/src/mods.cpp:230-248`、`librecomp/CMakeLists.txt:66` | 宿主没用到运行库的 MOD（`src/` 里没有 `recomp::mods` 引用；本项目的数据型 MOD 包与它无关）。照 Vita：`mods.cpp` 不编，`initialize_mods()/scan_mods()` 换空实现，重编译堆一并去掉 |
| 线程命名/优先级只有 Win32/Linux/Apple 分支 | `runtime-lifecycle/threads.cpp:65-151` | 加 `__SWITCH__` 分支：Horizon 没有给线程命名的调用（推断），空实现；优先级先不设（ultramodern 的游戏线程是协作式轮转，同一时刻只跑一个）。音频线程若欠载再用 `svcSetThreadPriority` |
| `std::thread` 默认栈 128 KiB | ultramodern 每个 N64 线程一个宿主线程（`threads.cpp:276`），宿主还有精灵工作线程、日志泵、调试接口 | 生成代码的 C 栈帧深（推断，大函数局部变量多）。用 `--wrap=pthread_create` 给默认 1 MiB，或在 `__syscall_thread_create` 前改默认值；S0 实测最大栈深 |
| `std::thread::detach()` 不支持 | 宿主 6 处：`console_log.cpp`、`update_check.cpp`、`debug_transport.cpp`、`stall_watchdog.cpp` 等 | `--wrap=pthread_detach` 空实现（DK64Recomp-NX 做法）；线程对象泄漏无所谓，进程退出由系统回收 |
| `std::filesystem::copy_file` 的 `fchmod` 被 SD 卡拒绝 | 宿主 4 处，含存档轮换 | 改成读写复制或重命名轮换。Vita V0 也踩到 `copy_file`，可以一起修成一个跨平台的 `copy_file_plain` |
| moodycamel 信号量只认 `_WIN32/__MACH__/__unix__` | `thirdparty/concurrentqueue/lightweightsemaphore.h:27,168` | 与 Vita 同：加 `__SWITCH__` 走 POSIX `semaphore.h`（newlib 2026-01-24 才实现；若 r29.1 没带就退回 libnx 的 `Semaphore`） |
| `WindowHandle` 没有 Switch 类型 | `ultramodern/include/ultramodern/renderer_context.hpp:48-62` | 与 Vita 同：`__SWITCH__` 走 `SDL_Window*` |
| `std::quick_exit` | `ultramodern/src/error_handling.cpp:27` | 未核实是否可用；不行就与 Apple 分支一样用 `std::_Exit` |
| 函数查找表 `LOOKUP_FUNC` | `recomp_overlays.inl` | 不用改 |

**JIT 与地址空间都不是问题**：本项目不要 JIT，16 MiB 堆分配不依赖 39 位地址空间，所以不需要特殊前导配置。

### 宿主与依赖

| 依赖 / 模块 | 现状 | Switch |
| --- | --- | --- |
| SDL3 + sdl2-compat | tarball 源码构建 | devkitPro 的 SDL2 2.28.5（宿主只调 SDL2 API） |
| FreeType 2.14.3、HarfBuzz 14.2.1 | tarball | devkitPro 有 freetype 2.13.3、harfbuzz 10.0.1（2026-10-17 核对 PKGBUILD），版本较旧。照桌面用 tarball 源码交叉编译钉住的版本，避免字形差异 |
| ICU 78.3（`portable_text.cpp` 10 处） | 数据裁到 0.45 MB | **devkitPro 没有 ICU 包。** 照安卓的交叉编译配方（裁过的数据）编 aarch64-none-elf；或用自写的 UAX#14 子集换掉 10 处 |
| librashader（`dlopen`） | 滤镜 | 不要（没有动态加载器） |
| 更新检查 `update_http.cpp`（`dlopen`） | 安卓/桌面 | 不要 |
| 调试接口（TCP，`debug_transport.cpp`） | 桌面、Deck、安卓、Vita 都保留 | 保留，经 Wi-Fi 用 `attach.py` 接 MCP；另用 `nxlinkStdio()` 把日志直接打到 Mac 终端 |
| `std::filesystem` | 102 个源文件 | 可用 |
| 平台判断 `__ANDROID__` | 13 个源文件 | Switch 不是 Unix 也不是安卓：逐个看这些分支（日志、问题报告、滤镜、更新、窗口标志、触屏）该走哪边；`src/native/app/runtime.cpp` 的随包资源路径要加 romfs |

### 线程与 CPU 预算（推断）

- N64 侧 4 个线程协作式轮转，同时只跑一个；加 Gfx（解释器 + GL 提交）、音频、VI；Mesa 自己还有一个 GL 线程。3 个核够用。
- 单核性能：A57 @1020 MHz 的整数性能约是 Vita A9 @444 MHz 的 3–4 倍，而且是原生 64 位（Vita 上 64 位 GPR 每条约 2 倍开销）。本作是 30 帧逻辑、2D 为主的战棋，桌面上解释一帧 0.2–0.75 ms；DK64（重 3D）在 1020 MHz 跑满 30 帧。**默认频率应该够**，不默认超频（libultraship 系默认拉到 1785 MHz，但 Erista 有唤醒失败的报告）。60 帧插帧依赖 RT64，GL 路径本来就没有。

## 方案

### 图形路线选择

| 方案 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- |
| A. `srw64-gl-host` 原样上 Mesa（GLES 2/3 或 GL 2.1 兼容上下文） | 代码已在桌面过 27 个检查；与 Vita 共用；Mesa 运行时编译着色器，没有 Vita 那种编译器不稳定问题 | Mesa 20.1 较老、驱动 CPU 开销与 GL 线程（推断）；无磁盘缓存 | **首发路线** |
| B. A + HD 图层的 GL 4.3 实现 | HD 在 Switch 上放得下；SPIR-V → GLSL 可以复用现有着色器 | 要写 `srw64::gpu` 的 GL 版和 RT64 哈希替换；`src/gl` 换 VBO/VAO | **S3** |
| C. 社区 NVK + RT64/plume Vulkan 原样 | 一份渲染代码、HD/插帧/宽屏全有 | NVK 移植刚出现一两个月、不在 devkitPro；自述不能做固定地址/保留映射；RT64 的着色器经 NAK 编译在 A57 上的开销未知；plume 要求的 Vulkan 扩展在 GM20B 上是否齐全未知 | S5 实验；若到 S3 开工时已成熟，可以替代 B |
| D. deko3d 后端 | 最低开销；本作状态集封闭（35 种），离线 uam 编译正合适 | 要给 `gl_renderer` 写一个新后端，RmlUi/对白/宿主层也要跟着换；与 Vita 不再共用 | 仅当实机上 Mesa 的 CPU 开销成瓶颈时考虑 |
| E. GLideN64（DK64Recomp-NX 做法） | 精度高，有现成 Switch 先例 | GPL-2.0 only，与 GPL-3 代码不能合并（见 [PS Vita 方案](psv-port.md#平台事实)）；而且本项目的原生对白/标签绘制钩子都在自写解释器里 | 排除 |
| F. libultraship Fast3D（BM64Switch 做法） | 有先例 | 自写解释器已经覆盖本作，再换回 Fast3D 没有收益 | 排除 |

### 平台层（新文件，`src/host/switch/` 或 `src/native/app/` 下）

- **入口：** `socketInitializeDefault`、调试构建 `nxlinkStdio`；`appletGetAppletType()` 不是应用就用 libnx 错误小程序提示「请按住 R 启动任意游戏后打开本程序」并退出（照 libultraship）；`romfsInit`。
- **生命周期：** `appletHook` 处理焦点、底座切换、挂起；进入 HOME 或待机前做一次自动存档（现有自动存档钩子），退出时等宿主正常返回再提交存档（安卓上踩过「进程被杀、存档没提交」）。
- **底座切换：** `appletGetOperationMode` → 掌机 1280×720、底座 1920×1080；界面大小掌机用「特大」（屏幕与 Deck 同为约 7 英寸、720 行），底座用「大」或按现有等效分辨率自适应。
- **时钟：** 不主动调频。`appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad)` 是官方的加载加速模式，但 libnx 注释写明它同时把 GPU 压到最低（已查证 `apm.h`），只适合开机预热着色器、读档这类不画画面的时段；持续提频要走 clkrst（libultraship 做法），等于超频，不做默认，S1 测完默认频率再决定要不要给开关。
- **存储：** `sdmc:/switch/Marchwind64/`：`Marchwind64.nro`、`rom.z64`、`saves/`（S0–S5 布局不变）、`settings/`、`dialogue/` 覆盖目录、`hd/`、`logs/`。所有路径用绝对路径（Ghostship 的前导因相对路径找不到工作目录，见下）。随包内容（对白文本 20 MB、字体 21 MB、关卡 metadata、界面词条）放 romfs，NRO 单文件分发。

### 输入

SDL2 的 Switch 后端把 Joy-Con/Pro 报成游戏手柄。默认绑定沿用 Deck 的功能表（`input_bindings.hpp`），按位置映射；按键图标用已有的 `PadFamily::Nintendo`（`src/native/text/button_prompts.hpp:27`），在 `__SWITCH__` 下固定这一族，不依赖 SDL 报告的手柄类型。A/B 按任天堂习惯（右为确定）还是按位置，S2 定。

| 宿主功能 | Switch |
| --- | --- |
| A / B | A / B（右 / 下） |
| C 键 | 右摇杆四向；C←（战斗中止）另给 Y |
| L / R / Z | L / R / ZL |
| Start | + |
| 设置面板 | − |
| 快进 / 跳过 / 回溯 / 2 倍速 | 沿用 Deck 默认（R 组合、ZR 等），按现有改键系统可改 |
| 原生页面指针 | 掌机触屏（`hidGetTouchScreenStates` 经 SDL 触摸事件）；安卓的触屏按钮层在掌机模式可选开启 |

HOME 与截图键归系统，不可用。

### 打包与分发

- **NRO：** devkitPro 的 CMake（`/opt/devkitpro/cmake/Switch.cmake`，`nx_generate_nacp` + `nx_create_nro`）生成，NACP 写名称「Marchwind64」、作者、版本，图标用 M64 徽标（256×256 JPEG），romfs 装随包内容。
- **ROM：** 玩家把自己的 `rom.z64` 放进 `sdmc:/switch/Marchwind64/`，沿用现有的哈希校验与三语错误提示。包内只有 ROM 派生的重编译代码，不含 ROM 数据，与安卓、Vita 同一侧载口径。
- **ROM 在主机上导入（2026-10-25，`switch-import` 分支）：** 没有 `game/prepared.json`，或它的 `build`（版本号＋导入元数据摘要）不是本程序的，宿主就在启动时用 `rom.z64` 导入（之后的版本没有 `rom.z64` 时用 `game/rom.z64` 那份），再照常 `run_prepared`。导入代码与电脑上的 `prepare_data.py` 是同一份（`src/native/app/console_data.cpp`），两边输出逐字节相同，只有图片路径带各自的文件夹名。`game/` 里有 ROM 本身和从 ROM 解出的图片、日文原文，所以不能由我们预先做好发出去。`prepared.json` 最后写，导入中途断了下次重做。每步秒数记在 `prepared.json` 的 `seconds` 里。
  - **实测（初代机、应用模式、空文件夹、Release NRO）：** 共 32.8 s——读校验 ROM 2.1、导入 9.8、合并对白文本 20.2（约 800 个小文件从 SD 卡读）、写入 0.7；Mac 上同样四步 1.18 s。导入之后到标题画面与平常一样。一次性的，用户认为可以接受。
  - **可选的提速：** 对白文本那 20 s 与 ROM 无关，可以在打包时合成一个文件放进 romfs；当前没做。
  - **顺带修了：** Switch 上 `std::filesystem::remove_all` 只删文件、留下空目录（newlib 的 `remove` 是 `unlink`），`remove_tree` 在 Switch 上改为目录用 `rmdir`。
- **HD 包（S3 起）：** 玩家把 HD zip 解压到 `sdmc:/switch/Marchwind64/hd/`，或由宿主首次启动时从 zip 解出（安卓有同样的导入逻辑可参考）。SD 卡可能是 FAT32：单文件 4 GiB 上限对 533 MB 的 zip 无影响。
- **NSP 前导：** 不发官方 NSP。理由：前导依赖 sigpatches，NH 指南把安装自制 NSP 列为封机风险之一（[FAQ](https://switch.hacks.guide/faq)，2026-03-01），HarbourMasters 也不支持（[Ghostship#213](https://github.com/HarbourMasters/Ghostship/issues/213)，2026-06-16，其中前导丢工作目录导致找不到资源）。玩家自己做前导时，绝对路径保证照样能用。
- **官网：** 下载页与系统要求加 Switch 一行（「需已破解、运行 Atmosphère 的主机，按住 R 启动」），三语 README 同步；措辞与安卓/Vita 的侧载说明一致。

## 测试路线

### 没有实机时（Mac）

| 工具 | 事实 | 用途 |
| --- | --- | --- |
| devkitPro Docker 镜像 `devkitpro/devkita64` | 先例项目 CI 都用它；与 Vita 的 VitaSDK 镜像同一方式 | 交叉编译、链接、出 NRO；`.text` 大小 |
| [Ryubing/Ryujinx](https://ryujinx.app/download)（Ryujinx 2024-10 停更后的社区分支） | 下载页称只正式支持 Apple Silicon 的 Mac；可「加载应用」直接开 NRO（二手：镜像文档）；稳定版 1.3.3 约一年前，canary 持续更新（二手：Flathub）。官方 Forgejo 在本次调研时连不上，没读到源码 | 冒烟：NRO 能加载、ROM 路径与存档逻辑、标题画面大体正确 |
| [Eden](https://git.eden-emu.dev/eden-emu/eden/releases)（yuzu 分支） | v0.2.1（2026-06-01）有 macOS dmg，发布说明称 macOS 版「实验性，预期有严重图形错误与崩溃」（已查证）；源码有 `loader/nro.cpp`（已查证） | Ryubing 不行时的备选 |
| 桌面 `srw64-gl-host` | 与 Switch 版同源，27 个检查脚本已通过 | **主要的功能验证手段**：平台无关的问题全在桌面上用 MCP 和检查脚本解决 |

模拟器的局限（推断）：Mesa 自制程序向 nvdrv 提交的是 Maxwell 3D 命令，模拟器按 GPU 命令级模拟，原理上能跑；SDL2 + Mesa 的 DevilutionX 2023 年在 Ryujinx 上跑过（[devilutionX#6740](https://github.com/diasurgical/devilutionX/issues/6740)，2023-10-24），但**没有找到任何 libultraship 或 N64Recomp NRO 在模拟器上运行的报告**。模拟器的内存大小可调、不模拟时钟、不模拟 nvdrv 限额与时序，直接加载的 NRO 多半按应用身份运行，所以小程序模式检查也测不出来。只能当冒烟测试。

### 实机

验收只认实机，环境与日常流程见下一节。初代 Erista 的默认 CPU/GPU 频率与 Mariko 相同，测性能有代表性；它超频余量小、功耗高，正好作为「不靠超频」的基准。

## 开发环境与开发流程

本节的 Switch 端步骤除特别标注外都来自 [NH Switch Guide](https://switch.hacks.guide/) 的源码（[nh-server/switch-guide](https://github.com/nh-server/switch-guide)，各页最后修改日期见括号），工具行为读的是各自仓库的源码，2026-10-17 查证。

### Switch 端一次性配置

1. **买之前确认未修补（已查证）。** 设置 → 系统 → 序列号信息（指南说比机身标签准），对照 [Getting Started](https://switch.hacks.guide/user_guide/getting_started)（2026-05-15）的序列号表与在线检查器：如 XAW1 在 XAW10065000000 以下未修补、到 XAW10120000000 为「可能已修补」、以上已修补；XAW9 与全部 XAK 只有「可能」；表外序列号都不可利用。2018 年中起出货的已是修补机；Lite、OLED 与续航版（Mariko）都要改芯片。买二手要请卖家拍设置里的序列号。
2. **SD 卡：FAT32，64 GB 起，建议 128 GB（容量是推断）。** hekate 分区时把 emuMMC 滑块设成「29 FULL」（OLED 是 58），其余空间是 hekate 建的 FAT32 分区（[partitioning_sd](https://switch.hacks.guide/user_guide/all/partitioning_sd)，2026-07-01）。备份 NAND 时 RAW GPP 先写到 SD 卡上，指南要求至少 32 GB 空闲，否则分块备份（[making_essential_backups](https://switch.hacks.guide/user_guide/all/making_essential_backups)，2026-03-02）。29 GB emuMMC＋29 GB 临时备份＋HD 包与日志，64 GB 是下限。Mac 上看到 emuMMC 分区「无法读取」时**不要格式化**（指南原话的同类警告）。
3. **进 RCM、从 Mac 注入（已查证）。** 短接器（jig）插右 Joy-Con 导轨，短接 10 号针与地，**别碰 4 号针（5 V）**，不要用回形针或锡纸（[entering_rcm](https://switch.hacks.guide/user_guide/rcm/entering_rcm)，2026-09-28）。按住音量＋再按电源进 RCM，屏幕保持黑。USB-C 线接 Mac，用 **OmniRCM**（`OmniRCM-osx.zip`，v1.1.1 2026-08-09）注入 hekate；备选是 Chromium 浏览器里的 WebUSB 注入器 `omnircm.nintendohomebrew.com/web/`（[sending_payload](https://switch.hacks.guide/user_guide/rcm/sending_payload)，2026-09-02）。
4. **先备份 NAND（已查证）。** hekate → Tools → Backup eMMC：先「eMMC BOOT0 & BOOT1」，再「eMMC RAW GPP」（即 rawnand，耗时长），完成后拷到 Mac 另存（[making_essential_backups](https://switch.hacks.guide/user_guide/all/making_essential_backups)）。这份备份是变砖后唯一的恢复手段，不进仓库、不上传。
5. **导出密钥（可选，按需）。** 开发循环本身不需要密钥。需要的场合只有两个：Ryubing 等模拟器启动时要 `prod.keys`（推断，模拟器的通行要求），以及自己做 NSP 前导。工具是 Lockpick_RCM：原仓库 shchmue/Lockpick_RCM 2023-05-08 因任天堂的 DMCA 被 GitHub 封禁（已查证，HTTP 451），现在由社区分支维护（GitHub 上 2026-10 仍有更新）；NH 指南已不教这一步，只在 emuMMC 的用途里提到「导出加密密钥」（[cfw_environment](https://switch.hacks.guide/user_guide/all/cfw_environment)）。密钥是这台机器的私有数据，只放本机。
6. **建 emuMMC，永远离线（已查证）。** hekate → emuMMC → Create emuMMC → SD Partition（分区式）→ Change emuMMC 选中它（[partitioning_sd](https://switch.hacks.guide/user_guide/all/partitioning_sd)）。按 [sd_preparation](https://switch.hacks.guide/user_guide/all/sd_preparation)（2026-09-02）放 Atmosphère、hekate 与 `atmosphere/hosts/emummc.txt`（DNS-MITM，屏蔽任天堂服务器）。指南：「emuMMC 保持离线就没有封机风险」「emuMMC 与 sysMMC 都联网迟早会被封」（[cfw_environment](https://switch.hacks.guide/user_guide/all/cfw_environment)；[launching_cfw](https://switch.hacks.guide/user_guide/all/launching_cfw)，2026-10-16）。sysMMC 保持原样，照常联网玩正版。版本：hekate v6.5.4 / Nyx 1.9.4（2026-09-28）、Atmosphère 1.12.1（2026-10-16，支持固件 23.0.0）。
7. **未修补机关机后要重新注入（已查证）。** 指南：主机彻底关机、没电或进了原版系统后自制系统就失效，要重新进 RCM 注入 hekate（[launching_cfw](https://switch.hacks.guide/user_guide/all/launching_cfw)）。平时用睡眠，开发期间基本不用重新注入；在自制系统里用电源菜单「重新启动」或 hbmenu 的 reboot to payload 会回到 hekate，不用短接器。**AutoRCM 默认不开**：它让主机以为自己变砖、每次开机直接进 RCM，从此离不开注入器，误用会造成真损坏，电量耗尽后在 RCM 里充电极慢（[autorcm](https://switch.hacks.guide/extras/autorcm)，2026-03-30）。
8. **应用模式的入口。** 指南：按住 R 启动「任意游戏（包括体验版、卡带）或应用（如 YouTube）」即进 hbmenu 的应用模式；从相簿进是小程序模式，内存少得多（[launching_cfw](https://switch.hacks.guide/user_guide/all/launching_cfw)，已查证）。离线 emuMMC 里可能没有可启动的游戏：emuMMC 是建立时从 sysMMC 复制的，装在主机内存里的游戏会跟过来；装在 SD 卡上的要把 `Nintendo` 文件夹再拷一份到 `sd:/emuMMC/RAWX/`（已查证，sd_preparation）；数字版在离线时能否通过授权检查取决于这台是不是该账号的主用机（推断）。**最省事是插一张实体卡带**，卡带不依赖授权与安装（「卡带算数」是已查证的，「最省事」是推断）。
9. **常用工具（已查证）。** ftpd（指南的 SD 准备步骤已放进 `switch/` 文件夹，[ftpd](https://switch.hacks.guide/homebrew/ftpd)）用来在 Mac 与 SD 卡之间传文件，不用每次拔卡。sys-clk（[sys-clk](https://switch.hacks.guide/homebrew/sys-clk)）可选：测性能时用它确认、锁定在默认 1020 MHz，而不是用来超频。

### 开发循环

| 步骤 | 做法 | 依据 |
| --- | --- | --- |
| 功能验证（主力） | Mac 上的 `srw64-gl-host`（`SRW64_GL_ONLY` 构建，与 Switch 版同源）配 MCP 与检查脚本；`export SRW64_HOST_BINARY=<srw64-gl-host>` 后 `Session.launch` 都走它 | `vita` 分支 V1 记录，27 个检查已通过。平台无关的问题都在这一步解决 |
| 编 NRO | Docker `devkitpro/devkita64`（`tools/release/switch/build.sh`，S0 新建），产出 `.nro` 与带符号的 `.elf`（崩溃定位要用，每次构建一起留存） | 先例项目 CI 同样用这个镜像（已查证 Starship/Ghostship `switch.yml`） |
| 冒烟 | Ryubing 加载 NRO（需本机密钥与固件，推断） | 见上一节模拟器的局限 |
| 推到实机 | Switch 上 hbmenu 按 **Y** 进 NetLoader（已查证 nx-hbmenu `nx_main/main.c`）；Mac 上 `nxlink -a <Switch IP> -s Marchwind64.nro`：上传后落到 `sdmc:/switch/Marchwind64.nro` 并启动。`-p` 是相对 hbmenu 当前目录（`sdmc:/switch`）的路径，hbmenu 不建目录，目录不存在时屏幕报 `open: err=2`、nxlink 只得到回应 255（2026-10-23 实机），`-s` 留在原地接收程序的 stdout/stderr（程序里调 `nxlinkStdio()`） | nxlink 选项读 [switch-tools `src/nxlink.c`](https://github.com/switchbrew/switch-tools/blob/master/src/nxlink.c)：上传端口 28280，回连端口 28771 |
| nxlink 装在 Mac 本机 | **不要在 Docker 里跑 nxlink。** hbmenu 把「上传连接的源地址」记作回连地址（`loadnro` 里 `ad->nxlink_host = remote`，已查证 nx-hbmenu `common/netloader.c`），程序再连回这个地址的 28771 端口。Docker Desktop 下容器出站经 NAT，Switch 看到的源地址是 Mac，而 28771 在容器里监听，不发布端口就收不到；不带 `-a` 时的 UDP 广播发现也出不了容器的网络（推断）。nxlink 来自 devkitPro 的 `switch-tools`：Mac 上装 devkitPro pacman（`devkitpro-pacman-installer.pkg`，[devkitPro/pacman 发布页](https://github.com/devkitPro/pacman/releases)，v6.0.2）后装 `switch-tools`，或直接从 switch-tools 源码编（只依赖 zlib；本机做法见 `tools/release/switch/README.txt`）。macOS 上不带 `-a` 的广播报 `sendto: No route to host`，一律带 `-a`。NetLoader 只接受一次连接：别用 `nc` 之类探端口，空连接会让它报 `error getting name length err=11` 并退出 | 已查证工具来源；NAT 行为是推断 |
| 调试接口接 MCP | 见下「调试接口的局域网模式」：Mac 上 `tools/release/switch/attach.py --host <Switch IP>` 写出本地会话的 `debug.json`，之后 `srw64_attach`、`srw64ctl.py`、检查脚本照常用 | 与 Deck（ssh）、安卓（adb）同一套 `Session.attach()` |
| 拷大文件 | ROM：`sdmc:/switch/Marchwind64/rom.z64`，一次性放好；HD 包、存档、日志走 ftpd 或读卡器 | — |
| 崩溃 | 见下「崩溃与调试」 | — |

### 调试接口的局域网模式（S0/S1 的工作项）

现状（本仓库，已查证）：`src/host/debug_transport.cpp` 在桌面上只监听回环地址（`listen_loopback`，系统挑端口，`INADDR_LOOPBACK`），首行必须带 `debug.json` 里的令牌；安卓走抽象 Unix 套接字、不带令牌，靠 `adb forward`；Deck 靠 ssh 端口转发（`tools/release/linux/attach.py`）。`vita` 分支为 Vita3K 改成了监听所有地址（psv-port.md V2 第 8 条）。**Switch 上没有 adb 也没有 ssh，回环地址从 Mac 根本连不到。**

要做的（推断的设计，S0 在桌面上先实现并测，S1 在实机上用）：

- `__SWITCH__`（以及 Vita 实机）下，调试接口开启时监听 `INADDR_ANY` 的一个固定端口，**令牌必须要**（不能沿用安卓的无令牌做法：局域网里任何人都能连）。
- 开启条件与桌面一致：关于页的「AI 调试接口」开关或 `sdmc:/switch/Marchwind64/debug` 标记文件；默认关。开着时在屏幕角落显示「调试接口已开启 IP:端口」，避免玩家无意中开着。
- 令牌照旧写进 SD 卡上的 `debug.json`，同时打到 nxlink 的 stdout。Switch 版的 `attach.py` 从 ftpd 取 `debug.json`（或解析 nxlink 输出），把 IP、端口、令牌写进 `build/recomp/debug/switch-<时间>/debug.json` 并设为当前会话；截图、录像、事件日志沿用现有「经同一连接取回」（`Session.local_file`）的做法。
- 套接字用 libnx 的 BSD socket（`socketInitializeDefault`），Vita 上踩到的 `poll()` 与 `accept` 问题在 Switch 上要再验一次。

### 崩溃与调试

- **崩溃报告（已查证）。** Atmosphère 的 creport 把报告写到 `sdmc:/atmosphere/crash_reports/<时间戳>_<程序ID>.log`（另有 `dumps/` 下的线程信息），列出各模块起始地址与崩溃线程的回溯（[creport](https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/components/modules/creport.md)；`stratosphere/creport/source/creport_crash_report.cpp`）。用 ftpd 拉回 Mac，地址减去 NRO 模块的起始地址就是 ELF 里的地址，`aarch64-none-elf-addr2line -fCe Marchwind64.elf <偏移>` 查函数与行号（Docker 镜像里有这个工具）。
- **自己的异常处理（已查证先例）。** libnx 允许程序定义 `__libnx_exception_handler` 并给 `__nx_exception_stack`（`nx/source/runtime/init.c`、`exception.s`）。DK64Recomp-NX 用它把 pc/lr 与回溯按模块偏移打进自己的日志，注释里就是同一条 addr2line 命令（`switch/crash_switch.c`）。本项目照做，写进 `sdmc:/switch/Marchwind64/logs/`，问题报告 zip 一并收走。
- **gdbstub（已查证）。** Atmosphère 自带 dmnt.gen2 的独立 gdbstub：在 `sdmc:/atmosphere/config/system_settings.ini` 的 `[atmosphere]` 下设 `enable_standalone_gdbstub = u8!0x1`（同时保持 `enable_htc = u8!0x0`，默认就是 0），重启后经 Wi-Fi 在端口 **22225** 上提供 GDB 远程协议；Mac 上用 devkitPro 的 `aarch64-none-elf-gdb`，`target extended-remote <Switch IP>:22225`，`info os processes` 列进程、`attach <pid>`；`monitor wait application` 让下一个启动的应用挂起等附加（1.2.3 起，[changelog](https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/changelog.md)；默认值见 `stratosphere/ams_mitm/source/set_mitm/settings_sd_kvs.cpp`，源码注释称「这个设置将来可能消失」）。注意：hbmenu 启动的 NRO 跑在被接管的游戏进程里；调试会用到网络的进程可能卡住（changelog 原话的意思）；gdb 附加时 libnx 的自定义异常处理不会接管。只在 emuMMC 里开。
- **日志。** 平时靠 nxlink 的 stdout 和 SD 上的日志文件；调试接口的事件日志照常可用。

### 实机冒烟结果（2026-10-23）

`tools/release/switch/build.sh smoke` 编出的 `srw64-smoke.nro`（6 MB）经 nxlink 推到用户的初代机（掌机模式），输出：

| 项目 | 结果 |
| --- | --- |
| 小程序类型 | library applet：这次是从相簿进的 hbmenu，进程总内存只有 369 MiB（已用 233）。正式测试要按住 R 启动游戏进应用模式 |
| GL 2.1 兼容上下文 | 4.9 ms 建成；实际给的是 `4.3 (Compatibility Profile) Mesa 20.1.0-rc3`，nouveau / NV120，GLSL 4.30 |
| GLES 2.0 上下文 | 2.1 ms 建成；实际给 `OpenGL ES 3.2`，GLSL ES 3.20 |
| 呈现 | 只清屏时 20 s 呈现 1203 帧（60.1/s），垂直同步正常 |

结论：两种上下文都能建，兼容档是 4.3，客户端顶点数组可用；S0 的「GLES 还是 GL 2.1」不再是阻塞项。

## 阶段

### S0 编译与冒烟（Docker 与模拟器，不需要实机）

1. `tools/release/switch/`：Dockerfile（基于 `devkitpro/devkita64`，加 switch-sdl2、switch-mesa、switch-glad）与构建脚本，照 `tools/release/vita/`。
2. `prepare_switch_runtime.py`：上表的运行库补丁（`mmap`→16 MiB 分配、MOD 空实现、线程分支、信号量、`WindowHandle`、`quick_exit`）+ 链接选项 `--wrap=pthread_detach`。
3. 交叉编译 `srw64_cpu`（全部 `funcs_*.c`）+ 运行库 + `srw64-gl-host` 的源码（`gl_api.hpp` 加 `__SWITCH__`；FreeType/HarfBuzz/ICU 源码交叉编译）。记录 `.text`、编译警告、`__ANDROID__`/`__linux__` 分支的去向。
4. 平台层最小版：入口、应用模式检查、romfs、`sdmc:/switch/Marchwind64/` 路径、GLES 上下文（对照 GL 2.1 兼容）、nxlink 日志、`__libnx_exception_handler`。
5. 调试接口的局域网模式（带令牌、监听所有地址）先在桌面宿主上实现并测；写 `tools/release/switch/attach.py`。
6. 出 NRO，在 Ryubing 上加载。

**门槛：** NRO 能编出来；模拟器上进到标题画面（若模拟器跑不了 Mesa 程序，记录原因，门槛降为「能加载并写出日志」）。

**S0 进度（2026-10-23，`switch` 分支）：** 第 1–3 步做完，`tools/release/switch/build.sh game` 链接出 `build/recomp/switch-build/Marchwind64.nro`（22.9 MB；ELF `.text` 21.2 MB，含 RmlUi、ICU、FreeType、HarfBuzz、SDL2、Mesa）。没上实机，也没进模拟器。

- **GL 路径：** 从 `vita` 分支摘了 V0 统计与 V1 的六个提交（到 `srw64-gl-host` 为止，不含 Vita 自己的工具与文档），在 main 上重放，冲突只在 `frontend.cpp` 与 UI 的 CMake；GL-only 宿主补上 main 后来加的 `legacy.cpp`、`quick_load.cpp`，Mac 上照常编出 `srw64-gl-host`。Vita 后来的提交（数据预先准备、着色器缓存、跳过重复呈现等）没摘。
- **运行库：** `prepare_switch_runtime.py` 写到 `build/recomp/switch-runtime/`：RDRAM 用 16 MiB `calloc`、去掉 MOD/LiveRecomp/重编译堆、`WindowHandle`、moodycamel 信号量走 POSIX（newlib 有 `semaphore.h` 但不定义 `__unix__`）、`std::_Exit`、存档复制改流。64 位所以 Vita 的 `DMULT` 与 `mods.hpp` 补丁都不要。ROM 仍按上游读取与校验。线程命名/优先级在 `src/host/switch/switch_platform.cpp` 里空实现；`pthread_detach` 没出问题，没加 `--wrap`。
- **依赖：** devkitPro 没有 ICU，`build_icu.sh` 交叉编译 ICU 78.3 静态库；数据不用 ICU 自己的库，而是 `icu_data.py c-source` 的 1.1 MB 子集（与安卓同一路，ICU 的数据构建会拒收裁过的 `.dat`）。FreeType、HarfBuzz、SDL2、Mesa 用镜像里的包。
- **宿主上的改动（都按 `__SWITCH__` 分支）：** GLES 2 头文件与 ES 上下文；`_GNU_SOURCE`（newlib 在严格 ISO 下藏起 `posix_memalign`）；去掉 `-ldl`（FindICU 会加）；日志不走管道（有 nxlink 时留在 nxlink，否则写 `console.log`）；无更新检查；调试接口监听所有地址（仍要令牌）；不要单实例锁（newlib 无 `flock`）。
- **还没做：** 第 4 步的入口（`sdmc:/switch/Marchwind64/` 用户目录、应用模式检查、romfs、异常处理）——现在 `main` 不带参数时走桌面的选 ROM 流程，上机会停在那里；第 5、6 步。

### S1 实机跑通（需要实机）

1. 应用模式启动；nxlink 推送与日志；调试接口的局域网模式在实机上接通 MCP（`attach.py`），验证 libnx socket 的 `poll`/`accept`。
2. 启动时预热已知的组合器程序，记录每个程序的编译时间与开机到标题的总时间。
3. 实测：默认 1020 MHz 下各场景帧时间（现有帧率读数）、进程堆占用峰值、音频欠载次数、最大线程栈深。
4. 在实机上跑 `vita` 分支已通过的 27 个检查脚本（4:3、原版画面）。

**门槛：** 第 1 话全程、一次战斗演出、一次存档读档；战术地图与场间页面稳定 30 帧；开机到标题 ≤15 秒；存档在 SD 卡上读写正常（`copy_file` 已替换）。

**S1 性能结论（2026-10-24，实机底座 1080p，`SRW64_PERF` 每 5 秒一行，MCP 远程驱动）：**

- **贴图上传：** Switch 的 Mesa 在 `glTexSubImage2D`／`glTexParameteri` 写到 GPU 还在读的贴图时会等 GPU，转场时上千张小贴图累计约 1 秒。拼图页改成影子内存＋每帧按行合并上传，每页三张 GL 贴图轮换（只写最近两帧没画过的那张），过滤方式只在变化时设；包裹贴图的独立贴图等到真有绘制越出周期时才建。对白画布同样三份轮换、每帧合并上传。之后一次上传 4644 张共 53 ms。DK64Recomp-NX、MegaMan64Recomp-NX（GLideN64）的做法是每张贴图一个新对象、从不改在用的贴图，它们首次进房间也有 33–72 ms 的卡顿。
- **界面锁：** 运行库每 1 ms 调一次窗口更新，界面每秒同步约 950 次、每次持锁 0.15–0.4 ms，绘制线程等锁每 5 秒最多 0.6 s；Switch 上限成每 8 ms 一次。调试模式（`debug` 文件）曾顺带打开运行文件，钩子持锁写 SD 卡并刷盘，开场与选人页每秒几次 30 ms；Switch 上运行文件改为只认 `SRW64_RUN_FILES`。`console.log` 由单独的线程每 0.25 秒落盘（同步写曾偶发几百毫秒）。选人页的选择改为投递给游戏线程的下一步执行，界面不再等它的锁。
- **其他：** MCP 截图的 PNG 编码移出绘制线程；掌机模式下用官方 `apmSetPerformanceConfiguration` 把 GPU 提到 768 MHz（失效时退到 460.8 MHz），失去焦点和退出时还原；运行库的 moodycamel 无锁队列在 Switch 上丢 VI 消息（世界地图卡死），换成互斥队列。
- **现状：** 战术地图 30 帧、单帧 <7 ms；战斗 50–58 帧、单帧 20–30 ms（对白整屏重画每次约 12 ms）；选人页 60 帧、单帧 17–24 ms；界面单次更新超过 20 ms 时打 `SRW64_UI_SLOW` 与分段耗时。界面大小默认特大（同 Deck）。

### S2 掌机化

1. GL 路径的 16:9 宽屏（与 Vita V3 共用：扩展 GBI 换算已有，补呈现与宿主图层），掌机 720p / 底座 1080p 切换。
2. Joy-Con 默认绑定、Nintendo 图标族、触屏指针；界面大小按模式。
3. 挂起/HOME/底座切换的稳定性；自动存档时机。
4. 设置页 Switch 分页（可选触屏按钮层；提频开关视 S1 结果）；问题报告 zip 写到 SD 卡。

**门槛：** 掌机与底座各连续 2 小时无崩溃；底座插拔 20 次画面与输入正常；主线前 10 话的自动检查（经 Wi-Fi 调试接口）通过。

### S3 HD 图层（GL 4.3）

1. 决策点：先看社区 NVK 的成熟度（S5），若 RT64 已能在上面跑且 CPU 开销可接受，改走 C，跳过本阶段。
2. `src/gl` 改 VBO + VAO，换 GL 4.3 核心上下文（或 GLES 3.1）；SPIRV-Cross 把 `native_gpu` 的 SPIR-V 转 GLSL 4.30；`srw64::gpu` 的 GL 实现；宿主自绘 HD 层逐个接回。
3. 解释器里按 RT64 同一哈希做贴图替换（地图机体图标、战斗地面、世界地图分块）。
4. HD 包的 Switch 变体：BC7/ASTC 压缩纹理；按场景的显存预算与淘汰；导入流程。
5. 桌面上用 `srw64-gl-host` + HD 与 RT64 版截图对照（照 V1 的做法），再上实机测内存峰值。

**门槛：** HD 开启时内存峰值留 500 MiB 余量；场景切换的加载卡顿 ≤1 秒；HD 截图与 RT64 版无缺层、错位。

**S3 实际做法（2026-10-24）：** 没换 GL 4.3 和 SPIRV-Cross，也没给 `srw64::gpu` 写 GL 实现。各图层都在现有的 GLES 2 子集上直接接，HLSL 手工改写成 GLSL ES 1.00（片段很短）：

- **准备数据：** Rust 工具 `tools/release/switch/prepare-hd` 把发布 HD 包转成 `.mwtex`（每级用 Intel ISPC 编成 BC7，再整体 zstd）。地图索引用 R8；RT64 包里的地图机体图标和对白框是像素画，存 RGBA8。宿主自绘层是预乘 alpha；RT64 包照 RT64 的读法保持直通 alpha，mip 按 alpha 加权。精灵层另带原图，因为游戏改调色板（受击闪白、金色形态）时要从原图重新着色。头像也带原图，给页面用。
- **不用 ASTC：** Switch 上的 Mesa 20.1（nouveau）把 ASTC 列为支持格式，但上传时在 CPU 上解码。实测一张 512×576 带 mip 的图要 70 ms，标题 logo 要 225 ms，战斗开始与结束各卡约 0.75 秒。BC7 由 GPU 直接读取，512² 上传 1.7 ms（2026-10-24 实机测）。BC7 每像素 8 bit，ASTC 6×6 只有 3.6 bit，所以 HD 目录约 1.2 GB，其中地图 578 MB。地图底图不透明，可再改 BC1 省一半。
- **宿主自绘层：** 头像、场间背景、战术地图（GPU 调色板偏移）、精灵层（标题、剧情图片、机体、额外图、战斗精灵），都经 `native_quads` 交给 `native_gl.cpp`，压缩纹理直接上传。
- **RT64 贴图替换（`hd_replace.cpp`）：**
  - 解释器的 TMEM 装载改成与 RT64 逐字节一致：整 64 位字、奇数行互换、RGBA32 分半。
  - 新贴图用 TMEMHasher v5 算键：XXH3，内容为采样范围内的 TMEM，加上实际用到的调色板项、尺寸、TLUT、line、siz、fmt。
  - 命中 `rt64.json` 时，渲染器绑定 HD 纹理，坐标沿用原贴图的归一化坐标，包裹方式随原贴图。
  - 受图像模式和地图图标开关控制；纹理按 96 MiB 预算淘汰闲置的。
- **3D 模型（`native_marker.cpp` 的 GL 段）：**
  - 解释器按 RT64 的规则识别模型的三角命令：段 4 指向原资源，命令在已知偏移。
  - 视图矩阵照 RT64 的规则从投影里拆出来，用于法线和水面的视空间。
  - 七个着色器（标记、光环、舰船/地标、航迹、名牌、烘焙场景、水面）用 GLSL 重写；航迹条带在 CPU 上展开。
- **开发流程：** 开发版 `SRW64_SWITCH_EMBED_HD=OFF build.sh game` 加 `package.py --hd`，把 hd/ 放在 NRO 旁；正式版打进 romfs。`build.sh game` 在 romfs 比 NRO 新时会重新打包（NRO 规则只看程序，换了 HD 包不会自己重打）。
- **启动（2026-10-25 实机）：**
  - 26 s 降到 3.4–3.8 s。原因有三：逐个文件查 SD 卡（`hd_texture::exists` 在 Switch 上直接当作存在）、JSON 逐字节读（改为整读）、模型包和地图元数据同步加载（模型包放后台、地图用合并的 `maps/metas.json`、殖民卫星帧不解码）。
  - 后台线程（读图、写日志、解码）固定在 2 号核、优先级 0x3B；0x3F 会被饿住，页面永远拿不到图。图形线程同核、高一档（0x3A），否则一帧要等后台画字，一个 82×100 的字形「上传」量到 80 ms。
  - 每 5 秒的性能日志曾调用 `mallinfo()`，它锁住分配器遍历整个堆，每次卡约 30 ms，已去掉。
  - 战斗数字（HUD 两种描边、横幅）开机时先画好；字形这类 256 KB 以下的小图不再按闲置释放，否则 HP 数字会空几帧。
- **载入画面：** `splash.py` 在打包时按语言生成底图（Steam 图的深蓝斜线 + 该语言 logo）和每一步的文字条，`switch_platform.cpp` 用 libnx framebuffer 显示，语言取 `SRW64_LOCALE`、`presentation.json`、主机语言。建窗口后马上把最后一步画进 GL 窗口，一直留到游戏画面不再是黑的（每帧缩成 32×18 读回检查），万代 logo 的淡入也被盖住。

### S4 发布

`build_release.py` 加 Switch 产物（NRO + 单独的 HD 包变体）；GitHub CI 加 devkitPro 镜像任务；官网下载、系统要求、三语 README 与安卓兼容页同类的说明；发版说明写明「只支持应用模式」「emuMMC 离线」。

### S5 可选：NVK 实验

在不影响主线的分支上，用社区 Mesa NVK 编 RT64 + plume Vulkan 的原宿主，看能否建设备、plume 要的扩展是否齐、着色器编译开销与帧时间。若可行，Switch 版可以与桌面完全同一渲染器（含 HD、插帧、滤镜）。

## 风险

| 风险 | 影响 | 应对 |
| --- | --- | --- |
| R1 Mesa 20.1 的驱动 CPU 开销 | 绘制调用多的场景（标题演示战斗、世界地图，每帧最多约 2,000 条命令）掉帧 | 解释器已把矩形并批（桌面每帧 draw call 中位数 4、最大 40）；仍不够再考虑 deko3d（D） |
| R2 没有着色器磁盘缓存 | 每次开机重编；首次遇到新状态卡顿 | 状态集封闭，开机预热；必要时用 `glGetProgramBinary` 自己落盘（Mesa 20.1 在关掉磁盘缓存时是否仍支持程序二进制，S1 验证） |
| R3 固件更新改 ABI | 旧 NRO 在新固件崩溃（固件 21 先例） | 始终用当前 libnx；emuMMC 固件按 Atmosphère 支持情况升级；发版前在当前固件回归 |
| R4 模拟器不可靠 | S0 无法验证画面 | 平台无关问题全在桌面 `srw64-gl-host` 上解决；实机到手前只做到「能编、能加载」 |
| R5 小程序模式内存不足 | 从相簿启动后崩溃 | 启动即检查并提示（照 libultraship） |
| R6 社区 NVK 不成熟 | S5 失败 | S5 只是实验，主线 A/B 不依赖它 |
| R7 线程栈与 `detach` | 深调用栈溢出、启动中止 | `--wrap` 两个 pthread 函数；S1 测栈深 |
| R8 nvdrv 传输内存 | 纹理多时 `GL_OUT_OF_MEMORY`（DK64Recomp-NX 先例） | 图集减少纹理对象；HD 阶段按需调大 libnx 的弱符号 `__nx_nv_transfermem_size`（默认 0x800000，已查证 `nv.c`） |
| R9 封机 | 用户的主机或账号被封 | 只在离线 emuMMC 上测；不发 NSP |
| R10 许可 | 引入 GPL-2-only 代码 | 不用 GLideN64；Mesa、libnx、SDL2 都与 GPL-3 兼容；社区 NVK 移植（无许可证文件）只做实验、不进发布包 |
| R11 调试接口暴露在局域网 | 同一网络里的人能驱动游戏 | 令牌必须、默认关、开着时屏幕常显提示 |

## 待定

- **GLES 还是 GL 2.1 兼容上下文。** 代码两条都支持；S0 看哪个在 Mesa 20.1 上建得出、哪个驱动开销小。
- **Switch 与 Vita 谁先上实机。** Switch 没有 32 位问题、内存充裕、着色器编译稳定，`srw64-gl-host` 上 Switch 可能比上 Vita 还快；但 Vita 实机已经在手。建议 Switch 等实机到手再开 S0，GL 路径的改动（宽屏、字体共享）在两个分支间共用。分支放法：Switch 工作也放 `vita` 分支，还是从它分出 `switch` 分支，开工时定。
- **HD 走 B（GL 4.3）还是 C（NVK）。** S3 开工时按 NVK 的成熟度定。
- **A/B 键位。** 按任天堂习惯（右确定）还是按 Deck 的位置映射。
- **提频开关。** 是否需要，S1 测完默认频率再说；FastLoad 只用于加载时段。
- **随包内容进 romfs 还是放 NRO 旁边。** romfs 单文件方便，但改对白文本（玩家可改的覆盖目录照旧在 SD 卡）不受影响；倾向 romfs。

## 不在本计划内

Switch 2（自制系统尚无公开可用的方案；初代 Switch 的 NRO 在 Switch 2 上的兼容性另议）；官方 NSP 与任何签名/sigpatches 相关的分发；Switch 版的滤镜、录像、MOD 代码补丁、更新检查；把 GL 路径合并进桌面发布包。
