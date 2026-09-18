# 同场景 Core Text 文字渲染验证

2026-09-09。已完成 macOS Core Text / Core Graphics 字形绘制与 RT64 最终帧 Metal 合成，使用 2026-09-08 保存的女主第一话开场对话任务。

这是**固定场景图形任务重放**。本轮验证文字画质与布局，文本片段由已核对的翻译 ID 显式绑定；尚未实现实时对白识别、剧情推进、分页输入或对话历史。没有运行新的游戏流程或操作存档。

## 场景与来源

- 原始快照：`assets/hd-ai/dialogue-type/live-medium-4x/`，保存时间 2026-09-08。
- `latest-gfx-rdram.bin` SHA-256：`237f1b494dd2a35e7a54348ab98928323905f582bafbf4b7446477d1d632ceb5`。
- `latest-gfx-task.bin` SHA-256：`98149c0ff084c347b25cf559f7537268a76db032ef5a0deb469e5653ee8ce37b`。
- 劳伦斯当前白色对白：`t00_17412`，第二个 STOP 片段，`同样至关重要。<BR>别人拿不到的情报，`。
- 玛娜米保留的灰色对白：`t00_17411`，最后一个片段，`还有人在痛苦和怨恨中<BR>不断死去啊！？”`。
- 文本来源：[`content/locales/zh-Hans.json`](../../content/locales/zh-Hans.json)。本轮没有改写译文。

对照图都由同一快照重放产生。用户指出的颜色发灰已定位为重放器遗漏游戏运行时 VI 特性覆盖：原模式表启用 gamma，但游戏随后明确关闭 gamma。`run-3` 已恢复经过游戏指令签名校验的特性设置；该问题与 Core Text 字体绘制无关。

## 可查看的结果

输出目录：`build/recomp/coretext-probe/run-3/`。实际输出均为 960×720；游戏内部渲染倍率为 4。

| 版本 | 字体与大小 | 断行方式 | GPU 图片 |
| --- | --- | --- | --- |
| 基准 | 既有 HarmonyOS Medium 高清纹理；名义屏幕字号 39px，原逻辑推进宽度 42px | 原游戏两行 | [基准](../../build/recomp/coretext-probe/run-3/baseline/present-60.png) |
| 同字体系统绘制 | Core Text / Core Graphics，HarmonyOS Medium，39px；使用字体实际推进宽度 | 保留同样的显式两行 | [HarmonyOS 系统绘制](../../build/recomp/coretext-probe/run-3/harmony-39/present-60.png) |
| 系统中文字体 | Core Text / Core Graphics，PingFang SC Medium，39px | 保留同样的显式两行 | [系统字体](../../build/recomp/coretext-probe/run-3/system-39/present-60.png) |
| 小字号 | PingFang SC Medium，33px | 去除这个显示片段的显式换行，由 Core Text 按宽度断行 | [33px](../../build/recomp/coretext-probe/run-3/system-33-wrap/present-60.png) |
| 大字号 | PingFang SC Medium，45px | 同上 | [45px](../../build/recomp/coretext-probe/run-3/system-45-wrap/present-60.png) |

字号均指本次最终输出画面的像素值，不是 macOS 点值或游戏 14px 字格。实际使用的字体 PostScript 名称、文件路径、文件哈希、逐行文本、字形范围和字形来源均保存在各目录的 `coretext-raster.json` 与 `report.json`。

![系统中文字体 39px](../../build/recomp/coretext-probe/run-3/system-39/present-60.png)

## 实现

[`run_coretext_probe.py`](../../tools/recomp/probes/run_coretext_probe.py) 对原始任务和内存进行身份验证，校验开场 overlay 签名、字形纹理头、六行位置及 40 个字形矩形。仅在独立副本内将这些矩形的 24 字节命令组替换为 F3DEX2 SP no-op。复制后的任务大小、其他绘制命令、纹理、头像及地图数据保持原内容。

[`coretext_probe.cpp`](../../src/host/coretext_probe.cpp) 调用 Core Text 进行 Unicode 排版，在 sRGB Core Graphics 位图上下文中绘制透明背景文字。它使用灰度抗锯齿，并按实际字形轮廓范围检查裁切。最后通过 Metal 在 RT64 游戏画面完成后、GPU 截图之前合成；纹理使用预乘 Alpha，按最终像素 1:1 读取，没有再经过游戏低分辨率画面的放大。

系统绘制版的字距来自字体度量；同字体对照包含这项布局差异，不能称作仅改变抗锯齿的对照。灰色对白的前景值从本次重放基准的实心字形像素读取，为 123/255，与保存的实时画面一致。

实验编译为独立 `srw64-coretext-frame-host` 目标；只有此目标定义 `SRW64_CORETEXT_PROBE`。普通宿主没有接入系统文字功能。

## 验证结果与范围

- 两个对照（基准、去字）及四个文字版本均正常完成图形任务重放，并取得第 60 次呈现的 GPU 完成后图片。
- 四个文字版本全部布局完整，没有缺字；相同场景仍维持劳伦斯白色、玛娜米灰色。
- 所有版本在指定文字区域之外的像素变化为 **0**。
- 四个文字版本的 GPU 颜色结果与独立计算的预乘 Alpha 合成结果最大误差均为 **0/255**。
- 原始快照在重放前后哈希相同。
- 已查看同字体版、系统字体版和两档自动换行版的 GPU 图片。自动换行可按宽度工作，但会出现“不断”跨行等断点；本次未实现中文语义分行或重新安排原脚本控制标记。
- `make check`：43 项 Python 测试、编译检查和依赖检查通过；两个重放宿主编译通过。
- VI 设置修复的独立 C++ 测试通过 AddressSanitizer / UndefinedBehaviorSanitizer；覆盖有效指令签名、状态位保留、截断输入、变更的 gamma 调用参数及本次真实快照。
- 修复后的基准与昨天实时截图在中央指示器区域之外逐像素一致；全图只有 1,826 个像素不同，范围为 `[438,305,522,377)`。海面、地图、头像、对话框的五个抽样点全部恢复为实时画面的 RGB 值。见 [颜色对比](../../build/recomp/coretext-probe/run-3/live-color-comparison.json)。
- 汇总证据：[acceptance.json](../../build/recomp/coretext-probe/run-3/acceptance.json)。

首轮 `run-1` 在系统字体的版面检查处失败，原因是用字体全局下降部度量判断小姓名框的裁切范围。保留了日志；修订版增加姓名区域高度，并检查实际字形轮廓范围，完整重跑为 `run-2`。不能把失败的首轮计为通过。

`run-2` 通过文字合成检查，但仍有旧重放器的 gamma 问题。游戏在 `0x8008BFB4..0x8008BFD8` 先调用 `osViSetMode`，随后调用 `osViSetSpecialFeatures` 设置 gamma off、gamma dither on、dither filter off、divot off。重放器此前只取模式表，得到 `0x311E`，现在恢复有效值 `0x3106`。新增 [`replay_vi.hpp`](../../src/host/replay_vi.hpp) 严格校验这段游戏代码再应用覆盖，保留其他模式位。所有六个版本已完整重跑为 `run-3`；旧结果继续保留作诊断证据。

本次只验证固定 960×720 输出。实时调整字号、窗口大小和跨显示器 DPI、逐字显示、控制符同步、完整分页和历史回看仍待实现。因此属于 [增强方案](native-enhancements-plan.md) 的 T0 部分完成，不代表 T1/T2 或全平台文字系统通过。

## 复现

使用新的输出目录运行：

```sh
.venv/bin/python tools/recomp/probes/run_coretext_probe.py \
  --output build/recomp/coretext-probe/run-4
```

脚本会检查已锁定快照和 HarmonyOS 字体、编译两个重放目标，依次输出所有版本并检查颜色与非文字区域。需要当前本地 ROM 派生快照、既有高清素材包及 macOS 字体，输出目录已存在时明确报错。

源代码引用的官方技术说明：[Core Text 概览](https://developer.apple.com/library/archive/documentation/StringsTextFonts/Conceptual/CoreText_Programming/Overview/Overview.html)、[文字布局操作](https://developer.apple.com/library/archive/documentation/StringsTextFonts/Conceptual/CoreText_Programming/LayoutOperations/LayoutOperations.html)。
