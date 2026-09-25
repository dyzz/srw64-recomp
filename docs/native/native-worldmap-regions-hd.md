# 剧情世界地图 HD：全部区域

2026-09-24。剧情里场景之间的背景是世界地图 overlay（`load_000A7EC0`）画的，现在所有区域都是 HD，画风都往第一话欧洲的 image_gen 版（[世界地图 HD](native-worldmap-hd.md)）靠。2026-09-25 起欧洲改由百炼在 image_gen 版上补细节，见[欧洲改用百炼](#欧洲改用百炼2026-09-25)。

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

结果：新增 152 块，加上已审的 57 块，一共 209 块地图替换。2026-09-25 欧洲改用百炼、其他区域换画风后仍是 213 块（`pack-v4`）。`srw64-worldmap-hd.json` 新增 `resources` 字段，宿主接受 5602–5606，第一话的单区域旧格式照样可用。

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

现在的包 `pack-v4` 是 2026-09-25 在 `pack-v3`（已含宇宙、对话框边框和战斗 HUD 边框）上重打的：地球、中亚、北美用 `restyle-*`，欧洲用 `europe-2`，第一话 image_gen 画的 57 块不再进包。

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
