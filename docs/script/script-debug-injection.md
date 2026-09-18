# 脚本注入调试：用自定义指令验证指令效果

更新：2026-09-16。本页记录原生宿主的脚本注入调试功能：把自己写的事件脚本交给原版脚本引擎在运行中的游戏里执行，用轮询跟踪、状态探针和采样帧核对每条指令的实际效果。它是[剩余指令语义确认](script-semantics-confirmation.md)第 3、4 步的工具，不是脚本写回，也不改变 ROM、目录数据或原事件。

## 宿主端机制

- 开关 `SRW64_SCRIPT_INJECT=1`；`src/host/script_inject.hpp` 由 `game_hooks.cpp` 与 `host.cpp` 调用。`run_host_probe.py` 只在日版 profile、`--graphics`、静音、有界 VI 且同时开启 `SRW64_SCRIPT_TRACE=1` 时接受这个开关，报告里写 `script_inject_enabled`。
- 请求文件 `script-inject.txt`：一行 `SRWJ1 <sequence> <at_vi> <hex>`，hex 为事件头 5 字（类型 + 四个参数）、指令序列和结尾 `FFFF`。VI 线程与 `control.txt` 同一节奏（每 6 VI）读取；序号必须递增，宿主忙时（已排队或正在执行）拒绝并记录。
- 脚本写入 RDRAM 顶部 64 KiB 暂存区 `807F0000`；应用前要求整个暂存区为零，完成后再清零。
- 空闲判定（`idle_reason`）：`engine+4 = 0xC0`、`+0x97C = 0x80`（无事件运行）、`+0x9AA = 0`（轮询阶段 0）、`8010F5E8 = 1`（我方阶段；阶段号 1 我方／2 敌方／3 第三方，关卡之间为 0）、`8010F6B0 = 0`（无败北流程）、`8015DA02 ∈ {3, 0xB}`（战术地图）。不满足时记 `deferred`，超过 1800 VI 记 `idle-timeout` 并放弃。
- 启动镜像 `8009EE98 → 8009EDB8`：`owner+0x1C` 指向暂存区第 10 字节（跳过事件头），清 `+0x22..+0x2E`、`+0x30`，`+8/+9 = FF`，`engine+0x990 = 0`，`+0x97C = 0x2000`。之后就是原版 `8009EFDC` 每帧轮询。
- 完成判定：PC 为 0 且状态 `0x80`，或 `+0x97C` 回到 `0x80`。此时恢复 `+0x98E` 登记数并清零暂存区，记录 `complete`；PC 离开暂存范围记 `escaped`；3600 VI 无进展记 `stalled`。
- 事件文件 `script-inject-events.jsonl`（schema `srw64.script-inject-event.v1`：`queued / deferred / rejected / applied / complete / escaped / stalled`，`applied` 与 `complete` 附引擎快照）。状态探针在 `script-inject-applied` 与 `script-inject-complete` 两个边界各存一份区域快照。
- 单元测试 `tests/native_script_inject.cpp`（ASan，`make recomp-script-inject-test`，并入 `recomp-native-check`）覆盖解析拒绝、空闲判定、应用、完成恢复、序号与忙碌拒绝。

## 客户端 `tools/recomp/script_lab/script_debug.py`

- `assemble`：输入 schema `srw64.debug-script.v1` 的 JSON（`{"op": "3D3B", "args": [0], "note": "…"}` 列表，可选 `event_type` 与 `header`）。参数长度只取自布局锁 `stage_scripts`，未知指令、空处理函数 `3D76/3D77`、不可达 `3D78/3D79` 和越界参数一律拒绝，输出 hex 与逐条偏移的清单。
- `inject`：写入 `script-inject.txt`，保存 `script-inject-N.json`，等待 `complete / rejected / escaped`。
- `report`：把 `SRW64_SCRIPT_TRACE` 的轮询行按实际 PC 关联到指令边界。一次轮询会先扫描条件与标记，再至多执行一条命令：命令仍在执行时 PC 停在其操作码之后，完成时 PC 停在下一边界；因此被假条件块或他人路线段跳过的命令不会被误记为已执行，列入 `commands_not_executed`。每条命令的开始／结束 VI 从 `frame-trace.rgb` 取最近采样帧存成 PNG，状态探针区域按字节比对并解码资金、变量、名册坐标。
- 固定脚本在 `config/recomp/debug-scripts/`：`fade`、`scroll`、`map`、`deploy`、`move`、`values`、`dialogue`。

## 运行证据：`build/recomp/script-debug/inject-2`

第一话（场景索引 1「出撃!スイームルグ」，マナミ路线）第 1 回合我方阶段的存档，静音，`--vis 12000`，帧采样 160×120。七个脚本依次注入，全部 `complete`，没有 `escaped` 或 `stalled`；报告 `script-debug-report.json`、事件 `script-inject-events.jsonl`、宿主日志 `build/recomp/script-debug/inject-2.native.log`。

| 序列 | 脚本 | 指令 | 结果 |
| --- | --- | --- | --- |
| 1 | fade | `3D3B` 0/1/2/3 各接 `3D38` | 四种模式各 88 VI；采样帧均值 54.6 → 0.6（黑不透明）→ 54（黑透明）→ 252.5（白不透明）→ 57（白透明）。`3D38` 45/30 计数分别 90/60 VI：每计数 2 VI。 |
| 2 | scroll | `3D35` (5,5)、`3D54`、`3D35` 0x4000／0x4103／0x4203 | `3D35` 同一轮询完成，画面在随后帧移动（绝对目标 15006/19200 像素变化）；0x4000 回到 `3D54` 记住的位置后画面复原；上 3／右 3 因视点贴边仅 85–100 像素变化。 |
| 3 | map | `3D34` 0,10,10,0 与 0,24,24,1 | 当前地图索引 `8010F5EE` 20 → 0，随机数表重建，整幅画面换成地图 0；64／42 VI。第一参数是地图编号，不是模式：`8020A874` 把它写入 `8010F5EE` 后重载资源（evidence `script_map_switch_direct`）。 |
| 4 | deploy | `3D45` 3 | 292 VI 后名册 0/2 出现 (12,21) 组 3 单位，新建机体、驾驶员（人物 204）与武器实例记录，画面中央出现我方单位。 |
| 5 | move | `3D3C` 28,(25,20)；`3D46` 503,1 | 100 VI 后名册 0/0 由 (19,5) 改为 (25,20)；`3D46` 36 VI 后名册 0/2 被移除，机体实例记录保留：退场确认。 |
| 6 | values | `3D5B` 5、`3D6C` 36,3、`3D5F`、`3E13` 7=2、`3E03` 7,2,9 块、`3E03` 7,1,9 块、`3DD4`／`3DD1`／`3DD0` 段各含 `3D5B` | 资金 0 → 8000 = (5+1+2)×1000；变量 7 由 3 写为 2；ACC = 9；变量 7=1 的块与アークライト段被跳过（`commands_not_executed`）。机体 36 改造段 +0x4C..+0x50 均为 3，武器实例同步重算；三名驾驶员 +0x20 由 100 变为 70，即气力 −30。 |
| 7 | dialogue | `3D3F`、`3D3E`、`3D48`、`3D38` | 注入文本以对白窗口显示，说话人按运行时路线解析为マナミ；两条对白分别 740／2260 VI（含等待按 A），`3D48` 立即完成。 |

序列 3 之后的实验都在地图 0 上进行；这不影响名册与数值类结论，但视点类的像素统计以当时画面为准。第一次运行 `inject-1` 因空闲判定把阶段号当成 0 起算而被拒绝（`not-player-side`），已修正为 1 起算并写入布局锁 `engine` 与 `operand_roles`。

这些结果已回填到 `config/data/original-jp-v1.json` 各指令的 `runtime` 字段（目录与查看器的指令页显示为“运行注入观察”）：`3D34`、`3D3B`、`3D46`、`3D5F`、`3D6C` 升为 `code-confirmed`，`3D34` 改名为“切换地图并卷动至位置”，`3D46` 改名为“单位退场”，`3D5F` 确认为全体气力 −30。普通指令现为 33 项 `code-confirmed`、21 项 `structure-confirmed`、19 项 `unknown`。

## 局限

- 只能在我方阶段、战术地图空闲、没有事件运行时注入；对白类指令需要通过 `control_host.py` 按键推进。
- 观察范围限于状态探针的固定区域、160×120 采样帧和脚本轮询；未被探针覆盖的内存改动不会出现在报告里。
- 注入证明的是“这条指令在这个上下文下的效果”，不能替代原脚本实例的核对；写回 ROM 或编辑原事件仍需独立的字节往返与验收。

## 复现

```sh
SRW64_SCRIPT_INJECT=1 SRW64_SCRIPT_TRACE=1 SRW64_STATE_PROBE=1 SRW64_FRAME_TRACE_FROM=2400 SRW64_FRAME_TRACE_TO=9600 \
  .venv/bin/python tools/recomp/run/run_host_probe.py --graphics --profile config/recomp/profiles/play-profile.json --language ja \
  --images original --original-name-entry --resolution-scale 2 --input config/recomp/inputs/load-continue.json \
  --save-from build/recomp/gfx-probes/female-map-audio-3/first-map-turn1.sram \
  --save-sha256 b340a9c686b627d00dfaee9b4d89c896039547f8d607cb51b035d3f7bb2d756b \
  --output build/recomp/script-debug/inject-3 --vis 12000
```

```sh
.venv/bin/python tools/recomp/script_lab/script_debug.py inject build/recomp/script-debug/inject-3 --script config/recomp/debug-scripts/fade.json
```

```sh
.venv/bin/python tools/recomp/script_lab/script_debug.py report build/recomp/script-debug/inject-3
```

每次运行使用新的输出目录；对白脚本用 `control_host.py --buttons` 按 A；结束用 `control_host.py --quit`。
