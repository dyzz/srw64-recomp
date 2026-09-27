# 剧情世界地图 HD：全部区域

2026-09-24。剧情里场景之间的背景是世界地图 overlay（`load_000A7EC0`）画的，现在所有区域都是 HD。2026-09-25 起全部四个地表由 Codex 的 image_gen 重画（[改用 image_gen](#改用-image_gen2026-09-25)），画风沿用第一话欧洲的 image_gen 版（[世界地图 HD](native-worldmap-hd.md)）。2026-09-25 起欧洲改由百炼在 image_gen 版上补细节，见[欧洲改用百炼](#欧洲改用百炼2026-09-25)。

## 有哪些区域

模型对象表 `801C5670` 列出各区域的地表。地点表 `801C5310` 共 127 项，每项三个有符号半字（地表＝模型表下标, x, y），见[迷你关卡](../script/mini-stage.md)。脚本里 `3D32`／`3D33` 共 827 次（661／166），按地点所在地表统计：

| 资源 | 内容 | 地点数 | 定位次数 | 做法 |
| --- | --- | ---: | ---: | --- |
| 5599 | 宇宙：星空、地球、月亮、陨石、两个小设施、名牌 | 32 | 369 | 星空整张绘制，其余 RT64 替换，名牌按语言重绘 |
| 5602 | 整个地球 | 28 | 184 | 生成 6 个窗口 |
| 5603 | 整个地球（与 5602 的图块逐块相同，只是网格不同） | 29 | 164 | 与 5602 共用 HD |
| 5606 | 北美 | 22 | 70 | 生成 9 个窗口 |
| 5604 | 地中海、欧洲、中东、北非（第一话） | 10 | 28 | image_gen 版作图1，百炼补细节，9 个窗口（2026-09-25） |
| 5605 | 中亚、印度 | 6 | 12 | 生成 8 个窗口 |

地球各区域是平铺的网格，由 64×64 CI4 图块拼成，每块有自己的 16 色调色板；透明的海显示清屏色 RGB(0,55,90)。

## 地球地表

[`worldmap_surfaces.py`](../../tools/hd_ai/worldmap_surfaces.py)：

- `prepare`：按网格顶点把各区域的所有部件拼成一张图（北朝上），切成 256×256、重叠 48 的窗口，最近邻放大 8 倍作为输入（2048²）。
- **画风参考**（图2）：从第一话欧洲的已审 HD 图里取四块纯陆地纹理（森林、山地、沙丘、沙漠山地），拼成 1024² 的样张。
  - 起初直接用一块欧洲地图作参考，模型会照搬它的海岸线：中亚窗口变成了地中海，太平洋里多出一块沙漠大陆。
  - 只给纹理、不给海岸线后，就不再照搬。
- `run`：模型 `qwen-image-3.0-pro`，每个窗口出一张，接着自动检查：
  - 把输出缩回原尺寸，与原图比较陆地/海洋的分布（IoU），忽略海岸线两侧 1 个原像素（模型会沿岸画一圈浅水）；
  - 低于 0.90 就换种子重画，最多 3 次，取最好的一张；
  - 照搬的输出只有 0.34–0.84，正常的都在 0.90 以上。
- `compose`：
  - 每个窗口按缩放和平移配准到原图网格；
  - 重叠区线性过渡，拼成整张；
  - 海岸线取原图遮罩放大后平滑，海面保持清屏色透明。
  - 画风本来就要改变颜色，所以默认不锁定原图的低频颜色（`--colour-lock` 可以打开）。
- `pack`：按原图块坐标切成 512×512，用 `rt64_hash.map_hash` 算出每块的 RT64 键：
  - 5603 与 5602 共用同一张 HD 图；
  - 整块全海的图块留给清屏色；
  - 同一个键对应两种不同内容的有 2 块，保持原样；
  - 第一话已审的 57 块原样保留（2026-09-25 前）。

结果：新增 152 块，加上已审的 57 块，一共 209 块地图替换。2026-09-25 欧洲改用百炼、其他区域换画风后仍是 213 块（`pack-v4`），改用 image_gen 后同样 213 块（`pack-v5`）。`srw64-worldmap-hd.json` 新增 `resources` 字段，宿主接受 5602–5606，第一话的单区域旧格式照样可用。

### 个别窗口

- 重画：earth-04、central-asia-05、coast-05、coast-06、coast-08 第一张的陆地分布不对，自动换种子后通过；earth-01 取了第三张。
- 青藏高原（central-asia-03）几乎全是陆地：
  - 前三张要么多出岛屿，要么把高原画成绿色草原；
  - 第四张在提示词里补充了「几乎全是陆地、棕褐色是高山、不要画成草原」，位置正确，但画风比邻近窗口淡。
  - 再重画时碰到了工具的单批花费上限（每个输出目录 18.12 元），先保留第四张。

## 宇宙

[`worldmap_space.py`](../../tools/hd_ai/worldmap_space.py)：

- 地球（4×4 块）、月亮（2×2）按顶点拼成公告板图；4 块陨石和两个小设施的 32×32 贴图放进一张网格。
- 星空 5582（CI4 320×240，调色板 5583）按整张背景处理。
- 各自请求一次 `qwen-image-3.0-pro`，共 4 次。配准后 Alpha 取原图遮罩并平滑，按图块切片；CI4 图块的 RT64 键由 `ci4_hash` 计算（64×64 与 `map_hash` 相同，32×32 行宽减半），共 27 块。它们在清单里是新类别 `space`，不参与世界地图的 64→512 核对。
- 7 块名牌（サイド1／2／3／5／6／7、スウィートウォーター）是文字，不做 RT64 替换。它们和舰船、地标的名牌是同一种 200×30 绿框牌子（5599 的第 6–12 条类型 5 显示列表），由世界地图过场模型 HD 的名牌重绘按阅读语言绘制：原生模型包把它们打成只有名牌、没有网格的条目，中英文为 Side 1…Side 7（台词译文的写法）和 甘泉／Sweetwater。

### 星空

星空画在精灵槽 0，与场间背景一样走 `80095974`，由 [`native_background.cpp`](../../src/host/native_background.cpp) 整张绘制，文件放在 `backgrounds/whole-v2`（场间背景 16 张加星空 1 张）。与场间背景相比有两处不同：

- CI4 图按 8 位、宽度减半载入（`SETTIMG` 宽 160）。`LOADTILE` 的列坐标要乘「原图宽 ÷ SETTIMG 宽」才是像素。
- 星空是每帧最先画的东西。宿主在第一块的位置画整张图时，RT64 还没执行这一帧的清屏，下一个渲染 pass 开始时会把它清成黑色。
  - 现在保留第一块由 RT64 按原样画出，用来触发它的 pass；整张图在最后一块的位置画出，盖住第一块。
  - 场间背景也这样处理，外观不变。

## 欧洲改用百炼（2026-09-25）

第一话的欧洲原先由编码工具自带的 image_gen 画（`worldmap-runtime/pack-v6`），合成时还保留了原图的海岸窄边和裁块边界，约 6% 的可见像素是原图放大，右上角有几块没有 HD。HD 包要公开发布、说明里写百炼生成，所以欧洲改用百炼重画，画风以 image_gen 版为准：

- 第一次（`worldmap-surfaces/europe-1`）照其他区域的做法从原图重画：不保色时阿拉伯半岛和埃及变成绿色草原；`compose --colour-lock` 保色后颜色对了，但画风灰、平，还有原图图块边界带来的接缝。用户看后认为远不如 image_gen 版，没有采用。
- 采用的做法（`worldmap-surfaces/europe-2`）：把 image_gen 版欧洲按 8 倍拼成整图（`approved_canvas`，没有 HD 的图块用原图最近邻放大），切成同样的 9 个窗口当图1，提示词要求画风、配色、构图和地貌分布完全不变，只增加少量细节。不给画风样张，不保色。`samples.json` 记下了每个窗口的输入摘要和提示词。
- 合成照常：配准到原图，海岸按原图遮罩平滑切出，模型多画的小岛随之去掉。
- 结果：61 块全部来自百炼，可见像素里与原图放大相同的为 0。右上角俄罗斯有 5 个位置共用同一个原图纹理（`b820c653dd7fc5ec`），一张 HD 图无法同时对上 5 处，仍按原图显示。

## 其他区域往 image_gen 画风靠（2026-09-25）

用户要求所有区域都往第一话 image_gen 版的画风靠。地球、中亚、北美没有 image_gen 版可当底图，所以用 `restyle`：

- 图1：`run-5` 合成好的该区域窗口，饱和度 ×1.3，让地貌颜色更分明；图2：同一张画风样张。
- 提示词（`RESTYLE_PROMPT`）只让改画法：每一处的颜色和地貌类型必须与图1相同，褐色高原仍是褐色岩石山地，黄色仍是沙漠，只有绿色的地方才画草原森林，不新增湖海。最初的提示词没有这些约束，把青藏高原画成了绿色草原并加了湖（`restyle-trial-central-asia`）。
- 验收：仍只用陆地重合（≥ 0.90）自动换种子。曾试过按颜色分类比较地貌、自动重画，但新笔触本身就会改变颜色分类，结果专挑「几乎没改」的候选，于是改为只记录这两个数（`terrain`、`new_water`），由人逐窗看图。
- 人工复查后另补了几次：`earth-01` 取第 3 个候选；`central-asia-04` 取第 3 个（第 1 个是杂乱的马赛克）；`earth-04` 第 2 个偏淡但其余候选把整块陆地挪了位，保留；`central-asia-07` 第 1 个偏杂，但另两个把孟加拉–中南半岛画成沙漠，保留第 1 个。
- 结果在 `restyle-earth`、`restyle-central-asia`、`restyle-coast`，与欧洲的 `europe-2` 一起打进 `pack-v4`。所有地表可见像素里与原图放大相同的为 0。

## 改用 image_gen（2026-09-25）

千问的两种做法（`europe-2`、`restyle-*`）用户看后都认为远不如 image_gen，于是四个地表改由用户在 Codex 里用 image_gen 画：

- 生成包 `assets/hd-ai/imagegen-kit`：每个区域 1 张整区（原图最近邻放大作图1）加 3:2 局部窗口（384×256 源像素，4 倍，1536×1024；几乎全海的窗口不画），共 24 张。图2 一律是 9-09 image_gen 画的欧洲局部（`worldmap-runtime/ai-detail-v1/map-ai.png`）。提示词由 9-09 的两段改写，另要求每处地貌类型不变；`manifest.json` 记窗口位置，Codex 的生成记录在 `outputs/generation-records.json`（工具只报 `image_gen.imagegen (built-in)`，不报具体模型）。
- 合成 `worldmap_surfaces.py imagegen --kit … --output worldmap-surfaces/imagegen-1`：
  - 整区图配准到 8 倍网格当底；窗口逐张配准。
  - 窗口重叠多达三分之二，平均两张画会糊，所以按「离自己的内侧边最远的窗口占主导」混合（`kit_weight`），只在分界线附近软过渡。
  - 窗口保留自己的细节，48 HD 像素以上的颜色取整区图，相邻窗口颜色一致；颜色只从画成陆地的像素取，海岸偏差带不进海色。
  - 原图陆地遮罩外圈有一条浅海边，画里是海：附近没有画成陆地的像素时，保留画面原样，不硬涂成陆地色。
  - 海岸仍按原图遮罩切出。
- 结果打进 `pack-v5`：四个地表的可见像素里与原图放大相同的为 0。右上角俄罗斯共用纹理的 5 块仍是原图。

## 接缝、海岸紫边、锐化与近景（2026-09-27）

实机看第一话和 5604 南部各地点（透视镜头把原图 1 像素放大到 1440p 窗口 19–30 像素），每一行图块的交界处都有一条横向接缝，利比亚、突尼斯海岸有一圈紫边，整体偏软。前两处都出在工具里：

- **接缝**：`assemble` 按 `UNIT`（611/64）摆放所有四边形，但网格的行距只有 600 个世界单位（62.85 像素），列距才是 611。于是拼图里每一行比上一行低 62–63 像素而不是 64，下一行盖掉上一行最后一两行像素；`pack` 再按 64 像素切块，就把下一块的开头又切进了上一块的末尾，游戏里那 1 像素（HD 8 像素）显示两次，成了横线。纵向接缝没有，因为列距正好 64。现在 `pack` 用 `tile_span` 取每块在拼图里实际拥有的范围（到同列下一块为止，62–64 行），再拉回 512×512（`cut_tile`）。拼图本身没有改，image_gen 生成包和已合成的图都按它注册。离线核对 5604 的 52 对上下相邻块：交界处相邻行的平均差从 14.4 降到 3.4，与块内相邻行相同。
- **紫边**：原图陆地遮罩外圈是一圈浅海，画里是海，合成时"保留画面原样"，而 image_gen 把这圈浅海涂成了紫色，并把紫色混进了紧贴海岸的一条沙地。`imagegen` 合成末尾加 `coast_tidy`：遮罩内被画成水（蓝大于红绿）的像素只把色相改成清屏海色的色相；海岸内侧 `COAST_BAND`（10 HD 像素）宽的一条陆地，颜色取自更内陆的陆地（`land_blur`），保留自己的明度，画的纹理不变；小岛没有内陆可取（`land_blur` 权重趋零会算出灰黄红的杂色，马耳他两端出过），只在内陆足够的地方换色。
- **锐化**：画稿是 4 倍，拉到 8 倍再被镜头放大，看着软。合成的陆地图整体做一次 USM（`SHARPEN`，半径 3、60%），在 `coast_tidy` 之前。

重新合成到 `imagegen-2`，在 `pack-v5` 上重打成 `pack-v6`：

```sh
.venv/bin/python -m tools.hd_ai.worldmap_surfaces imagegen --kit assets/hd-ai/imagegen-kit \
  --output assets/hd-ai/worldmap-surfaces/imagegen-2
.venv/bin/python -m tools.hd_ai.worldmap_surfaces pack --output assets/hd-ai/worldmap-surfaces/imagegen-2 \
  --base-pack assets/hd-ai/worldmap-surfaces/pack-v5 --pack-output assets/hd-ai/worldmap-surfaces/pack-v6 --bind
```

### 近景窗口

镜头对每个地表一样近：定位时屏幕中心约 3.2 屏幕像素（320 基准）对 1 原像素，一屏约 100×75 原像素。image_gen 的局部窗口是 384×256 原像素画成 1536×1024（4 倍），拉到 8 倍再放大 2.5–7 倍，近景发虚。改善只能按地点再出更小的窗口：

- `closeup-kit`：从 ROM 地点表 `801C5310` 和已提取的场景事件（`assets/original-data/records/stage_events.jsonl` 里的 3D32/3D33 字，共 827 次，地球地表 458 次）算出每个地点的使用次数，按次数贪心覆盖：每个窗口 120×80 原像素（`CLOSEUP`），以最常用的未覆盖地点为中心，吸收中心附近的地点，`--always` 指定优先给窗口的地点（默认第一话的 4、0、1、2），`--count` 限定张数。每张给两幅图：`*-input.png` 是当前合成图（`--base` 运行目录）上这个窗口的 8 倍裁片（960×640），`*-source.png` 是原图硬像素放大 12 倍（1440×960）；提示词让模型保持构图、颜色和画风，只把细节密度提高，海岸线以图2为准。`manifest.json` 记窗口位置（`box`，拼图坐标）、覆盖的地点和次数；`index.jpg` 是缩略图。
- `closeup`：把画好的 `outputs/*-out.png` 配准回各自的原图窗口，按 8 倍放到合成图上，边缘 12 原像素羽化交接，海岸 alpha 沿用底图，再做一次 `coast_tidy`，写成新的运行目录给 `pack` 用。

```sh
.venv/bin/python -m tools.hd_ai.worldmap_surfaces closeup-kit --base assets/hd-ai/worldmap-surfaces/imagegen-2 \
  --output assets/hd-ai/imagegen-closeup-kit --count 13 --always 4,0,1,2
.venv/bin/python -m tools.hd_ai.worldmap_surfaces closeup --kit assets/hd-ai/imagegen-closeup-kit \
  --base assets/hd-ai/worldmap-surfaces/imagegen-2 --output assets/hd-ai/worldmap-surfaces/imagegen-3
.venv/bin/python -m tools.hd_ai.worldmap_surfaces pack --output assets/hd-ai/worldmap-surfaces/imagegen-3 \
  --base-pack assets/hd-ai/worldmap-surfaces/pack-v6 --pack-output assets/hd-ai/worldmap-surfaces/pack-v7 --bind
```

2026-09-27 用户在 Codex 画完 13 张，`closeup` 合成到 `imagegen-3`，打成 `pack-v7` 并绑定。配准的缩放在 0.93–1.02 之间（image_gen 会把画幅收进去几个百分点），`place` 补边会在没画到的边上拉出条纹，所以羽化从画稿真正覆盖到的范围起算（报告里的 `covered`）。实机看第一话的阿尔卑斯（地点 0）、意大利（地点 2）：山峰、树丛一颗颗分明，与周围没画近景的地方衔接自然；阿特拉斯（地点 15，没有近景窗口）只靠锐化，也没有接缝了。

开场镜头（地点 4）仍虚：5603 画的是与 5602 相同的图块，但地图横向转了 4 块（拼图按顶点位置看不出来，7 帧原版画面按 15–20 倍缩回去与地表原图做 NCC 校准，y 全对、x 差 256）。`locations()` 对下标 17 的 x 加 256 按 576 取模（`SURFACE_WRAP`）；`closeup-kit --extend` 保留已画的窗口、只为没盖到的地点新增，补出 closeup-14 到 19，其中 14 是开场镜头。六张当天画完，19 张一起合成到 `imagegen-3`、打成 `pack-v8` 并绑定；实机开场镜头、格陵兰、南美都已是近景。

近景仍是 8 倍贴图（宿主 `graphics.cpp` 只接受 512 的替换块）。要到 16 倍得让审计接受 1024 块并让 `pack` 对近景块单独切 1024，待定。

## 花费

- 地球地表：34 次，17.68 元。其中 3 个试验窗口 5 次、其余 20 个窗口 27 次（含 7 次自动重画）、补 5604 一次、青藏高原第四张一次。
- 画风试验的前几轮（写实风格与单张地图参考）：6.76 元，作废。
- 宇宙：4 次，2.08 元。
- 欧洲（2026-09-25）：`europe-1` 9 次 4.68 元（未采用）；`europe-2` 10 次 5.20 元（一个窗口重画一次）。
- 其他区域换画风：试验 2 次 1.04 元；`restyle-*` 共 46 次 23.92 元，其中约一半花在后来撤掉的颜色分类自动重画上。

## 实机（2026-09-24，HD 模式）

- `worldmap-regions` 迷你关卡依次到地点 3（中亚 5605）、18（北美 5606）、5（整个地球 5602）、9（整个地球 5603）、0（地中海 5604）。
  - 起初按 12 字节一项读地点表，选到的是 5605、5603 和三次 5604，北美和 5602 没到；改正后重跑，中亚、北美、5602 上的印度与青藏、5603 上的中东都是 HD。
- `worldmap-space` 依次到宇宙的全部 32 个地点。
- 各区域的地表、宇宙的星空、地球、陨石都换成了 HD，设置切回原图后恢复。
- 星空的 1506 次绘制全部改写，识别失败 0 次。
- 名牌：带原生模型包、中文运行，7 块名牌各绘制 7300 次；画面上是 甘泉、Side 1／2／3／7，和舰船名牌（天秤座）一致。

## 命令

现在的包 `pack-v5` 是 2026-09-25 在 `pack-v4` 上重打的：四个地表都取 image_gen 合成的 `imagegen-1`。

```sh
.venv/bin/python -m tools.hd_ai.worldmap_surfaces imagegen --kit assets/hd-ai/imagegen-kit \
  --output assets/hd-ai/worldmap-surfaces/imagegen-1
.venv/bin/python -m tools.hd_ai.worldmap_surfaces pack --output assets/hd-ai/worldmap-surfaces/imagegen-1 \
  --base-pack assets/hd-ai/worldmap-surfaces/pack-v4 --pack-output assets/hd-ai/worldmap-surfaces/pack-v5 --bind
```

`pack-v4`（千问版）的做法：

`pack-v4` 是 2026-09-25 在 `pack-v3`（已含宇宙、对话框边框和战斗 HUD 边框）上重打的：地球、中亚、北美用 `restyle-*`，欧洲用 `europe-2`，第一话 image_gen 画的 57 块不再进包。

```sh
for s in earth central-asia coast; do
  .venv/bin/python -m tools.hd_ai.worldmap_surfaces restyle --output assets/hd-ai/worldmap-surfaces/restyle-$s \
    --from assets/hd-ai/worldmap-surfaces/run-5 --surface $s
  .venv/bin/python -m tools.hd_ai.worldmap_surfaces run --output assets/hd-ai/worldmap-surfaces/restyle-$s --env-file .env
  .venv/bin/python -m tools.hd_ai.worldmap_surfaces compose --output assets/hd-ai/worldmap-surfaces/restyle-$s
done
.venv/bin/python -m tools.hd_ai.worldmap_surfaces compose --output assets/hd-ai/worldmap-surfaces/europe-2
.venv/bin/python -m tools.hd_ai.worldmap_surfaces pack --output assets/hd-ai/worldmap-surfaces/restyle-earth \
  --extra-run assets/hd-ai/worldmap-surfaces/restyle-central-asia --extra-run assets/hd-ai/worldmap-surfaces/restyle-coast \
  --extra-run assets/hd-ai/worldmap-surfaces/europe-2 \
  --base-pack assets/hd-ai/worldmap-surfaces/pack-v3 --pack-output assets/hd-ai/worldmap-surfaces/pack-v4 --bind
```

`pack-v1` 当初的做法：

```sh
.venv/bin/python -m tools.hd_ai.worldmap_surfaces prepare --output assets/hd-ai/worldmap-surfaces/run-5
.venv/bin/python -m tools.hd_ai.worldmap_surfaces run --output assets/hd-ai/worldmap-surfaces/run-5 --env-file /path/to/.env
.venv/bin/python -m tools.hd_ai.worldmap_surfaces compose --output assets/hd-ai/worldmap-surfaces/run-5
.venv/bin/python -m tools.hd_ai.worldmap_surfaces pack --output assets/hd-ai/worldmap-surfaces/run-5 \
  --base-pack assets/hd-ai/portrait-matte/v2/pack --pack-output assets/hd-ai/worldmap-surfaces/pack-v1 --bind
.venv/bin/python -m tools.hd_ai.worldmap_space pack --output assets/hd-ai/worldmap-space/run-1 \
  --pack assets/hd-ai/worldmap-surfaces/pack-v1 \
  --backgrounds-from assets/hd-ai/backgrounds/whole-v1 --backgrounds-to assets/hd-ai/backgrounds/whole-v2 --bind
```

`SRW64_BG_DUMP=1` 会把没有 HD 的精灵背景第一次绘制的显示列表写到运行目录 `background-draws.jsonl`，接新图时用来看绘制方式。
