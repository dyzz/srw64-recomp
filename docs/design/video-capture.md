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
| 8 | `victory` 要等地图空闲帧，每话多等 30–60 秒 | 开场演出没放完时注入被拒 | 在精简关末尾事件追加 `3D4A`（脚本层面，`shoot_v2.py --victory-inline`）；宿主不改 |

## 用法

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

- 2026-11-01：静态完成，Mac 上编过宿主与单元测试；**没有实机验证**。要验证的顺序：① 阿克篇第 1 话镜像带 `route: 2` 直接进入，看阿克／埃尔利希头像；② 开场里 `mini_stage.load` 另一关，看是否切过去、对白阅读器是否干净；③ 带 `story_vars` 的关卡看条件台词是否出现；④ 选项框 `choice.pick`；⑤ `record.start`（Windows 上要先放一个 ffmpeg.exe）录 20 秒演出，看 `held`／`dropped` 与成片抖动。
- 地图上重进没有走淡出，是硬切；录像里不要留这一帧。
- 第三类条件（谁在场）仍靠改精简关；要是以后需要“把某几个人放到名册上”再做宿主侧的名册预置。
