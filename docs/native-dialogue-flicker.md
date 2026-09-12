# 剧情对白间歇闪烁修复

日期：2026-09-11。用户报告高清模式下剧情对白会间歇闪一下。本次后续回归均关闭声音。

## 实际发现

连续 GPU 回读发现两类问题，单张截图或每隔 60 帧采样不能充分覆盖：

1. **偶尔混入旧对白帧。** 静止在 `base:t00_17412` segment 1，并切换图像模式后，某些帧突然出现原版日文字形、原版对白布局，下一帧恢复中文原生界面。地图和头像仍是高清。修复前的连续记录中发现 24 次非预期画面跳变，对应 12 个短暂旧对白帧的进入/退出。
2. **正常试玩携带周期性重录制。** 旧宿主每 60 个呈现帧在 GPU 完成回调中编码 PNG，每 120 个图形任务导出 8 MiB 内存。高清静止场景中，截图后的间隔平均 4.87 VI、最大 5 VI（约 81–83 ms）；其他帧平均约 2 VI（33 ms）。

修复前的样例：[旧对白闪入帧](../build/recomp/flicker-check/switch-before-1/present-4023.png)、[下一帧恢复中文](../build/recomp/flicker-check/switch-before-1/present-4024.png)。这轮抓到的是对白布局交替，并未抓到整幅黑帧。

## 修改

### 保留另一个显示列表的对白快照

原游戏可以提前准备 A、B 两个显示列表缓冲。原先 `take_frame()` 取走 A 后执行 `drawings.clear()`，会连 B 已准备好的快照一起删除。渲染 B 时便回到原版字形路径，形成闪帧。

现在使用 `src/native/presentation/display_list_snapshots.hpp` 管理有界快照队列，仅删除当前任务匹配的记录；场景失效时才整体清空。队列由现有对白 mutex 同步，呈现端继续按实际 workload 查找，不从最新游戏状态猜一帧文字来遮盖问题。

C++ 回归覆盖：先发布 A、B，再消费 A，B 必须仍可读取；每份快照只能消费一次；不匹配的任务不消费其他快照；切场景使待处理快照失效。

### 普通试玩使用轻量诊断

`run_host_probe.py` 的 `--interactive` 默认使用 `--diagnostics light`，因此所有现有试玩启动器都会受益。轻量模式关闭周期性 GPU 截图、8 MiB 内存导出和逐帧对白 JSON 落盘，保留原生对白绘制、图像切换和游戏保存。

有界探针仍默认 `--diagnostics full`，维持已有截图/回放验收流程；也可显式选择。报告记录诊断模式，轻量运行没有截图时不再被误报为渲染失败。

## 连续帧复核

可用 `SRW64_FRAME_TRACE_FROM` 和 `SRW64_FRAME_TRACE_TO` 指定 VI 范围。范围内每个完成的 GPU 帧记录 160×120 RGB 样本、workload、图像模式和相邻帧变化；明显变化再保存完整 PNG。此诊断默认关闭，不进入正常玩家路径。

```sh
# 静音：不传 --audio。
SRW64_BACKGROUND=1 SRW64_WINDOW_CONTROL=1 \
SRW64_FRAME_TRACE_FROM=7000 SRW64_FRAME_TRACE_TO=10800 \
.venv/bin/python tools/recomp/run_host_probe.py --graphics \
  --diagnostics light --profile config/recomp/play-profile.json \
  --input build/hd-ai/dialogue-polish/dialogue-only.json \
  --output build/recomp/flicker-check/new-run --vis 11000

.venv/bin/python tools/recomp/analyze_frame_trace.py \
  build/recomp/flicker-check/new-run --from-vi 7100 --require-stable
```

分析仅适用于上述静止对白：允许水滴转动，排除主动切图的那一帧。不能用它把正常的剧情转场认作闪烁。

证据目录 `build/recomp/flicker-check/` 保留修复前、仅关闭重录制、快照修复后的完整诊断与轻量诊断运行。结果见下方验收记录。

## 本次验收

以下为原版日文 ROM、中文原生对白、原生水滴模型的实际 RT64/Metal 游戏运行，不是固定显示列表回放。两组最终运行均关闭音频输出，各运行 11000 VI；在静止对白期间主动切换原图／高清四次。

| 运行 | 连续完成帧 | 非预期画面跳变 | 结果 |
| --- | ---: | ---: | --- |
| 修复前，完整诊断，切换图像 | 2144 | 24 | 12 个旧对白闪入帧及其恢复 |
| 修复后，完整诊断 | 1823 | 0 | 较重录制负载下未再混入旧对白 |
| 修复后，轻量诊断 | 1846 | 0 | 正常玩家配置及切图检查通过 |

最终轻量运行在最后一次切回高清并稳定后（VI 9500 起），650 个连续帧的 649 个间隔全部为 2 VI，约 33 ms。完整诊断仍保留录制成本，不能把它的帧时间作为普通游玩性能。切图瞬间的资源更新、诊断追踪主动保存变化截图也不计入稳定场景性能结论。

另外，以 `--interactive` 启动而不传 `--diagnostics` 的静音实跑确认默认选用 `light`，正常退出，且没有周期性 PNG 或 8 MiB 内存文件。`make check` 的 60 项 Python 测试、编译检查和依赖检查通过；`make recomp-content-test` 的快照队列回归及 Core Text／阅读控制检查通过。

汇总、源文件与证据哈希见 [acceptance.json](../build/recomp/flicker-check/acceptance.json)。这证明了本次复现的对白闪帧和周期性录制停顿已修复；验收范围是第一关这段对白及图像切换，不代表全部剧情或所有类型的黑屏／闪烁都已覆盖。

## 自动播放时底部状态栏消失

同日后续反馈定位到底部“自动 3／字号 13／操作提示”栏。前面的静止对白检查没有覆盖自动换人的交接状态。

实际复现发现：原游戏确认一句并切换说话人时，两个对白框可以暂时都处于非活动状态，但仍然显示在画面中。原生底栏在 `native_dialogue_text.cpp` 的 `if (box.active)` 内绘制，因此会跟随活动说话人的暂时空缺消失，再随下一句出现。这与前面的快照队列丢帧是不同的触发条件。

底栏现改为双框共用，每帧绘制一次；只要匹配当前 GPU workload 的原生对白框仍可见，就显示阅读模式、字号和操作提示。活动对白仍决定正文亮度和页码。对白框完全关闭时底栏也关闭，没有延时补帧或沿用其他 workload 的文字。

使用同一输入脚本、同一原版 ROM 和高清配置，以自动速度 3 进行静音实跑，前后各运行 11400 VI：

| 检查 | 修复前 | 修复后 |
| --- | ---: | ---: |
| 同一段对白，VI 7200–8580 | 669 个完成帧 | 685 个完成帧 |
| 底栏意外消失 | 8 次，各一帧 | 0 次 |
| 扩展检查，VI 7200–11200 | — | 1959 个完成帧 |
| 有对白且没有活动说话人的帧 | — | 109 帧，底栏均保留 |
| 对白框已经关闭的帧 | — | 73 帧，底栏均隐藏 |

前后的完成帧数量不同，是因为旧版闪烁会额外触发诊断 PNG 保存。扩展检查逐帧将实际 GPU 像素中的底栏与该 workload 的对白可见状态对照，没有不一致项。检测同时检查底栏的暗色底板和提示文字，避免把整幅黑色过场误认成底栏。

修复前的消失帧见 [present-3628.png](../build/recomp/auto-toolbar-check/before/present-3628.png)；修复后同一 VI、双方暂时非活动时的 GPU 采样见 [handoff-3627-sample.png](../build/recomp/auto-toolbar-check/after/handoff-3627-sample.png)。后者是连续回读的 160×120 样本，不是原生分辨率截图。

复核命令：

```sh
.venv/bin/python tools/recomp/analyze_toolbar_trace.py \
  build/recomp/auto-toolbar-check/after --to-vi 8580 --require-visible
.venv/bin/python tools/recomp/analyze_toolbar_trace.py \
  build/recomp/auto-toolbar-check/after --require-matching-dialogue \
  --output build/recomp/auto-toolbar-check/after/toolbar-boundaries.json
```

本次 `make check` 的 60 项 Python 测试及 `make recomp-content-test` 通过。运行配置、输入和证据哈希见 [底栏修复验收](../build/recomp/auto-toolbar-check/acceptance.json)。
