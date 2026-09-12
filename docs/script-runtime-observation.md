# 第一次静音脚本运行观察

后续进展：[男性超级系开场与 3D3C 单位移动](script-3d3c-runtime.md)。下面保留第一次运行的证据范围。

2026-09-12。实际运行原始日版 recomp，从新游戏选择女性超级系默认姓名，经第一话开场到可操作战术地图。运行约 185.8 秒、11,100 VI 后通过控制协议退出，宿主退出码 0。音频设备输出关闭，原游戏的音频任务仍正常计算。使用独立空 SRAM，没有读取或覆盖试玩历史。

## 本次证据

运行目录：`build/recomp/script-analysis/opening-1/`。ROM SHA-256 为 `ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e`。

| 检查 | 结果 |
| --- | --- |
| 实际事件 | 场景 1，开场事件 `0019C1B0` |
| 普通／对白指令 | 123 次，全部观察到完成 |
| 对白 | 58 条指令，与该开场原始顺序完全相同 |
| 指令种类 | 13 种 |
| 参数原始字节 | 123/123 匹配 |
| 参数或 PC 推进不一致 | 0 |
| 日志损坏／无法对应的轮询 | 0 / 0 |
| 自动整段对白跳过 | 未使用；只用普通 A 键推进对白 |

启动输入仅跳过前面的缩放序章文字，原姓名网格使用默认姓名；从第一句对白开始完整经过事件指令。选择 Original 图片及模型、日文语言，仍使用原生对白排版，所以这不是原硬件逐像素对照。

报告：[`acceptance.json`](../build/recomp/script-analysis/opening-1/acceptance.json)、[`script-observation.json`](../build/recomp/script-analysis/opening-1/script-observation.json)、[`report.json`](../build/recomp/script-analysis/opening-1/report.json)。

## 得到的结论

### 3D32：与世界地图位置呈现相关，仍需拆清表字段

本次参数按 `4 → 0 → 1` 执行，三个表项均与 ROM 一致。表 `801C5310` 每项三个有符号半字；参数 0 对应 `(18, -257, -1010)`，参数 1 对应 `(18, -144, -372)`，参数 4 对应 `(17, 721, -435)`。

`801C4BCC` 把后两个半字转为浮点值并传入地图定位／绘制调用；根据当时状态，它可能直接设置，也可能经过淡出和后续状态机。因此更准确的研究方向是“选择地图呈现配置并定位”，不能仅把所有表项叫作同一种镜头移动。

- 参数 0：VI 3179–3293，耗时 114 VI；后续画面为大陆内陆位置。
- 参数 1：VI 5791–5845，耗时 54 VI；后续画面为海岸／岛屿位置。
- 参数 4：在同次轮询完成。不能据此认定它无效果。

对应实机宿主帧：[参数 0 后](../build/recomp/script-analysis/opening-1/present-1680.png)、[参数 1 后](../build/recomp/script-analysis/opening-1/present-2940.png)。本次只观察原参数，没有做单参数替换；正式语义配置仍保留原置信度。

### 3D4D：观察到世界地图到战场载入的转换

该指令在 VI 7803 执行并推进 PC；下一条 `3D65` 到 VI 8415 才开始，中间出现战场载入和第一话标题画面。这说明本实例中它触发了外层流程切换，不能用“本条立即完成”推断整个画面切换也立即完成。

证据：[第一话标题帧](../build/recomp/script-analysis/opening-1/present-4140.png)。外层状态机和其他调用上下文仍需追踪，本次不升级为全局语义确认。

### 3D45：三个部署组都由开场事件执行

组 0、1、2 分别在 VI 8423–8845、8849–9141、9465–9777 执行；实际画面出现敌我单位，之后可以选择我方单位。证据：[登场后的地图](../build/recomp/script-analysis/opening-1/present-4800.png)、[单位选择范围](../build/recomp/script-analysis/opening-1/present-5220.png)。

三次调用都位于 `0019C1B0` 开场事件，本次没有执行独立的类型 13 事件 `0019C3A0`。因此剧情查看器的“初期配置”分类不代表新游戏中一定按这个顺序额外执行；C2 的进入路径仍待确认。

### 仍没有得到的证据

本段没有 `3D3C`、`3D36`、`3D55`，不能据本次试玩确认它们。没有完成战斗、击破触发、关卡通关，也没有验证写回或 Mod。下一次可选择包含 `3D3C` 的男性超级系开场，复用记录器分析原值，然后再做隔离的单参数对照。

## 记录器与复现

`SRW64_SCRIPT_TRACE=1` 在现有 `8009EFDC` 包装处开启只读轮询边界记录，默认关闭。日志进入 `RUN.native.log`，以 `SRW64_SCRIPT_TRACE` 开头。`tools/recomp/analyze_script_trace.py` 使用场景运行时入口、原始指令边界、handler 和参数字节作唯一匹配，输出实际经过的普通／对白指令。

记录器不拦截每条内部条件函数；一次轮询可能扫描多个条件，再执行一个普通指令。分析器不会把扫描越过的内容宣称为实际执行。指令耗时使用宿主 VI，不直接当成原脚本等待参数。

```sh
SRW64_SCRIPT_TRACE=1 .venv/bin/python -B tools/recomp/run_host_probe.py \
  --graphics --profile config/recomp/play-profile.json --language ja \
  --images original --original-name-entry --resolution-scale 2 \
  --input build/recomp/script-analysis/opening-input.json \
  --output build/recomp/script-analysis/opening-next --vis 14500
.venv/bin/python -B tools/recomp/analyze_script_trace.py \
  build/recomp/script-analysis/opening-next
```

不传 `--audio` 即关闭设备输出。运行目录必须不存在，按键脚本及其摘要已保存在本次证据中。

**记录版本边界：** 本次完整运行使用 `script-poll.v1`，PC、handler、状态、参数正常，但辅助回合只捕获高字节、阶段捕获原始 32 位值、阵营数捕获相邻原始半字。本次验收不使用这些辅助字段。最终 `v2` 修正了字段宽度并锁定完整日志行，已通过 ASan/UBSan 的端序、字段宽度、边界和无写入测试及宿主编译；本次完整游戏日志仍保留 v1，不冒充 v2 的运行证据。

退出码只说明本次正常结束，不替代已有的线程退出生命周期问题验收。
