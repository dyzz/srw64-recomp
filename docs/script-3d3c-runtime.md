# 3D3C：男性超级系开场的静音运行观察

后续进展见[单参数对照实验](script-3d3c-experiment.md)。本文保留原参数观察时的证据边界。

2026-09-12。原始日版 recomp 实际运行约 154.9 秒，从男性超级系默认姓名进入第一话，经过完整开场后到达可操作战术地图。通过控制协议在 VI 9252 退出，退出码 0，未留下游戏测试进程。音频设备输出关闭，音频任务仍计算；使用独立空 SRAM、Original 图片／模型、日文及原生对白排版。本次未修改 ROM 或脚本参数。

## 运行证据

目录：`build/recomp/script-analysis/male-opening-2/`。ROM SHA-256：`ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e`。

| 检查 | 结果 |
| --- | --- |
| 场景／路线 | 0／`3DD3`，男性超级系 |
| 实际事件 | `base:stage_events:0019bf10` |
| 普通／对白指令 | 56 次，全部完成；顺序与整个开场事件一致 |
| 对白指令／指令种类 | 18／17 |
| 原参数字节 | 56/56 匹配 |
| PC 或参数不一致／无法匹配的轮询／坏日志 | 0／0／0 |
| 记录器版本 | `srw64.script-poll.v2`，103 条轮询边界记录 |
| 音频输出 | `false` |

完整报告：[验收](../build/recomp/script-analysis/male-opening-2/acceptance.json)、[指令对应](../build/recomp/script-analysis/male-opening-2/script-observation.json)、[宿主报告](../build/recomp/script-analysis/male-opening-2/report.json)。最终画面：[战术地图的单位选择](../build/recomp/script-analysis/male-opening-2/present-4615.png)。按键只跳过开场缩放文字，事件对白以普通 A 键推进。

## 参数含义的修正

`3D3C` 原标注“地图位置效果（类型, 位置）”，现在改为 **“单位移动至位置（驾驶员, 位置）”**，`unknown → structure-confirmed`。第一参数关联驾驶员档案。

机器码链路：

1. 常驻 `800A0360` 关闭窗口，用 `800A38DC` 解码第二参数，再调用战场 overlay 的 `8020A030(x, y, 第一参数)`。
2. `8020A030` 遍历 `8015E100` 的三个阵营、每阵营 30 槽，槽步长 `0x14`、阵营步长 `0x258`。有效槽的 `+0xC` 指向单位，单位 `+0x38` 指向驾驶员，比较驾驶员 `+2` 的编号。取遍历顺序中的首个匹配项。因此第一参数是驾驶员编号。
3. 目标格坐标转为 `格坐标 × 16 + 32`。状态表 `8021E230` 依次指向横向移动 `8020A4BC`、纵向移动 `8020A5F0`、收尾 `8020A724` 及空处理。横纵移动使用带符号的 8 单位步进；不能直接换算成原游戏“每帧几格”，因为需要区分 VI、轮询和绘制频率。
4. `8020A788` 随精灵位置更新镜头，在状态 3 返回完成。常驻 handler 才把参数 PC 向前推进 4 字节。

证据窗口 `script_actor_movement` 锁定 `load_000AB160:8020A030..8020A874` 的原 ROM 字节与 SHA-256；目录中的指令证据链接已关联这一窗口。回归测试独立核对 ROM 的单位→驾驶员→编号加载指令，并检查五个真实脚本实例的角色引用与位置解码。

## 五次原参数调用

偏移相对事件开头；格坐标是脚本原始值，不是屏幕像素。

| 事件偏移 | 驾驶员编号 | 目标位置 | VI 起止 | 耗时 VI |
| --- | --- | --- | --- | --- |
| `006C` | 298 | `1508` → (21, 8) | 5668–5700 | 32 |
| `00BE` | 27 ブラッド | `191C` → (25, 28) | 6932–6958 | 26 |
| `00C8` | 27 ブラッド | `1912` → (25, 18) | 6994–7054 | 60 |
| `00DE` | 31 カーツ | `171C` → (23, 28) | 7560–7586 | 26 |
| `00E8` | 31 カーツ | `1713` → (23, 19) | 7622–7678 | 56 |

GPU 连续帧确实出现对应单位的移动与镜头跟随。以下接触表从 `frame-trace.rgb` 按 `frame-trace.jsonl` 的记录顺序取帧，每帧 160×120 RGB；标题为真实 VI。它们用于对齐行为，不替代逻辑单位坐标的内存验收。

![布拉德第二段移动](../build/recomp/script-analysis/male-opening-2/move-2-27.png)

![卡兹第二段移动](../build/recomp/script-analysis/male-opening-2/move-4-31.png)

其他实例：[298](../build/recomp/script-analysis/male-opening-2/move-0-298.png)、[布拉德第一段](../build/recomp/script-analysis/male-opening-2/move-1-27.png)、[卡兹第一段](../build/recomp/script-analysis/male-opening-2/move-3-31.png)。

同段还执行了两次 `3D36(2)`，分别为 VI 3946–3964、4142–4160，各等待 18 VI；这只确认执行与耗时，没有确认该参数的完整演出语义，置信度保持未知。

## 尚未验收与下一次对照

本次没有完成战斗、击破触发或通关，也未观察内部条件指令。普通绝对坐标的移动有代码与画面相互支持；以下仍待确认：相对位置及修正分支、多个有效单位共用驾驶员编号、找不到目标、越界位置、移动完成后的逻辑坐标与占格状态、存档写回。

下一次最小对照已经可以固定：只改事件 `0019BF10 + 00C8` 的第二参数 `1912 → 1911`，即布拉德第二段目标从 (25,18) 改到 (25,17)，保留驾驶员 27 与其他所有字节。参数原 ROM 地址为 `0019BFDC`；本次场景的运行时参数地址为 `8019B4CC`，必须重新核对加载身份后才能覆盖，不能把运行时地址泛用到其他场景。

预期只改变这段移动的终点和时长。对照需同时记录精灵位置、逻辑单位坐标、占格状态与后续卡兹移动，并明确把变更字节登记为实验，不能让原值一致性检查静默放行。本次仅确定实验设计，没有执行参数替换。

## 复现

```sh
SRW64_SCRIPT_TRACE=1 SRW64_FRAME_TRACE_FROM=3600 SRW64_FRAME_TRACE_TO=12000 \
.venv/bin/python -B tools/recomp/run_host_probe.py \
  --graphics --profile config/recomp/play-profile.json --language ja \
  --images original --original-name-entry --resolution-scale 2 \
  --input build/recomp/script-analysis/male-opening-input.json \
  --output build/recomp/script-analysis/male-opening-next --vis 14500
.venv/bin/python -B tools/recomp/analyze_script_trace.py \
  build/recomp/script-analysis/male-opening-next
```

不传 `--audio`。输入文件摘要在宿主报告中；输出目录须不存在。到战术地图后用 `control_host.py RUN --quit` 结束，避免后续 A 键继续选择单位。初次尝试 `male-opening-1` 因 `--vis 13000` 小于输入事件范围而在启动游戏前拒绝；本报告只使用随后成功的 `male-opening-2`，没有混合两次日志。

验证：`PYTHONDONTWRITEBYTECODE=1 make check`，97 项测试通过，编译检查及依赖检查通过；原始目录重新生成，82 个证据项、全部引用可解析。
