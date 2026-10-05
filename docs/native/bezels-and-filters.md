# 框体与滤镜（RetroArch 兼容）

日期：2026-10-05。设置「通用」页的三行：**框体**、**滤镜**、**滤镜行数**。滤镜支持 Metal（macOS）与 Vulkan（Linux、Steam Deck；Mac 上可用 MoltenVK 试），D3D12（Windows）还没有。用户定：框体只在画面比例设成 4:3 时才有（宽屏照旧填满）；滤镜要兼容 RetroArch 的全部 slang 预设，所以用 librashader 跑，不自己写着色器。

## 1. 用法

- **滤镜**：「选择…」打开一个逐级浏览的列表，起点是「我的滤镜文件夹」（用户目录下的 `shaders/`）和 RetroArch 自带的 `shaders/shaders_slang`（装了 RetroArch 才有，按平台找默认位置，见 `presentation_settings.cpp` 的 `retroarch_folders`）。选一个 `.slangp` 即生效，编译时设置里显示「正在编译滤镜……」，编译不过显示原因，画面保持原样。
- **滤镜行数**：预设读到的画面高度。默认「240 行」＝原版的行数，CRT 类最像；480／960 行更清晰、扫描线更细；「窗口」是窗口自己的像素。
- **框体**：同样的浏览列表，起点是用户目录的 `bezels/` 和 RetroArch 的 `overlays/`。可以选 `.png`，也可以选 RetroArch 的 overlay `.cfg`（取 `overlay0_overlay` 指的图）。
- 三项随 `presentation.json` 保存（`bezel`、`filter` 为绝对路径，`filter_scale` 为 0–4）。

## 2. 滤镜怎么接

- **librashader**（v0.12.0，MPL 2.0）：运行时 `dlopen`，没有它时宿主照常运行、设置里写「这个版本还不能用滤镜」。`tools/recomp/toolchain/fetch_librashader.py` 放到 `build/recomp/thirdparty/librashader/<系统>/`：
  - macOS：取官方发布包（核对 SHA-256，只带 Metal 与 OpenGL）。CMake 把 `librashader.dylib` 拷到宿主旁边；发布版由 `build_release.py` 放进 `.app` 的 `Contents/MacOS`，许可证进 `Resources/licenses`。
  - Linux／Steam Deck：官方不发二进制，在 Linux 构建容器里按固定提交（`87e8a97`）用 Cargo 编译（容器里装了 Rust 1.97.1，`tools/release/linux/Dockerfile`），只开 Vulkan 运行时；`build_linux.py` 把 `librashader.so` 放进包的 `lib/`，许可证进 `licenses/`。
  - `--from-source` 在任何机器上从源码编译；在 Mac 上编一个带 Vulkan 的版本，配合 `SRW64_GRAPHICS_API=vulkan`（MoltenVK）与 `SRW64_LIBRASHADER=<库>` 试 Vulkan 这一路（`check_filter.py --vulkan <库>`）。
  - 「关于」页注明 librashader 与许可证。
- **位置**（`graphics.cpp` 的 `capture_frame`）：RT64 把游戏画面（含我们的 HD 图层）画进交换链、我们画完对白之后，界面分两遍画：
  1. 属于游戏画面的页面（标题页的 Library／战斗鉴赏／MOD 字样、场间与战前页面、姓名页等）先画；
  2. 滤镜作用于画面矩形（游戏画面＋对白＋这些页面）；
  3. 其余界面画在上面、保持清晰：设置窗口（含图鉴、战斗鉴赏、MOD 管理）、提示、帧率、框体、触屏按钮、标题页的版本号与设置按钮。
- **怎么分两遍**：每个界面文档开头放一个 `<layer-mark>`（`chrome='1'` 表示第 3 类），它被画到时告诉渲染代理接下来的几何属于哪一层；代理（`frontend.cpp` 的 `LayeredRender`）在每一遍里丢掉另一层的绘制。同一帧的第二遍不能等「上一帧界面还在 GPU 上」（会自己锁死），也不能重置渲染器的顶点缓冲（第一遍的命令还没执行）：`prepare_frontend.py` 给渲染器适配层加了 `start(…, continue_frame)`。没有滤镜时照旧一遍画完。
- **标题页字样跟着画面**：Library／战斗鉴赏／MOD 的位置按画面矩形算（右下角各留 6%），4:3 时在画面里面，不会被框体盖住，也能被滤镜照到。
- **每帧**：把画面矩形从交换链拷进一张纹理 → 用我们自己的直通预设（一个线性加 mipmap 的 pass，写在运行目录的 `filter-passthrough/`）缩到「行数」指定的高度 → 玩家的预设从这张小图画回同一个矩形。行数选「窗口」时跳过缩小。
- **编译**放在后台线程，编好了下一帧换上；换下的旧链等 4 帧再释放，免得 GPU 还在用。Metal 直接用 RT64 的命令队列（Metal 队列可多线程提交）；Vulkan 用延迟创建（`libra_vk_filter_chain_create_deferred`），上传命令录进我们自己的命令缓冲，提交时拿 plume 的队列锁（`VulkanQueue::mutex`），不和 RT64 的提交抢。
- **Vulkan 细节**：交换链图像在 plume 里不带格式，按 RT64 的约定当 B8G8R8A8（安卓 R8G8B8A8）告诉 librashader，否则它建 render pass 时报 `FORMAT_NOT_SUPPORTED`；拷贝和布局切换用 plume 的 `barriers`／`copyTextureRegion`，librashader 画完后让 plume 重新绑定自己的管线和帧缓冲。

## 3. 框体怎么画

- 画面位置不动：找出图里透明的窗口（从图中心向四周找 alpha < 128 的范围，`bezel.hpp` 的 `find_hole`），把整张图拉伸到窗口正好盖住 4:3 画面，超出屏幕的部分裁掉（`place`）。这样所有贴着画面画的东西（对白、原生页面、HD 图层）都不用改坐标。窗口不是 4:3 的框体（例如 RetroArch 的 `tv-integer`，窗口约 1.14:1）会被横向略拉宽。
- 中心不透明的图当作没有窗口，整张铺满屏幕。
- 画法：一个放在所有界面文档最底下的 RmlUi 文档（`frontend.cpp` 的 `bezel_sync`），每帧 `PushToBack`；在对白之上、其他界面之下。

## 4. 验证

- 单元：`make recomp-bezel-test`（overlay `.cfg` 解析、窗口查找、拉伸位置）。
- 实机（2026-10-05，`tools/recomp/debug/check_filter.py`，Metal 运行 `build/recomp/debug/20261005T093403.998838Z`、MoltenVK 上的 Vulkan 运行 `build/recomp/debug/20261005T093742.409287Z`，各 12 项全过；分两遍后标题页的 Library 字样带上扫描线，版本号和设置窗口清晰）：标题画面上 crt-lottes 按 240 行、多 pass 的 crt-guest-advanced 按 480 行都能编译和显示；坏预设报错且画面不变；关掉恢复原样；4:3 下 RetroArch 自带的 `snes-lttp.cfg` 框体窗口正好对准画面；在设置里经浏览列表选 `crt/crt-easymode` 与 `borders/snes-lttp.cfg`，两者同时生效。
- 没做：发布包（macOS 与 Linux）还没实际打一次，Steam Deck 实机还没看；D3D12（Windows）与安卓（要用 NDK 编库）；预设参数调节（RetroArch 的 shader parameters）；动画框体。
