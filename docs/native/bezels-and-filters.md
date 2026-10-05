# 框体与滤镜（RetroArch 兼容）

日期：2026-10-05。设置「通用」页的三行：**框体**、**滤镜**、**滤镜行数**。用户定：框体只在画面比例设成 4:3 时才有（宽屏照旧填满）；滤镜要兼容 RetroArch 的全部 slang 预设，所以用 librashader 跑，不自己写着色器。

## 1. 用法

- **滤镜**：「选择…」打开一个逐级浏览的列表，起点是「我的滤镜文件夹」（用户目录下的 `shaders/`）和 RetroArch 自带的 `shaders/shaders_slang`（装了 RetroArch 才有，按平台找默认位置，见 `presentation_settings.cpp` 的 `retroarch_folders`）。选一个 `.slangp` 即生效，编译时设置里显示「正在编译滤镜……」，编译不过显示原因，画面保持原样。
- **滤镜行数**：预设读到的画面高度。默认「240 行」＝原版的行数，CRT 类最像；480／960 行更清晰、扫描线更细；「窗口」是窗口自己的像素。
- **框体**：同样的浏览列表，起点是用户目录的 `bezels/` 和 RetroArch 的 `overlays/`。可以选 `.png`，也可以选 RetroArch 的 overlay `.cfg`（取 `overlay0_overlay` 指的图）。
- 三项随 `presentation.json` 保存（`bezel`、`filter` 为绝对路径，`filter_scale` 为 0–4）。

## 2. 滤镜怎么接

- **librashader**（v0.12.0，MPL 2.0）：运行时 `dlopen`，没有它时宿主照常运行、设置里写「这个版本还不能用滤镜」。开发构建用 `tools/recomp/toolchain/fetch_librashader.py` 取官方发布包（核对 SHA-256），CMake 把 `librashader.dylib` 拷到宿主旁边；发布版由 `build_release.py` 放进 `.app` 的 `Contents/MacOS`，许可证进 `Resources/licenses`，「关于」页注明。
- **只有 Metal 已接**：macOS 发布包只带 Metal 与 OpenGL 运行时。Vulkan（Linux、Steam Deck、安卓）与 D3D12（Windows）要另接 `libra_vk_*`／`libra_d3d12_*` 并自己编译库（官方只发 macOS 和 Windows 的二进制），还没做。
- **位置**（`graphics.cpp` 的 `capture_frame` → `post_filter::apply`）：RT64 把游戏画面（含我们的 HD 图层）画进交换链之后、我们画对白之后、画界面之前。所以滤镜作用于游戏画面和对白；设置、原生页面、提示、框体都在它上面，保持清晰。
- **每帧**：把画面矩形从交换链拷进一张纹理 → 用我们自己的直通预设（一个线性加 mipmap 的 pass，写在运行目录的 `filter-passthrough/`）缩到「行数」指定的高度 → 玩家的预设从这张小图画回同一个矩形。行数选「窗口」时跳过缩小。
- **编译**放在后台线程（`libra_mtl_filter_chain_create` 用同一个命令队列），编好了下一帧换上；换下的旧链等 4 帧再释放，免得 GPU 还在用。

## 3. 框体怎么画

- 画面位置不动：找出图里透明的窗口（从图中心向四周找 alpha < 128 的范围，`bezel.hpp` 的 `find_hole`），把整张图拉伸到窗口正好盖住 4:3 画面，超出屏幕的部分裁掉（`place`）。这样所有贴着画面画的东西（对白、原生页面、HD 图层）都不用改坐标。窗口不是 4:3 的框体（例如 RetroArch 的 `tv-integer`，窗口约 1.14:1）会被横向略拉宽。
- 中心不透明的图当作没有窗口，整张铺满屏幕。
- 画法：一个放在所有界面文档最底下的 RmlUi 文档（`frontend.cpp` 的 `bezel_sync`），每帧 `PushToBack`；在对白之上、其他界面之下。

## 4. 验证

- 单元：`make recomp-bezel-test`（overlay `.cfg` 解析、窗口查找、拉伸位置）。
- 实机（2026-10-05，`tools/recomp/debug/check_filter.py`，运行 `build/recomp/debug/20261005T083758.818304Z`，12 项全过）：标题画面上 crt-lottes 按 240 行、多 pass 的 crt-guest-advanced 按 480 行都能编译和显示；坏预设报错且画面不变；关掉恢复原样；4:3 下 RetroArch 自带的 `snes-lttp.cfg` 框体窗口正好对准画面；在设置里经浏览列表选 `crt/crt-easymode` 与 `borders/snes-lttp.cfg`，两者同时生效。
- 没做：发布包（`package_macos.py` 带上 librashader）还没实际打一次；Vulkan／D3D12；预设参数调节（RetroArch 的 shader parameters）；动画框体。
