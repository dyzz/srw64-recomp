---
title: 用 AI 智能体操作游戏（MCP）
summary: 让 Claude Code 等支持 MCP 的 AI 工具连上正在运行的游戏：读取状态、截图、按键、操作界面、读写内存。
section: tools
order: 1
updated: 2026-10-06
---

Marchwind 64 内置一个调试接口。在选项里打开后，游戏会在本机开一个端口，仓库里的 MCP 服务器通过它把游戏变成一组工具，交给支持 [Model Context Protocol](https://modelcontextprotocol.io/) 的 AI 客户端使用（Claude Code、Codex、Cursor 等）。

接上之后，智能体可以：

- 读取当前画面在做什么：标题、对白、场间画面、战前确认、设置窗口等状态；
- 截图、录一段 MP4；
- 像玩家一样按键，或者直接操作原生界面上的按钮和输入框；
- 切换语言、图片模式、规则和界面大小；
- 读写游戏内存（排查问题用）。

我们自己开发时就是这样让 AI 跑流程、复现 bug、核对翻译排版的。玩家也可以拿它来写自动化脚本、录视频，或者让 AI 帮忙看出了什么问题。

## 需要什么

- **游戏**：Windows、macOS、Linux 或 Steam Deck 版都可以。
- **Python 3**：系统自带的就行，不用装额外的包。Windows 上从 [python.org](https://www.python.org/downloads/) 安装，命令是 `python`。
- **MCP 服务器脚本**：在[源代码仓库](https://github.com/dyzz/srw64-recomp)里，克隆下来即可，不需要编译：

  ```bash
  git clone https://github.com/dyzz/srw64-recomp.git
  ```

  用到的文件是 `tools/recomp/debug/mcp_server.py` 和它旁边的 `session.py`。
- **一个支持 MCP 的 AI 客户端**。

## 1. 在游戏里打开调试接口

打开「选项 → 关于」，把「AI 调试接口（MCP）」设为「开」。立即生效，不用重启；设置会保存，以后每次启动游戏都会在画面上方提示一次，提醒你它还开着。不用时在同一个地方关掉，已有的连接会马上断开。

开着的时候，这一行下面会显示正在监听的地址和这次运行的目录，旁边的「复制运行目录」按钮可以把完整路径复制到剪贴板。

游戏只在本机（`127.0.0.1`）上开一个端口，并在运行目录里写一个 `debug.json`，记下端口号和一串随机令牌。连接时必须先出示令牌，而这个文件只有你自己的账户读得到。游戏退出或关掉开关时，这个文件会被删掉。

<details>
<summary>也可以用启动参数 <code>--debug</code> 打开（只对这一次运行有效）</summary>

- **macOS**：`/Applications/Marchwind64.app/Contents/MacOS/srw64-gfx-host --play --rom ~/Games/srw64.z64 --debug`（`.z64`、`.v64`、`.n64` 都可以）
- **Windows**：在解压出的文件夹里运行 `Marchwind64.cmd --debug`
- **Linux**：`./marchwind64.sh --debug`
- **Steam Deck**：Steam 里这个游戏的「属性 → 启动选项」填 `%command% --debug`

用参数打开时，选项里的开关显示为「开」，本次运行不能在那里关闭。

</details>

各系统的运行目录：

| 系统 | 运行目录 |
|---|---|
| Windows | `%LOCALAPPDATA%\SRW64Recomp\sessions\<会话>\run\` |
| macOS | `~/Library/Application Support/SRW64Recomp/sessions/<会话>/run/` |
| Linux / Steam Deck | `~/.local/share/srw64-recomp/sessions/<会话>/run/` |

## 2. 把 MCP 服务器加到 AI 客户端

MCP 服务器是一个通过标准输入输出通信的 Python 脚本。

**Claude Code**：

```bash
claude mcp add srw64 -- python3 /你的路径/srw64-recomp/tools/recomp/debug/mcp_server.py
```

**其他客户端**：大多用类似下面的 JSON 配置（文件位置和外层键名以各客户端的说明为准）：

```json
{
  "mcpServers": {
    "srw64": {
      "command": "python3",
      "args": ["/你的路径/srw64-recomp/tools/recomp/debug/mcp_server.py"]
    }
  }
}
```

Windows 上把 `python3` 换成 `python`，路径写成 `C:\\你的路径\\srw64-recomp\\tools\\recomp\\debug\\mcp_server.py`（JSON 里反斜杠要写两个）。

加好后客户端里应该能看到一组以 `srw64_` 开头的工具。

## 3. 连接并开始使用

直接让智能体连上游戏就行，不用告诉它路径：

```text
用 srw64_attach 连上游戏，截一张图告诉我现在在哪个画面。
```

`srw64_attach` 不带参数时，会在你的用户目录里找开着调试接口的游戏，连上最新启动、而且能应答的那一个。同时开着好几个游戏，或者想指定某一个时，把「复制运行目录」得到的路径传给它的 `run` 参数。

连上以后就可以用自然语言布置任务，比如：

- 「等到主菜单出现后开始新游戏，一路快进到第一次选择。」
- 「把语言切成英文，截图对照中文版同一个画面，看有没有文字溢出。」
- 「录 10 秒，我要看这段战斗动画哪里卡了。」
- 「对白卡住不动了，读一下状态和最近的事件日志，看看停在哪条指令。」

智能体一般会先看 `srw64_status` 和截图判断画面，再用 `srw64_wait` 等条件满足，避免盲目等待。

## 工具一览

| 工具 | 用途 |
|---|---|
| `srw64_attach` | 连接一个运行中的游戏。不填参数时自动找最新的那个；`run` 填运行目录可以指定。 |
| `srw64_launch` | 从源码构建并启动一个隔离的开发会话（需要完整的开发环境，见下文）。 |
| `srw64_status` | 当前状态：帧数、窗口、语言、图片模式、规则、标题与序章、对白（页、字号、速度、快进）、各原生页面、提示条、按住的键。 |
| `srw64_screenshot` | 截取下一帧（含原生叠加界面），直接返回图片；也可以截设置窗口等其他窗口。 |
| `srw64_record` | 录接下来若干秒的 MP4，返回文件路径。 |
| `srw64_record_start` / `srw64_record_stop` | 开始 / 结束一段不定长录像。 |
| `srw64_wait` | 等待条件成立：帧数、对白出现、某个页面打开、回到主菜单、对白里出现某段文字、事件日志里出现某类事件。 |
| `srw64_events` | 读取事件日志（对白、序章、规则、设置、各场间画面、脚本等）。 |
| `srw64_keys` | 游戏键盘。固定使用经典布局，不受玩家改键影响：Z＝A、X＝B、Enter＝START、方向键、Q/E＝L/R、I/K＝C 上/下、WASD＝摇杆。支持组合键和按住时长，比如 `e+z` 快进。 |
| `srw64_pad` | 虚拟手柄，按键名用 Steam Deck 的叫法，效果与默认绑定一致（View 打开设置等）。 |
| `srw64_buttons` | 直接按 N64 手柄按键，跳过键盘层。 |
| `srw64_ui_tree` | 列出原生界面（姓名页、设置窗口、菜单栏等）上的控件、位置和文字。 |
| `srw64_click` / `srw64_type` / `srw64_ui_key` | 按文字或坐标点击控件、在输入框里打字（支持输入法组字）、给界面发按键。 |
| `srw64_menu` | 按路径点菜单栏项目，或列出菜单。 |
| `srw64_window` | 调整窗口大小、置前或关闭。 |
| `srw64_settings` | 直接改规则、语言、图片模式、界面大小、宽高比和各画面的原版/现代模式。 |
| `srw64_mini_stage_load` | 加载并进入一个迷你关卡镜像，见[迷你关卡](../mini-stage/)。 |
| `srw64_viewer_start` | 在战斗查看器里播放指定的一场战斗动画。 |
| `srw64_memory` / `srw64_memory_write` | 读 / 写游戏内存（十六进制）。写入一次最多 4096 字节。 |
| `srw64_quit` | 正常退出游戏，返回本次运行的报告。 |

工具出错时以错误结果返回，不会让 MCP 服务器退出。

## 注意事项

- **这是你真正的游戏和存档。** 智能体连上的就是你平常玩的游戏，读写的是平常那套存档和设置，智能体存档、改设置都会真的生效。想放心折腾，先备份用户目录下的 `saves/`，或者用 `--user-dir` 指定一套单独的目录（把 ROM 路径和需要的存档复制过去）。
- **`srw64_memory_write` 可能让游戏出错。** 它绕过游戏逻辑直接改内存，只在你清楚自己在做什么时使用。
- **接口只对本机开放。** 游戏只监听 `127.0.0.1`，局域网里的其他设备连不上；本机的其他程序也要拿到 `debug.json` 里的令牌才能操作。不要把这个文件发给别人。远程连接要靠 ssh 转发（见下）。
- **`srw64_quit` 会关掉游戏。** 只想断开、继续玩的话，在「选项 → 关于」把开关关掉就行。
- **不用时关掉开关。** 开关会一直保存，启动时的提示就是提醒这一点。

## 从电脑连 Steam Deck

前提：电脑能用 ssh 登录 Deck（例如 `~/.ssh/config` 里配好了一个叫 `Deck` 的主机），Deck 上的游戏已在「选项 → 关于」打开调试接口（或者按上文带 `--debug` 启动）。在电脑的仓库目录里运行：

```bash
python3 tools/release/linux/attach.py --host Deck
```

它会在 Deck 上找到正在运行的游戏，用 `ssh -L` 把端口转发到本地，并设为「当前会话」。之后智能体直接调用 `srw64_attach`（不带参数）就能连上 Deck。截图、录像和事件日志是游戏写在 Deck 上的文件，会通过同一条连接自动取回。

`attach.py --start` 会通过 ssh 带 `--debug` 启动游戏；再加 `--data-dir '~/srw64-debug'` 则使用一套单独的数据目录（ROM 和 HD 包用链接，存档和设置是复制的），调试不会写你自己的存档。

## 安卓

安卓版同样在「选项 → 关于」打开。手机和电脑之间要靠 adb：先在手机的开发者选项里打开 USB 调试（或无线调试），用 adb 连上电脑，然后在仓库目录里运行：

```bash
python3 tools/release/android/attach.py --no-start
```

它用 `adb forward` 把游戏的接口转发到电脑，并设为「当前会话」，之后同样调用 `srw64_attach`。截图、录像和事件日志也会通过这条连接取回。安卓上不用令牌，电脑通过 adb 转发访问接口。

## 开发者：从源码启动

如果你在本地搭好了完整的构建环境（见仓库里的构建文档），`srw64_launch` 会按需构建并启动一个独立的调试会话。它的运行目录在仓库的 `build/recomp/debug/` 下，不碰你平常玩的存档；还可以直接指定语言、图片模式、规则、存档或迷你关卡。会话随 MCP 服务器一起退出。

仓库里的 `docs/guide/debug-interface.md` 有更完整的说明，包括命令行工具 `srw64ctl.py`、Python 脚本接口和接口覆盖范围。
