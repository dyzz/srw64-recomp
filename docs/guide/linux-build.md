# Linux 与 Steam Deck 构建

2026-09-25。[三平台移植计划](../design/three-platform-port.md) X2 的第一个版本：Linux x64 上用 Vulkan 运行游戏，**只有原版画面**。HD 图层（整张头像、背景、剧情文字图、3D 标记与舰船）仍然直接调用 Metal，要等 X1 移植到 plume，所以在 Linux 上由 `native_layers_stub.cpp` 关掉，交给 RT64 按原版显示列表绘制。

## 从 Mac 构建

先在 Mac 上照常 `make`，并准备好字体（`tools/content/prepare_fonts.py`）。然后启动 Docker Desktop，在仓库根目录运行：

```sh
tools/release/linux/build.sh --jobs 8
```

脚本做三件事：

1. 在 Mac 上跑 `prepare_rt64.py`，确认 RT64 补丁（含“呈现完成”钩子）已经打上；
2. 构建 `tools/release/linux/Dockerfile` 描述的 Ubuntu 22.04 x64 镜像；
3. 在容器里运行 `tools/release/build_linux.py`。容器把仓库挂在同一个绝对路径，这样生成文件里记录的路径与 Mac 一致。

Apple Silicon 上容器通过 x64 模拟运行，第一次构建（依赖加 RT64 加生成代码）需要较长时间。之后只重编改动的文件。

## 构建步骤与产物

`build_linux.py` 只在 x86-64 Linux 上运行，产物都在 `build/linux-x64/` 下：

| 步骤 | 内容 |
| --- | --- |
| 输入检查 | 生成代码与 `cpu-bound/report.json` 的摘要一致、RT64 已有呈现完成钩子、RSPRecomp 音频源和字体存在 |
| 依赖 `deps/prefix` | 用 macOS 那份锁（`config/recomp/macos-dependencies.json`）里的同一批源码包，编译 SDL3、sdl2-compat、FreeType、HarfBuzz、ICU 的共享库。源码包缓存与 macOS 配方共用 `build/macos-deps/sources` |
| 宿主 `gfx-build` | `src/host` 以 `SRW64_ENABLE_RT64=ON` 构建 `srw64-gfx-host`，编译器 clang，链接器 lld；RT64 的文件对话框走 xdg-desktop-portal（`NFD_PORTAL=ON`），不链接 GTK |
| 打包 `SRW64-<提交>-linux-x64.tar.gz` | 程序 `srw64`、`lib/`（上面五个库，RUNPATH 设为 `$ORIGIN`）、`fonts/`、`dialogue/`、`licenses/`、启动脚本 `srw64.sh`、`README.txt` |

打包时有两项检查，失败即停：

- 程序和随包库依赖的系统库只能是 glibc、libstdc++、libgcc_s、zlib、libdbus；
- 要求的 glibc 符号版本不超过 2.35。

SDL3 运行时才加载 X11/Wayland、PipeWire/PulseAudio/ALSA，Vulkan 由 plume 经 volk 动态加载，所以它们都不会出现在依赖列表里。报告写在 `build/linux-x64/package.json`。

## 安装与运行

见包内的 `README.txt`。概括：

1. 解压到任意目录。
2. 把 ROM 放到 `~/.local/share/srw64-recomp/rom.z64`，或者放在 `srw64.sh` 旁边、命名为 `rom.z64`。
3. 运行 `./srw64.sh`。

`srw64.sh` 先找 ROM，第一次启动时默认简体中文，然后执行 `srw64 --play`。找不到 ROM 时，用 `kdialog`（SteamOS 桌面自带）或 `zenity` 弹出说明，因为 Deck 的游戏模式里看不到终端输出。存档和设置在 `~/.local/share/srw64-recomp`，与 macOS 版的目录结构相同。

在 Steam Deck 上：进入桌面模式 → Steam →“添加非 Steam 游戏”→ 选 `srw64.sh`。之后就能从游戏模式启动。手柄映射见 `graphics.cpp` 的控制器段，README 里有列表。

## 验证记录

**2026-09-25 容器冒烟测试。** 测试方式：

- Ubuntu 22.04 x64 容器，Apple Silicon 上经 Rosetta 运行；
- 显示用 Xvfb，Vulkan 用 Mesa 的软件实现 lavapipe（llvmpipe、Vulkan 1.3）；
- 包解压后通过 `srw64.sh` 启动。

结果：

- 程序加载随包库；
- 首次导入 ROM：51174 条文本、16 张头像；
- `SRW64_GRAPHICS_API 1`（Vulkan）；
- 开场按 60 VI/s 连续运行 5 分钟以上，渲染到 BANPRESTO 标志和版权页，没有崩溃；
- 结束时窗口线程正常处理退出事件，说明界面没有卡在等待 GPU 完成通知上。

有三点这次**没有确认**：

- 没进到标题和原生页面：软件渲染太慢，xdotool 合成的按键是否送达游戏也没确认。
- 手柄桥接没有测。
- 画面有斜向虚线接缝。它出现在 lavapipe 加 MSAA 的软件光栅上，是否在真 GPU 上出现要看 Deck。

以上三点都要在 Steam Deck 实机上确认。

## 与 macOS 的差别

| 项目 | Linux 现状 | 由哪一阶段补上 |
| --- | --- | --- |
| HD 图层 | 关闭，只有原版画面 | X1 |
| 调试接口截图 | 返回错误：plume 的 Vulkan 后端没有纹理→缓冲拷贝，交换链也不能作为拷贝源 | X1 |
| GPU 完成通知 | 由 RT64 呈现队列的 fence 等待之后调用 `RenderHookPresented`（`prepare_rt64.py` 补丁），代替 Metal 的 completion handler | 已完成 |
| 菜单栏 | 没有；设置窗口用 Ctrl+, 打开（`frontend.cpp:1537`）。Deck 只用手柄时暂时打不开，可以在 Steam 输入里把一个背键映射成 Ctrl+, | X2 后续：手柄 Select 键打开设置 |
| ROM 选择 | `srw64.sh` 按固定位置查找 | X2 后续：改为 RmlUi 选择页 |
