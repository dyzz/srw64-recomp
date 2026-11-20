# Windows 老机器：GL（ANGLE）回退渲染器

状态：**已实现，未发布（2026-11-20）**。Switch / Vita 用的 GL 渲染器（`src/gl`）编进了 Windows 的 `Marchwind64.exe`，底层走 ANGLE；启动时检测显卡，RT64 用不了就在同一个程序里改用 GL 渲染器（见「自动回退」）。发布包仍只有一个 exe，另加 ANGLE 的两个 DLL。CI 的 Windows 任务已编出并打包（`build.yml`）。

## 为什么

RT64 在 Windows 上要 D3D12（功能级别 11_0、SM 6.0、`ID3D12Device8`，Win10 2004 以上），不行再退 Vulkan 1.2，两个都没有就画不出来（`gpu_report.hpp` 的 `SRW64_GPU … rt64=no`）。落在这个缺口里的（推断，未实测）：Intel 4、5 代核显（D3D12 只到 SM 5.1，Windows 上没有 Vulkan）、NVIDIA Fermi、AMD GCN 以前的卡、Win10 2004 以前的系统。这些机器基本都有 D3D11。RT64 上游不收 AI 贡献，老显卡只能绕开 RT64。

## 做法

- **GL 路径原样复用，和 RT64 编进同一个程序。** `src/gl` 解释器、RmlUi 的 GL 后端、对白和宿主标签的 GL 绘制、HD 图层的 GL 版，都是 Switch 已经在用的代码。Windows 的 RT64 宿主定义 `SRW64_GL_RENDERER`（和 Mac 开发用的一样），启动时选一次渲染器（`graphics.cpp` 的 `gl_mode`）。
- **HD 图层运行时切换。** `src/host/renderer_mode.hpp` 的 `renderer_mode::gl()`：GL-only 宿主（Switch、Vita）恒为真，两种都有的宿主按启动时的选择，其余恒为假。头像、背景、图标、精灵、地图五个模块原来用 `SRW64_GL_ONLY` 二选一，现在两套都编进来：两种钩子都装（只有正在用的渲染器会调用），按 `renderer_mode::gl()` 准备对应的图片（GL 用 CPU 上的像素，RT64 用 Plume 纹理）。Switch/Vita 的编译结果不变。
- **模型标记模块没接**（世界地图 3D 模型、5600 标记、战斗场景的烘焙模型与水面）：它的 GL 版和 RT64 版连数据结构都不同，回退时画原版模型，和「回退降画质」一致。
- **ANGLE 延迟加载。** `libGLESv2.dll` 用 `/DELAYLOAD`，正常走 RT64 的电脑根本不加载它；`libEGL.dll` 由 SDL 按需加载。exe 只大了约 0.7 MB（单独的 GL 程序要 24 MB）。
- **底层用 ANGLE，不用系统 OpenGL。** `src/gl/gl_api.hpp` 的 `_WIN32` 分支取 ANGLE 的 GLES3 头文件、定义 `SRW64_GLES`，和 Switch 走同一条 GLES 路线。SDL 通过 `SDL_HINT_OPENGL_ES_DRIVER` 用 EGL 建上下文（`graphics.cpp`），加载游戏旁边的 `libEGL.dll`、`libGLESv2.dll`。理由：Windows 的 `opengl32.dll` 不带加载器只有 GL 1.1；老 Intel 的 OpenGL 驱动问题最多，D3D11 稳得多（浏览器在这类机器上也靠 ANGLE）。
- **ANGLE 的后端。** 默认 D3D11。vcpkg 编的 `libGLESv2.dll` 也带 D3D9 后端（导入了 `d3d9.dll`；D3D9 只给 ES 2.0，本路径只用 ES 2 的子集），但 D3D11 建不起来时会不会自动退到 D3D9 还没验证。环境变量 `ANGLE_DEFAULT_PLATFORM=d3d11|gl` 可以强制选后端，只用于测试。
- **构建：** vcpkg 的 `angle` 端口（chromium_7258），`find_package(unofficial-angle)`；端口的目标不带头文件目录，`CMakeLists.txt` 用 `find_path` 补。`-DSRW64_GL_ONLY=ON` 的 `srw64-gl-host` 在 Windows 上也仍能编（只用来检查 Switch 那条路没被改坏，不发布）。

## 自动回退

`src/host/renderer_fallback.hpp`、`src/host/windows/renderer_fallback_windows.cpp`。只有一个 `Marchwind64.exe`，玩家和 RecompLauncher 的启动方式不变。

1. **启动前检测。** `main` 一开头（还没接管输出流、没开日志、没拿用户目录锁），只对会开游戏窗口的启动方式（无参数、`--choose-rom`、`--play`，不含 `--play --help`／`--prepare`）检测：有没有一块非软件、且不是微软厂商号 0x1414（Basic Render Driver，即 WARP）的显卡能建 D3D12 设备（功能级别 11_0）且 SM ≥ 6.0、有 `ID3D12Device8`；没有的话再看 Vulkan：加载器带 `VK_KHR_surface`／`VK_KHR_win32_surface`，能建 1.2 实例，且有一块非 CPU 的物理设备 `apiVersion` ≥ 1.2。两项都不满足、且 exe 旁有 ANGLE 的两个 DLL，就记下「用 GL」，之后建窗口、界面渲染器、HD 图层都按 GL 走。整个过程在同一个进程里。
2. **运行中途失败。** 检测通过但 RT64 实际初始化失败（`graphics_failure`，例如驱动报的能力和实际不符）：不再弹「显卡不支持」的错误框，而是用同一命令行、带 `SRW64_GRAPHICS_API=gl` 重新启动自己，然后立即退出；新进程拿用户目录锁时 `UserLock` 会等最多 10 秒，等旧进程退干净。这条路径下 RecompLauncher 会以为游戏已结束（它等的是第一个进程），属少见情况，可以接受。
3. **玩家手动选。** 环境变量 `SRW64_GRAPHICS_API`（原来就用来在 D3D12／Vulkan 之间选，launch.cpp 会保留它）：`gl` 直接用 GL 渲染器；`d3d12`、`vulkan` 跳过检测强制用 RT64（失败时仍会改用 GL 重启）。
4. **日志。** 用 GL 时开头写 `SRW64_RENDERER_FALLBACK <原因>`（`chosen (SRW64_GRAPHICS_API=gl)`、`no GPU for RT64 …`、`RT64 failed: …`），进问题报告的 console.log。设置窗口里不支持的三行会灰掉（见下）。
5. **exe 旁没有 ANGLE 的 DLL**（开发构建）时一切照旧。
6. **测试开关：** `SRW64_RENDERER_PROBE=no-d3d12` 让检测把 D3D12 当作不可用，用来在好显卡上走 Vulkan／GL 分支；配合假的 `vulkan-1.dll`（exe 旁放一个不报 surface 扩展的加载器）就能模拟老机器。

实测（同一台 1080 Ti）：

| 用例 | 结果 |
| --- | --- |
| 自动，正常显卡 | 留在 RT64，无回退 |
| `SRW64_GRAPHICS_API=gl` | 同一进程里用 GL 渲染器，HD 精灵、头像、背景、战术地图与图标都正常 |
| `SRW64_RENDERER_PROBE=no-d3d12`＋假 `vulkan-1.dll` | 判定 `no GPU for RT64`，同一进程里用 GL 渲染器 |
| `SRW64_GRAPHICS_API=vulkan`＋假 `vulkan-1.dll` | RT64 `SETUP_FAILED result=3` → 带 `SRW64_GRAPHICS_API=gl` 重启自己，新会话进入游戏，没有错误框 |

## 显卡名单

`src/host/windows/gpu_rules.hpp`：按 PCI 厂商号＋设备号，而不是按驱动报的能力处理的显卡。只会往下降级，玩家仍可用 `SRW64_GRAPHICS_API` 自己选。

- **改用 Vulkan**（RT64 照用，但不走 D3D12）：AMD Vega 核显（Raven Ridge、Picasso/Dali/Pollock、Renoir、Lucienne、Cezanne、Barcelo）。依据 issue #12：5600G 的 Vega 7（驱动 31.0.21925.1001）在 D3D12 下卡死、驱动重置，Vulkan 正常；其他 N64 重编译项目也报过这类核显。启动检测在只剩这类显卡能跑 D3D12 时把 `SRW64_GRAPHICS_API` 设为 `vulkan`（Vulkan 也不行就走 GL）。
- **直接用 GL**（老机器，RT64 官方门槛 Intel HD 510／GeForce GT 630／Radeon HD 7750 以下）：Intel Ironlake 到 Broadwell（含 Bay Trail、Cherry Trail/Braswell）、NVIDIA Tesla 与 Fermi、AMD TeraScale（HD 2000–6000 和 GCN 以前的 APU）。这些显卡按能力检测本来也会被送去 GL；写进名单是防止某个驱动误报能力。设备号范围按代际划分，止于下一代（Intel Gen 9 从 0x1902 起、NVIDIA Kepler、AMD GCN），避免误伤 RT64 能跑的卡。
- 有一块不在名单上、能跑 D3D12 的显卡（比如笔记本里的独显）就照常用 RT64，名单只看「剩下的」显卡。Vulkan 检测同样跳过名单里标为 GL 的设备。
- 显卡报告行末写 `listed=<规则名>(gl|vulkan)`；走名单时日志有 `SRW64_RENDERER_FALLBACK listed GPU …` 或 `Vulkan for …`。
- 测试开关 `SRW64_GPU_RULE_TEST=VVVV:DDDD:gl|vulkan`（十六进制）临时多列一块显卡；在 1080 Ti 上列为 gl → GL 渲染器，列为 vulkan → RT64 走 Vulkan，不列 → D3D12，三种都实测过。
- 以后玩家报告某块显卡在 RT64 下有问题，就按问题报告里的 `SRW64_GPU` 行（厂商号、设备号、驱动）往这里加一条。

## 回退时降画质

只要走回退（Windows 上用 GL 渲染器），

- **内部分辨率**：游戏底图在主机上是原版的 3 倍，Windows 回退默认 2 倍；`GL_RENDERER` 是 CPU 渲染器（Basic Render Driver／WARP、SwiftShader、llvmpipe、softpipe）时 1 倍，日志 `SRW64_GL_QUALITY`。文字、头像、页面等宿主图层仍按窗口分辨率画，不受影响。
- **不重画相同的画面**：Switch 原有的「画面没变就保留上一次交换」在 Windows 也打开，键里加了窗口尺寸。120 Hz 下原来每张画面要整屏重画 4 次。
- **玩家可以改**：设置「通用」页「画面精细度」：自动／1×／2×／3×（只在 GL 渲染器下出现，`RendererFeatures::picture_scale`），存为 `presentation.json` 的 `picture_scale`，重启游戏后生效。主机版同样有这一行（自动＝3×）。开发用环境变量 `SRW64_GL_SCALE`（launch.cpp 保留）优先于两者。

## 无显卡云主机实测（2026-11-20）

AWS ap-southeast-1，m5.xlarge（4 vCPU Xeon 8175M 2.5 GHz），Windows Server 2022，没有显卡，只有「Microsoft Basic Display Adapter」。开机脚本装 OpenSSH、设自动登录；包（不含 HD 111 MB、HD 670 MB）几秒传完。测完实例与临时安全组已删，约 45 分钟。

- **原来的版本在这里会崩。** 第 0 块适配器是 Basic Render Driver，**没有**标成软件适配器，报 D3D12 FL12_1、SM 6.0、`ID3D12Device8` 都有，`gpu_report` 写 `rt64=yes`；RT64 选了它，标题后在 `D3D12Core.dll` 里访问冲突（0xC0000005）。没装显卡驱动的玩家机器很可能一样。现在检测与 `gpu_report`（`rt64=no(warp)`）都按厂商号 0x1414 排除它。
- 排除后自动回退：ANGLE 在 WARP 的 D3D11 上建出 ES 3.0，标题、片头、演示战斗画面都正确。

演示战斗（标题演示，原速 30 帧）：

| 设置 | 游戏逻辑（帧/秒） | 进程 CPU（核） |
| --- | --- | --- |
| 3 倍，每次 VI 都重画（改前） | 25–27 | 约 3.4 |
| 3 倍＋不重画相同画面 | 30 | 约 2.0 |
| 2 倍＋不重画 | 30 | 约 1.25 |
| 1 倍＋不重画（现在的软件渲染默认） | 30 | 约 0.8 |
| 1 倍＋HD 图 | 30 | 约 1.05 |

另：这台机器和 1080 Ti 上用 GL 渲染器时，HD 包装好时「图像」默认都是「原版」（`requested: false`），手动切到 HD 正常；原因还没查。

## 原型实测（Windows 11，GTX 1080 Ti）

| 后端 | 结果 |
| --- | --- |
| ANGLE D3D11（默认） | `OpenGL ES 3.0.0 (ANGLE 2.1.7258) … Direct3D11 vs_5_0 ps_5_0`；版权页、标题、演示战斗、迷你关战术地图都正常；HD 图（精灵、头像、背景、地图、图标）正常；16:10 铺满；呈现跟随 120 Hz；BC7 可用 |
| ANGLE OpenGL（`ANGLE_DEFAULT_PLATFORM=gl`） | 上下文建成（底下是 NVIDIA 的 GLES 3.2），标题动画、设置面板正常 |

两次运行都正常退出（退出码 0），日志里没有 GL 报错。启动中 `dialogue load → host setup` 约 12 秒，RT64 版首次运行也一样，与 GL 无关。

**包体：** 单 exe 只大约 0.7 MB（28.2 → 28.9 MB），另加 `libGLESv2.dll` 5.6 MB、`libEGL.dll` 不到 0.1 MB。（原型阶段曾是单独的 `Marchwind64-gl.exe`，23.9 MB。）

**GL 路径没有的东西：** 60 帧插帧（刷新率设置）、MSAA、RetroArch 滤镜（`gl_only_stubs.cpp` 空实现）。设置窗口按 `settings::renderer_features()` 自动把这三行灰掉，下面写「当前渲染方式不支持此项」（`settings_renderer_unsupported`）；刷新率显示为「原版」、MSAA 显示为「关」，存档里的选择不动，回到 RT64 时照旧生效。GL-only 宿主一开始就没有这三项，带 GL 渲染器的 RT64 宿主（`SRW64_RENDERER=gl`）在建渲染器时改写。Switch 版同样受益（以前这几项显示了但不起作用）。边框是 RmlUi 图层，GL 下照常可用。倍速（含 3、4 倍）由调度器和游戏帧间隔控制，与渲染器无关，保留可用；GL 下没有实测过。

## 还没做 / 没验证

1. **没有显卡的机器（已测）：** 见「无显卡云主机实测」。
2. **真实老显卡。** 老 Intel 核显的驱动问题和性能只有真机测得出来，见下节。
3. **双击启动（无参数）的回退没实测**：那条路径用真实的用户目录，测试机上没跑；代码与 `--play` 走同一个函数。设置窗口里也还没有「渲染方式」选项，目前只能用环境变量手动选。
4. **CI 与打包（已做）：** `build.yml` 的 Windows 任务多装 vcpkg `angle`（首次 16 分钟，已入缓存），RT64 宿主连同 GL 渲染器一起编，包里加 `libEGL.dll`、`libGLESv2.dll`（随 vcpkg 的 bin 一起复制）和 `licenses/ANGLE-LICENSE.txt`，并检查两个 DLL 都在。
5. **发版时要同步：** 系统要求（官网安装页 `web/src/copy/install.ts`、三语 README、FAQ）里 Windows 的显卡门槛要改成「D3D12/Vulkan 走 RT64，否则 D3D11 兼容模式」；发布说明写上无驱动机器的闪退修复。发版前别改官网。
6. **HD 内存：** 桌面 GL 路径直接解 PNG/JPEG（`hd_texture.cpp` 只在 Switch 读预处理的 BC7 包），老核显共享显存小，可能要像 Switch 一样用预处理包或者限预算。

## 测试做法

- **构建：** 照 CI 的 Windows 任务（clang-cl＋vcpkg），另装 `vcpkg install angle:x64-windows`（首次约 7–16 分钟）；RT64 宿主（`-DSRW64_ENABLE_RT64=ON`）在 Windows 上自动带 GL 渲染器，构建后 `libEGL.dll`、`libGLESv2.dll` 复制到 exe 旁边。
- **运行：** `SRW64_GRAPHICS_API=gl` 后 `Marchwind64.exe --play --rom <ROM> --user-dir <目录> --debug`，`ANGLE_DEFAULT_PLATFORM` 选 ANGLE 后端，调试接口不变。
- **真机：** 收一台 Intel 4、5 代核显的 Win10 笔记本，按现有 Windows 测试机的远程流程测。重点看 `SRW64_GPU` 行、`SRW64_GRAPHICS_API` 行（ANGLE 实际用的后端和驱动）、帧率读数和 HD 开关。
