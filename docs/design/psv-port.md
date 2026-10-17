# PS Vita 移植方案（调研）

2026-10-16。接续[安卓移植方案](android-port.md)与[三平台移植计划](three-platform-port.md)。本页是调研结论和建议方案，依据有三部分：

- 对本仓库宿主代码与固定提交的上游（N64Recomp `ffb39cd`、N64ModernRuntime `cdf5abb`、RT64 `4337374`、RecompFrontend `b1a1477`）的静态审计，行号以 main `399892ad` 为准；
- Vita 自制平台的公开资料：vitaGL 与 VitaSDK 的仓库、社区整理的 [PSVita-Dev-Wiki](https://github.com/robin994/PSVita-Dev-Wiki)（其中 libultraship 系移植的实机踩坑记录最有参考价值）、DaedalusX64-vitaGL 与 [goldenballoon-vita-port](https://github.com/zm2283145/goldenballoon-vita-port) 的源码与移植记录；
- 社区现有的 N64 移植先例：读了源码和文档，没有编译或运行。

**本机没有装 VitaSDK，没有为 Vita 编译或运行过本项目，也没有 Vita 实机参与本次调研。** 文中标“推断”的地方是工程判断，不是实测。

## 结论

- **可行，但性质和前几次移植不同。** Windows/Linux/安卓都是“换 plume 后端”，RT64 本体不动。Vita 的 GPU 只有 OpenGL ES 2.0 级能力，没有 Vulkan、D3D12、Metal，也没有计算着色器；RT64 的光栅器和 plume 三个后端都用不上，而宿主的全部 GPU 代码只在 `SRW64_ENABLE_RT64` 下编译（`src/host/CMakeLists.txt:79-270`），没有任何后端抽象。所以 Vita 版的主体工作是**换掉整条渲染路径**：N64 显示列表解释器、宿主自绘层、对白合成器、RmlUi 渲染接口，全部要有 GL 2 / GLES 2 级别的实现。
- **路线：** VitaSDK + vitaGL（OpenGL 子集翻译到 sceGxm）+ SDL2。N64 画面用 libultraship 的 Fast3D（MIT，F3DEX2 显示列表解释器 + 组合器到着色器）接到 ultramodern 的 `RendererContext` 上，或在 V0 统计出本作用到的 GBI/组合器子集后自写一个小解释器；RmlUi 用其自带的 GL2 后端；对白合成器与文字覆盖层改成 GL 纹理四边形。
- **先在桌面上把 GL 后端做出来，再上 Vita。** 这条 GL 路径在 Linux/macOS 的 OpenGL 2.1 上就能跑，现有的 MCP 截图与 `check_*.py` 脚本可以直接拿 RT64 版做对照。Vita 上的迭代慢（打包、安装、只能靠日志和 coredump），而且 Sony 的运行时着色器编译器不稳定（见下文），把平台无关的大头先在桌面做完，Vita 阶段只剩平台问题。
- **CPU 侧的 32 位阻塞项都在运行库里，数量有限，可以用现有的补丁脚本方式解决**：`__int128`、4 GiB 的 `mmap` 保留、MOD 系统的 64 位断言、sljit JIT、RSP 向量单元没有 ARMv7 路径。生成的 `funcs_*.c` 本身没有 64 位乘除指令。
- **Vita 版的范围是“原版画面 + 三语文本 + 现代页面”**：不带 HD 包（558 MB、全解压 RGBA8 带 mip，机器装不下），不带滤镜、插帧、录像、MOD。文字翻译的三条路（对白合成器、原版界面文字覆盖层、原生页面）都要保留，否则汉化就没了。
- **主要工作量（推断）：**
  1. GL 渲染路径：Fast3D 接入与本作特有钩子（约等于一个新的 `graphics.cpp` + `native_gpu`），占一多半；
  2. 运行库 32 位补丁与交叉编译；
  3. Vita 平台层：打包、输入、存档目录、内存与着色器预热、时钟策略；
  4. 宽屏（扩展 GBI 的五条指令要在新解释器里实现）与触屏。
- **前提：** 一台破解的 Vita，并按 vitaGL 要求提取 `libshacccg.suprx`（两者已备好，见「待定」里的实机一条）。没有实机就只能走到 Vita3K 模拟器，而社区记录里有一整类 bug 只在实机出现。

## 目标与范围

| 项 | 选择 | 理由 |
| --- | --- | --- |
| 硬件 | PS Vita / PS TV，4×Cortex-A9 444 MHz（可到 500）、SGX543MP4+、960×544 | 只有一种硬件，没有机型分化 |
| 工具链 | VitaSDK（GCC，`--enable-threads=posix`，newlib + pthread-embedded），用官方 Docker 镜像在 Mac 上交叉编译 | 本机没装 SDK；Docker 已有（29.6），与 Linux 包的构建方式一致 |
| 图形 | vitaGL（LGPL-3.0/GPL-3.0）+ 新写的 GL2/GLES2 渲染路径 | RT64 不可用；vitaGL 是 Vita 上事实标准的 GL 层，SDL2、libultraship 系移植都用它 |
| N64 显示列表 | libultraship Fast3D（MIT）为底，或自写 | 见[图形后端选择](#图形后端选择) |
| 窗口、输入、音频 | VitaSDK 自带 SDL2；vitaGL 由我们自己 `vglInit`，`vglSwapBuffers` 呈现 | 宿主用的是 SDL2 API（经 sdl2-compat）；Vita 的 SDL2 库存版本 GL 支持要走 Northfear 分支或直接调 vitaGL，SSB64 移植选了后者 |
| 内存 | `ATTRIBUTE2=12`，用户内存 365 MiB；显存 CDRAM 112 MiB | 默认只有 256 MiB；DaedalusX64、DevilutionX、xash3d 都开到 12 |
| 分发 | VPK 自用侧载，玩家把 `rom.z64` 放进 `ux0:data/Marchwind64/` | 与安卓相同：包里含 ROM 派生代码，只做侧载 |
| 验收 | 先 Vita3K，再实机 | Vita3K 已装好（见[开发环境](#开发环境2026-10-16-已在本机搭好)），适合早期迭代；实机才算数 |

## 平台事实

以下来自 vitaGL/VitaSDK 仓库与 PSVita-Dev-Wiki，2026-10-16 核查：

- **CPU。** 四核 Cortex-A9，ARMv7-A，带 NEON 和 VFP，没有 64 位模式。前台应用按惯例只算 3 个可用核。时钟 333/444 MHz，PSVshell 可到 500。DaedalusX64 启动时设 `scePowerSetArmClockFrequency(444)`、总线 222、GPU 222、XBAR 166（`Source/SysVita/main.cpp:432-435`）；社区建议“先查再只升不降”，别覆盖玩家自己的超频。
- **GPU。** PowerVR SGX543MP4+，GLES 2.0 级：可编程顶点/片元着色器，无计算着色器、无整数纹理、无 StructuredBuffer。TBDR 架构，切换渲染目标代价高。原生 API 是 sceGxm，着色器语言是 Cg；vitaGL 把 GL 调用翻译到 sceGxm，GLSL 由它内置的 GLSL→Cg 翻译器处理，**只认 GLSL ES 1.00 方言**（goldenballoon 的移植记录：`#version 320 es`、`in/out`、`texture()` 一律要改写）。
- **运行时着色器编译不可靠。** `SceShaccCg` 单次编译可达 10 秒以上，同一份源码有时成功有时报内部错误，与进程堆占用相关（SSB64 移植：堆约 68 MiB 时能编，涨到 107–115 MiB 后新着色器全失败）；失败后立刻重试会挂死。四个 libultraship 系移植的共同做法是 `glGetProgramBinary` 落盘缓存；SSB64 移植进一步在启动后、加载资源前预热全部 41 组着色器，并给缓存加了版本头、哈希和 `GL_LINK_STATUS` 校验。
- **内存。** 主存 512 MiB 中用户分区 365 MiB，默认限 256 MiB，`vita-mksfoex -d ATTRIBUTE2=12` 解锁全部；CDRAM 128 MiB 中 112 MiB 可用；物理连续池 26 MiB（1 MiB 对齐）。没有 `mmap`，没有惰性增长的栈：线程栈大小创建时定死。
- **线程。** VitaSDK 的 GCC 以 posix 线程模式构建（`vitasdk/buildscripts`：`BuildGccFinal.cmake:76`），`std::thread`/`std::condition_variable` 可用，底层是 `sceKernelCreateThread`。社区记录的“手工换栈的协程里不能做内核调用”一类崩溃**与本项目无关**：ultramodern 给每个 N64 线程开一个真正的宿主线程（`build/recomp/runtime-lifecycle/threads.cpp:276`），不是纤程。
- **显示。** 960×544，约 16:9.4。vitaGL 支持 MSAA（DaedalusX64 默认 4×）。
- **许可。** vitaGL 是 LGPL-3.0/GPL-3.0，与本项目的 GPL-3.0-or-later 兼容。GLideN64 的 LICENSE 是 GPL v2 且没有“or later”，**不能与 GPL-3 代码合并**；DaedalusX64 的渲染器同为 GPL-2.0，同样不用。libultraship 是 MIT。

## 已有先例

| 项目 | 要点 |
| --- | --- |
| [DaedalusX64-vitaGL](https://github.com/Rinnegatamante/DaedalusX64-vitaGL) | 动态重编译的 N64 模拟器，HLE 图形与音频，vitaGL 渲染。newlib 堆 256 MiB、`ATTRIBUTE2=12`、`vglInitExtended` 给 24 MiB 显存池（`main.cpp:67,460`；`Source/CMakeLists.txt:150`）。Rinnegatamante 称 SSB64 在它上面能满 60 帧，说明 GPU 吞吐不是瓶颈。GPL-2.0，只做性能参照。 |
| Rinnegatamante 的 libultraship 系移植：Ghostship（SM64）、2ship2harkinian、SpaghettiKart、Starship | 都是 Fast3D 经 vitaGL。渲染层改动在他自己的 libultraship 分支里，**没有公开**；SSB64 移植者是私下拿到后合并的。着色器都是运行时惰性编译加 `glProgramBinary` 落盘。 |
| [goldenballoon-vita-port](https://github.com/zm2283145/goldenballoon-vita-port)（DKR 反编译） | **公开、记录最全。** 基于上游 libultraship 的 `gfx_opengl.c` 加 Vita 专用的 GLSL 改写；实机踩坑：ILP32 下“像指针的值”被当指针、64 位专用分支在 32 位是空操作、栈上大缓冲溢出、`SDL_GetWindowSize` 返回 0×0、LiveArea 模板格式。1.7.2 已能全程通关，带 HD 贴图包支持。 |
| battleship-vita（SSB64，见 PSVita-Dev-Wiki 案例） | 着色器预热与校验缓存的出处；另记录了 `SDL_GL_SwapWindow` 在库存 SDL2 下呈现错表面，要直接 `vglSwapBuffers`。 |
| N64Recomp 到 32 位平台 | **没有找到任何先例。** 上游只有 x86_64 与 ARM64 构建，`recomp.h` 没有 32 位分支。 |

## 本仓库现状与阻塞项

### 运行库：32 位 ARM 阻塞项

全部在上游运行库里，生成代码本身无关。本仓库已经用 `tools/recomp/toolchain/prepare_*.py` 按白名单字符串替换给 RT64 与运行库打补丁，Vita 的改动走同一机制。

| 阻塞项 | 位置 | 做法 |
| --- | --- | --- |
| `__int128` 不存在，`#error` 中止 | `N64Recomp/include/recomp.h:39-70`（`DMULT/DMULTU`） | 加 32 位分支：用四次 32×32 乘法拼 128 位。生成代码里 `DMULT/DDIV` 用量为 0，正确性风险低 |
| RDRAM 用 `mmap` 保留 4 GiB、`mprotect` 512 MiB | `runtime-lifecycle/recomp.cpp:976-995`（`addresses.hpp:10,12`） | 改为 `malloc`/`memalign` 16 MiB：游戏用 4 MiB，另有覆盖段 14（0x80400000）与宿主暂存区（0x807F0000）都在 8 MiB 内；`guest_memory.hpp:8-10` 本就按 0x800000 设限。地址算术是 `rdram + (reg + off - 0xFFFFFFFF80000000)`，ILP32 下按低 32 位截断，偏移在范围内即正确 |
| MOD 系统 `static_assert(sizeof(size_t) == 8)`，`uintptr_t << 32` | `librecomp/include/librecomp/mods.hpp:54`、`mods.cpp:2495` | Vita 版不要 MOD/JIT：`mods.cpp` 不编，`initialize_mods()/scan_mods()`（`recomp.cpp:968-969`）替换为空实现。重编译堆（o1heap，约 496 MiB）一并去掉 |
| sljit JIT（LiveRecomp）与 `PROT_EXEC` | `librecomp/CMakeLists.txt:65`、`mods.cpp:225-247` | 随 MOD 系统一起不编 |
| RSP 向量单元只有 x86_64/aarch64 的 SIMD 路径 | `rsp_vu.hpp:23-31` | 先走标量路径（271 个 `_mm_` 用法都有守卫）。只有音频微码（`audio.cpp`，73 KB）走 RSP 重编译，每帧量小；若实测占 CPU，再试 sse2neon 的 ARMv7 模式 |
| 函数查找表 | `recomp_overlays.inl`：17 段、3,468 项，`unordered_map<int32_t, func*>` | 与指针大小无关，不用改；7,381 处 `LOOKUP_FUNC` 的开销在 444 MHz 上值得看一眼（可换成按地址直接索引） |
| 宿主线程优先级 | `threads.cpp:100,114-116,151` 三个平台都没生效 | Vita 上同样不设。但 `std::thread` 默认栈要显式给够（pthread-embedded 的默认值待查） |

**V0 实做（2026-10-16，`tools/recomp/toolchain/prepare_vita_runtime.py`）：** 上表的补丁都已写成生成替身（不改上游检出，写到 `build/recomp/vita-runtime/`，带前后哈希的 manifest），加上交叉编译时才暴露的三处头文件平台判断：

| 新发现 | 位置 | 做法 |
| --- | --- | --- |
| `WindowHandle` 只为 Win32/Linux/Android/Apple 定义，`__vita__` 下没有类型 | `ultramodern/include/ultramodern/renderer_context.hpp:48-62` | `defined(__vita__)` 走 Linux 的 `SDL_Window*` |
| moodycamel 信号量只认 `_WIN32`/`__MACH__`/`__unix__`，Vita 不定义 `__unix__` 就 `#error` | `thirdparty/concurrentqueue/lightweightsemaphore.h:27,168` | 加 `__vita__`：newlib 有 `semaphore.h`，POSIX 分支直接可用（`blockingconcurrentqueue.h` 一并复制到替身目录，让它的同目录包含找到补过的头） |
| newlib 的 libstdc++ 没有 `std::quick_exit` | `ultramodern/src/error_handling.cpp:27` | 与 Apple 分支一样用 `std::_Exit` |
| `allocation_size = 4 GiB` 在 32 位 `size_t` 上溢出为 0（只是告警） | `librecomp/include/librecomp/addresses.hpp:12` | 替身里的 RDRAM 分配不再用它；整套运行库在 `-Wall -Wextra` 下只有这一条与指针宽度有关的告警（arm-linux-gnueabihf GCC 13，47 条告警里其余是 sign-compare/unused） |

原先列的 `misc_ultra.cpp:12`、`ultra64.h:43,106` 三处 `uintptr_t`/`intptr_t` 转换在 32 位下不产生告警（宽度正好相等），`native_marker.cpp:318-321` 属于 Vita 不编的 GPU 代码。`threads.cpp` 的线程命名只有 Win32/Linux/Apple 三个分支，Vita 下 `set_native_thread_name`/`set_native_thread_priority` 没有定义：静态库能编，链接整机时要补一个空分支（V2）。

### 渲染：要替换的清单

| 组件 | 现状 | Vita 做法 |
| --- | --- | --- |
| N64 画面 | `SRW64Renderer : ultramodern::renderer::RendererContext`（`src/host/graphics.cpp:418`），`send_dl` 把 F3DEX2 2.08 显示列表交给 RT64（674-739），扩展 GBI 0x64 常开（705） | 新的 `RendererContext`：显示列表交给 Fast3D（或自写解释器），输出到 vitaGL 的 FBO 或直接后备缓冲 |
| 本作对 RT64 的钩子 | `native_model_hook_patches.py`：在 `tri1/tri2/texrect` 上分类，命中时回调宿主在场景目标上画；`SetRenderHooks` 的 init/capture_frame/deinit；工作负载/呈现 ID | 在新解释器的绘制循环里提供同样的 classify/render 回调；呈现 ID 对应到 `vglSwapBuffers` |
| 宿主自绘层 `srw64::gpu`（`native_gpu.hpp/cpp`，557 行）与五个层、11 对 HLSL（534 行） | `SV_VertexID` 无顶点缓冲、StructuredBuffer 顶点拉取、push constant、`Texture2D<uint>` 整数读取、深度测试 | 新 `srw64::gl` 层：顶点属性 + uniform，GLSL ES 1.00。**首批只做 HdSprite（标题菜单、章节标题卡、序章结局页、界面文字覆盖层、战斗横幅）和 HdMap 的菜单边框分支**（调色板在 CPU 解成 RGBA 再上传）。HD 地图、背景、头像、5600 标记与世界地图模型不做 |
| 对白合成器 | `dialogue_plume.cpp`（91）+ `pixel_compositor.cpp`（139）：局部纹理更新 + 全屏三角形 | 直接 `glTexSubImage2D` + 一个纹理四边形，固定管线即可；CPU 光栅（FreeType/HarfBuzz/ICU）不变 |
| RmlUi | RecompFrontend 的 plume 适配器（`frontend-adapter/ui_renderer.cpp`，738 行），`RenderInterfaceCompatibility` API：即时几何、裁剪、4×4 变换、纹理 | 改用 RmlUi 自带的 `RmlUi_Renderer_GL2.cpp`（固定管线，无着色器，已在检出里）；图片缓存要加淘汰 |
| 呈现钩子 `capture_frame`（`graphics.cpp:231-416`） | 两侧清黑、姓名页遮盖、对白、RmlUi、滤镜、截图/录像回读 | 重写为 GL 顺序绘制；滤镜去掉；截图回读改 `glReadPixels`（调试接口要用） |
| 滤镜 `post_filter.cpp`（560 行） | librashader，`dlopen` 加载 | 不编 |
| 插帧 | RT64 内部 | 没有 |
| CPU 侧显示列表编辑 | `wide_map`、`battle_hud`、`menu_widen`、`rom_art`、`ui_text`、`game_hooks` | 原样编译。`wide_map`/`battle_hud` 插入的扩展 GBI 指令（PushScissor/SetScissor/SetRectAlign/PopScissor/SetRectAspect）要由新解释器实现，否则只能 4:3 |
| 战斗残影 | 原版 `80083744` 把上一帧当 64×16 的 texrect 画回来，RT64 靠帧缓冲跟踪 | Fast3D 不回写帧缓冲到 RDRAM。办法：命中 VI 帧缓冲地址段的贴图加载时改绑上一帧的 FBO 纹理；或在 Vita 关掉这个效果。V1 要定 |

着色器来源：宿主 22 个 `.hlsl` + 对白 2 个 + RmlUi 2 个，都由 DXC 构建时编成 SPIR-V/DXIL/MSL 嵌入（`cmake/NativeGpu.cmake`、`PixelCompositor.cmake`）。Vita 版的 GLSL ES 1.00 源码直接随包，运行时交给 vitaGL。

### 依赖

| 依赖 | 现状 | Vita |
| --- | --- | --- |
| SDL3 3.4.16 + sdl2-compat | tarball 源码构建 | 用 VitaSDK 的 SDL2 包（宿主只调 SDL2 API，79 个符号）。窗口标志 `SDL_WINDOW_VULKAN/METAL`（`graphics.cpp:917-924`）改为无标志，GL 上下文由 vitaGL 管 |
| FreeType 2.14.3、HarfBuzz 14.2.1 | tarball | VitaSDK 镜像里是 FreeType 2.13 系（libtool 26.6.20，`libfreetype.a` 0.75 MB）与 HarfBuzz 14.5.0（`libharfbuzz.a` 2.1 MB），2026-10-16 核对；先用它们，字形对照有差异再按本项目钉的版本源码交叉编译 |
| ICU 78.3 | 只有 `portable_text.cpp` 10 处用（断行等），数据已裁到 0.45 MB | **VitaSDK 镜像里有 ICU 73.2**（`arm-vita-eabi/lib/libicuuc.a`、`libicui18n.a`、`libicudata.a` 与 `include/unicode/`；2026-10-16 核对，本页先前写“没有”是错的）。库可直接链，不用交叉编译也不用改这 10 处；但它的 `libicudata.a` 只有 916 字节（空壳，不带数据），数据要自己给：本项目裁过的数据包（安卓做法，断行/双向/文字系统约 0.45 MB）按 ICU 73 重新生成一份，启动时 `udata_setCommonData` 喂进去。另：VitaSDK 的 FreeType 是 2.13 系（libtool 26.6.20）、HarfBuzz 14.5.0、SDL2 2.x，与本项目钉的 2.14.3 / 14.2.1 不同版本，V2 若对照出字形差异再源码交叉编译 |
| librashader | 预编译 + `dlopen` | 不要 |
| nlohmann json、moodycamel、o1heap、miniz、xxHash | 头文件/单文件 | 直接用 |
| `std::filesystem` | 102 个源文件用 | newlib 的 `std::filesystem` 实现不全（推断，V0 验证）；`src/native/app` 已有 POSIX 路，缺的函数在那里补 |
| 调试接口 | TCP（`debug_transport.cpp`） | Vita 有 BSD socket，可按 Deck/安卓的 `attach.py` 方式远程接 MCP，值得保留 |

### 体积与内存预算（生成代码与运行库已实测，其余推断）

V0 用 VitaSDK 的 `arm-vita-eabi-gcc` 15.2.0（ARMv7-A+NEON，Thumb-2，`-DNDEBUG`）把重编译代码、音频微码与运行库编成静态库（`tools/release/vita/build.sh`，2026-10-16，Docker 10 核 arm64）：

| 静态库 | `-O2` .text | `-Os` .text | 说明 |
| --- | --- | --- | --- |
| `srw64_cpu`（70 个 `funcs_*.c`，30.3 MB 源码） | 3,092,324 B（2.95 MiB） | 2,678,556 B（2.55 MiB） | 原先估的 8–12 MB 高了 3–4 倍：每条 MIPS 指令在 Thumb-2 上平均约 3 字节 |
| `srw64_audio`（RSP 音频微码，标量路径） | 21,788 B | 13,816 B | |
| `librecomp`（去 MOD/JIT/堆） | 43,760 B | 30,152 B | `.rodata` 78 KB |
| `ultramodern` | 44,592 B | 30,112 B | |
| 合计 | 3,202,464 B（3.05 MiB） | 2,752,636 B（2.63 MiB） | 全部编译 35 s / 29 s（`-j10`） |

| 项 | 数据 | Vita 估计 |
| --- | --- | --- |
| 生成代码 | 见上表 | 约 3 MiB 代码（`-O2`；`-Os` 省 13%，先不用），远在任何 ELF 限制之内，不需要拆模块 |
| RDRAM | 8 MiB（`osMemSize`），实际用 4 MiB | 16 MiB `calloc`（替身里已这样分配，qemu 下验证可跑） |
| ROM | 32 MiB 整读进 `std::vector`（`recomp.cpp:206-220`） | 先整读（够用）；紧张时改 `pi.cpp` 走文件流 |
| 字体 | `HarmonyOS_Sans_SC.ttf` 20.6 MB，RmlUi 与对白各自 `FT_New_Memory_Face`，最坏 5–10 份（`android-port.md:220-224`） | **必须共享一份缓冲**。许可证禁止子集化，不能靠裁字体省内存 |
| 对白文本 | 786 个文件，20 MB | 按需读，常驻的只是当前关 |
| RmlUi | `librmlui.a` 154 MB（静态库含调试信息，不代表运行占用）；图片缓存不淘汰 | 页面与字形图集估 20–40 MB；要加淘汰 |
| 显存 | HD 包全解压 RGBA8 带 mip，背景到 1920×1440 | 不带 HD。N64 贴图很小，vitaGL 池给 24–48 MiB 足够 |
| 合计 | — | 不带 HD 约 150–200 MiB，在 365 MiB 内。带 HD 不可行 |

### 线程

- N64 侧 4 个 `osCreateThread`（入口 0x80078148、0x8007F660、0x8007FC48、调度器 0x80080838），各占一个宿主线程，协作式轮流运行，同一时刻只有一个在跑。
- 运行库：VI、SP 任务、Gfx、定时器、线程清理（MOD 配置线程随 MOD 系统去掉）。
- 渲染：RT64 的呈现/渲染/上传/贴图流/着色器线程全部消失；vitaGL 自己有一个垃圾回收线程。Fast3D 在 Gfx 线程里同步解释显示列表。
- 宿主：精灵工作线程、日志泵、调试接口。HD 地图解码、滤镜编译、更新检查不要。
- 3 个可用核够用：同一时刻真正忙的只有“当前 N64 线程 + Gfx 线程 + 音频”。

## 方案

### 图形后端选择

| 方案 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- |
| A. libultraship Fast3D（MIT）剥出显示列表解释器 + `gfx_opengl` 后端 | F3DEX2 全集、TMEM/贴图格式/组合器→着色器都现成；Vita 上有四个成品 + 两个公开移植的踩坑记录 | 上游版本深度绑了 OTR 资源指针，要加一条“RDRAM 地址模式”；Vita 专用改动（Rinnegatamante 分支）不公开，要照 goldenballoon 自己做；没有帧缓冲回写 | 备选；TMEM 装载与贴图解码照它写 |
| B. 自写解释器 | 只实现本作用到的子集：V0 统计为 7 种组合器、35 种绘制状态、3 种贴图格式、全单周期（[统计](../data/gbi-usage.md)）；着色器写死并预热；与现有钩子和扩展 GBI 一体设计，绘制调用合并可以按本作的列表结构做；无外部代码 | 组合器与 TMEM 细节要自己踩一遍；统计外的状态（后期关卡、未跑到的演出）要在 V1 对照时补 | **V0 后首选** |
| C. GLideN64 | 精度最高，GLES2 路径成熟 | GPL-2.0 only，与 GPL-3 不兼容；GLSL 动态生成量大 | 排除 |
| D. DaedalusX64 的 HLE 渲染器 | 为 Vita 调优 | GPL-2.0；与它的模拟器结构耦合 | 排除 |
| E. 不移植宿主，用 DaedalusX64-vitaGL 跑原 ROM | 零工作量 | 本项目的汉化是运行时文字与页面，不是 ROM 补丁；无翻译、无多存档、无原生页面 | 排除 |

### 分层

1. **`srw64::gl`（新）** 取代 `srw64::gpu` 与 plume 的直接用法：`Texture`（RGBA8，`glTexSubImage2D` 局部更新）、`Quad`/`Mesh`（顶点属性数组）、`Program`（GLSL ES 1.00，uniform）、帧内顺序与裁剪。这一层在桌面 OpenGL 2.1 与 vitaGL 上同源。
2. **N64 解释器** 实现 `ultramodern::renderer::RendererContext`，暴露与现在相同的 classify/render 钩子、呈现回调与工作负载 ID，让 `game_hooks.cpp`、`native_sprite.cpp` 等上层代码尽量不改。
3. **呈现顺序** 保持现状：N64 画面 → 两侧清黑/姓名页遮盖 → 对白 → RmlUi → 调试截图回读。
4. **编译开关** `SRW64_RENDERER=rt64|gl`。桌面默认 rt64 不变；gl 在桌面只用于开发对照，不进发布包（避免多一套要维护的玩家可见配置）。

### 着色器策略

- 本作的组合器组合是有限集合：V0 用桌面 RT64 版的计数钩子（`src/host/gbi_stats.cpp`）跑了九个场景，7 种组合器取值、28 种 RDP 模式、35 种绘制状态，全部单周期（[统计](../data/gbi-usage.md)）。着色器按这 35 种状态预先写成固定集合（组合器 7 种 × 是否贴图/调色板 × Alpha 比较），不做运行时生成。
- Vita 上按 SSB64 的做法：启动后、加载资源前预热全部已知组合，落盘缓存带版本头 + 源码哈希 + 负载哈希 + `GL_LINK_STATUS` 校验，失败只重生成那一条。遇到列表之外的组合才惰性编译。
- 宿主自绘层只需要 2–3 个固定着色器（纹理四边形、带调色板已解码的四边形、纯色），对白合成器与 RmlUi GL2 走固定管线，不进运行时编译器。
- 不碰 `vglSetupRuntimeShaderCompiler` 的优化档位：社区实测偏离默认值只会把失败变成挂死。

### 画面与宽屏

- V1/V2 先 4:3（960×544 内居中，两侧 208 px 清黑或原版侧幅），宿主已有的画面宽度判断按 4:3 走，`wide_map`/`battle_hud` 不插指令。
- V3 在解释器里实现扩展 GBI 的五条指令（语义只是裁剪矩形与矩形对齐/宽高比提示），恢复“随屏幕 4:3–16:9”的填满策略。
- MSAA 先关；vitaGL 的 MSAA 在 TBDR 上便宜，V3 实测后再定 2×。
- 分辨率：N64 画面按 2× 内部分辨率（640×480→等比到 544 高）或原生 1×，V2 看帧时间定。

### 输入

Vita 比 N64 少 C 键，多一根右摇杆、前后触摸。沿用 `input_bindings.hpp` 的手柄家族机制加一个 Vita 家族，默认：

| N64 / 宿主功能 | Vita |
| --- | --- |
| A / B | ○ / × |
| C← C↑ C↓ C→ | 右摇杆四向；C← 另给 □（战斗中止键，见 `srw64-battle-animation-loop`） |
| L / R / Z | L / R / 按住 L |
| Start | Start |
| 设置面板 | Select |
| 快进 / 跳过 | R+○ / R+Start（沿用调试表的组合） |
| 2 倍速 | 前触摸右上角（与安卓 2× 一致）或 Select 长按 |
| RmlUi 页面指针 | 前触摸 |

按键图标字体 `SRW64Prompts.ttf` 加 PlayStation 形状一组。

### 文件与存档

- `ux0:data/Marchwind64/`：`rom.z64`（玩家放入，沿用哈希校验与错误提示）、`saves/`（S0–S5 布局不变）、`settings/`、`dialogue/` 覆盖目录（F5 热重载的玩家可改文本照旧）、`shader_cache/`、`logs/`。
- VPK：`sce_sys/`（LiveArea 用旧版 `livearea-background` 模板，goldenballoon 记录新模板在部分机器装不上）、`eboot.bin`、`content/`（对白文本 20 MB、字体 21 MB、关卡 metadata）。
- 关于页的问题报告 zip 保留；更新检查去掉（Vita 的 TLS 栈旧，`update_http` 不值得适配）。

### 不做

HD 包与全部 HD 图层（地图、背景、头像、5600 标记、世界地图模型、机体立绘/图标哈希替换）、RA 滤镜与框体、插帧/帧率设置、录像、MOD 与 JIT、更新检查、Vita 专用的图形设置页之外的任何新功能。

## 开发环境（2026-10-16 已在本机搭好）

| 项 | 事实 |
| --- | --- |
| 模拟器 | Vita3K v0.2.1（CI 持续构建 4186-f0273e4c，`macos-arm64-latest.dmg`），装在 `/Applications/Vita3K.app`。arm64 原生，自带 MoltenVK（Vulkan 后端）与 Qt，要求 macOS 13.3+。Homebrew 没有 cask，更新就重下 dmg |
| 数据目录 | `~/Library/Application Support/Vita3K/Vita3K/`：`fs/` 是模拟的文件系统（`os0`/`vs0`/`sa0` 固件已装，`ux0/app/<TITLEID>` 是安装的自制，`ux0/data` 是应用数据，`ur0/data` 放着色器编译器），`config.yml`、`vita3k.log`、`shaderlog/` |
| 固件 | Sony 官方 3.74：系统包 `PSVUPDAT.PUP`（133.8 MB，playstation.com 支持页链接）与字体包 `PSP2UPDAT.PUP`（56.8 MB，Sony 更新服务器）都只能走 http，命令行 `Vita3K --console --firmware <pup>` 安装，各用约 3 秒 |
| 着色器编译器 | Vita3K 对 `SceShaccCg` 的 HLE 全是 `UNIMPLEMENTED`（`vita3k/modules/SceShaccCg/SceShaccCg.cpp`）。vitaGL 经 vitaShaRK 用 `sceKernelLoadStartModule("ur0:/data/libshacccg.suprx")` 加载真模块，在模拟 CPU 上跑。已从实机（3.68 + h-encore，用 ShaRKBR33D 导出）拷入 `fs/ur0/data/libshacccg.suprx`：3,407,920 字节，假 SELF（SCE 头类型 0xC0，0xA0 处即明文 ELF）。该文件是 Sony 的模块，不进仓库、不随包分发 |
| 验证 | DaedalusX64-vitaGL（Nightly，2026-10-12 构建）：`Vita3K --console --frontend sdl --log-level 2 -r DEDALOX64`。日志里 `Module SceShaccCg ... module_start returned 0x0`，随后 4 个着色器（`ux0:data/shader_cache/v9931/{v,f}/*.gxp`）先未命中、编译后写盘再读回；3D 卡带浏览界面正常显示，识别到放在 `fs/ux0/data/DaedalusX64/Roms/` 的原版 ROM。固件、VPK 安装、启动、vitaGL 运行时着色器编译整条链路都通 |
| 命令行 | `-r <TITLEID>` 运行已装应用，`--frontend sdl` 只开游戏窗口、应用退出即退出，`--console` 日志到终端，`--log-level 0–6`，`--gdbstub`（config.yml 里 `gdbstub: true`），`--lle-modules`，`--log-active-shaders`，`-s <gxp>` 单独反编译一个着色器。注意 `--console` 模式下应用卡在系统对话框时 SIGTERM 不退出，要 `kill -9` |
| 工具链 | Docker 镜像 `vitasdk/vitasdk:latest`（8227da1b，2.17 GB；另有 `2026.08` 标签）：GCC 15.2.0、CMake 3.28、`$VITASDK=/usr/local/vitasdk`，`share/vita.toolchain.cmake`，`vita-elf-create`/`vita-make-fself`/`vita-mksfoex`/`vita-pack-vpk` 齐全；库里已有 `libvitaGL.a`（含 GLSL 翻译器接口）、`libvitashark.a`、`libSDL2.a`、`libfreetype.a`、`libharfbuzz.a`，没有 ICU |
| 模拟器的局限 | Vita3K 不能当性能依据（动态重编译到 arm64，速度与实机无关）；社区记录有一整类“只在实机崩”的问题（内核调用、NEON 预编译库、真实着色器编译器的堆压力）在 Vita3K 上不复现。它的用途是：验证交叉编译产物能加载、文件/存档/输入逻辑、画面大体正确，以及用 `--gdbstub` 查崩溃 |

## 阶段

### V0 可行性探针（桌面与 Docker，不需要实机）

1. 用已拉取的 `vitasdk/vitasdk` 镜像在 `tools/release/` 下新建 vita 目录（Dockerfile 与构建脚本），照 Linux 的 `build.sh` 方式。
2. 给运行库打上表里的 32 位补丁（`prepare_vita_runtime.py`），把 `srw64_cpu`（全部 `funcs_*.c`）+ `audio.cpp` + ultramodern/librecomp 交叉编译成 ARMv7 静态库。
3. 记录：`.text` 大小、编译时间、编译警告里与指针宽度相关的项（`uintptr_t`/`intptr_t` 转换共 3 处已知：`misc_ultra.cpp:12`、`ultra64.h:43,106`、`native_marker.cpp:318-321`）。
4. 在 qemu-arm（Docker 内 `arm-linux-gnueabihf` 构建同一套源码）下跑一个无图形的“只跑 N 帧开场”的宿主，验证 ILP32 下生成代码的正确性（只看正确性，不看速度）。
5. 桌面 RT64 版加组合器/GBI 计数，跑一遍 `check_*.py` 场景集，导出：用到的 F3DEX2 指令、RDP 模式、组合器键、贴图格式、每帧绘制调用数。
6. 决定 ICU 的去留与 Fast3D/自写的取舍，把数字填回本页。

**门槛：** 全部源码在 ARMv7 下编译通过；qemu 下开场前 300 帧的 RDRAM 快照与 macOS 版一致（现有 `srw64_memory` 工具可比对）；组合器清单到手。

**结果（2026-10-16，门槛三项都过）：**

1. 工具在 `tools/release/vita/`：`build.sh` 建镜像（`vitasdk/vitasdk:latest` 加 `gcc-arm-linux-gnueabihf` 与 `qemu-user`）并在容器里跑 `build_probe.py`；`CMakeLists.txt` 是探针工程；`probe_host.cpp` 是无图形宿主。运行库补丁在 `tools/recomp/toolchain/prepare_vita_runtime.py`（生成替身，见「运行库」一节的表）。结果汇总在 `build/recomp/vita/report.json`。
2. 四个静态库在 `arm-vita-eabi-gcc` 15.2.0 下全部编过，`-O2` 与 `-Os` 各一次，大小见「体积与内存预算」。
3. 指针宽度告警只有 `addresses.hpp:12` 一条；`-Wall -Wextra` 全开也没有别的（见「运行库」一节）。
4. 正确性：同一探针宿主（全部原生钩子调原版、地图宽度按 4:3，原版规则）用 `arm-linux-gnueabihf-g++` 13 编成 32 位 ARM，在 qemu-arm 下从开机跑到第 300 个帧边界（`80085F30`，游戏线程上截快照），与 macOS arm64 原生构建比对 8 MiB RDRAM：**只差 4 处共 50 字节，全是 `OSThread.context`（运行库存进客体内存的宿主指针，64 位 8 字节、32 位 4 字节）**；两次 qemu 运行逐字节相同，两次原生运行也只差这几个指针槽。VI 数 300、显示列表 147、音频任务 298 三边一致。做法上有一点要记住：运行库的 VI 线程按墙钟 60 Hz 发中断，游戏线程快慢不同时两边截到的帧相位不同（第一次比对差了 2,165 字节，全在音频合成器的工作区）；探针把 VI 改成「不快于 60 Hz、且游戏过了帧边界才发下一拍」（`build_probe.py` 写出的 `probe_events.cpp`），之后才可逐字节比。qemu 下 300 帧跑 5.3 s，与原生一样跟得上 60 Hz（qemu 是 arm64 上的动态翻译，不代表 Vita 速度）。两处无关 ILP32 的现象：32 位 Linux 要 `-D_FILE_OFFSET_BITS=64`，否则 Docker 挂载卷上 `stat()` 报 EOVERFLOW；qemu-user 下 libstdc++ 的 `copy_file` 失败，运行库每次写存档都报「Failed to write to the save file」（原生 0 次），只影响存档文件，不影响快照。
5. 显示列表统计：`src/host/gbi_stats.cpp`（宿主调试接口开着并设 `SRW64_GBI_STATS=<文件>` 才工作，在 RT64 读列表之前走一遍，不改渲染）、`tools/recomp/debug/check_gbi_usage.py`（九个场景各一次运行，原版图片、原版画面）、`tools/recomp/analysis/gbi_usage.py`（合并解码成表）。结果在[本作用到的 F3DEX2 指令与组合器](../data/gbi-usage.md)，结论见「待定」。

### V1 桌面上的 GL 渲染路径

1. `srw64::gl` 层 + GLSL ES 1.00 的 HdSprite/HdMap 边框着色器。
2. N64 解释器（Fast3D RDRAM 模式或自写）接到 `RendererContext`；补 classify/render 钩子与呈现 ID；战斗残影方案落地。
3. 对白合成器 GL 版；RmlUi 换 GL2 后端 + 图片缓存淘汰；`capture_frame` 的 GL 顺序；`glReadPixels` 截图。
4. 字体缓冲只读一份。
5. 在 Linux（Mesa）与 macOS（OpenGL 2.1）上用 `--renderer gl` 启动，`check_dialogue.py`、`check_ui_text.py`、`check_battle_animation.py`、`check_intermission.py`、`check_title_menus.py` 等在 4:3 原版画面模式下与 RT64 版截图对照。

**门槛：** 开场→第 1 话→战斗演出→场间全部页面→存档读档，GL 版通过与 RT64 版相同的检查脚本（允许像素级差异，不允许缺字、缺层、错位）。

**进度（2026-10-16 开工，`vita` 分支）：** Vita 的全部工作放在 `vita` 分支（main 上只留本方案文档），V0 的提交已摘到分支上。

1. **解释器与 GL 后端（`src/gl/`，已做）。** `display_list.cpp` 是自写的 F3DEX2 子集解释器：RSP 矩阵、视口、光照、背面剔除；TMEM 照 RDP 的存法保存（奇数行 32 位字互换，LOADTILE/LOADBLOCK/LOADTLUT），按内容哈希解码成 RGBA8，只解一次；单周期组合器键、混合器的两种结果（alpha 混合，或不混合时按 alpha 比较/覆盖率裁掉）、Z 缓冲、纹理矩形与填充矩形；宿主写进列表的 RT64 扩展指令（裁剪栈、矩形对齐，按原点换算）；从颜色缓冲装的贴图改读那张画面（标题速度线、转场漩涡、战斗残影都是「上一帧整张以 78% 叠回」的反馈效果）；宿主打了标签的绘制（`G_NOOP` 带 SDW/SBG/SPT/SSN，窗框、剧情文字等）输出成「原生绘制」命令交给宿主。`gl_renderer.cpp` 只用 OpenGL 2.1 与 ES 2.0 的交集：每个颜色缓冲一个渲染目标（共享深度），组合器键各生成一个程序，小贴图装进 1024² 图集页（顶点带夹取边界，wrap/mirror 的单周期绘制折回 [0,1]），**标题画面约 400 个矩形每帧 5–6 次 draw call**。
2. **离线对照（已做）。** 宿主 `SRW64_GL_DUMP` 每隔 N 个列表转储整份 RDRAM 与列表（`SRW64_GL_DUMP_RUN` 连续几帧），同时存下 RT64 呈现同一 workload 的截图；`check_gbi_usage.py --dump` 在 4:3 窗口里跑各场景，`srw64-gl-replay` 离屏回放成 PNG（`--trace` 打印逐条命令、TMEM 装载与解出的贴图），`compare_gl_replay.py` 拼成「RT64 | GL | 差异」。八个场景 239 帧：世界地图 3D、战术地图、幕间、战前确认与连续帧的标题/剧情关与 RT64 只差宿主自绘层（文字、窗框）；每帧 draw call 中位数 4、p90 6、最大 40；桌面上解释一帧 0.2–0.75 ms。连续帧里第一帧缺「上一帧」（反馈效果偏暗），属转储方式，不是渲染错误。
3. **宿主接入（桌面已通）。** 同一个桌面宿主加运行时开关 `SRW64_RENDERER=gl`（不是编译开关：这样调试接口、MCP 截图和检查脚本原样可用）：`graphics.cpp` 的 `GLRenderer` 在窗口线程建 GL 2.1 上下文、交给图形线程；顶层列表读原生对白改过的副本；按 VI origin 找到颜色缓冲 4:3 居中呈现；调试截图用 `glReadPixels`；固定原版图片（`image_mode.configure(false)`；`original_only()` 在 HD 图层看来等于「HD 能开就开」）、固定 4:3。
   - **原生对白**（`dialogue_gl.cpp`）：同一套 CPU 光栅，补丁 `glTexSubImage2D`、预乘混合。
   - **RmlUi**（`src/native/ui/ui_renderer_gl.cpp`）：照 Plume 适配器做（非预乘混合、0 号白/1 号透明纹理、按名字排队的图片用 stb_image 解码）；RmlUi 可能在窗口线程建/释放纹理，所以 GL 调用全推迟到图形线程的 start…end。设置窗口、标题原生按钮、原生页面、`ui.click` 都可用。
   - **宿主标签绘制**（`native_quads.hpp`、`native_gl.cpp`）：模块报告「标签 id 画什么」——预乘 RGBA 像素加屏幕矩形或模型空间四边形；`native_map` 提供窗框（底图按当前调色板在 CPU 上平移，即 HdMapPS 的逐像素运算，按「资产＋调色板」缓存），`native_sprite` 提供剧情/标题/原版界面文字。解释器的原生命令带上三角形的变换与深度标志，GL 渲染器在帧内原位置回调宿主再恢复自身状态。坑：标签第二个字就是绘制 id（RT64 的 classify 这样读），先前为保证非零写成 `w1|1`，相邻两个记录撞号，标题环上「读取」画成了「继续」。
   - **实跑对照（10-17，同一脚本 RT64 与 GL 各跑一遍）：** 标题环形菜单、原版幕间菜单（窗框＋中文）、机体改造页、4:3 下的战斗演出（3D、立绘、原生台词、HUD）两边一致。
4. **还没做：** 宿主标签绘制的深度写入那一遍（HD 机体图才用，Vita 不带 HD）；字体缓冲共享；用现有 `check_*.py` 在 GL 下整套过一遍（含存档读档、全部原生页面）；GL 下的宽屏（扩展指令按原点换算已有，呈现与宿主图层还按 4:3）；把 plume 依赖从 GL 构建里剔除（V2 交叉编译的前提：`native_gpu`、`native_map`/`native_sprite` 里的 Plume 部分、`dialogue_plume`、Plume 版 RmlUi 适配器要能不编），以及 stb_image 的实现目前借用 RT64 的。

### V2 Vita 跑通

1. 宿主其余部分交叉编译：SDL2、vitaGL 初始化与呈现、`std::filesystem` 缺口、文件路径、线程栈大小。
2. VPK 打包、LiveArea、ROM 放置与错误提示。
3. Vita3K 跑通开场；实机跑通同一流程。着色器预热与校验缓存。
4. 时钟策略：查当前频率，只升到 444/222/222/166，不降。
5. 测量：帧时间（现有帧率读数）、`sceKernelGetFreeMemorySize`、音频欠载次数、各线程 CPU 占比。

**门槛：** 实机上第 1 话全程、一次战斗演出、一次存档读档；战术地图与场间页面稳定 30 帧（原版逻辑帧率）；开机到标题不超过 15 秒（含预热）。

### V3 掌机化与发布

1. 扩展 GBI 五条指令 → 16:9 填满；触屏指针；界面大小按 Vita 的 dp 选档（960×544 约等于“标准”）。
2. 性能：RSP 向量的 NEON 路径（若音频线程占比高）、`LOOKUP_FUNC` 直索引、Gfx 线程 1 ms 轮询改事件。
3. 设置页加 Vita 分页（时钟、MSAA、内部分辨率）；问题报告 zip；调试接口经 Wi-Fi 接 MCP。
4. `build_release.py` 加 Vita 产物、官网下载与系统要求、三语 README。

**门槛：** 用现有脚本集过一遍主线前 10 话的自动检查（经 Wi-Fi 调试接口）；连续游玩 2 小时无崩溃；内存峰值留 50 MiB 余量。

### V4 可选

头像 HD 的缩小版（768²→256²，JPEG 解码后上传，约 10 MB 显存）；2× 内部分辨率；PS TV 的 1080i 输出。只在 V3 之后、且实机有余量时考虑。

## 风险

| 风险 | 影响 | 应对 |
| --- | --- | --- |
| R1 运行时着色器编译器不稳定 | 首次进入某画面卡 10 秒或黑屏 | 组合器集合封闭 + 启动预热 + 校验缓存；宿主层与 UI 全走固定管线或 3 个固定着色器 |
| R2 444 MHz 上重编译代码不够快 | 战斗演出或剧情掉帧 | 64 位 GPR 在 ILP32 上每条指令约 2 倍开销（推断）。本作是 2D 为主的战棋，DaedalusX64 动态重编译在同机能跑满 SSB64；V0 的 qemu 只验正确性，速度只能实机测。预留 `LOOKUP_FUNC`、RSP NEON、Gfx 轮询三项优化 |
| R3 Fast3D 保真度 | 残影、两周期模式、texrect 边缘、半透明排序与 RT64 不同 | V1 在桌面逐场景对照；残影单独处理；无法解决的效果在 Vita 关闭并记录 |
| R4 内存 | 字体多份、RmlUi 缓存、ROM 整读叠加超 256 MiB | `ATTRIBUTE2=12`；字体一份；缓存淘汰；预算表随 V2 实测更新 |
| R5 newlib/VitaSDK 的 C++ 标准库缺口 | `std::filesystem`、`std::thread` 栈、locale | V0 编译阶段暴露；`src/native/app` 的 POSIX 路补函数 |
| R6 没有实机或实机迭代慢 | V2 之后无法推进 | 先确认有破解 Vita；Vita3K 只做早期；调试接口经 Wi-Fi 复用现有 MCP 流程 |
| R7 ELF 大小 | 加载失败 | 没有公开上限；V0 实测 `.text`，必要时把生成代码拆成多个 `suprx` 模块 |
| R8 许可 | 引入 GPL-2-only 代码 | 只用 MIT 的 Fast3D 与 LGPL/GPL-3 的 vitaGL；不看 GLideN64/Daedalus 的渲染代码 |
| R9 ILP32 专有 bug | “像指针的值”被当指针、64 位专用分支成空操作（goldenballoon 两例） | 审 `uintptr_t`/`intptr_t` 三处与 `OSThread.context`（宿主指针存进 RDRAM，32 位下占 4 字节，布局与 N64 原版一致，反而更自然）。**V0 已验：** 同一份源码的 32 位 ARM 构建在 qemu-arm 下跑开场 300 帧，RDRAM 快照与 arm64 原生构建只差 `OSThread.context` 四个槽位（见 V0 一节），生成代码在 ILP32 下没有发现错算 |

## 待定

- **实机。** 已备好：破解的 Vita 已用 VitaDeploy 的“Quick 3.65 Install”换成 3.65 + Ensō 永久破解（2026-10-16），开机自动生效。`ur0:data/libshacccg.suprx` 已在机上，Vita3K 也放了一份。插件登记在 `ur0:tai/config.txt`：vitacompanion 1.08（常驻 FTP 1337 与命令端口 1338：`launch`/`quit`/`reboot`/`press`/`promote` 等，已验证）、PSVshell（帧率、CPU、内存显示与调频，测性能前要确认在默认频率档）、iTLS。V2 的装包、启动、按键、重启都能在 Mac 上脚本化。机上 Wi-Fi 实测约 1.1 MiB/s，日常只替换变动的 `eboot.bin`，大文件走 VitaShell 的 USB 模式。崩溃时系统在 `ux0:data` 写 `.psp2dmp`，到时在 Mac 上装 vita-parse-core 解析。
- **Fast3D 还是自写：自写（V0 定，2026-10-16）。** 九个场景 22,222 帧、9,568 万条指令的统计（[本作用到的 F3DEX2 指令与组合器](../data/gbi-usage.md)）：组合器只有 7 种取值、全部单周期，绘制时的 (othermode, combine, geometry) 组合 35 种，混合式 4 种，贴图格式 3 种（CI4、CI8、RGBA16），没有两周期、雾、LOD、S2DEX、`G_BRANCH_Z`，`G_DL` 最深 2 层，颜色缓冲恒为 320 宽 RGBA16。这远小于 Fast3D 的覆盖面，而且着色器可以按 35 种状态预先写死、启动时全预热，正好绕开 Sony 运行时着色器编译器（R1）。自写要补的硬功夫只有 TMEM 装载/贴图解码（照 Fast3D 的 `gfx_pc.c` 写）与**绘制调用合并**：2D 场景每帧 300–650 条纹理矩形、标题演示战斗与世界地图最多约 2,000，一条一次 draw call 在 Vita 上跑不动，解释器要把连续同贴图同状态的矩形并成一个顶点缓冲，贴图按内容哈希缓存（`G_SETTILE`/`G_LOADTILE` 每帧重复装同样的小贴图）。统计没覆盖全部战斗演出与后期关卡，V1 做桌面 GL 路径时遇到表外的状态按同一套表加。
- **ICU：链 VitaSDK 自带的 ICU 73.2，数据自己给（V0 定）。** 镜像里有 `libicuuc.a`/`libicui18n.a` 与头文件，但 `libicudata.a` 是 916 字节的空壳；把安卓那套裁过的数据包（`tools/release/android/build_dependencies.py` 的做法，断行/双向/文字系统约 0.45 MB）用 ICU 73 的工具重新生成，启动时 `udata_setCommonData` 喂进去，`portable_text.cpp` 的 10 处用法原样保留。若 73 与 78 的 API 有差异再退到安卓的交叉编译流程（已有脚本，加一个 Vita 目标）。
- **现代页面默认开还是关。** RmlUi 在 444 MHz 上的布局开销未测；页面不脏时每帧只重绘，推断可以接受。V2 测后定。
- **分辨率档位。** 1× 原生还是 2× 内部分辨率。
- **桌面 `gl` 后端是否长期保留。** 本页按“只做开发对照、不进发布包”写；若将来要支持更老的 PC，可以另议。

## 不在本计划内

Vita 版的 HD 美术、滤镜、插帧、录像、MOD；Nintendo 3DS/Switch 等其他主机（3DS 比 Vita 更弱，Switch 有 Vulkan 可走 plume，是另一个计划）；把 Vita 的 GL 路径合并进桌面发布包。
