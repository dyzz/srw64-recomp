# 调试接口与 MCP

日期：2026-09-20。给开发者和 Claude 共用的实机调试入口：启动隔离的调试会话、按键、截图、读状态、操作原生界面、退出，都通过同一个接口完成，不用再靠人工按键或各自为政的控制文件。

## 结构

| 层 | 位置 | 作用 |
| --- | --- | --- |
| 宿主调试服务 | `src/host/debug_server.cpp`，开关 `SRW64_DEBUG=1` | 在运行目录下监听 `debug.sock`（权限 0600），每行一条 JSON-RPC 2.0 请求/回应。普通试玩不开启。需要 SDL/RmlUi 的操作排进窗口线程执行。 |
| 游戏键盘层 | `src/host/debug_protocol.hpp`、`graphics.cpp` | 虚拟按键与真实按键走同一条读取路径：绑定到同名扫描码，F6/F7/F8/Esc 按下沿经过同样的姓名页与语言切换门控；姓名页关闭后的释放检查也计入虚拟按键。虚拟按键不需要窗口焦点，游戏可以在后台被驱动。 |
| 原生界面层 | `src/host/debug_ui.hpp`、`src/native/ui/frontend.cpp` | 共享 SDL/RmlUi 页面提供界面树、稳定 ID／文字／坐标点击、输入文字和按键；设置与通知都在游戏 surface 内。 |
| 会话与客户端 | `tools/recomp/debug/session.py` | 启动会话（经 `run_host_probe.py --graphics --interactive --diagnostics full`，输出到 `build/recomp/debug/<时间戳>/`，不碰 `profile-play` 的存档和偏好）、socket 客户端、等待条件、事件日志增量读取。 |
| 命令行 | `tools/recomp/debug/srw64ctl.py` | 给人用的同一套操作。 |
| MCP 服务器 | `tools/recomp/debug/mcp_server.py`、仓库根 `.mcp.json` | 标准库实现的 stdio MCP（项目环境没有 `mcp` 包），Claude Code 批准项目 MCP 并重开会话后即可调用 `srw64_*` 工具。 |

## 覆盖范围

目标是游戏接收的每一种输入都能经接口发出，并尽量走与玩家相同的代码路径：

| 游戏接收的输入 | 玩家的来源 | 接口 |
| --- | --- | --- |
| 18 个游戏键（14 个 N64 按键对应的键与 WASD 摇杆） | SDL 键盘状态 | `keys`：同名虚拟键并入同一次键盘读取，不需要窗口焦点 |
| F6 画面、F8 迷你关卡、Esc 退出 | SDL 按键事件 | `keys`：虚拟按下沿经过与真实按键相同的姓名页、语言切换门控 |
| F7 语言 | SDL 键盘事件，组字期间交给输入法 | `keys f7`：进入相同的 SDL 组字／repeat 门控 |
| N64 手柄（绕过键盘层） | 无（诊断用） | `buttons` |
| 主角选择页：四张卡片、←→、Enter／Z | SDL 鼠标与键盘 | `ui.click --text <主角全名>`（高亮后按继续或 Enter）、`ui.key right`／`return`；`status.name_page` 给出 `route` 与四个选项 |
| 场间主菜单：九项（「（前）」场景后两项）、のりかえ 二级菜单 | SDL 鼠标与键盘 | `ui.click --text intermission:N`（或可见文字）确认第 N 项，`intermission-swap:0|1` 选驾驶员／妖精；`ui.key up`／`down`／`return`／`escape`；`status.intermission_page` 给出 `cursor`、`submenu`、`swap_refused`、回合数与资金 |
| 改造画面：机体列表、五项改造、确认窗与消息 | SDL 鼠标与键盘 | `ui.click --text upgrade:N`（当前行确认、其他行移动）、`upgrade-confirm`／`upgrade-cancel`／`upgrade-dismiss`；`ui.key up`／`down`／`left`／`right`／`return`／`escape`；`status.upgrade_page` 给出 `screen`、`rows`、`window`、`funds`；資金直接修改：`ui.click --text upgrade-funds`（主菜单 `intermission-funds`）、`ui.type <数字>`、`ui.key return` |
| 联动页：三张作品卡片、←→、空格／Z、Enter、Esc／X | SDL 鼠标与键盘 | `ui.click --text <作品名>`（每次切换勾选）、`ui.click --text <继续按钮>`、`ui.key right`／`space`／`return`；`status.link_page` 给出 `joined` 与 `scheduled` |
| 姓名页：字段、按钮、Tab／Enter／Esc、输入法组字 | SDL 鼠标与键盘 | `ui.click`、`ui.type`（`marked`／`unmark` 模拟组字与提交）、`ui.key` |
| 战前确认页：双方概率、应对、动画、开始／返回；按键同游戏（Z/Enter、X/Esc、方向键/WASD、Q、E、K）及手柄 | SDL/RmlUi | `ui.click --id battle-confirm`、`battle-weapon`、`battle-counter`、`battle-evade`、`battle-defend`、`battle-spirits`、`battle-animation`、`battle-back`；`status.battle_page` 是游戏线程发布的快照 |
| 运行时加载关卡文件 | 标题菜单时把文件拖到窗口 | `mini_stage.load {"path": <镜像或关卡源文件>}`；直接进入，无需启动时指定关卡，见[迷你关卡](../script/mini-stage.md) |
| 主菜单迷你关卡入口 | RmlUi 按钮／F8 | 带 mini stage 启动后 `ui.click --id mini-enter`，或 `keys f8`；等待 `status.mini_stage.ready`。自动完成默认人物初始化，普通新游戏不变 |
| 游戏内「选项」及规则设置 | RmlUi 控件、Ctrl/Cmd+, | `ui.click`、`ui.key`；`menu` 保留本地化规则标题的兼容转发 |
| 场间 強化パーツ 页面：机体列表、槽位／库存、持有者 | RmlUi 页面 | `ui.click --id parts:N`／`parts-slot:N`，或 `keys` 的方向键、Z／X；`status.parts_page`，等待条件 `parts_page`，事件日志 `parts` |
| 场间 ユニット能力／パイロット能力 页面 | RmlUi 页面 | `ui.click --id ability:N`，或 `keys` 的方向键、Z／X、Q／E；`status.ability_page`，等待条件 `ability_page`，事件日志 `ability` |
| 场间 のりかえ 页面：驾驶员／妖精列表、目标列表、确认 | RmlUi 页面 | `ui.click --id swap:N`／`swap-yes`／`swap-no`，或 `keys`；`status.swap_page`，等待条件 `swap_page`，事件日志 `swap` |
| 场间 データセーブ 页面：介质选择、存档栏、覆盖确认、Pak 提示 | RmlUi 页面 | `ui.click --id save:N`／`save-yes`／`save-no`，或 `keys`；`status.save_page`，等待条件 `save_page`，事件日志 `save` |
| 共享设置：规则、预设、语言、画面、战前确认界面、场间画面、主角选择与姓名输入 | RmlUi 页面 | `ui.click`／`ui.tree`／默认 `screenshot`；或用 `settings` 直接设定（`rules`／`images`／`locale`／`battle_ui`／`intermission_ui`／`name_entry_ui`） |
| 游戏窗口：尺寸、前台、关闭按钮 | 窗口管理 | `window`（`width`/`height`、`front`、`close`） |
| 正常退出 | Esc、关窗、⌘Q | `quit`，或 `keys escape`、`window close` |

`tests/test_debug_coverage.py` 静态核对这张表的前提，新增按键或方法时漏接接口会让测试失败：宿主读取的每个 SDL 扫描码都有同名虚拟键；F6/F8/Esc 有虚拟按下沿，F7 有对应虚拟键；`buttons` 的按钮表与输入编译器（`native_inputs.BUTTONS`）一致；MCP 工具描述的键名与宿主一致；每个宿主方法都有 MCP 工具。

## 宿主方法

| 方法 | 参数 | 说明 |
| --- | --- | --- |
| `status` | `history` | VI、运行目录、窗口焦点与尺寸、语言、画面模式、规则、开场状态（`title_major` 3 为主菜单，`step` 为当前页）、对白阅读器（页、字号、速度、自动、回看、跳过、各对白框文字）、姓名页请求、联动页（`link_page`）、场间主菜单（`intermission_page`）、改造画面（`upgrade_page`）、战前确认页（`battle_page`）、迷你关卡状态（`mini_stage.available/entering/active/ready`）、最近的原生提示条（`notices`，如离队退款）、原生窗口与焦点、按住的虚拟键 |
| `keys` | `press`+`hold_ms` / `down` / `up` / `release_all` | 游戏键盘；键名 `z x space return up down left right q e i k j l w a s d escape f6 f7 f8 f5`（F5 重新载入台词文本），组合用 `+`，如 `e+return` |
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

## MCP 工具

`srw64_launch`、`srw64_attach`、`srw64_status`、`srw64_keys`、`srw64_buttons`、`srw64_screenshot`（直接返回图片）、`srw64_ui_tree`、`srw64_click`、`srw64_type`、`srw64_ui_key`、`srw64_menu`、`srw64_window`、`srw64_settings`、`srw64_mini_stage_load`、`srw64_wait`（`vi`、`dialogue_active`、`intro_active`、`name_page`、`link_page`、`intermission_page`、`battle_page`、`title_major`、`text`、`event`）、`srw64_events`（日志：`dialogue`、`intro`、`name`、`rules`、`images`、`control`、`script`、`mini_stage`、`settings`、`refunds`、`link`、`intermission`）、`srw64_quit`。工具错误以 `isError` 返回，不会中断服务器。

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
- 键盘以外的手柄没有接入宿主，因此也不在接口范围内。

战前页回归可运行 `.venv/bin/python tools/recomp/debug/check_battle_ui.py`；精神与主动攻击返回流程可运行 `.venv/bin/python tools/recomp/debug/check_battle_spirits.py`。两者构建当前 native 宿主，通过主菜单 `mini-enter` 进入，不启用旧版人物选择。`status.mini_stage.waiting_reason` 可诊断关卡尚未就绪的原因；只有 `ready=true` 后才开始地图操作。截图和断言结果保存在各自的 debug 会话目录。

战前换武器与现场施放精神可运行 `.venv/bin/python tools/recomp/debug/check_battle_actions.py`：从新构建进入迷你关卡，验证主动攻击换武器、SP 实际扣除、精神效果刷新、反击／回避选择保留、同乘驾驶员 SP，以及中／日／英左右镜像布局。通过 `ui.click`、`ui.key`、`status.battle_page` 和 GPU 截图执行，不改写战斗快照。
