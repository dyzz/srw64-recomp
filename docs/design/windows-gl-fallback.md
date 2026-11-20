# Windows 老机器：GL（ANGLE）回退渲染器

状态：**原型（2026-11-20）**。`srw64-gl-host`（Switch / Vita 用的 GL 路径）已能在 Windows 上编译运行，底层走 ANGLE。还没有和正式版 `Marchwind64.exe` 接成自动回退，也没进 CI 和发布包。

## 为什么

RT64 在 Windows 上要 D3D12（功能级别 11_0、SM 6.0、`ID3D12Device8`，Win10 2004 以上），不行再退 Vulkan 1.2，两个都没有就画不出来（`gpu_report.hpp` 的 `SRW64_GPU … rt64=no`）。落在这个缺口里的（推断，未实测）：Intel 4、5 代核显（D3D12 只到 SM 5.1，Windows 上没有 Vulkan）、NVIDIA Fermi、AMD GCN 以前的卡、Win10 2004 以前的系统。这些机器基本都有 D3D11。RT64 上游不收 AI 贡献，老显卡只能绕开 RT64。

## 做法

- **GL 路径原样复用。** `src/gl` 解释器、RmlUi 的 GL 后端、对白和宿主标签的 GL 绘制、HD 图层的 GL 版，都是 Switch 已经在用的代码（`SRW64_GL_ONLY`）。
- **底层用 ANGLE，不用系统 OpenGL。** `src/gl/gl_api.hpp` 的 `_WIN32` 分支取 ANGLE 的 GLES3 头文件、定义 `SRW64_GLES`，和 Switch 走同一条 GLES 路线。SDL 通过 `SDL_HINT_OPENGL_ES_DRIVER` 用 EGL 建上下文（`graphics.cpp`），加载游戏旁边的 `libEGL.dll`、`libGLESv2.dll`。理由：Windows 的 `opengl32.dll` 不带加载器只有 GL 1.1；老 Intel 的 OpenGL 驱动问题最多，D3D11 稳得多（浏览器在这类机器上也靠 ANGLE）。
- **ANGLE 的后端。** 默认 D3D11。vcpkg 编的 `libGLESv2.dll` 也带 D3D9 后端（导入了 `d3d9.dll`；D3D9 只给 ES 2.0，本路径只用 ES 2 的子集），但 D3D11 建不起来时会不会自动退到 D3D9 还没验证。环境变量 `ANGLE_DEFAULT_PLATFORM=d3d11|gl` 可以强制选后端，只用于测试。
- **构建：** vcpkg 的 `angle` 端口（chromium_7258），`find_package(unofficial-angle)`；端口的目标不带头文件目录，`CMakeLists.txt` 用 `find_path` 补。SDL2 用 RT64 自带的那份 2.26.3，两个宿主共用一个 `SDL2.dll`。Windows 平台文件（选 ROM 对话框、菜单栏、崩溃转储、显卡报告、清单、WinHTTP）与 RT64 宿主相同。

## 原型实测（Windows 11，GTX 1080 Ti）

| 后端 | 结果 |
| --- | --- |
| ANGLE D3D11（默认） | `OpenGL ES 3.0.0 (ANGLE 2.1.7258) … Direct3D11 vs_5_0 ps_5_0`；版权页、标题、演示战斗、迷你关战术地图都正常；HD 图（精灵、头像、背景、地图、图标）正常；16:10 铺满；呈现跟随 120 Hz；BC7 可用 |
| ANGLE OpenGL（`ANGLE_DEFAULT_PLATFORM=gl`） | 上下文建成（底下是 NVIDIA 的 GLES 3.2），标题动画、设置面板正常 |

两次运行都正常退出（退出码 0），日志里没有 GL 报错。启动中 `dialogue load → host setup` 约 12 秒，RT64 版首次运行也一样，与 GL 无关。

**包体：** `Marchwind64-gl.exe` 23.9 MB、`libGLESv2.dll` 5.6 MB、`libEGL.dll` 不到 0.1 MB；与 RT64 版并排发约多 30 MB（未压缩）。

**GL 路径没有的东西：** 60 帧插帧（刷新率设置）、MSAA、RetroArch 滤镜（`gl_only_stubs.cpp` 空实现）。设置窗口按 `settings::renderer_features()` 自动把这三行灰掉，下面写「当前渲染方式不支持此项」（`settings_renderer_unsupported`）；刷新率显示为「原版」、MSAA 显示为「关」，存档里的选择不动，回到 RT64 时照旧生效。GL-only 宿主一开始就没有这三项，带 GL 渲染器的 RT64 宿主（`SRW64_RENDERER=gl`）在建渲染器时改写。Switch 版同样受益（以前这几项显示了但不起作用）。边框是 RmlUi 图层，GL 下照常可用。倍速（含 3、4 倍）由调度器和游戏帧间隔控制，与渲染器无关，保留可用；GL 下没有实测过。

## 还没做 / 没验证

1. **没有显卡或只有微软基本显示适配器（WARP）的机器。** ANGLE 只能用 EGL 属性选 WARP，SDL2 建显示时传不进去，环境变量也不支持。打算用不分配显卡的 Hyper-V 虚拟机测（那里 D3D11 硬件设备本身就是 WARP）。
2. **真实老显卡。** 老 Intel 核显的驱动问题和性能只有真机测得出来，见下节。
3. **接成自动回退。** 候选做法：`Marchwind64.exe` 启动前按 `gpu_report` 判断没有一块显卡满足 RT64、也没有 Vulkan surface 时，改为启动同目录的 `Marchwind64-gl.exe`（同参数），设置里再加手动选项。要处理父子进程的「已在运行」锁、RecompLauncher 的启动方式和问题报告。另一条路是把 GL 渲染器编进同一个 exe、运行时切换，但 `SRW64_GL_ONLY` 是编译期开关，涉及约十个文件的分支，工作量大得多。
4. **CI 与打包：** `build.yml` 的 Windows 任务要多装 `angle`（vcpkg 缓存会变大）、多编一个宿主（约多几分钟）、包里加 exe 和两个 DLL。
5. **HD 内存：** 桌面 GL 路径直接解 PNG/JPEG（`hd_texture.cpp` 只在 Switch 读预处理的 BC7 包），老核显共享显存小，可能要像 Switch 一样用预处理包或者限预算。

## 测试做法

- **构建：** 照 CI 的 Windows 任务（clang-cl＋vcpkg），另装 `vcpkg install angle:x64-windows`（首次约 7 分钟），用 `-DSRW64_GL_ONLY=ON` 配到单独的构建目录（不能和 RT64 宿主共用）后编 `srw64-gl-host`，宿主约 4 分钟。构建后 `SDL2.dll`、`libEGL.dll`、`libGLESv2.dll` 会复制到 exe 旁边；打包时把 exe 改名为 `Marchwind64-gl.exe` 放进正式包目录。
- **运行：** `Marchwind64-gl.exe --play --rom <ROM> --user-dir <目录> --debug`，`ANGLE_DEFAULT_PLATFORM` 选后端，调试接口与 RT64 版相同。
- **真机：** 收一台 Intel 4、5 代核显的 Win10 笔记本，按现有 Windows 测试机的远程流程测。重点看 `SRW64_GPU` 行、`SRW64_GRAPHICS_API` 行（ANGLE 实际用的后端和驱动）、帧率读数和 HD 开关。
