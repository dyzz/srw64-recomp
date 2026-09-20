# SDL/RmlUi 游戏界面

2026-09-20。游戏内界面默认切换为 SDL2 事件、RmlUi 布局与 FreeType 字体、
固定版本 RecompFrontend RT64/Plume 渲染器。主角选择、姓名输入、确认、Link Battler、
设置与提示条都在游戏的 GPU surface 内绘制。默认宿主不再编译对应的 AppKit 页面。

右上角「选项」或 Ctrl/Cmd+, 打开共享设置，Esc 或「返回」关闭；规则、预设、语言、
Original/HD 仍调用原来的接口。F7 统一进入 SDL 事件路径，组字期间交给输入法。
游戏 adapter 继续负责姓名编码、重复检查、写回、联动状态与脚本推进。

## 代码与线程边界

- `src/native/ui/frontend.cpp`：SDL 事件、RmlUi context、共享设置／联动／通知与 debug UI。
- `name_page.*`、`text_input.*`：与独立原型共用的姓名页和组字桥接。
- `presentation_settings.cpp`：原语言请求／完成／按键释放逻辑，保存改用公共 `app::atomic_write`。
- `window_test_control.cpp`：窗口尺寸和关闭的 SDL QA 后端。
- `src/host/graphics.cpp`：连接 RT64 render hook，在 workload 对应的姓名遮挡之后绘制 UI，
  GPU 完成回调解锁资源；截图包含同一 GPU 提交里的 UI，不再做 AppKit 截图叠加。

所有 RmlUi 调用受同一互斥锁保护。窗口线程负责事件、布局和语义动作，渲染线程只录制
绘制命令。下次布局／释放纹理等待上一提交完成；固定 RT64 present queue 本身逐帧等 fence，
满足上游 renderer 的非双缓冲约束。SDL 文本启动、停止和候选框定位延迟到窗口线程执行。
GPU 销毁前先销毁 RmlUi 与 renderer；保持原宿主的线程回收顺序。

UI 只读 `names::Request`、`link_page::Request`、不可变语言目录及设置快照，不读 RDRAM。
姓名遮挡继续按 RT64 workload 查找，晚到的旧 workload 不会因窗口线程切页而重新暴露原版网格。
头像以内部资源名注册，避免 RmlUi URL 规范化改变绝对文件路径；Original/HD 仍使用本地素材。
关闭模态页后的按键释放门控计入 SDL 实际状态和调试虚拟按键。

## 构建与字体

`make host` / `run_host_probe.py --graphics` 自动准备 `config/recomp/frontend.json` 中固定的
RecompFrontend 与 RmlUi；不会覆盖脏上游或错误版本。源码准备、编译需要开发工具，游戏
运行和本地 ROM 导入仍无需 Python。独立姓名页原型保留为 `make recomp-ui-probe`。

共享 UI 使用 FreeType 读取本机字体。开发环境可设置 `SRW64_UI_FONT`，否则依次查找本机
Arial Unicode、微软雅黑或 Noto Sans CJK 的已知文件位置；找不到时明确报错，不显示乱码。
`--play` 入口会清除开发环境变量，因此目前使用默认字体发现。字体不复制、不入库；发行
字体与可配置的正式字体选项尚待落实。CoreText 对话层当前仍使用原字体配置。

## 调试和回归

`ui.tree` 返回 RmlUi 元素的 `id`、文字、可用、焦点和窗口点坐标。
`ui.click` 的 `text` 支持可见文字或稳定 ID，如 `route1`、`field0`、`next`、
`settings-open`、`rule:esp-level`、`locale:en`、`images:hd`、`link:0`。
点击经 RmlUi 命中测试，不直接调用游戏函数。`ui.key` 使用 SDL key 名称，
`ui.type` 使用 `SDL_TEXTINPUT` / `SDL_TEXTEDITING_EXT`；不再依赖 macOS key_code。
截图使用默认游戏窗口，设置不再是独立 OS 窗口。`menu` 保留按本地化设置／规则标题转发的兼容入口，
不会枚举系统菜单。旧 AppKit 姓名／规则控制文件脚本不是共享 UI 的验收入口。

在**新的隔离会话**中执行（会开始新游戏并修改姓名，不要对玩家会话运行）：

```sh
SRW64_DEBUG=1 .venv/bin/python tools/recomp/run/run_host_probe.py \
  --graphics --interactive --diagnostics light \
  --profile config/recomp/profiles/play-profile.json --language zh-Hans \
  --images original --output build/recomp/shared-game-test
# 在另一终端运行：
.venv/bin/python tools/recomp/verify/verify_shared_ui.py --run build/recomp/shared-game-test
```

检查覆盖：真实开场与选角、拒绝不受支持字符、组字期间 Return/F7 隔离、文字提交、
语言切换保留编辑、规则设置、800×600 与 1100×760 缩放、搭档／确认、游戏姓名写回与故事启动。
验证报告和 GPU 截图保留在本地 run 目录，不入库。

## 本地证据（2026-09-20，共享 UI 提交 a8a2de）

- `build/recomp/sdl-game-07/shared-ui-verification.json`：真实开场、选角、组字、语言、设置、
  小窗口、姓名写回、搭档确认与故事开始；按住 W 关闭设置、按住 Return 确认故事的释放门控，
  路线序章跳过成功，确认关闭页面后游戏按键恢复。
- `build/recomp/sdl-game-03/shared-small.png` 等 GPU 回读经过人工图像检查；800×600 与
  1100×760 的输入框、头像和按钮均在界面范围内。
- `build/recomp/sdl-link-01/shared-link-verification.json`：读取既有 SRAM，进入联动页，
  选中 F91 与 Zambot；原游戏 adapter 记录 `selection=5`、`kind=linked`。
  `status.link_page.scheduled` 是打开页面时的快照，不拿它推断提交后的游戏状态。
- 运行经 `window close`／`quit` 退出码 0，日志 `created=4 joined=4 remaining=0`。
  额外 `sdl-shutdown-01` 打开 OS 线程 trace：释放后观察为 0，但释放前记录为
  `UNOBSERVED`，所以该报告的 `shutdown_lifecycle_verified=false`；不把正常退出
  或 join 计数当成完整 OS 边界门禁通过。
- `build/recomp/ui-probe-sdl-final`：独立 66 步姓名页脚本通过。
- `make check`：256 项测试中 245 项通过、11 项跳过，另有 compileall 与依赖检查。

以上为该共享 UI 提交在 macOS 上的真实游戏和 GPU 证据，不是后续对白后端拆分的游戏验收；
组字是 SDL 事件注入，未验证 OS 候选窗。提示条已接入共享渲染，但本轮没有触发剧情退款来单独验收提示条。

## P2a：对白 CPU / GPU 边界

对白不再把 UTF 转换、CoreText 排版、CPU 绘制、Metal 纹理上传和呈现写在同一个源文件。
本批保留现有 CoreText 字体、行高、逐字／分页、双框、回看、阅读指示器和 Metal 新纹理生命周期，
先建立可独立验证的后端边界，尚未替换为跨平台排版器。

| 位置 | 当前责任 |
| --- | --- |
| `src/native/text/unicode.hpp` | 严格 UTF-8/UTF-16 转换；不依赖系统 locale，拒绝过长编码及孤立代理项，不做正规化 |
| `src/host/native_dialogue_text.cpp` | 原 `dialogue::utf8/utf16` 接口的可移植兼容层 |
| `src/native/presentation/raster_image.hpp` | 拥有像素的 `Bgra8Surface`：顶向下、紧密行、BGRA8、预乘 alpha；边界检查先于内存分配 |
| `src/host/dialogue_raster.hpp` | CPU 栅格输出接口，输入不可变 Frame，输出像素与诊断元数据 |
| `src/host/macos/dialogue_coretext.cpp` | CoreText 排版、字素边界、CoreGraphics 绘制；没有 GPU 状态、文件输出或 live guest 读取 |
| `src/host/macos/dialogue_metal.cpp` | workload 查找、缓存、纹理上传、GPU 合成和诊断文件；不调用文字排版 API |
| `cmake/DialogueCpu.cmake` | 游戏和独立测试共用 CPU 目标；不链接 RT64/SDL/Metal renderer |

CPU 栅格化自行作用域绑定 Frame 的语言目录；全局语言改变不能影响已经排队的旧帧。
纹理仍使用 BGRA8 UNORM 与 ONE / ONE_MINUS_SRC_ALPHA，不能在迁移时悄悄变为非预乘或增加
sRGB 纹理转换。当前 `metal_*` 调用接口保留，窗口 surface/readback/completion 不在本批改动范围。

UTF 转换现在保留字符串内的 NUL，而不是在 `c_str()` 返回时截断；这不是放宽 ROM 文本或姓名校验。
字素分段、禁则与字体度量仍由 CoreText 负责，不把“Unicode 转换可移植”当成排版已跨平台。
排版器新增无效尺寸检查，并在极大高度转换为整数前限制页容量。

### 独立测试

不需要 ROM、SDL 或 GPU 的公共字符／像素测试：

```sh
cmake -S tests/dialogue_cpu -B build/dialogue-cpu -DCMAKE_BUILD_TYPE=Release
cmake --build build/dialogue-cpu --config Release
ctest --test-dir build/dialogue-cpu -C Release --output-on-failure
```

`.github/workflows/dialogue-cpu.yml` 在 Windows/Linux/macOS 跑上述转换与像素接口；macOS 额外：
编译真实 CoreText CPU 后端，运行原 `tests/native_dialogue.cpp` 的 Reader/分页/字素回归，运行
`tests/native_dialogue_raster.cpp` 的透明度、通道顺序、缩放、逐字和语言快照测试，并对着固定的
Plume/Metal 头文件编译真实 compositor。Release 显式保留原测试的 assert。

`tests/make_dialogue_reference.py` 只接受 `a8a2de` 对应的原渲染文件 blob 摘要，从其中保留原
CoreText/绘制逻辑，仅替换 GPU 上传和文件输出，生成测试 oracle。CI 比较拆分前后 CPU 像素和
诊断块，覆盖空帧、双框交接、10/13/18 字号、分页、中日文与组合字符、自动阅读、退款回看和
800×600／1100×760。对照源码、生成物和合成帧均不含游戏 ROM；没有分享本机字体。

这些是 CPU 行为／像素对照与 compositor 编译检查，不是实际 GPU 呈现、真实 ROM 或完整游戏验收。
最新运行结果以本批对应提交的 CI 为准，不能借用上节 a8a2de 的游戏报告。

## 尚未完成的跨平台工作

默认游戏页面已经共享；对白的 CPU/GPU 依赖已经分开，但 CoreText 仍是 macOS CPU 后端。
`graphics.cpp` 的 Metal surface/readback/present completion、原生 marker，以及桌面首次 ROM
选择器仍有 macOS 实现；非 Apple 图形构建 gate 保留。下一步可以独立实现跨平台文字后端及
通用 RT64/Plume 像素合成，不需要重做 Reader 或本次已建立的像素接口。

真实中日文 OS 输入法候选窗、手柄导航、可再分发字体与三平台第一话／存档冷启动仍需独立验收。
