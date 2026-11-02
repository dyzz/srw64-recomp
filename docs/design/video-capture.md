# 剧情视频录制支持（video-capture 分支）

2026-11-01。四位主角剧情解析视频要从游戏里截几百张对白画面、录几十段战斗演出。第一轮在 Windows 台式机上用 `shoot.py` 拍阿克篇时遇到六个难点（原话见 `local/hero-story-videos/review-revised-20261101.md` 后的报告），这个分支专门解决它们。改动都在调试接口和迷你关卡这两个只有开发者碰得到的地方，普通游玩路径不变；分支可以一直留着，不必并回 main。

## 难点与对策

| # | 难点 | 原因 | 对策（本分支） |
| --- | --- | --- | --- |
| 1 | 直接进关时主角头像花屏 | 迷你关卡的直接进入跳过姓名页，路线变量 `801602EE` 没写；头像绘制 `8008F970` 用它把人物 25（主角）／29（搭档）换成各路线的人（`− 0x3DB8`／`− 0x3DB4`），为 0 时查到垃圾 | 镜像带 `route`，`direct_scene` 写路线变量，并照姓名页的确认从 ROM 默认名表写两人的四组姓名字段（`names::write_route_defaults`） |
| 2 | 发布版不支持旧入口；要本地改启动器保留 `SRW64_MINI_STAGE_DIRECT`／`_ROUTE` | 旧入口（新游戏 → 选主角）只为了初始化头像 | 难点 1 解决后不再需要旧入口和那两个环境变量；台式机上的两处本地改动可以丢掉 |
| 3 | 回不到标题，每话重启游戏 40 秒 | F10 的回标题只在地图有 UI 的帧接受；原版 Z+START 在剧情中不生效 | `mini_stage.load` 在地图上也接受：下一帧按原版回标题的收尾（`801DFDBC`：`800836CC(0)`）加标题出口的状态重置 `800A5138`，直接切到新关卡，不经标题 |
| 4 | 台词受剧情变量、以前的选项、谁在场控制 | 新游戏进来这些都是空的 | 前两类：镜像的 `story_vars`／`last_choice` 在场景登记时写入，早于开场事件；第三类（谁在场）仍只能把精简关里的条件块改成无条件（`shoot.py` 已这么做） |
| 5 | 选项框读不到状态，只能定时盲按 | 选项框不在对白阅读器状态里 | `status.choice`（选项数、文本号、光标、阶段）＋ `choice.pick {index}` |
| 6 | 只能截图，不能录像：原始帧几百 MB 要逐帧拉回 Mac | 录像把原始 RGB 写盘，由客户端编码 | 宿主有 ffmpeg 时自己编码（`record.start` 的 `ffmpeg`／`SRW64_FFMPEG`／PATH），只回传一个 MP4；帧按呈现时的 VI 排时间线，`lockstep` 让编码慢只拖慢游戏不丢帧，顺带修掉“录像掉帧致抖动混叠”那个待办 |
| 7 | 每句对白要等提示条淡出约 5 秒 | 对白出现时操作提示条显示 300 VI | `settings {dialogue_hints: "off"}`，提示条始终不画（不存设置） |
| 8 | `victory` 要等地图空闲帧，每话多等 30–60 秒 | 开场演出没放完时注入被拒 | 在精简关末尾事件追加 `3D4A`（脚本层面，`shoot_v2.py` 的 `victory`）；宿主不改 |
| 9 | 要录“选择的那一刻”，选项框得能随时调出来 | 选项框只在剧本走到 `3D44` 时出现 | `choice.show {text_id, options, lines}`：用脚本注入在下一个空闲帧跑几句对白＋一条 `3D44`，再 `choice.pick` 作答（2026-11-01 用户要求）；精简关里也可以直接保留原版的 `3D44`，计划的 `choices` 给答案 |
| 10 | 每集几十张画面要人工拼计划 | — | `make_plan.py` 从四集文案的截图清单生成每话一个迷你关的计划（目标台词、路线、是否需要过关、要答的选项框、演出清单），`shoot_v2.py` 据此编精简关 |

## 用法

0. 计划：`make_plan.py` 读 `N-*.md` 的截图清单，每话一个 run（`plan-<集>.gen.json`）；人工只改 `choices`（每个 `3D44` 选第几项）和偶尔的 `vars`。
1. 关卡：`original_stage.py` 取原场景，`shoot_v2.py` 精简（只留要拍的台词、条件块改无条件），写入 `route`、`story_vars`、`last_choice`，`mini_stage.py compile` 得镜像。
2. 进关：第一关在标题主菜单 `mini_stage.load`；之后每一关在地图上直接 `mini_stage.load`，等 `mini_stage.applied` 增加即进了新关。整局游戏只启动一次。
3. 拍：`settings {dialogue_hints:"off", game_speed:1}`；按 `status.dialogue` 的 `text_id` 对目标台词截图；`status.choice.active` 时 `choice.pick`。
4. 录：`record.start {width:1920, fps:30, ffmpeg:"D:\\...\\ffmpeg.exe"}` … `record.stop` → 回 `mp4`，`Session.record_stop` 只拉这一个文件。演出录像前把游戏速度放回原速。

调试接口各方法的参数见 [debug-interface.md](../guide/debug-interface.md)，镜像键见 [mini-stage.md](../script/mini-stage.md) 末节。

## 代码

- `src/host/mini_stage.hpp`：`Image::route/story_vars/last_choice`、`parse`、`register_hook`（变量与选择）、`direct_scene`（路线与姓名）、`restartable_mode`／`take_restart`、`load_file` 地图上的重进。
- `src/host/native_name_entry.cpp`：`write_route_defaults`（ROM `0x10D6A0`／`0x10D710` 的默认名表 → `8010F5F8` 起四组字段），经 `mini_stage::route_names` 钩子挂上，单元测试不依赖它。
- `src/host/game_hooks.cpp`：帧边界上 `take_restart` 与直接进入共用一套交接；`srw64_overlay_loading()`（host.cpp）挡住 overlay 装载中的那几帧。
- `src/host/debug_server.cpp`：`choice_state`／`choice.pick`、`settings.dialogue_hints`、`record.start` 的新参数、`mini_stage.load` 回应 `restart`。
- `src/host/debug_protocol.hpp`：`Recording` 重写——GPU 回调只拷贝入队，工作线程缩放、按 VI 排时间线、喂 ffmpeg；`behind()` 给 VI 线程做背压（`host.cpp on_vi`）。
- `src/host/native_dialogue.cpp`：`set_hints_hidden`。
- `src/native/app/launch.cpp`：启动器放行 `SRW64_FFMPEG`。
- `tools/recomp/script_lab/mini_stage.py`：新键校验与透传；`tools/recomp/debug/session.py`、`mcp_server.py`：录像参数、`mp4` 回传、`choice_active` 等待条件、`srw64_choice_pick`。
- 测试：`tests/native_mini_stage.cpp`、`tests/test_mini_stage.py`。

## 状态

- 2026-11-01：静态完成，Mac 上编过宿主与单元测试。
- 2026-11-02：Win 台式机实机（a6406234，D:\srw64-video\game，`deploy-v2.sh` 部署）全部通过：
  - 阿克篇第 1 话带 `route: 2` 直接进入：路线变量 3DD1，两人姓名四组字段写入，阿克／埃尔利希头像正确，提示条不画（`dialogue_hints off`）。
  - 地图上 `mini_stage.load` 换关 0.8 秒进下一话；第 43 话 `story_vars {18: 2}` 控制的台词出现；第 10 话选项框由 `choice.pick` 作答（答案 3DDA）；空闲地图上 `choice.show` 调出第 10 话的选项框并作答。
  - 录像（宿主端 ffmpeg 9.0.2，1920×1080、30 fps）：对白片段 40 s 1200 帧，重复帧 0.8%；鉴赏战斗 17.4 s 521 帧，重复帧 2.9%（含淡入）；标题 10 s 整 300 帧、0 补帧 0 丢帧；音画同长；录像时呈现率仍是 30/s，与不录时相同。`encode:false` 回落（原始帧由客户端编码）也通过。
  - 一局游戏连拍三话（`shoot_v2.py` 的方式）用时：第 2 话 67 s（4 张）、第 12 话 59 s、第 43 话 83 s、第 10 话 28 s。
- 实机踩到并已改的：
  - 换关只能在稳定模式做（战术地图 3/B/11/16/22、场间 4/12–15、世界地图 D）；在模式 C（场景装载中）换关会登记场景但开场永远不跑。宿主已排除 C，`shoot_v2.enter` 等稳定模式，等不到就 `title.return` 走标题。
  - 鉴赏战斗跑在模式 0x1C（标题演示战斗），结束看 `status.battle_viewer.running` 转否；`returned` 只亮一帧。
  - 从地图回标题要在原版接受 Z+START 的帧（`status.quick_load.usable_title`），事件跑着时不接受；`rec_v2.to_title` 轮询到可用再发。
  - 台式机的源码副本没有反汇编扫描（cpu-scan），`generate_cpu` 跑不了：`deploy-v2.sh` 改为把 Mac worktree 为本分支生成的 `build/recomp/cpu-bound` 打包传过去；ROM 也要拷进 src。
  - `small` 是 windows.h 的宏，不能做标识符。
- 本地工具（`local/hero-story-videos/win/`，不进仓库）：`make_plan.py` 生成每集计划；`shoot_v2.py` 一局连拍；`rec_v2.py` 一条命令录鉴赏片段（`viewer` 规格）或关卡片段（`stage` 规格：对白出现／战斗开始时开录，按阅读节奏翻页、答选项，读完或战斗结束 2 秒后停录）或手动驱动时自动起止（`--arm`）；`analyze_clip.py` 看成片的帧数与重复帧；`verify_v2.py`、`fallback_test.py` 是这次的验证脚本。
- 2026-11-02 下午，阿克篇整篇实拍（`shoot_v2.py plan-1-ark.v2.json`，23 个 run、132 张，全部到手；`rec_v2.py` 录得演2／演4／演6 三段）中确定的做法：
  - 精简关里去掉出击选择（`3D3D`／`3D59`）：有可选机体时那个画面要玩家操作，脚本过不去（第 17 话）。
  - 同一话里几条都设成「回合 1 触发」的事件只跑第一条，其余等一个没人结束的回合：拍摄脚本把它们合并成一条事件顺序执行（第 56 话）。
  - 过关指令 `3D4A` 只接在地图事件末尾；目标句只在开场里时改用调试命令 `victory` 在空闲帧发（第 140 话）。
  - 就地换关只在战术地图空闲（引擎无事件）时接；结算后的场间走原版 Z+START 回标题（8 秒）；拍完后游戏自己跳进下一场景开场时，快进到可复位的点再回（1–2 分钟）。`title.reset` 这种强制交接从剧情里发出只会把模式写成 1 而没有后文，从开机的标题演示里发出会让标题进不了关——别用，留着只为空闲帧。
  - 鉴赏战斗跑在模式 0x1C，结束看 `battle_viewer.running` 转否；战斗后停在鉴赏页，要按 B 退出再回标题菜单。
  - 第 2 话的首战不是脚本战斗，和 S41、S56 的演出一样要玩家出手（`rec_v2.py --arm`）。
  - 一次崩溃待查：第 43 话舰内事件刚结束时用 Z+START 回标题，`Failed to find function at 0x801C5138`（D:\srw64-video\data 下 crash-20261102-115936.dmp）。
- 2026-11-02 傍晚，需要真打一场的三段（演1 首战、演3 S41 对埃尔利希、演5 S56）也由精简关自动完成（`rec_v2.py` 的 `stage.attack`）：
  - 关卡：`deployments` 往原版部署块里加一条阿克的记录（组号取开场部署的组，位置贴着目标，`level_offset` 60 保证命中），`keep_events` 保留部署目标的事件，`keep_types [2,4,5,9]` 让战斗类事件保留原触发头（类型 9 是 説得：相邻＋变量门槛才出现在菜单里，`[25, 29, 9, 0]` 即阿克对埃尔利希、变量 9 ≥ 0）。
  - 出手：等脚本引擎在事件之间、地图 UI 空闲（`0x80172EB0` 为 5）→ 写光标到阿克格、A 开单位菜单（移动／说服／攻击／精神／能力）→ 说服走完对话（不消耗行动）→ 再开菜单选攻击 → 武器列表（状态 0x17）逐件试：A 选中后第二字节变 0x17 进入选目标，目标光标自由移动，落在目标格按 A，战前确认页（原生页，`pad a`）出现才算这件武器够得着 → 开战。
  - 台词：战斗台词在 `dialogue-events.jsonl` 里记 `battle_quote`（状态里看不到）；通用攻击台词 16 选 1，`quote.force {text_id}` 让下一次通用抽取改说指定记录：包装通用抽取 `80222B14(record, slot, context)`，按 ROM 段表 `0x1161C0`（每声部 36 字节、九对起止）核对该记录属于这位说话人这个情境，属于才写进槽位（一次性；槽里已有的条件台词也让位）。说话人与情境都由记录本身决定，不用再传槽位。第一版包装 `8022245C` 答 1000 时替换行不通：那一步之后通用抽取还会再抽一次并以三分之二概率顶掉。
  - S56：说服后再攻击埃尔利希那一格宿主必崩（3 次，`Marchwind64.exe+0x9A948E`，dump 13:17／13:22／13:27），推断说服后他已退场；演5 以说服对话收尾。
  - 第 2 话的首战不是脚本战斗（开场里的 3D49 另有用途），由阿克用第 4 件武器打 6 格外的无人机。
- 待办：第三类条件（谁在场）仍靠改精简关；地图上换关是硬切没有淡出，录像里不要留那一帧；`演1` 这类「首战演出」要有人操作攻击（`rec_v2.py --arm RUN out.mp4 --start battle` 已能在攻击时自动起止）。
