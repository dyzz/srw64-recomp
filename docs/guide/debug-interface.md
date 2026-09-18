# 调试接口与 MCP

日期：2026-09-18。给开发者和 Claude 共用的实机调试入口：启动隔离的调试会话、按键、截图、读状态、操作原生界面、退出，都通过同一个接口完成，不用再靠人工按键或各自为政的控制文件。

## 结构

| 层 | 位置 | 作用 |
| --- | --- | --- |
| 宿主调试服务 | `src/host/debug_server.cpp`，开关 `SRW64_DEBUG=1` | 在运行目录下监听 `debug.sock`（权限 0600），每行一条 JSON-RPC 2.0 请求/回应。普通试玩不开启。需要 SDL/AppKit 的操作排进窗口线程执行。 |
| 游戏键盘层 | `src/host/debug_protocol.hpp`、`graphics.cpp` | 虚拟按键与真实按键走同一条读取路径：绑定到同名扫描码，F6/F7/F8/Esc 按下沿经过同样的姓名页与语言切换门控；姓名页关闭后的释放检查也计入虚拟按键。虚拟按键不需要窗口焦点，游戏可以在后台被驱动。 |
| 原生界面层 | `src/host/debug_ui.hpp`、`src/host/macos/debug_ui_macos.mm` | 对我们覆盖原版界面的原生 AppKit UI（姓名页、设置窗口、菜单栏以及以后的页面）提供通用的键盘与鼠标：界面树、按文字或坐标点击、输入文字、发送按键、按标题路径按菜单。不包含任何页面专用代码。 |
| 会话与客户端 | `tools/recomp/debug/session.py` | 启动会话（经 `run_host_probe.py --graphics --interactive --diagnostics full`，输出到 `build/recomp/debug/<时间戳>/`，不碰 `profile-play` 的存档和偏好）、socket 客户端、等待条件、事件日志增量读取。 |
| 命令行 | `tools/recomp/debug/srw64ctl.py` | 给人用的同一套操作。 |
| MCP 服务器 | `tools/recomp/debug/mcp_server.py`、仓库根 `.mcp.json` | 标准库实现的 stdio MCP（项目环境没有 `mcp` 包），Claude Code 批准项目 MCP 并重开会话后即可调用 `srw64_*` 工具。 |

## 覆盖范围

目标是游戏接收的每一种输入都能经接口发出，并尽量走与玩家相同的代码路径：

| 游戏接收的输入 | 玩家的来源 | 接口 |
| --- | --- | --- |
| 18 个游戏键（14 个 N64 按键对应的键与 WASD 摇杆） | SDL 键盘状态 | `keys`：同名虚拟键并入同一次键盘读取，不需要窗口焦点 |
| F6 画面、F8 迷你关卡、Esc 退出 | SDL 按键事件 | `keys`：虚拟按下沿经过与真实按键相同的姓名页、语言切换门控 |
| F7 语言 | AppKit 本地事件监视器（SDL 收不到） | `keys f7`：发出与监视器相同的语言请求 |
| N64 手柄（绕过键盘层） | 无（诊断用） | `buttons` |
| 姓名页：字段、按钮、Tab／Enter／Esc、输入法组字 | AppKit 鼠标与键盘 | `ui.click`、`ui.type`（`marked`／`unmark` 模拟组字与提交）、`ui.key` |
| 菜单栏「选项」（游戏性调整各项、「设置…」⌘,）与应用菜单 | 菜单栏 | `menu`；快捷键也可用 `ui.key`（如 `,` 加 `cmd`） |
| 设置窗口：规则、预设、语言、画面 | AppKit 窗口 | `ui.click`／`ui.tree`／`screenshot` 加 `window: "选项"`；或用 `settings` 直接设定 |
| 游戏窗口：尺寸、前台、关闭按钮 | 窗口管理 | `window`（`width`/`height`、`front`、`close`） |
| 正常退出 | Esc、关窗、⌘Q | `quit`，或 `keys escape`、`window close` |

`tests/test_debug_coverage.py` 静态核对这张表的前提，新增按键或方法时漏接接口会让测试失败：宿主读取的每个 SDL 扫描码都有同名虚拟键；F6/F8/Esc 有虚拟按下沿，F7 有对应虚拟键；`buttons` 的按钮表与输入编译器（`native_inputs.BUTTONS`）一致；MCP 工具描述的键名与宿主一致；每个宿主方法都有 MCP 工具。

## 宿主方法

| 方法 | 参数 | 说明 |
| --- | --- | --- |
| `status` | `history` | VI、运行目录、窗口焦点与尺寸、语言、画面模式、规则、开场状态（`title_major` 3 为主菜单，`step` 为当前页）、对白阅读器（页、字号、速度、自动、回看、跳过、各对白框文字）、姓名页请求、原生窗口与焦点、按住的虚拟键 |
| `keys` | `press`+`hold_ms` / `down` / `up` / `release_all` | 游戏键盘；键名 `z x space return up down left right q e i k j l w a s d escape f6 f7 f8`，组合用 `+`，如 `e+return` |
| `buttons` | `buttons`、`vis` | N64 手柄层按键（`a b z start up down left right l r c_up c_down c_left c_right`），立即生效，不经过键盘层 |
| `screenshot` | `path`、`overlays`、`window`、`timeout_ms` | 抓下一次呈现的 GPU 回读，再把游戏窗口上可见的 AppKit 覆盖层（如姓名页）画上去；`window` 为其他窗口（如 `"选项"`）时直接渲染该 AppKit 窗口。不依赖完整诊断。 |
| `ui.tree` | `window` | 可见原生窗口的视图树：类名、`frame`（点，左上角为原点，与截图方向一致；像素 = 点 × `scale`）、文字、可用、焦点；游戏的 Metal 视图标为 `role: game` |
| `ui.click` | `text` 或 `x`/`y`，`window`、`button`、`count` | 按按钮标题、输入框文字或无障碍标签点击，精确匹配优先。应用在前台时发送真实鼠标事件；在后台时 AppKit 会把第一次点击用于激活窗口，因此改为对按钮 `performClick:`、对输入框直接设为焦点，回应里的 `delivery` 注明实际方式。 |
| `ui.key` | `key`（或 `key_code`+`characters`）、`modifiers`、`window` | 向原生界面发送按键（return、tab、escape、delete、方向键、a–z、0–9、f1–f12） |
| `ui.type` | `text`、`marked`、`unmark`、`window` | 向获得焦点的输入框插入文字，等同键入；`marked: true` 留作输入法组字（带下划线，未提交），`unmark: true` 提交组字；回应 `composing` 表示是否仍在组字 |
| `menu` | `path` | 按标题路径按下菜单项；路径停在子菜单或为空时列出条目 |
| `settings` | `rules`（预设名或 ID 列表）、`locale`、`images` | 直接改规则、语言、画面 |
| `window` | `width`/`height`、`front`、`close` | 调整游戏窗口尺寸（640–2560 × 480–1600 点）、带到前台（只在要验证真实焦点或真实鼠标事件时需要）、按下关闭按钮（`NSWindow performClose:`，与玩家关窗同一路径）；回应窗口状态 |
| `wait_vi` | `vi`、`timeout_ms` | 等到指定 VI |
| `quit` | — | 正常退出，报告记为控制退出 |
| `methods` | — | 列出宿主支持的方法 |

窗口选择：`"game"`（默认）、`"key"`、窗口编号，或标题子串（如设置窗口的 `"选项"` / `"Options"`）。

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

## MCP 工具

`srw64_launch`、`srw64_attach`、`srw64_status`、`srw64_keys`、`srw64_buttons`、`srw64_screenshot`（直接返回图片）、`srw64_ui_tree`、`srw64_click`、`srw64_type`、`srw64_ui_key`、`srw64_menu`、`srw64_window`、`srw64_settings`、`srw64_wait`（`vi`、`dialogue_active`、`intro_active`、`name_page`、`title_major`、`text`、`event`）、`srw64_events`、`srw64_quit`。工具错误以 `isError` 返回，不会中断服务器。

## 与现有控制文件的关系

`control.txt`、`script-inject.txt`、窗口／语言／规则／设置／姓名页的控制文件全部保留，现有验证脚本继续使用；调试接口调用的是同一批底层函数。按键方面，`control.txt` 只在手柄层注入、每 6 VI 轮询一次，碰不到键盘焦点、F 键和姓名页释放逻辑；调试接口的游戏键盘层覆盖了这些路径。

## 实测（`build/recomp/debug/20260918T090743.895227Z`）

从冷启动全程由接口驱动、无人工按键：Enter 回到标题并在环形菜单选「スタート」，E+Enter 跳过公共序章（VI 12826），Z 选择超级系男主并确认，在现代姓名页用 `click --text` 依次按下「继续：搭档」「继续：确认」「开始故事」（应用在后台，按钮经 `performClick:`），男主路线序章按 E+Enter 后在 VI 18154 跳过（group 1）；进入对白后 I、I、K 使字号 13→14→15→14（三次都生效），按住 E+Z 时 14 VI 内连推 3 段；截图叠加了姓名页覆盖层；经菜单「选项 → 设置…」打开设置窗口并截图，用 `click --text` 勾选再取消「头目假身：次数减半」，规则与事件日志同步变化；`quit` 退出码 0，socket 被清理。

同日另一次会话（启动时 `--reuse-build`）：`window --size 1280 960` 后 `status` 与截图都报告 1280×960；在姓名页 `type なな --marked` 返回 `composing: true`，截图可见名字栏里带输入法高亮的「なな」，`type --unmark` 后 `composing: false`；`quit` 退出码 0。`window --close` 复用窗口 QA 已验证的 `performClose:` 路径，只做了编译检查。

这也复验了 2026-09-18 的姓名页修复：修复前姓名页关闭后按住的游戏键一律被吞，路线序章跳过、字号和快进都无效。

## 限制

- 开机后到标题画面出现前不要按 Enter（START）：原版开机时检测到按住 START 会进入 Controller Pak 管理画面，其中调用的 `osPfsIsPlug` 目前被生成代码拦截，宿主会中止。启动后先 `wait --vi 600`。
- 截图依赖游戏正在呈现画面；窗口最小化或游戏暂停呈现时会超时。
- 原生界面层只覆盖本程序自己的窗口；系统对话框、输入法候选窗不在范围内（组字本身用 `ui.type` 的 `marked` 模拟）。
- 应用在后台时，`status.ui.focus` 为空（没有 key window），各窗口的 `focused` 仍以 `ui.tree` 为准。
- 菜单项按标题匹配，标题随界面语言变化。
- 键盘以外的手柄没有接入宿主，因此也不在接口范围内。
