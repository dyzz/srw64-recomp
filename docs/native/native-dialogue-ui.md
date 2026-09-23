# 实时苹方对话 UI

2026-09-11：新增统一原始 JP ROM 的日中语言目录入口，见 [原生内容架构第一批实现](native-content-foundation.md)。当前使用统一 profile；下文的历史运行证据仍保留原验证范围。

2026-09-09。此实现将女性超级系开场世界地图、第一张战术地图中的标准双框剧情对白接到宿主 UI。它运行原剧情脚本，读取当前文本 ID、说话人、STOP 片段和等待状态，由 macOS Core Text 排版并在最终 Metal 画面上绘制。

## 操作

开场星空中的缩放文字也支持 E + Enter 跳过整段，详见 [开场缩放文字](native-intro.md)。此处下表其余阅读功能针对标准对白。

| 功能 | 游戏按键 | 当前键盘映射 |
|---|---|---|
| 下一阅读页；最后一页才推进原剧情 | 确认 A | Z |
| 提高自动阅读速度，按住连续调节 | 上 | ↑ |
| 降低速度，降到 0 恢复手动 | 下 | ↓ |
| 关闭自动阅读／取消正在跳过 | B | X |
| 按住快速阅读，松开停止 | R + A | E + Z |
| 跳过当前脚本段，遇脚本结束、选择或场景切换停止 | R + START | E + Enter |
| 打开／关闭对话回看 | L | Q |
| 回看滚动／返回；返回不推进剧情 | 上下／A、B、L、START | ↑↓／Z、X、Q、Enter |
| 正文字号 10–18，默认 13 | C 上／C 下 | I / K |

自动速度 1–4 同时改变显字速度与读完后的等待；0 为手动。确认键直接进入下一阅读页，不要求先补完逐字动画再按一次。按住普通确认不会连续推进。快速剧情仍按阅读页推进；跳过当前段则直接确认该脚本已经到达的文本片段。剧情动画、事件效果和地图载入仍由原代码执行，未修改整个游戏的时钟。

当前界面保持原来的头像和双框布局。人名使用蓝色 `#69BFFF`；当前正文为白色，另一对话框为灰色。回看保存当前会话最近 256 条对白；正在读的一条只记录已经显出的内容，确认过的页可完整回读，未到达的后续脚本不会提前加入。

**排版（2026-09-23，规则见[对白排版](../design/dialogue-typesetting.md)）：**
- 名字 10 号，放在框内左上；正文区紧贴在下面，宽 177、高 43。框内从文字原点上方 20 到下方 34，共 54 高。
- 每页行数按字号自适应：先按最小行距（中文 1.08 倍、英文 1.15 倍）算能放几行，再把剩余高度分给各行，最多 1.22 倍。默认 13 号中英文都是每页 3 行。英文正文按字号设置的 0.85 倍排，底栏显示的数字不变。
- 翻页位置用动态规划选：页数最少，其次少切在句子中间、少切在逗号处、少留一两个字的末行。中文行末的句读标点能因此多放一个字时压成半宽。
- **一条对白整条连排。** 原版每次按 A 翻出的片段（`<STOP>` 之间）连成一段，中文直接相连、英文用空格相连，按框的大小重新分页。宿主翻到的页一旦包含下一片段的开头，就在后台替原版确认上一个 `<STOP>`；最后一页按 A 时再确认 `<END>`。原版确认次数总是片段数加一，只会前进。逐字显示走到原翻页点时停 0.3 秒。
- 片段数以日文 ROM 记录为准（游戏实际执行的是它）。译文的 `---` 少于原文时，缺的确认放到最后一页；多出的只当普通断句。
- 改字号时整条重排，当前页仍从同一个字开始。切换语言时整条换成另一语言，从原版当前片段的开头接着读。
- 战斗台词不连排：仍按原版片段逐段替换显示，放不下时逐级缩小字号。
- 与 Python 参考实现对照的用例在 `tests/data/dialogue-paging-cases.json`：30 条（真实台词的中英文，含不同字号和强制翻页）。每条带上排版规则用到的全部中间量（字素边界和宽度、合法断行位置、每个起点的整行），以及游戏排出的行距、行界、页界和压半宽的位置。输入在同目录的 `dialogue-paging-inputs.json`，由 `tests/dialogue_cpu/paging_cases.cpp` 生成；用 `--check` 重排比对，CMake 设了 `SRW64_TEST_FONT_DIR` 时作为 `dialogue-paging-cases` 测试运行。

最新增加自动档位刻度、下一次推进进度和双框当前说话人标记，见[阅读提示与实机验证](native-reading-indicators.md)。

回看中的姓名按说话人交接在蓝色与橙色间交替，同一人连续片段保持同色；滚动回看或裁掉较旧记录不改变已有姓名颜色。

## 数据与脚本边界

- `8008C9C0` 文本加载完成后关联实际文本 ID 与脚本实例；剧情对白以每个槽位的加载世代为事件身份（一条记录一个事件），战斗台词再加上 STOP 序号。重复帧不重复入历史。
- `8008D748` 是原对话推进函数。正文缓冲为 `0x800FBAB0 + slot × 0x218`；姓名缓冲为 `0x8015CB00 + slot × 52`。读取状态、文本、姓名、定时模式、STOP 计数和当前片段，不把文本资源加载次数当作剧情进度。
- 这条原版显示路径一次绘制整个 STOP 片段，没有可直接沿用的原版逐字游标。新 UI 自己维护 Unicode 字形簇的显字进度；游戏提供片段与等待状态。组合字符和 UTF-16 代理对不会被切成半个字。
- 默认外部文本源为 `content/locales/zh-Hans.json`。通过完整 TextKey 选择 Unicode 译文，未覆盖的文本从原始 ROM 源目录回退。允许增加文字和换行，必须保留原 STOP / END 脚本屏障。动态姓名从游戏当前姓名缓冲展开。
- 新 UI 的阅读页只消费宿主输入。宿主页到达下一片段时，临时向原 `8008D748` 提交一次 A 触发确认该 `<STOP>`，并立即恢复输入字段；原版没有前进时 0.5 秒后重发。最后一页按 A 后，所有 `<STOP>` 确认完再提交 `<END>` 的那一次。STOP 增量、完成状态和后续剧情全部由原函数处理。定时对白同样先等待新 UI 的阅读页。事件记录里每次后台确认记 `guest_stop`，`<END>` 记 `guest_confirm`。
- `8009EFDC` 提供脚本实例边界；回看只暂停所属脚本的轮询。`8009FA94` 选择分支入口、脚本终止和 overlay 变更均取消跳过，防止带入下一阶段。
- 适配器同时检查世界地图／战术地图 overlay（ROM `0xA7EC0`、`0xAB160`）、姓名可见标记、坐标和 `189×61` 标准对话框，非匹配界面继续走原显示。
- 说话人姓名按姓名标签里的记录号（`+0`）取当前语言的译名；标签显示的字形与该记录日文原文不一致时（例如玩家输入的主角姓名），保持原样。回看条目按语言保存说话人，F7 切换时一起换。
- **战斗台词**（2026-09-23 加入；同日在 `battle-ui` 迷你关卡实机验证：4 句台词均为原生重绘、无原字形残留，中文与切换后的英文正文及说话人都来自语言目录与台词文件，没有 `battle_overflow`）：战斗 overlay（ROM `0x121560`）的台词经 `8008FFAC → 8008F648 → 8008CD8C` 进入同一套正文与姓名缓冲和同一个 `189×61` 框，所以也在适配范围内，但只做**显示替换**：不创建阅读事件、不接管输入、不改定时字段，原版照常推进与计时，帧数和随机数播种不变。译文不分页，一页放不下时从当前字号逐级缩小到 9；仍放不下时日志记 `battle_overflow`，每句记 `battle_quote`。底部阅读状态栏不显示，战斗台词也不进回看。

## 绘制

`native_dialogue.cpp` 从游戏线程发布不可变快照。`graphics.cpp` 只在原 `8008DC40` 对话绘制范围内移除匹配两个文本框的旧字形矩形，修改提交给 RT64 的副本，不改变 RDRAM 原始显示列表。背景、头像、框线、地图标记分别保留。

RT64 呈现 hook 暴露当前 workload ID，新文字与对应游戏帧关联，避免渲染队列延迟导致姓名／对白串帧。Core Text 在逻辑文本宽度内计算换行；Core Graphics 按最终 drawable 像素光栅化，Metal 在最终呈现时合成。窗口变化会重新生成清晰文字，不把低分辨率字图拉伸。当前仍是居中的 4:3 游戏布局；拉宽窗口不等于扩大地图视野。

## 复现与证据

`scripts/Play SRW64 Native.command` 默认开启。重看新游戏开场可运行：

```sh
.venv/bin/python tools/recomp/run/play_native.py --profile config/recomp/profiles/play-profile.json --new-game
```

选择女性超级系与默认姓名。该入口仍使用独立试玩目录及存档副本。

实时验证命令（输出目录必须是新目录）：

当前自动验证命令见[原生开发指南](../guide/native-development.md#静音验证与窗口控制)。
旧补丁 ROM 的专用运行参数已移除；下方运行目录保留为历史渲染证据。


在另一终端执行：

```sh
.venv/bin/python tools/recomp/verify/verify_native_dialogue.py build/recomp/native-dialogue/live-check
```

控制验证只向宿主提交按键和可选 SDL 窗口尺寸，不修改游戏内存。证据分开记录：`dialogue-state.json` 为 CPU 对话状态，`dialogue-events.jsonl` 为片段／确认／边界事件，`dialogue-present.json` 为实际呈现 workload，`present-*.png` 为 GPU 完成后的回读，`ui-checks/acceptance.json` 为运行检查。

静态检查：`make check`。Core Text 与阅读状态检查：`cmake --build build/recomp/gfx-build --target srw64-dialogue-test`，随后运行 `build/recomp/gfx-build/srw64-dialogue-test`。

实际运行检查位于 `build/recomp/native-dialogue/live-4/ui-checks/acceptance.json`：目标对白 `17412 / STOP 1`、18 号分页、回看暂停和滚动、确认返回不前进、自动速度回到 0、三个窗口尺寸、快进松键、开场跳过边界和战术地图第一句 `17460` 已通过。截图均为 CPU 脚本实际运行后的 GPU 回读。跳过在 VI 18056 的 overlay 切换停止，地图第一句在 VI 19364 出现并保持手动等待。

`live-3` 是前一轮检查：按键功能可见，但 resize 截图检查当时误用了较粗的 60-VI 状态采样，可能重复引用旧帧，因此窗口缩放验收采用修正后的 `live-4` 独立帧。`live-4` 的初次快进检查也曾在按键释放前取样；改为等待控制回执中的实际 VI 和持续时间后，在同一游戏进程复测通过。上述问题属于验证脚本的采样时间，不将失败断言计作成功证据。

此轮不等于全游戏所有 UI 已接管。姓名输入、选项菜单、战术／武器菜单和图片内文字仍沿用已有路径；战后分支、读档后历史分段及所有真实显示器 DPI 组合须分别增加运行覆盖。当前手柄按钮通过宿主 N64 输入状态验证；此宿主尚未接入实体手柄设备。

## 宿主退出修复

高清入口的实际启动／退出检查发现，固定版本运行时的 timer 线程使用 `detach`，主程序释放 RDRAM 并析构队列后，该线程仍可能运行。崩溃报告定位到 `timer_thread` 的队列等待。

`prepare_runtime_lifecycle.py` 从锁定且未修改的上游源码生成两个本地编译副本：以停止消息唤醒 timer 线程，并在释放 RDRAM 前 `join`。上游 checkout 保持原样；生成文件的前后 SHA-256 记录在 `build/recomp/runtime-lifecycle/manifest.json`，宿主运行报告也记录这一适配。`tests/native_timer_shutdown.cpp` 对实际适配源码测试空队列、长时间等待、周期定时器、重复关闭和 RDRAM 生命周期，30 次循环通过 AddressSanitizer / UndefinedBehaviorSanitizer。

最终高清入口启动／退出检查完成 1,704 VI、exit 0，初始 SRAM 的 SHA-256 未改变。本次使用 SDL dummy 音频设备验证进程生命周期，不作为扬声器试听证据。47 项 Python 检查、Core Text／阅读状态检查及 CPU 宿主启动退出检查通过。总交付记录为 `build/recomp/native-dialogue/acceptance.json`，区分了 22,758 VI 的实时 UI 运行、后续长文本自动等待修正的组件测试，以及最终生命周期修复后的入口检查。

复现 timer 检查：

```sh
clang++ -std=c++20 -fsanitize=address,undefined -g \
  -I build/recomp/runtime-lifecycle \
  -I build/recomp/upstream/N64ModernRuntime/ultramodern/include \
  -I build/recomp/upstream/N64ModernRuntime/thirdparty \
  -I build/recomp/upstream/N64ModernRuntime/thirdparty/concurrentqueue \
  tests/native_timer_shutdown.cpp -o build/recomp/native-dialogue/timer-shutdown-test
build/recomp/native-dialogue/timer-shutdown-test
```
