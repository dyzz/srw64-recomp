# 剧情世界地图 HD：全部区域

2026-09-24。剧情里场景之间的背景是世界地图 overlay（`load_000A7EC0`）画的，除了第一话的欧洲，现在所有区域都是 HD，画风与第一话的欧洲（[世界地图 HD](native-worldmap-hd.md)）一致。

## 有哪些区域

模型对象表 `801C5670` 列出各区域的地表。脚本用 `3D32`／`3D33` 在世界地图上定位约 260 次，按地点表 `801C5310`（每项 12 字节，首半字是模型表下标）统计：

| 资源 | 内容 | 地点数 | 定位次数 | 做法 |
| --- | --- | ---: | ---: | --- |
| 5599 | 宇宙：星空、地球、月亮、陨石、两个小设施、名牌 | 16 | 87 | 星空整张绘制，其余 RT64 替换，名牌保持原样 |
| 5603 | 整个地球（与 5602 的图块逐块相同，只是网格不同） | 12 | 84 | 与 5602 共用 HD |
| 5606 | 北美 | 12 | 74 | 生成 9 个窗口 |
| 5605 | 中亚、中东、印度 | 3 | 42 | 生成 8 个窗口 |
| 5602 | 整个地球 | 16 | 41 | 生成 6 个窗口 |
| 5604 | 地中海、欧洲、北非（第一话） | 5 | 18 | 已审的 57 块保持不变，右上角 5 块俄罗斯补齐 |

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
  - 第一话已审的 57 块原样保留。

结果：新增 152 块，加上已审的 57 块，一共 209 块地图替换。`srw64-worldmap-hd.json` 新增 `resources` 字段，宿主接受 5602–5606，第一话的单区域旧格式照样可用。

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
- 7 块名牌是日文文字，保持原样。

### 星空

星空画在精灵槽 0，与场间背景一样走 `80095974`，由 [`native_background.cpp`](../../src/host/native_background.cpp) 整张绘制，文件放在 `backgrounds/whole-v2`（场间背景 16 张加星空 1 张）。与场间背景相比有两处不同：

- CI4 图按 8 位、宽度减半载入（`SETTIMG` 宽 160）。`LOADTILE` 的列坐标要乘「原图宽 ÷ SETTIMG 宽」才是像素。
- 星空是每帧最先画的东西。宿主在第一块的位置画整张图时，RT64 还没执行这一帧的清屏，下一个渲染 pass 开始时会把它清成黑色。
  - 现在保留第一块由 RT64 按原样画出，用来触发它的 pass；整张图在最后一块的位置画出，盖住第一块。
  - 场间背景也这样处理，外观不变。

## 花费

- 地球地表：34 次，17.68 元。其中 3 个试验窗口 5 次、其余 20 个窗口 27 次（含 7 次自动重画）、补 5604 一次、青藏高原第四张一次。
- 画风试验的前几轮（写实风格与单张地图参考）：6.76 元，作废。
- 宇宙：4 次，2.08 元。

## 实机（2026-09-24，HD 模式）

- `worldmap-regions` 迷你关卡依次到中亚、北美、整个地球、地中海；`worldmap-models` 在宇宙区域。
- 各区域的地表、宇宙的星空、地球、陨石都换成了 HD，设置切回原图后恢复。
- 星空的 1506 次绘制全部改写，识别失败 0 次。

## 命令

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
