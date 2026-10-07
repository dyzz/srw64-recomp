# 问题报告

2026-10-07。设置窗口的「反馈」页（`frontend.cpp feedback_page`，在「操作」和「关于」之间）有三行：

- **平台信息**：「复制平台信息」把几行文字放进剪贴板（`bug_report::summary`：版本、原版／HD、HD 包版本；系统、机型、处理器、架构、内存；图形 API 与显卡；窗口与像素尺寸、界面大小、语言、宽屏、滤镜和框体；手柄、触屏、掌机；开着的修正和作弊数），页面上同时显示复制了什么，玩家贴进 Issue。
- **问题报告**：「导出问题报告」写一个 zip，到 GitHub 提问题时附上（Issue 表单 `.github/ISSUE_TEMPLATE/bug.yml` 请他们附）。
- **去哪里反馈**：「到 GitHub 反馈」打开 Issues 的表单选择页；「官网提文本意见」打开当前语言的官网剧情页。

所有平台都有，包括安卓和 Steam Deck。8 个页签挤一行，页签左右内边距从 10dp 减到 6dp，英文页名用 Report（`test_settings_window` 按宽度核对）。

## 导出什么

`<用户目录>/reports/Marchwind64-report-<日期>-<时间>.zip`（`src/host/bug_report.cpp`）：

| 文件 | 内容 |
| --- | --- |
| `report.json` | `srw64.bug-report.v1`：`system`（平台、系统版本与构建号、机型、处理器、核数、内存、是否 Steam Deck；Linux 另有发行版、内核、桌面与会话类型，Windows 有 RtlGetVersion 的版本号和是否 Wine，安卓有系统版本、SDK、厂商机型、芯片）；`game`（`frontend.cpp report_facts`：版本、语言、界面大小、原版／HD、HD 包是否装了与版本、宽屏、滤镜和框体的文件名、开着的规则修正与作弊、手柄名、触屏、掌机、调试接口、窗口尺寸、图形 API 与显卡名称／厂商／驱动／显存 `srw64_graphics_info`）；`files`（zip 里的其他文件） |
| `settings/*.json` | 用户目录里的 `presentation.json`、`input.json`、`rules.json`、`update.json`、`saves/settings.json`、`hd/hd.json`（有哪个带哪个） |
| `sessions/<编号>/launch.json`、`sessions/<编号>/run/*` | 用户目录保留的最近 3 次运行（本次运行排第一）：`launch.json`，以及运行目录里非空的 `.json`、`.jsonl`、`.log`、`.txt`，包括 `console.log` |

不带：ROM 和运行目录里的 ROM 副本（`runtime-data/`）、存档、录下的音频（`audio-output.s16`）、截图、`debug.json`（调试接口的令牌）、`last-rom.txt`。每个文本文件里的家目录都写成 `~`（Windows 上连 JSON 转义的 `C:\\Users\\…` 和正斜杠写法也换）。单个文件超过 4 MiB 只留最后 4 MiB。zip 在内存里用 miniz 写好（librecomp 已经链接），再用 `std::filesystem` 路径写出，所以 Windows 上用户名不是 ASCII 也能写。

导出后这一行显示文件位置；能打开文件夹的平台（桌面和安卓）给「打开所在文件夹」，安卓打开「文件」App 里的 `reports` 文件夹；Steam Deck 游戏模式、触屏和掌机上给「复制文件路径」。

## console.log

此前玩家这边没有任何日志文件：宿主的 `SRW64_*` 行和 RT64 的输出只到 stderr，macOS 从访达打开时直接丢掉，Windows 只在控制台窗口里。现在 `main` 一开始调用 `console_log::start()`（`src/host/console_log.cpp`）：stdout、stderr 各接一根管道，读出来的内容照旧写回原来的地方（终端；安卓转到 logcat，取代原来的 `forward_output_to_logcat`），同时写进本次运行目录的 `console.log`。运行目录在 `run_host` 里才知道（`console_log::attach`），之前的输出先存在内存里（最多 1 MiB），接上后先写。退出时最多等 0.2 秒让管道排空。Linux 的崩溃处理（`crash_backtrace`）除了 stderr，还把回溯直接写进 `console.log`，因为进程正在崩溃时管道不一定还会被读。

所以崩溃或卡死后，玩家重新打开游戏再导出，上一次运行的 `console.log` 也在报告里（用户目录保留最近 3 次）。
