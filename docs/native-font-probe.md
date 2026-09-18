# 原生分辨率字体实验

2026-09-08：已完成真实原生对话绘制任务的单帧 RT64/Metal 实验。它证明轮廓字体
可以按窗口像素密度重新生成，字号、字距、行高和换行可以独立于原 14 像素字库调整。
本实验尚未接入实时对话引擎，不代表所有菜单、角色名或不同 DPI 已验收。

## 实际结果

输出均为 960×720，参考底图、人物立绘和对话文字来自同一原生运行快照。

| 版本 | 字形来源 | 版式 |
| --- | --- | --- |
| 原字库 | ROM 中的 8×14 / 14×14 位图 | 保留原版 |
| 清晰字体 | 系统 Hiragino Sans GB，40 像素轮廓字体，42 像素高字格 | 保留原坐标和推进宽度 |
| 大字号 | 同一字体，52 像素轮廓字体，54 像素高字格 | 逻辑字格由 14 增至 18，调整字距、行高，正文重排为两行 |

验收图片：

- [原字库](../build/recomp/font-probe/settled-original/present-60.png)
- [40 像素轮廓字体](../build/recomp/font-probe/settled-font-40px/present-60.png)
- [52 像素轮廓字体与换行](../build/recomp/font-probe/settled-font-52px/present-60.png)
- [像素及内存差异验证](../build/recomp/font-probe/verification.json)

![52 像素轮廓字体与重新换行](../build/recomp/font-probe/settled-font-52px/present-60.png)

已逐图确认姓名 `ローレンス` 和完整正文 `「やはり帝国軍のようでございます」` 可见。
大字号正文分为 `「やはり帝国軍` / `のようでございます」` 两行。
新版文字矩形范围为逻辑坐标 `(22,168)..(124.75,222)`，位于本样例明确指定的
对话框内部 `(22,168)..(201,224)`。两种字体版本的全部像素变化都限制在文字区域，
背景、立绘及文本框外像素与参考图完全相同。

## 证据与实现

1. 从 `gfx-probes/female-story-2` 保存了 RDRAM 和 OSTask，源文件及哈希在
   `build/recomp/font-probe/source-1/source.json`。该游戏进程运行到 28,115 VI 时收到
   SDL_QUIT 事件并退出，尚未到战术地图；不能把它算作完整战斗通过。
2. 字库资源 0、1 的解压内容在快照中各有唯一的完整匹配。RT64 的 TMEM dump
   提供真实纹理哈希、图块位置和调色板，结合现有 glyph map 确认字符。
3. `build_native_font_probe.py` 从桌面字体重新栅格化 22 个匹配纹理，生成本地 RT64
   字体替换包。保留原窄字与宽字的推进比例，未放大 ROM 位图生成新字形。
4. 大字号版本仅改动快照中相应文字矩形及纹理步进，共 213 字节。原 ROM 和其余
   快照内存未修改。按画面行顺序布局，保留原显示列表的绘制次序。
5. `srw64-frame-host` 使用快照内的活动 VI mode 表、真实显示列表和 RT64。它不执行
   游戏逻辑；为连续呈现，交替使用原目标和位于快照范围外的专用渲染目标。
6. 验收采用第 60 次呈现，GPU command buffer 完成后才读取图片；元数据使用
   `replay_iteration`，不把重放迭代称为游戏 VI。早期曾怀疑首帧漏字，但复核发现
   52 像素版的首帧和第 60 次呈现 PNG 逐字节相同，没有支持漏绘或资源时序故障的证据。

每次正式重放目录的 `report.json` 记录输入、宿主二进制、源码、字体包各文件和
输出图片哈希。字体原文件留在系统目录，生成纹理和快照仅保留在被忽略的 `build/`。

复现一个新大字号实验（输出目录须不存在）：

```sh
.venv/bin/python tools/recomp/probes/build_native_font_probe.py \
  --source build/recomp/font-probe/source-1 \
  --textures build/recomp/font-probe/replay-original-1/textures \
  --font '/System/Library/Fonts/Hiragino Sans GB.ttc' \
  --output build/recomp/font-probe/font-new \
  --cell-height 18 --pixel-scale 3

python3 tools/recomp/probes/run_frame_probe.py \
  --source build/recomp/font-probe/font-new/source \
  --font-pack build/recomp/font-probe/font-new/pack \
  --output build/recomp/font-probe/replay-new --native-resolution
```

## 后续接入边界

当前明确使用一个对话框样例的边界和既有窄/宽比例。实时实现还需要按真实文本框
读取边界、保留动态逐字显示及控制 token、处理不同字宽和标点换行、随窗口像素密度
选择字形缓存，并逐项验收菜单和角色名。仅替换本样例的 22 个纹理不是全字库支持。
公共序章中另有已经烘焙在图片里的文字，不会自动经过这条动态字库路径。
