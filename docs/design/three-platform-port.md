# 三平台移植计划：Windows / Linux / macOS

2026-09-24。接续 [P0 发布计划](cross-platform-release-plan.md) 与 [P1 原生导入](native-rom-importer.md)。
本页取代 P0 中 P2–P4 的顺序与门槛；P0 的目标、玩家流程、授权边界与“不删功能来宣布完成”的约束仍然有效。
结论来自当天对宿主 C++、构建工具链与上游 RT64/plume/N64ModernRuntime 的静态审计，**没有在 Windows/Linux 上实际构建或运行过**。

## 目标平台

| 平台 | 图形后端 | 编译器 | 首批包 | 验收机器 |
| --- | --- | --- | --- | --- |
| macOS arm64 | Metal（经 plume） | Apple clang | `.app` zip | 当前开发机 |
| Linux x64（含 Steam Deck） | Vulkan（SDL Vulkan 窗口） | clang | tar.gz，glibc 2.35 基线 | Linux x64 机器或 Deck |
| Windows x64 | D3D12 默认、失败回退 Vulkan | clang-cl + Ninja | zip | Windows x64 机器 |

- **后端选择。** 沿用 RT64 的 `GraphicsAPI::Automatic`，不自己写选择逻辑。它的规则是：Windows 用 D3D12，在 Wine 下或遇到已知坏驱动时改用 Vulkan；Apple 用 Metal；其他平台用 Vulkan（`rt64_user_configuration.cpp:142`、`rt64_application.cpp:133-289`）。
- **自绘 GPU 代码。** 全部走 plume 的通用接口（`RenderDevice`/`RenderCommandList`）。着色器用 HLSL 写，构建时预编译成 SPIR-V、DXIL（仅 Windows）和 MSL，嵌进程序，做法照 `cmake/PixelCompositor.cmake`。不再在运行时编译 MSL 源码。
- **macOS 也换到同一套实现。** 不保留两套 HD 图层代码。
- **编译器。** clang-cl 与 RT64、Zelda64Recomp 的 Windows CI 一致；重编译生成的 C 在 `recomp.h` 里已兼容 MSVC、clang 和 gcc。

## 现状

**已经可移植：**

- **上游：** RT64 可编 D3D12、Vulkan、Metal 三个后端。N64ModernRuntime 在 Windows/Linux 可用。
- **重编译输出：** 生成的 `funcs_*.c` 与平台无关，不含主机路径，也没有 OS 条件。
- **对白与界面：** 对白走 `PixelCompositor`，文字用 FreeType/HarfBuzz/ICU。RmlUi 界面已经有 SPIR-V/DXIL/MSL 三个分支。
- **输入与音频：** SDL 手柄和键盘输入（`graphics.cpp:520-620`），SDL 音频。
- **应用层：** `src/native/app` 已分出 Windows 和 POSIX 两路，覆盖文件锁、原子替换、用户目录与可执行文件路径。
- **钩子：** RT64 的 native-mesh 钩子和 present 钩子与后端无关。本仓库对 RT64 打的钩子补丁只用 plume 类型。

**阻塞项，按工作量排：**

1. **五个 HD 图层直接调用 Metal。** 涉及 `native_marker/map/portrait/background/sprite.cpp`：约 250 行 MSL、约 380 行 Metal API。它们直接取 `plume::Metal*` 底层对象，自己结束 RT64 的编码器、另开渲染通道，用 `setVertexBytes` 塞常量（最大 4 KiB），用 `replaceRegion` 上传纹理，靠 Metal 的完成回调收尾。其余约 60% 是 RDRAM 识别和 JSON 等逻辑，可以直接复用。
2. **plume 的三处缺口：**
   - Vulkan 和 Metal 上没有纹理→缓冲拷贝，截图读回做不了。
   - Vulkan 交换链图像没有 `TRANSFER_SRC` 标志，不能作为拷贝源。
   - 没有 GPU 完成回调：`graphics.cpp:94,161` 与 `dialogue_plume.cpp:51,59` 的 `addCompletedHandler` 没有对应接口。
3. **编译器与 CMake：**
   - 非 Apple 平台直接 `FATAL_ERROR`（`src/host/CMakeLists.txt:59`）。
   - 无条件使用 `-framework`、`-fblocks`、OBJCXX；`-include stdlib.h` 在 clang-cl 下会编译失败。
   - `flockfile`（`script_trace.hpp:49`）经 `state_probe.hpp` 几乎被所有宿主源文件包含，会直接挡住 Windows 编译。
   - `path::c_str()` 在 Windows 是 `wchar_t`，大约 12 处传给 `stbi_*` 或 `%s`。
   - 缺 `/utf-8` 和 `M_PI` 定义。
4. **Python 工具链：**
   - `venv/bin/python` 路径和无 `.exe` 的可执行文件名写死。
   - `bootstrap.py:55` 和 `run_host_probe.py:206` 强制 clang + Ninja。
   - 约 264 处 `read_text`/`write_text` 没有指定编码。其中 CMake 配置期调用的 `build_import_spec.py` 在 Windows 本地编码下会把界面文案写成乱码。
   - `fcntl` 在模块顶层导入。
5. **调试接口：** 用的是 AF_UNIX 套接字（`debug_server.cpp:297`、`session.py:48`）。迷你关卡编译用 POSIX 引号的 `std::system`（`mini_stage.hpp:274`）。MCP 的 stdio 没有固定 UTF-8。
6. **窗口与平台界面：**
   - 窗口：`SDL_WINDOW_METAL` + Cocoa 句柄（`graphics.cpp:466-480`）。
   - ROM 选择与错误框用 AppKit（`macos/desktop_macos.mm`）。菜单栏 `app_menu.mm` 在其他平台已有空实现和 RmlUi 替代路径。
7. **依赖与构建环境：**
   - Linux 预编译的 `dxc-linux` 要求 glibc 2.34。
   - Debian 11 / Steam Runtime sniper 的 ICU（67）和 HarfBuzz（2.7.4）低于要求（≥70、≥2.8）。
   - Windows 上 RT64 自带的 SDL2 2.26.3 和宿主 `find_package(SDL2)` 会冲突。

## 阶段

### X0 可移植基础（在 Mac 上做，行为不变）

不需要新机器。目标是让代码在三个平台都能进入编译，但 Mac 上的行为完全不变。

- **CMake：**
  - 用 `SRW64_ENABLE_RT64` 代替“Metal 宿主”这个说法。
  - Apple 专属源文件和链接选项都收进 `if(APPLE)`，`-fblocks` 只给仍然引用 Metal 头文件的文件。
  - `-include` 按编译器分别写成 `-include` 或 `/FI`；Windows 加 `/utf-8`、`NOMINMAX`、`_USE_MATH_DEFINES`。
  - x64 上给 RSP 向量代码加 `-msse4.1`。
  - 增加 `CMakePresets.json`，分 `macos-arm64`、`linux-x64`、`windows-x64-clangcl` 三套。编译器不再由脚本写死。
- **源码：**
  - `flockfile` 换成 `std::mutex`，或在 Windows 上对应 `_lock_file`。
  - 路径统一改用 `path.u8string()`，再经一个小工具函数交给 `stbi_*`。
  - 迷你关卡编译改为以 argv 列表直接启动子进程。
- **调试接口：** 改成本机回环 TCP：端口临时分配，令牌写进现有的 `debug.json`。`srw64ctl`、MCP 和测试一并调整。三个平台的实机验收都要靠它驱动，所以放在第一批。
- **Python：**
  - Makefile 和 CMake 的 `execute_process` 设置 `PYTHONUTF8=1`，工具函数逐步补上 `encoding="utf-8"`。
  - `fcntl` 换成兼容层（Windows 用 `msvcrt.locking`）。
  - venv 路径和 `.exe` 后缀交给一个共用的辅助函数处理。
  - `generate_cpu.py` 写文件时固定 `newline="\n"`，保证生成物的摘要跨平台一致。
- **验收：**
  - Mac 上 `make`、`make recomp-native-check` 和现有检查脚本全部通过。
  - 在 Linux 容器里至少能编译纯 CPU 的 `srw64-host` 和根 CMake 测试。本机装有 Docker，但当前没有启动。

### X1 图形层改走 plume（在 Mac 上完成并验收）

**2026-09-25 进展：**

- **辅助层：** `src/host/native_gpu.{hpp,cpp}`；着色器是 `src/host/shaders/` 下的 HLSL，由 `cmake/NativeGpu.cmake` 编成 SPIR-V、MSL（经 RT64 的转换工具，`flip_vert_y` 抵消 `-fvk-invert-y`）和 DXIL。
- **每次绘制的数据：** 写进一个共享的 `StructuredBuffer<float4>` 环形缓冲，下标经 push constant 传给着色器。RT64 每个 workload 提交后都等 GPU 完成（`rt64_workload_queue.cpp:843-844`），所以复用环形槽位是安全的；这样也避开了 Vulkan 128 字节 push constant 的下限。
- **RT64 补丁：** `NativeMeshDraw` 带上场景目标的颜色格式、深度格式和采样数（`native_model_hook_patches.py`）。
- **已移植：**
  - 场间背景 `native_background.cpp`：Mac 上前后对比，同一存档、HD 模式的场间画面，两张截图最大差 1 级（约 2% 像素），肉眼无别。
  - 战术地图 `native_map.cpp`：还没实机对比，它只在设了 `SRW64_HD_MAPS` 时启用；新增 `"alpha": true` 的半透明素材（预乘，供窗框用）。
- **待移植：** 标记（`native_marker.cpp`）、精灵（`native_sprite.cpp`）、头像（`native_portrait.cpp`）。
- **前后对比方法：** `srw64ctl launch --binary PATH`（即 `run_host_probe.py --binary`）运行指定的程序。移植前的程序由一个 worktree 构建：当前源码加上 HEAD 版的待移植层。

这是最大的一块，而且全程可以在 Mac 上验证：plume 的 Metal 后端就是现在的底层。

- **共用辅助层**（暂名 `native_gpu`）：
  - 管线缓存的键：颜色格式、深度格式、采样数、深度模式、混合模式。plume 把深度和混合状态烘进管线，所以都要进键。
  - 按帧轮转的常量缓冲，代替 `set*Bytes`。192 B 的 uniforms 超过 Vulkan 保证的 128 B push constant 下限；2–4 KiB 的调色板和四边形数据本来就要走缓冲。
  - 纹理上传走暂存缓冲 + `copyTextureRegion` + 屏障；mipmap 在 CPU 上生成。
  - 按 fence 延迟释放资源，替代现在“闲置 20 秒后释放”的假设。
- **移植顺序**（从简单到复杂，每步单独验收）：map → background → portrait → sprite → marker。marker 有五条管线、四种深度状态、4 MiB 航迹缓冲。
- **绘制位置：** 直接在 RT64 已绑定的帧缓冲里画，不再结束编码器、另开渲染通道。钩子返回后，RT64 会重绑管线布局、描述符集和视口（`rt64_framebuffer_renderer.cpp:477-488`），但不会重设帧缓冲，所以钩子不能换掉它。
- **`graphics.cpp`：**
  - 姓名页遮挡改成 `setFramebuffer` + `clearColor`。
  - 截图读回和帧缓冲格式查询改走 plume。
  - 两处 `GraphicsAPI::Metal` 改成 `Automatic`。
- **对白：** `dialogue_plume.cpp` 移出 `macos/`，`metal_*` 入口改名，着色器格式取自 `getCapabilities().shaderFormat`。
- **RT64/plume 补丁**（沿用 `prepare_rt64.py` 的白名单字符串替换，不 fork）：
  - Vulkan 和 Metal 补上纹理→缓冲拷贝。
  - Vulkan 交换链图像加 `TRANSFER_SRC`。
  - `rt64_present_queue.cpp` 在 `presentGraphicsWorker->wait()` 之后调一个“呈现完成”钩子，带上 workload id，取代 Metal 完成回调。它仍须满足 P0 的约束：界面只对应正在呈现的 workload，不能读“最新 RDRAM”覆盖旧帧。
- **验收：**
  - 用 [舰船模型](../native/native-ship-model.md) 里的定点截图法（确定时序的迷你关卡，每 50 VI 截一张），对比移植前后 HD 画面，逐个图层核对。
  - 姓名页遮挡无闪烁；截图和调试接口的 screenshot 正常；F6/F7 与缩放正常。
  - 可选：本机已装 Homebrew 的 MoltenVK 1.4.1。如果 RT64 的 Vulkan 后端能在 Mac 上跑起来，就能在拿到 Linux 机器之前先把 SPIR-V 路径测一遍。**这一点未验证。**

### X2 Linux x64 与 Steam Deck

2026-09-25 调整顺序：用户要先在 Steam Deck 上玩，所以 X2 先于 X1 开始，分两步：

- **D1 原版画面版**（已写好代码，构建与说明见 [Linux 构建](../guide/linux-build.md)）：
  - 五个 HD 图层在非 Apple 平台上换成空实现（`native_layers_stub.cpp`），交给 RT64 按原版显示列表绘制。
  - 窗口、后端选择、GPU 完成通知（RT64 补丁 `RenderHookPresented`）、姓名页遮挡、对白合成都已与后端无关。
  - 调试截图在 Vulkan 上暂时返回错误。
  - 共享界面加了手柄→按键桥接：除战斗页以外的原生页面原来只认键盘，现在手柄按下会转成同一套键（`frontend.cpp` 的 `pad_keys`），Deck 只用手柄也能操作场间、标题、存档和姓名页。
- **D2 HD 版**：X1 完成后去掉空实现，Linux 与 Mac 共用 plume 实现。

以下是 X2 的完整清单：

- **窗口：** `SDL_WINDOW_VULKAN`，把 `SDL_Window*` 交给 RT64/ultramodern；`RT64_SDL_WINDOW_VULKAN` 已经打开。
- **依赖：**
  - 把 `config/recomp/macos-dependencies.json` 泛化成按平台分组的依赖锁，沿用同一批 SDL、FreeType、HarfBuzz、ICU 源码包和 SHA-256。
  - Linux 上静态链接，或放在程序旁用 `$ORIGIN` rpath 加载。不依赖系统或 Steam Runtime 自带的 ICU。
- **构建环境：** 用 Ubuntu 22.04 x64 容器（glibc 2.35），满足预编译 `dxc-linux` 的要求。CMake 和 Python 3.11 单独固定版本。
  - SteamOS 的 glibc 比 2.35 新，打出的包以“非 Steam 游戏”方式直接运行即可。
  - Steam Runtime sniper（glibc 2.31）留到以后：需要从源码编 DXC，或者从别处喂入预编译的着色器。
- **Steam Deck：**
  - 全屏 1280×800。
  - 所有 RmlUi 页面都能只用手柄操作。
  - 游戏模式下没有桌面对话框，所以 ROM 选择和错误提示改成 RmlUi 页面，与“界面只用 RmlUi”的方针一致。建议 macOS 也改用这个页面，然后删掉 `desktop_macos.mm`。
  - 字体用随包的 HarmonyOS；`frontend.cpp:1449` 的后备字体列表补上 Arch 的 `noto-cjk/` 路径。
- **验收：** Linux 桌面和 Deck 游戏模式各跑一遍，Deck 上只用手柄：
  - 开场 → 姓名 → 第一话 → 存档后冷启动；
  - 窗口缩放、F6/F7、对白遮挡与闪烁、安全退出；
  - 通过前保持平台闸门，不宣称支持。

### X3 Windows x64（2026-09-25 细化）

Windows 与 Linux 共用 X2 已做的与后端无关部分：窗口句柄、后端交给 RT64 自动选择、GPU 完成通知、对白合成。`graphics.cpp` 里的 `_WIN32` 窗口分支已经写好，但还没在 Windows 上编译过。剩下的主要是编译器、系统服务与打包。

**构建环境**

- Windows 10 22H2 或 11，x64。
- 工具：VS 2022 Build Tools 加 “C++ Clang tools for Windows”（clang-cl）、CMake ≥ 3.20、Ninja、python.org 的 Python 3.11。Git 设 `core.autocrlf=false` 和 `core.longpaths=true`，保证上游摘要检查逐字节一致。
- 在 “x64 Native Tools” 命令行里运行 `tools/release/build_windows.py`（待写，结构照 `build_linux.py`：依赖 → 宿主 → 打包 → 链接检查）。
- 与平台无关的输入从 Mac 拷过去，和 Linux 一样：`build/recomp/cpu-bound/`、`build/recomp/audio-probe/audio.cpp`、`build/fonts/`、打过补丁的 `build/recomp/upstream/`。
- **不在 Mac 上交叉编译。** RT64 构建时要运行 `dxc.exe` 产出 DXIL。Parallels 的 Windows 11 ARM 虚拟机能靠 x64 仿真做编译检查，但没有 D3D12，不能做实机验收。

**代码改动**（按阻塞程度排）

1. **CMake：**
   - 放开 WIN32 闸门。
   - clang-cl 下：`-include stdlib.h` 改为 `/FIstdlib.h`，`-march=x86-64-v2` 改为 `/clang:-march=x86-64-v2`。
   - 加 `/utf-8`、`NOMINMAX`、`_USE_MATH_DEFINES`。
   - release 链接用 `/SUBSYSTEM:WINDOWS` 和 `/OPT:NOICF`，与 Zelda64Recomp 相同。
2. **`flockfile`/`funlockfile`**（`script_trace.hpp:49,53`、`script_move_probe.hpp:34`）换成可移植封装，Windows 上对应 `_lock_file`。它经 `state_probe.hpp` 进入几乎所有宿主源文件，是第一个编译错误。
3. **宽字符路径：** `path::c_str()` 在 Windows 是 `wchar_t`。涉及 `graphics.cpp` 的截图写 PNG、五个 `native_*.cpp` 和 `frontend.cpp:270` 的 `stbi_load`、`debug_server.cpp`，统一改走一个 `u8string()` 辅助函数。
4. **SDL：**
   - RT64 在 WIN32 上链接自带的 SDL2 2.26.3（RT64 `CMakeLists.txt:294-298`），宿主用的是 `find_package(SDL2)`，两者会冲突。
   - 用 `prepare_rt64.py` 补丁让 RT64 在 WIN32 也走 `find_package`，三个平台统一用锁定的 SDL3 + sdl2-compat。
5. **依赖：**
   - SDL3、sdl2-compat、FreeType、HarfBuzz 用同一批源码包，走 CMake 构建。
   - ICU 用源码包自带的 MSBuild 工程（`source/allinone/allinone.sln`）。
   - 不引入 vcpkg，避免出现第二个版本来源。
6. **启动：**
   - 无参数启动时，先在用户目录（`%LOCALAPPDATA%\SRW64Recomp`）和程序旁找 `rom.z64`；找不到就用 `SDL_ShowSimpleMessageBox` 说明。
   - 以后换成 RmlUi 选择页，与 Deck 游戏模式共用。
   - Linux 现在由 `srw64.sh` 做同样的事，届时一并收回 C++。
7. **Unicode 路径：** 加应用清单 `activeCodePage=UTF-8`（Windows 10 1903 起支持）。`launch.cpp` 经环境变量和 argv 传的路径遇到非 ASCII 用户名（如 `C:\Users\太郎`）就不会失真。
8. **调试接口：** AF_UNIX（`debug_server.cpp`、`tools/recomp/debug/session.py:48`）改为回环 TCP + 令牌，这是 X0 的一项。原因是 CPython 在 Windows 上没有 `socket.AF_UNIX`，而 Windows 的实机验收要靠调试接口驱动。
9. **迷你关卡：** `mini_stage.hpp:274` 的 `std::system` 用了 POSIX 引号，改为直接启动子进程。这是开发功能，可以先在 Windows 上关掉。
10. **截图：** plume 的 D3D12 后端已经支持纹理→缓冲拷贝（`plume_d3d12.cpp:2302`），所以 Windows 的 D3D12 可以比 Vulkan 先恢复调试截图。交换链是否带 COPY_SOURCE 用法还要核对。

**打包：** `package_windows.py` 打 zip，内容如下：

- 程序：exe，以及 `SDL3.dll`、`SDL2.dll`、`freetype.dll`、`harfbuzz.dll`、ICU 的 DLL、`dxcompiler.dll`、`dxil.dll`；
- 资源：`fonts/`、`dialogue/`、`licenses/`。

用 `llvm-readobj --coff-imports` 检查导入表，只允许系统 DLL（kernel32、user32、d3d12、dxgi 等）。Vulkan 经 volk 动态加载，不会出现在导入表里。

**验收：**

- 与 X2 同一套流程；
- D3D12 和 Vulkan 各跑一遍（加一个只供测试的 `SRW64_GRAPHICS_API=d3d12|vulkan` 开关）；
- 非 ASCII 用户目录；
- 150% 显示缩放。

**顺序：**

- W1 编译通过：第 1–5 项；
- W2 能进游戏：第 6、7 项；
- W3 调试接口与截图：第 8、10 项；
- W4 打包，干净机器验收。

### X4 发行包与本地验证矩阵

- 在 `package_macos.py` 旁边加上 `package_windows.py`（用 `file(GET_RUNTIME_DEPENDENCIES)` 收集 DLL）和 `package_linux.py`（检查 NEEDED 列表与 glibc 基线）。只按清单收集文件，绝不打包整个 `build/`。
- GitHub Actions 保持关闭。提交 `bb3319d` 删掉的三平台组件测试 workflow 可以改写成本地脚本，作为每台机器的组件检查。
- 每个平台记录两组结果：组件测试和持 ROM 的实机流程，分开记录。
- AppImage/Flatpak、安装器、签名与公证放到首批之后。

## 构建步骤在哪台机器做

| 步骤 | 位置 |
| --- | --- |
| ROM 布局、函数扫描、CPU 代码生成（Python + N64Recomp） | 任意一台机器做一次，一般是 Mac。整个 `build/recomp/cpu-bound/` 连同 `report.json` 按字节原样拷到其他机器（`run_host_probe.py:194` 会校验摘要）。这是 ROM 派生物，不能公开 |
| RSPRecomp 音频输出 | 同上；`run_host_probe.py:204` 要加“跳过重生成”的开关 |
| 上游源码拉取、`prepare_rt64`/`prepare_frontend`/`build_import_spec` | 每台目标机器；`bootstrap.py` 需要一个只拉源码的模式 |
| 着色器编译、C/C++ 编译、依赖、打包 | 每个目标平台；DXIL 只能在 Windows 生成 |

目标机器上不需要编译 N64Recomp、RSPRecomp、n64sym，也不需要 `build/recomp/venv`。

## 待定

- **验收机器：** 是否有 Linux x64 机器（或直接在 Deck 上用 distrobox）和 Windows x64 机器。X0、X1 只要 Mac；X2、X3 没有对应机器就无法验收。
- **Windows 后端：** 本计划默认“D3D12 + Vulkan 回退”。只做 Vulkan 可以省掉 DXIL 构建，但会偏离 RT64 在 Windows 上的默认选择。
- **Linux 基线：** 首批采用 glibc 2.35 tar.gz；是否还要进 Steam Runtime 容器。
- **ROM 选择页：** macOS 是否也换成 RmlUi 页面。

## 不在本计划内

HD 素材包的分发方式（P1 仍然只覆盖 Original 模式）、发行授权边界（见 P0）、IME 候选窗（P3 已记录）。这些与平台无关，另行规划。
