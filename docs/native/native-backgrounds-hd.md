# 场间背景 HD

2026-09-24。インターミッション画面的 8 张背景图已生成 HD，并以整张图接入游戏，亮版、暗版各一套。

## 背景是什么

- 共 8 张 320×240 CI8 的三维 CG 插画，资源 5470–5477（`0x155E`–`0x1565`）。每张是一台主角机，右上角有 SRW64 标志。
- 每张有两套 256 色调色板：亮版 5478–5485，暗版 5486–5493。首次进入用亮版；菜单构建好以后和从子画面返回时换暗版。图本身不变。
- 按第一台主角机选图，不随话数变化，详见[场间主菜单](native-intermission-menu.md) §3。
- 0 号色是透明黑。游戏背后清成黑色，所以 HD 图做成不透明。

## 游戏怎么画

背景画在精灵槽 0，模式 4，`80098158(槽, 0, 4, 0xA4, 0, 图, 调色板, 0)`，绘制函数是 `80095974`（模式 2 也走它）。实机抓到的显示列表：

- 先设 `E3000C00`、`E3001001`（TLUT RGBA16），combiner `FC119623 FF2FFFFF`，也就是颜色和 Alpha 都是 TEXEL0 × PRIM；再用 `FA` 设 PRIM（淡入淡出就靠它）。
- 然后载入 256 色调色板。
- 接着画 80 块 32×32：每块 `SETTIMG`（整张图），`LOADTILE` 载入这一块的区域（多载 1 像素给双线性过滤），`SETTILESIZE` 把渲染 tile 设为 (0,0)–(31,31)，最后 `TEXRECT` 画到屏幕上。
- 精灵子记录 +0xC/+0xE 是资源句柄。按 `8008A11C` 的句柄表（`0x160340`，每项 20 字节，+2 是 ROM 资源号）可以换回资源号。

## 整张替换

[`native_background.cpp`](../../src/host/native_background.cpp) 的做法和头像相同：

1. 挂 `80095974`；
2. 用资源号查（图, 调色板）对应的 HD 图；
3. 每块从 `LOADTILE` 的起点、`TEXRECT` 的 S/T 和 dsdx/dtdy 算出这块在原图上的范围，记下（屏幕矩形, 原图矩形）；
4. 最后一块改成带标签的标记，第一块照原样保留（让 RT64 先开始这一帧的渲染 pass，见[剧情世界地图 HD](native-worldmap-regions-hd.md)的星空一节），其余置空；
5. 宿主用 Metal 实例化一次画出全部块，采样同一张 HD 图，所以块与块之间没有接缝。

着色器乘 PRIM，淡入淡出与原版一致；纹理为预乘 Alpha，带 mipmap。原图模式下不改写。卷动或只画一部分时，同样按每块自己的原图范围贴图。

## 生成

[`background_hd.py`](../../tools/hd_ai/background_hd.py)：

- `prepare`：亮版按最近邻放大 6 倍作为输入。提示词要求保留构图、机体和标志文字（大字「SRW64」、小字「super robot wars 64」）。
- `run`：每张请求一次 `qwen-image-3.0-pro` 和一次 `qwen-image-3.0`，输出 2048×1536，共 16 次、5.76 元。全部一次成功，没有出现 400。
- `compose`：按窗口配准，逐轴拟合缩放和平移（偏差都在 0.2% 以内），重采样到 1920×1440（6 倍），并输出对比图。
- `build`：取选定的模型（默认 Pro）作亮版。暗版用两套 ROM 调色板拟合的 17³ 颜色表，从亮版映射出来。
  - 暗版不是统一变暗：各色比例在 0.5–0.95 之间。
  - 用原图验证：颜色表与游戏暗版平均相差 1.2–2.6 级，统一乘 0.75 则相差 3–23 级。

两个模型都保住了构图、零件和标志文字。Pro 的阴影和曲面更干净，普通版稍锐、边缘略有锯齿，缩回原尺寸后与原图的差异两者都在 2–5 级。默认用 Pro，可以用 `build --choice '{"background-5470": "qwen-image-3.0"}'` 逐张改选。

资源在 `assets/hd-ai/backgrounds/whole-v1`：16 张 1920×1440，共 42 MB。清单现在用的是 `whole-v2`，即 whole-v1 加剧情世界地图的星空（[剧情世界地图 HD](native-worldmap-regions-hd.md)）。[`stage1-hd.json`](../../content/art/stage1-hd.json) 的 `backgrounds` 段列出它们，`compile_art` 校验后复制到运行目录 `art/backgrounds/`。

## 实机（2026-09-24，HD 模式）

- 读第一话通关存档进入インターミッション：背景（スイームルグ，5476）显示为 HD 暗版，菜单面板照常叠在上面。
- 设置切到原图再切回，两次 HD 截图逐像素一致。
- 退出计数：986 次背景绘制全部改写，识别失败 0 次，解码 2 张（亮、暗）。

## 命令

```sh
.venv/bin/python -m tools.hd_ai.background_hd prepare --output assets/hd-ai/backgrounds/run-1
.venv/bin/python -m tools.hd_ai.background_hd run --output assets/hd-ai/backgrounds/run-1 --env-file /path/to/.env
.venv/bin/python -m tools.hd_ai.background_hd compose --output assets/hd-ai/backgrounds/run-1
.venv/bin/python -m tools.hd_ai.background_hd build --output assets/hd-ai/backgrounds/run-1 --images assets/hd-ai/backgrounds/whole-v1 --bind
```

改了 `generate_cpu.py` 的钩子后要先重跑它，再构建宿主。
