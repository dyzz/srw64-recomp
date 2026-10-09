# 调试接口与 MCP

日期：2026-09-20。给开发者和 Claude 共用的实机调试入口：启动隔离的调试会话、按键、截图、读状态、操作原生界面、退出，都通过同一个接口完成，不用再靠人工按键或各自为政的控制文件。

## 结构

| 层 | 位置 | 作用 |
| --- | --- | --- |
| 宿主调试服务 | `src/host/debug_server.cpp`、`debug_transport.cpp`，开关 `SRW64_DEBUG=1` | 监听本机回环 TCP（`127.0.0.1`，端口由系统分配），每行一条 JSON-RPC 2.0 请求/回应；端口和令牌写在运行目录的 `debug.json`（见下文「连接方式」）。普通试玩不开启。需要 SDL/RmlUi 的操作排进窗口线程执行。 |
| 游戏键盘层 | `src/host/debug_protocol.hpp`、`graphics.cpp` | 虚拟按键与真实按键走同一条读取路径：绑定到同名扫描码，F6/F7/F8/Esc 按下沿经过同样的姓名页与语言切换门控；姓名页关闭后的释放检查也计入虚拟按键。虚拟按键不需要窗口焦点，游戏可以在后台被驱动。 |
| 原生界面层 | `src/host/debug_ui.hpp`、`src/native/ui/frontend.cpp` | 共享 SDL/RmlUi 页面提供界面树、稳定 ID／文字／坐标点击、输入文字和按键；设置与通知都在游戏 surface 内。 |
| 会话与客户端 | `tools/recomp/debug/session.py` | 启动会话（经 `run_host_probe.py --graphics --interactive`，输出到 `build/recomp/debug/<时间戳>/`，不碰 `profile-play` 的存档和偏好）、连接客户端（`Client(运行目录)`）、等待条件、事件日志增量读取。 |
| 命令行 | `tools/recomp/debug/srw64ctl.py` | 给人用的同一套操作。 |
| MCP 服务器 | `tools/recomp/debug/mcp_server.py`、仓库根 `.mcp.json` | 标准库实现的 stdio MCP（项目环境没有 `mcp` 包），Claude Code 批准项目 MCP 并重开会话后即可调用 `srw64_*` 工具。 |

## 覆盖范围

目标是游戏接收的每一种输入都能经接口发出，并尽量走与玩家相同的代码路径：

| 游戏接收的输入 | 玩家的来源 | 接口 |
| --- | --- | --- |
| 18 个游戏键（14 个 N64 按键对应的键与 WASD 摇杆） | SDL 键盘状态 | `keys`：按旧键盘表（`input::classic_keys`：Z＝A、X＝B、空格＝Z、IJKL＝C…）并入同一次键盘读取，不看玩家的绑定（玩家键盘默认是 PCSX2 布局），不需要窗口焦点 |
| F6 画面、F8 迷你关卡、Esc 退出 | SDL 按键事件 | `keys`：虚拟按下沿经过与真实按键相同的姓名页、语言切换门控 |
| F7 语言 | SDL 键盘事件，组字期间交给输入法 | `keys f7`：进入相同的 SDL 组字／repeat 门控 |
| N64 手柄（绕过键盘层） | 无（诊断用） | `buttons` |
| 主角选择页：四张卡片、←→、Enter／Z | SDL 鼠标与键盘 | `ui.click --text <主角全名>`（高亮后按继续或 Enter）、`ui.key right`／`return`；`status.name_page` 给出 `route` 与四个选项 |
| 场间主菜单：九项（「（前）」场景后两项）、のりかえ 二级菜单 | SDL 鼠标与键盘 | `ui.click --text intermission:N`（或可见文字）确认第 N 项，`intermission-swap:0|1` 选驾驶员／妖精；`ui.key up`／`down`／`return`／`escape`；`status.intermission_page` 给出 `cursor`、`submenu`、`swap_refused`、回合数与资金 |
| 改造画面：机体列表、五项改造、确认窗与消息 | SDL 鼠标与键盘 | `ui.click --text upgrade:N`（当前行确认、其他行移动）、`upgrade-confirm`／`upgrade-cancel`／`upgrade-dismiss`；`ui.key up`／`down`／`left`／`right`／`return`／`escape`；`status.upgrade_page` 给出 `screen`、`rows`、`window`、`funds`；資金直接修改：`ui.click --text upgrade-funds`（主菜单 `intermission-funds`）、`ui.type <数字>`、`ui.key return` |
| 联动页：三张作品卡片、←→、空格／Z、Enter、Esc／X | SDL 鼠标与键盘 | `ui.click --text <作品名>`（每次切换勾选）、`ui.click --text <继续按钮>`、`ui.key right`／`space`／`return`；`status.link_page` 给出 `joined` 与 `scheduled` |
| 选角与确认页：卡片、按钮、←→／Enter／Esc | SDL 鼠标与键盘 | `ui.click`、`ui.key`；`status.name_page` 给出 `person`（3 选角、2 确认）与选角页的四条路线 |
| 战前确认页：双方概率、应对、动画、开始／返回；按键同游戏（Z/Enter、X/Esc、方向键/WASD、Q、E、K）及手柄 | SDL/RmlUi | `ui.click --id battle-confirm`、`battle-weapon`、`battle-counter`、`battle-evade`、`battle-defend`、`battle-spirits`、`battle-animation`、`battle-back`；`status.battle_page` 是游戏线程发布的快照 |
| 运行时加载关卡文件 | 调试接口开着时，标题菜单时把文件拖到窗口 | `mini_stage.load {"path": <镜像或关卡源文件>}`；直接进入，无需启动时指定关卡，见[迷你关卡](../script/mini-stage.md) |
| 主菜单迷你关卡入口 | RmlUi 按钮／F8 | 带 mini stage 启动后 `ui.click --id mini-enter`，或 `keys f8`；等待 `status.mini_stage.ready`。自动完成默认人物初始化，普通新游戏不变 |
| 游戏内「选项」及规则设置 | RmlUi 控件、Ctrl/Cmd+, | `ui.click`、`ui.key`；`menu` 保留本地化规则标题的兼容转发 |
| 场间 強化パーツ 页面：机体列表、槽位／库存、持有者 | RmlUi 页面 | `ui.click --id parts:N`／`parts-slot:N`，或 `keys` 的方向键、Z／X；`status.parts_page`，等待条件 `parts_page`，事件日志 `parts` |
| 场间 ユニット能力／パイロット能力 页面 | RmlUi 页面 | `ui.click --id ability:N`，或 `keys` 的方向键、Z／X、Q／E；`status.ability_page`，等待条件 `ability_page`，事件日志 `ability` |
| 场间 のりかえ 页面：驾驶员／妖精列表、目标列表、确认 | RmlUi 页面 | `ui.click --id swap:N`／`swap-yes`／`swap-no`，或 `keys`；`status.swap_page`，等待条件 `swap_page`，事件日志 `swap` |
| 场间 データセーブ 页面：介质选择、存档栏、覆盖确认、Pak 提示 | RmlUi 页面 | `ui.click --id save:N`／`save-yes`／`save-no`，或 `keys`；`status.save_page`，等待条件 `save_page`，事件日志 `save` |
| 共享设置：规则、预设、语言、画面、画面比例、界面大小、战前确认界面、场间画面、主角选择、标题菜单画面（分五页，按 id 点击会先翻到所在页） | RmlUi 页面 | `ui.click`／`ui.tree`／默认 `screenshot`；或用 `settings` 直接设定（`rules`／`images`／`aspect`（`auto`／`4:3`）／`locale`／`battle_ui`／`ui_size`／`intermission_ui`／`name_entry_ui`／`title_ui`） |
| 游戏窗口：尺寸、前台、关闭按钮 | 窗口管理 | `window`（`width`/`height`、`front`、`close`） |
| 正常退出 | Esc、关窗、⌘Q | `quit`，或 `keys escape`、`window close` |

`tests/test_debug_coverage.py` 静态核对这张表的前提，新增按键或方法时漏接接口会让测试失败：宿主读取的每个 SDL 扫描码都有同名虚拟键；F6/F8/Esc 有虚拟按下沿，F7 有对应虚拟键；`buttons` 的按钮表与输入编译器（`native_inputs.BUTTONS`）一致；MCP 工具描述的键名与宿主一致；每个宿主方法都有 MCP 工具。

## 宿主方法

| 方法 | 参数 | 说明 |
| --- | --- | --- |
| `status` | `history` | VI、运行目录、窗口焦点与尺寸、语言、画面模式、规则、开场状态（`title_major` 3 为主菜单，`step` 为当前页）、对白阅读器（页、字号、速度、自动、回看、跳过、各对白框文字）、姓名页请求、联动页（`link_page`）、场间主菜单（`intermission_page`）、改造画面（`upgrade_page`）、战前确认页（`battle_page`）、迷你关卡状态（`mini_stage.available/entering/active/ready`）、最近的原生提示条（`notices`，如离队退款）、原生窗口与焦点、按住的虚拟键 |
| `keys` | `press`+`hold_ms` / `down` / `up` / `release_all` | 游戏键盘；键名 `z x space return up down left right q e i k j l w a s d escape f6 f7 f8 f5`（F5 重新载入台词文本），组合用 `+`，如 `e+return` |
| `pad` | `press`+`hold_ms` / `down` / `up` / `release_all` | 虚拟手柄，每帧并入真实手柄的状态（手柄提示、页面、L2/R2 等宿主键都当真手柄处理）；键名按 Steam Deck：`a b x y menu view l1 r1 l2 r2 up down left right ls_up… rs_down…`，组合用 `+`。命令行 `srw64ctl.py pad r2 l2:600 wait:300` |
| `buttons` | `buttons`、`vis` | N64 手柄层按键（`a b z start up down left right l r c_up c_down c_left c_right`），立即生效，不经过键盘层 |
| `screenshot` | `path`、`overlays`、`window`、`timeout_ms` | 抓下一次呈现的 GPU 回读，已包含共享 UI。`window` 使用默认游戏窗口；不再提供独立设置窗口或 AppKit 合成。 |
| `ui.tree` | `window` | RmlUi 元素树：tag、`id`、`frame`（窗口点坐标）、文字、可用、焦点；像素 = 点 × `scale`。 |
| `ui.click` | `text` 或 `id` 或 `x`/`y`，`button`、`count` | 稳定 ID 或可见文字匹配后，通过 RmlUi 鼠标命中测试点击；前后台使用相同路径。 |
| `ui.key` | `key`、`modifiers` | SDL 键名：return、tab、escape、delete、方向键、a–z、0–9、f1–f12；不再使用 macOS `key_code`。 |
| `ui.type` | `text`、`marked`、`unmark`、`window` | 向获得焦点的输入框插入文字，等同键入；`marked: true` 留作输入法组字（带下划线，未提交），`unmark: true` 提交组字；回应 `marked` 表示本次是否注入组字 |
| `menu` | `path` | 兼容转发设置和规则的本地化标题；不再枚举 OS 菜单。 |
| `settings` | `rules`（预设名或 ID 列表）、`locale`、`images` | 直接改规则、语言、画面 |
| `window` | `width`/`height`、`front`、`close` | 调整游戏窗口尺寸（640–2560 × 480–1600 点）、带到前台（只在要验证真实焦点或真实鼠标事件时需要）、按下关闭按钮（`SDL_WINDOWEVENT_CLOSE`）；回应窗口状态 |
| `wait_vi` | `vi`、`timeout_ms` | 等到指定 VI |
| `memory.read` | `address`、`size`（≤ 0x10000） | 读客体内存，返回十六进制；不暂停游戏，是调试视图而非快照（[短跳过](../native/script-skip.md)的状态对照用它） |
| `memory.write` | `address`、`hex`（整字节，≤ 4096 字节） | 写客体内存（探针用，比如强制战斗背景；只在调试会话里） |
| `record.start`、`record.stop` | `width`（默认 960） | 录像：开始后每次呈现都读回、缩到 `width`、追加写入运行目录下 `record-<VI>/` 的原始帧和时间，游戏声音按送进输出设备的样子写 `audio.s16`（立体声 s16le）；停止时返回帧数与尺寸，以及 `audio_frames`、`audio_rate` 和 `audio_start`（第一块声音相对开始录像的秒数）。`Session.record(秒数)`（或 `record_start` … `record_stop(path)`，中间可以操作游戏）把画面排成固定 30 帧的时间线（每一刻显示当时最新的一帧，卡住的地方就是定格），按 `audio_start` 对齐混入 AAC 音轨，用 ffmpeg 编成 MP4 并删掉原始文件；MCP 是 `srw64_record`、`srw64_record_start`／`srw64_record_stop`（可给 `path`，目录不存在会建）。调试会话默认静音，要声音得 `srw64_launch(audio=true)`；开机前约 27 秒（logo 与开场）游戏本身没有声音。实测标题画面录 8 秒得 240 帧、最长间隔 40 ms，录像本身几乎不拖慢 |
| `quit` | — | 正常退出，报告记为控制退出 |
| `methods` | — | 列出宿主支持的方法 |

所有游戏页面现在共用一个 SDL 窗口；截图使用默认窗口或 `"game"`。

## 命令行

```sh
.venv/bin/python tools/recomp/debug/srw64ctl.py launch --language zh-Hans   # 构建并启动，打印运行目录
.venv/bin/python tools/recomp/debug/srw64ctl.py wait --title-menu
.venv/bin/python tools/recomp/debug/srw64ctl.py keys return                  # 主菜单确认
.venv/bin/python tools/recomp/debug/srw64ctl.py keys e+return:200            # 跳过序章
.venv/bin/python tools/recomp/debug/srw64ctl.py click --text 继续            # 姓名页按钮
.venv/bin/python tools/recomp/debug/srw64ctl.py type ナナ --marked            # 输入法组字；type --unmark 提交
.venv/bin/python tools/recomp/debug/srw64ctl.py keys i i k e+z:1500          # 字号、快进
.venv/bin/python tools/recomp/debug/srw64ctl.py shot                         # 截图路径与元数据
.venv/bin/python tools/recomp/debug/srw64ctl.py window --size 1280 960
.venv/bin/python tools/recomp/debug/srw64ctl.py events dialogue --kind font
.venv/bin/python tools/recomp/debug/srw64ctl.py quit
```

`keys` 的每一项是一个组合键，可加 `:按住毫秒`；`wait:500` 只是停顿。`launch` 之外的命令默认接入最近一次启动的会话（`build/recomp/debug/current`），也可用 `--run` 指定。`--reuse-build` 在源码未变时跳过重新构建。

会话寿命：脚本里 `Session.launch` 启动的游戏只活到启动它的进程结束——检查跑完、断言失败、超时被杀（含 `kill -9`）都会让 `run_host_probe.py` 发 `quit` 关掉游戏（无 socket 时 SIGTERM），报告记 `ended_with_owner`；构建期间脚本就没了则不再启动游戏。机制是一条只有启动进程持有写端的管道（`SRW64_DEBUG_OWNER_FD`）。`srw64ctl.py launch` 的会话要留给后续命令，不受此限，用完要 `quit`；MCP 启动的会话随 MCP 服务器退出。脚本结束后还想留着窗口给人看，传 `Session.launch(detach=True)`。

## MCP 工具

`srw64_launch`、`srw64_attach`、`srw64_status`、`srw64_keys`、`srw64_buttons`、`srw64_screenshot`（直接返回图片）、`srw64_record`（录一段带声音的 MP4，返回路径）、`srw64_record_start`／`srw64_record_stop`、`srw64_ui_tree`、`srw64_click`、`srw64_type`、`srw64_ui_key`、`srw64_menu`、`srw64_window`、`srw64_settings`、`srw64_mini_stage_load`、`srw64_memory`、`srw64_wait`（`vi`、`dialogue_active`、`intro_active`、`name_page`、`link_page`、`intermission_page`、`battle_page`、`title_major`、`text`、`event`）、`srw64_events`（日志：`dialogue`、`intro`、`name`、`rules`、`images`、`control`、`script`、`mini_stage`、`settings`、`refunds`、`link`、`intermission`、`unit_name`）、`srw64_quit`。工具错误以 `isError` 返回，不会中断服务器。宿主不再定期截图或导出内存（2026-10-01 删掉了「完整诊断」：每两秒左右截一张整窗图、导出 8 MiB 内存，标题火焰会从 30 帧掉到 18）；要画面就用 `srw64_screenshot`，要一段过程就用录像。

## 连接方式

2026-10-06 起三个桌面平台（macOS、Linux、Windows）统一用本机回环 TCP，取代原来的 Unix socket（`debug.sock`）。原因是 Windows 版 CPython 没有 `socket.AF_UNIX`；顺带去掉了 socket 路径的长度上限（macOS 104 字节）。

- 宿主监听 `127.0.0.1:0`，端口由系统分配；Windows 上加 `SO_EXCLUSIVEADDRUSE`。
- 运行目录的 `debug.json`（`srw64.debug-endpoint.v2`）写 `transport: "tcp"`、`host`、`port`、`token`（256 位随机数，十六进制）和 `pid`。文件先建成仅属主可读写再写入令牌，写完改名，客户端不会读到半份；游戏退出时删除。
- 每个连接的第一行必须是 `{"jsonrpc":"2.0","id":0,"method":"auth","params":{"token":"…"}}`，令牌按常量时间比较。错了或缺了就回一条错误并断开；握手前最多缓存 4 KiB。
- 令牌取代了原来 socket 文件 0600 的作用：回环端口本机任何进程都连得上，只有玩家自己读得到 `debug.json`。
- 客户端 `Client(运行目录)` 先读 `debug.json`；没有时找 `debug.tcp`（Android：adb 转发好的本地端口，不带令牌）。`Session.launch`、`Session.attach` 都以「有没有这两个文件」判断会话是否在运行。
- Android 不变：仍是抽象 socket `@srw64-debug`，只有 adb 转发够得着。改成 TCP 加令牌反而不行：发布版 APK 不可调试，adb 读不到应用私有目录里的令牌。
- 新旧不兼容：新版客户端连不上 2026-10-06 之前的游戏，反之亦然。

## 打开方式：选项里的开关

玩家不用命令行：「选项 → 关于 → AI 调试接口（MCP）」（`presentation.json` 的 `debug_interface`，默认关）。窗口线程每帧（`debug::service_main`）比较开关与监听状态：

- 打开：立即监听（每次新端口、新令牌），并贴一条提示（`debug_interface_notice`）；开关存着，以后每次启动都会监听并提示一次。
- 关闭：`transport::stop()` 停止接受连接、断开已有连接、删掉 `debug.json`；排队中的窗口线程请求以错误返回。
- `--debug`（`SRW64_DEBUG=1`）照旧在窗口打开前就监听，不贴提示；开关显示为「开」且不可点，本次运行关不掉。开发会话（`Session.launch`）都走这条。
- 关于页在开着时显示监听地址和运行目录（家目录写成 `~`，过长只显示结尾），「复制运行目录」复制完整路径。设置里的状态经 `settings::set_debug_endpoint` 传给页面。

监听失败（例如写不了 `debug.json`）时日志记 `SRW64_DEBUG_FAILED`，直到开关再次变化前不再重试。

## 不带参数的 attach

`Session.attach()`（MCP 的 `srw64_attach` 不带 `run`）按 `running_games()` 找：`build/recomp/debug/current`（最近一次 `srw64ctl launch` 或 `attach.py`）加上本机玩家用户目录（`player_data()`：macOS `~/Library/Application Support/SRW64Recomp`、Windows `%LOCALAPPDATA%\SRW64Recomp`、Linux `$XDG_DATA_HOME/srw64-recomp`）下所有 `sessions/*/run/debug.json`，按文件时间从新到旧逐个试连（3 秒超时，`methods`），第一个能应答的就是。崩溃留下的 `debug.json` 连不上，自然跳过。

## 另一台机器上的文件

截图、录像的帧和事件日志是宿主写在自己机器上的文件。宿主有两个只限本次运行目录的方法：`file.read {path, offset, size}`（一次最多 4 MiB，base64，回给文件总长和 `eof`）和 `file.remove {path}`（不能删运行目录本身）。`Session.local()` 为假（运行目录里有 `remote.json` 或 `debug.tcp`，即 Deck 或手机）时，`Session.local_file()` 用 `file.read` 分块取回到 `remote-files/`，`Session.record()` 用 `file.remove` 清掉宿主那边的帧目录，`Session.events()` 按 `status.run` 取远端的日志。Deck 不再需要 scp，安卓也能截图和录像。

## 在 Steam Deck（或其他 Linux 机器）上

发布包的 `--play` 会清掉所有 `SRW64_*` 环境变量，`SRW64_DEBUG=1` 在那里不起作用；要打开调试接口就给游戏传 `--debug`。`debug.json` 写在这次会话的运行目录里（`~/.local/share/srw64-recomp/sessions/<id>/run/`）。不开 `--debug` 时与以前完全一样。Windows 上同样传 `--debug`（`Marchwind64.cmd --debug`），运行目录在 `%LOCALAPPDATA%\SRW64Recomp\sessions\<id>\run\`。

1. Deck 上：Steam 里这个快捷方式的「属性 → 启动选项」写 `%command% --debug`，然后照常用手柄玩。出问题时不用退出。
2. Mac 上：`.venv/bin/python tools/release/linux/attach.py`（默认 ssh 主机 `Deck`，`--host` 可改）。它经 ssh 读出正在运行、开着调试接口（选项里的开关或 `--debug`）的游戏的 `debug.json`，挑一个本地空闲端口用 `ssh -L` 转发到远端端口，在 `build/recomp/debug/deck-<时间>/debug.json` 写下本地端口和原令牌（权限 0600），并设为当前会话；之后 `srw64ctl.py`、`Session.attach()`、MCP 的 `srw64_attach` 都直接可用。
3. 不想动 Steam 设置时，`attach.py --start` 会通过 ssh 带 `--debug` 启动游戏（游戏模式在 Xwayland `:1`，桌面模式传 `--display :0`）。加上 `--data-dir ~/srw64-debug` 则用一套单独的数据目录：ROM 和 HD 包是软链，存档和设置是复制的，调试不会写玩家自己的存档。游戏参数写在 `--` 之后。

运行目录里的 `remote.json` 记着主机、远端运行目录和转发进程。截图、录像（`srw64_record`）和事件日志（`srw64_events`、`wait` 的 `event` 条件）是宿主写在 Deck 上的文件，`Session.local_file()` 经同一条连接（`file.read`）取回到 `remote-files/`。`quit` 会关掉游戏和本地转发，报告留在 Deck 上。游戏的标准错误在 Steam 启动时进 `journalctl --user`（记在 steam 进程名下），`--start` 时游戏作为用户 systemd 的临时单元 `srw64-debug` 运行（SteamOS 开着 `KillUserProcesses=True`，留在 ssh 登录会话里的进程会随 ssh 断开被清掉），输出看 `journalctl --user -u srw64-debug`。

## 与现有控制文件的关系

`control.txt`、`script-inject.txt` 与 SDL 窗口／语言控制保留。旧 AppKit 姓名／规则控制文件
属于旧后端，共享 UI 使用 JSON-RPC 和 `tools/recomp/verify/verify_shared_ui.py` 验收。
`control.txt` 仅注入手柄状态；组字、页面点击与窗口焦点需使用 `ui.*`。

## 历史实测：旧 AppKit 后端（`build/recomp/debug/20260918T090743.895227Z`）

从冷启动全程由接口驱动、无人工按键：Enter 回到标题并在环形菜单选「スタート」，E+Enter 跳过公共序章（VI 12826），Z 选择超级系男主并确认，在现代姓名页用 `click --text` 依次按下「继续：搭档」「继续：确认」「开始故事」（应用在后台，按钮经 `performClick:`），男主路线序章按 E+Enter 后在 VI 18154 跳过（group 1）；进入对白后 I、I、K 使字号 13→14→15→14（三次都生效），按住 E+Z 时 14 VI 内连推 3 段；截图叠加了姓名页覆盖层；经菜单「选项 → 设置…」打开设置窗口并截图，用 `click --text` 勾选再取消「头目假身：次数减半」，规则与事件日志同步变化；`quit` 退出码 0，socket 被清理。

同日另一次会话（启动时 `--reuse-build`）：`window --size 1280 960` 后 `status` 与截图都报告 1280×960；在姓名页 `type なな --marked` 返回 `composing: true`，截图可见名字栏里带输入法高亮的「なな」，`type --unmark` 后 `composing: false`；`quit` 退出码 0。`window --close` 复用窗口 QA 已验证的 `performClose:` 路径，只做了编译检查。

这也复验了 2026-09-18 的姓名页修复：修复前姓名页关闭后按住的游戏键一律被吞，路线序章跳过、字号和快进都无效。

## 限制

- 开机后到标题画面出现前不要按 Enter（START）：原版开机时检测到按住 START 会进入 Controller Pak 管理画面，其中调用的 `osPfsIsPlug` 目前被生成代码拦截，宿主会中止。启动后先 `wait --vi 600`。
- 截图依赖游戏正在呈现画面；窗口最小化或游戏暂停呈现时会超时。
- 原生界面层只覆盖本程序自己的窗口；系统对话框、输入法候选窗不在范围内（组字本身用 `ui.type` 的 `marked` 模拟）。
- `status.ui.focus` 是 RmlUi 的焦点元素，`active` 单独说明 SDL 窗口是否有系统键盘焦点。
- 菜单项按标题匹配，标题随界面语言变化。
- 真实手柄的输入不经过接口；接口有自己的虚拟手柄（`pad`／`srw64_pad`），和真实手柄的输入合并。
- Windows 上接口已实现（回环 TCP）。2026-10-06 在 AWS（Windows Server 2022、Tesla T4、D3D12）实机连过：游戏用 `Marchwind64.cmd --debug` 启动，Mac 上 `ssh -L` 转发 `debug.json` 里的端口，本地运行目录写转发后的 `debug.json`（端口、原令牌）和 `remote.json`，`srw64_attach`（带 run）、status、screenshot、keys、pad、ui.tree、ui.click、settings、window、wait 都正常；截图经 `file.read` 取回（`local_file` 按 Windows 路径取文件名）。当时修掉的坑：plume 的 D3D12 `copyTextureRegion` 拷到缓冲时对空纹理断言，第一次截图游戏就中止（`prepare_rt64.py` 补丁）。`Session.launch`（`srw64ctl launch`、MCP 的 `srw64_launch`）在 Windows 上仍不可用：它用 `os.pipe` 加 `pass_fds` 让游戏随启动进程退出；玩家路径（选项里的开关或 `--debug`，加 `srw64_attach`）不受影响。

战前页回归可运行 `.venv/bin/python tools/recomp/debug/check_battle_ui.py`；精神与主动攻击返回流程可运行 `.venv/bin/python tools/recomp/debug/check_battle_spirits.py`。两者构建当前 native 宿主，通过主菜单 `mini-enter` 进入，不启用旧版人物选择。`status.mini_stage.waiting_reason` 可诊断关卡尚未就绪的原因；只有 `ready=true` 后才开始地图操作。截图和断言结果保存在各自的 debug 会话目录。

战前换武器与现场施放精神可运行 `.venv/bin/python tools/recomp/debug/check_battle_actions.py`：从新构建进入迷你关卡，验证主动攻击换武器、SP 实际扣除、精神效果刷新、反击／回避选择保留、同乘驾驶员 SP，以及中／日／英左右镜像布局。通过 `ui.click`、`ui.key`、`status.battle_page` 和 GPU 截图执行，不改写战斗快照。

2026-10-09 安卓后台超时修复：切到系统「文件」后 SDL 窗口线程可能暂停。`on_window` 的回调现在持有参数副本，并从队列撤销尚未开始的超时任务，避免请求返回错误后访问已销毁参数或恢复前台时执行过期点击。Seeker / Android 16 实测后台点击超时 5 秒，再从启动器返回后仍为同一进程，设置窗口保持打开；超时的关闭操作未执行。
