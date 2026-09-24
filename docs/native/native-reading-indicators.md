# 自动阅读档位、推进进度与双框焦点

日期：2026-09-11。参考用户提供的《机战 Z》截图，把自动档位和推进进度加入原生双对话框。

## 游戏中的表现

- 底栏显示“自动 3/4”及四段刻度，点亮数量与当前档位一致。自动上限为 4，手动模式显示“手动”，刻度熄灭。
- 当前说话的对话框增加青色边角（2026-09-24 起为圆角，与高清边框一致）和姓名左侧三角标记；姓名更亮，另一框的姓名和正文降低亮度。标记同时提供形状与颜色区别。
- 当前框顶部的细条表示距离下一次自动推进的进度：显字时为青色，正文显示完后的等待阶段为橙色，填满后推进。长文本先进入下一阅读页，最后一页才交回原游戏进入下一片段。2026-09-23 起不再显示「1/2」这类页码：续页和原版自己的翻页一样，按 A 接着显示。
- 回看时暂停阅读计时，返回后继续；手动、快进或跳过时不显示自动倒计时条。
- 回看中的姓名在换人时按蓝色／橙色交替，同一人连续片段保持同色。颜色在写入回看记录时确定，滚动或淘汰较旧记录不会让已有姓名变色；正文保持白色。
- 换人交接时，如果两个框暂时都非活动，标记保留在刚读完且仍可见的框上，下一位开始后切换。对白框关闭或脚本失效后不保留标记。

操作沿用现有按键：↑↓ 调档，X 恢复手动，Z 下一阅读页，Q 回看，I/K 改字号，E+Z 快进，E+Enter 跳过当前段。日文和中文沿用各自外部 UI 词条；没有把新增界面文字写死在中文分支。

## 实际截图

以下为最初八档版本的实机截图。根据后续反馈，当前已收为四档；保留原先 1–4 档的速度，数字、刻度和按键上限统一使用同一个档位常量。

上框说话：

![上框自动阅读](../../build/recomp/reading-indicators/zh-full/present-3600.png)

下框说话：

![下框自动阅读](../../build/recomp/reading-indicators/zh-full/present-3660.png)

日文、原图、18 号正文和分页：[实机截图](../../build/recomp/reading-indicators/ja-full/present-3780.png)。中文、高清、18 号正文和分页：[实机截图](../../build/recomp/reading-indicators/zh-full/present-5220.png)。均为 GPU 完成后的回读，非示意图。

## 状态与计时

`dialogue_model.hpp` 的 `page_timing()` 同时供实际自动确认和进度显示使用，沿用原来的显字速度及等待时长公式，避免另建一个与剧情推进脱节的动画计时器。进度按阅读器已经处理的 tick 计算，夹在 0–1000；回看会暂停这条时间线，新阅读页重置，等待游戏接收确认时保持满格。

游戏线程把阅读事件、进度和界面状态加入不可变 `Frame`。呈现仍使用对应 RT64 workload 的快照。`focused_box()` 优先选择唯一活动框；没有活动框时，只能选择该帧内与最后阅读事件相符、仍可见的框。不会向别的 workload 借用说话人或文字。

进度条利用原对话框顶部空间，未缩小姓名宽度或正文排版区域。底栏继续按整个对白界面的可见性显示，保留此前的[自动播放底栏闪烁修复](native-dialogue-flicker.md)。

## 验证

两组均为原版日文 ROM 的实际 RT64/Metal 执行，各运行 11800 VI，关闭声音：

| 检查 | 中文 / 高清，13→18 号 | 日文 / 原图，18 号 |
| --- | ---: | ---: |
| 连续 GPU 完成帧 | 2206 | 2300 |
| 底栏档位像素检查 | 2135 | 2300 |
| 当前说话人三角标记像素检查 | 1955 | 2120 |
| 推进条填充像素检查 | 1756 | 1919 |
| 回看暂停帧 | 180 | 180 |
| 没有活动框的交接帧 | 107 | 10 |
| 检查失败 | 0 | 0 |

上述初版两组都经过当时的 0–8 全部档位、上下两个说话框、显字／等待、手动恢复、回看、长文本分页。回看期间事件、阅读页和进度值保持不变。中文组另有 71 帧对白关闭，底栏正常隐藏；仍可见的对白帧中，底栏没有意外消失。四档调整后另行完成宿主构建和现有阅读组件检查；初版运行证据保留原状。

`verify_reading_indicators.py` 从连续 GPU 像素读取点亮刻度、姓名三角和进度条长度，与该 workload 的状态逐帧对照。回看覆盖对话框时不检查被遮住的三角和进度条；底栏仍检查。端点允许一个低分辨率采样像素的抗锯齿误差。

`make check` 的 60 项 Python 测试、编译和依赖检查通过；`make recomp-content-test` 通过。新增组件回归覆盖实际自动截止时间、进度单调与满格、回看冻结、手动／快进隐藏、分页清零、交接焦点、双活动框歧义及关闭框。

复核：

```sh
.venv/bin/python tools/recomp/verify/verify_reading_indicators.py build/recomp/reading-indicators/zh-full
.venv/bin/python tools/recomp/verify/verify_reading_indicators.py build/recomp/reading-indicators/ja-full
.venv/bin/python tools/recomp/analysis/analyze_toolbar_trace.py build/recomp/reading-indicators/zh-full \
  --require-matching-dialogue --output build/recomp/reading-indicators/zh-full/toolbar-boundaries.json
```

源文件、输入和证据哈希见 [acceptance.json](../../build/recomp/reading-indicators/acceptance.json)。本次实机范围为开场世界地图对白，覆盖所列两种图像／语言配置；使用完整诊断录制，不将其帧时间作为正常游玩的性能数据。
