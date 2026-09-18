# 本地模型资源浏览与 5600 验证

5600 已增加“原生水滴 · 3,968 面”选项及实际游戏对照。该版本使用宿主 GPU 网格和逐像素光照；试玩入口、接入方式与证据见 [原生模型替换](native-model-replacement.md)。

2026-09-09 建立，2026-09-10 补充实际游戏验证。页面源码在 `tools/model_viewer/`，生成页面、模型和贴图保留在忽略的 `build/model-viewer/`。服务只监听 `127.0.0.1`，没有上传、云端发布或外部贴图请求。

## 浏览页面

页面收录本次解析的 **483** 个几何容器，合计 **82,122** 条三角形绘制；包含空间几何和共面几何，不能把容器数量当成独立机体或场景数量。

- 按编号／名称查找、几何类型和已定位状态筛选；左侧缩略图来自原始坐标。
- 模型窗口上方的“上一个／下一个”和键盘左右方向键，按当前筛选结果切换；显示当前位置，首尾自动禁用对应按钮。选中卡片会在列表内滚动到可见位置。
- 鼠标拖动旋转、滚轮缩放、右键平移；提供斜视／正面／顶视、自转、复位和线框叠加。
- 源贴图、灰模、线框三种显示方式；多显示列表资源可以单独显示部件。
- 按原始局部坐标导出当前部件的几何 OBJ；不输出材质、骨骼或动画。
- 默认打开 5600；可切换原版菱形（8 面）与高模试作（96 面）。右上角分开展示实际游戏的原版／高模／地图移动／战术地图，以及原始、隐藏、拉伸、高模四张 GPU 回放图。URL 片段可直接定位，例如 `/#5584`。

复用当前静态勘测输入：

```sh
npm ci --prefix tools/model_viewer --ignore-scripts --no-audit --no-fund
.venv/bin/python tools/model_viewer/build.py
.venv/bin/python tools/model_viewer/serve.py
```

服务启动后打印本地 URL；可用 `--port` 指定端口。构建输入为 `assets/models/3d-2026-09-09/` 下的原始资源、`geometry-data.json` 和 `geometry-survey.json`，详见 [3D 分析](3d-model-replacement-analysis.md)。Three.js 固定为 `0.180.0`，依赖许可随页面输出。

## 显示边界与检查

页面展示局部顶点、三角形和简化材质，不模拟原游戏场景变换、镜头、动画、完整 RDP 混合或灯光。多个部件也可能是效果帧，不能将同时显示的所有部件当成一个游戏姿态。

已提取 **2,713** 个纹理文件；有 **27** 个资源包含尚未确认的纹理装载方式，对应批次回退为纯色，页面明确提示。源贴图视图便于辨认资产，不宣称等同于 RT64 游戏画面。

构建后检查了全部 483 份模型的三角形数量、顶点／UV 长度、有限数值以及所有纹理／缩略图引用。5604、5605 各 70 张源纹理与既有地图提取器的 RGBA 像素逐一交叉一致。JavaScript 语法、Python 编译和本地 HTTP 请求通过。

浏览器复现了目录卡片被自动网格行压缩到 2 像素高的问题；改为按内容计算行高，并让缩略图按比例缩放。修复后检查了全部 483 张卡片的布局高度，实际查看了 5631 和 5604 页面截图，并实测 5631 → 5632 → 5631、已定位筛选中的首尾禁用、空搜索结果禁用。浏览器未记录 JavaScript 错误。这些检查仅验证页面交互，不增加游戏运行时证据。

## 5600：已确认画面归属与固定拓扑变形

使用 `female-story-2` 的既有任务快照，生成三份独立副本，任务描述符逐字节相同，原快照保持不变。

| 回放 | 修改范围 | GPU 图片结果 |
| --- | --- | --- |
| baseline | 不修改 | 原始黄色菱形与虚线环 |
| hidden | 12 组三角形命令改为 F3DEX2 SP no-op；52 个字节实际变化 | 菱形与虚线环消失；1,369 个像素变化，范围 `(437,305)–(521,377)` |
| stretched | 只改六个立体顶点，X/Z 乘 2、Y 乘 3；14 个字节实际变化 | 立体部分变长，环及其他画面保留；4,639 个像素变化，范围 `(439,282)–(521,395)` |

三次 RT64/Metal 回放均 exit 0；已逐张查看 GPU 完成后读回的 960×720 图片。目标标记区域外的像素差异为 0。

这证明 **5600 确实对应剧情地图黄色菱形及环形平面，也证明修改其顶点会进入实际 RT64 绘制**。本节证据范围为单任务渲染回放；后续拓扑替换与实际游戏加载见下文。

准备新的验证输入：

```sh
.venv/bin/python tools/recomp/model5600/prepare_model_5600_probe.py \
  --source build/recomp/gfx-probes/female-story-2 \
  --output assets/models/model-5600-probe-new
```

对生成的 `baseline-source`、`hidden-source`、`stretched-source` 分别运行 `tools/recomp/probes/run_frame_probe.py`，为每项指定新的 `--output`。本次原始结果保存在 `assets/models/model-5600-probe/`，其中 [acceptance.json](../assets/models/model-5600-probe/acceptance.json) 记录逐帧哈希和差异。

## 5600：96 面高模试作与新增拓扑回放

新增 [prepare_model_5600_highpoly.py](../tools/recomp/model5600/prepare_model_5600_highpoly.py)，生成 50 个位置、96 个三角形的封闭菱形。横截面有 16 个点，三层截面形成圆角与腰部倒角；尺寸仍为 X/Z `-5..5`、Y `-12..12`。原有虚线环的贴图、顶点、8 次三角形绘制全部保留，因此整个替换资源合计 104 次三角形绘制、54 个引用位置。菱形采用黄色切面颜色；这是带新材质的原创几何试作。

原始顶点坐标是整数，直接在 `-5..5` 范围内增加圆角容易被量化掉。试作将新顶点放大 256 倍存储，绘制时压入 `1/256` 的模型矩阵，结束后弹出。每批最多加载 30 个顶点，符合现有 32 项缓存；生成后独立解码二进制命令，验证实际三角形与作者网格一致、矩阵栈平衡、封闭流形和范围正确。

新显示列表与顶点使用该快照内经核验为空的 `0x700000` 区域，共 5,208 字节。原菱形首条三角形命令改为子列表调用，余下七条改为 SP no-op。仅支持指定快照的完整 RDRAM／任务 SHA-256；这个地址**只供独立任务回放使用，不是实时游戏的内存分配方案**。原 ROM、原快照及 `graphics.cpp` 均未修改。

原版与高模使用同一渲染器二进制，各得到 GPU 完成后的 960×720 图片。原版图与此前验证的原版 PNG 哈希完全一致；高模改变 **990 个像素**，包围框为 `[461,320,500,363)`，菱形检查区外像素差异为 0。已查看实际 GPU 图片和浏览器高模预览。页面版本切换、统计变化（16 ↔ 104）、切换到其他资源时隐藏版本入口、四张对照图和线框模式均已检查，未记录 JavaScript 错误。OBJ 导出处理已触发并显示完成提示，但浏览器自动化未收到下载事件，因此文件落盘未作为浏览器验收项；独立生成的实体 OBJ 已校验为 50 个顶点、96 个面。

可复现命令（输出目录必须尚不存在）：

```sh
.venv/bin/python tools/recomp/model5600/prepare_model_5600_highpoly.py \
  --source build/recomp/gfx-probes/female-story-2 \
  --output assets/models/model-5600-highpoly
SRW64_BACKGROUND=1 .venv/bin/python tools/recomp/probes/run_frame_probe.py \
  --source assets/models/model-5600-highpoly/baseline-source \
  --output assets/models/model-5600-highpoly/baseline --resolution-scale 3
SRW64_BACKGROUND=1 .venv/bin/python tools/recomp/probes/run_frame_probe.py \
  --source assets/models/model-5600-highpoly/highpoly-source \
  --output assets/models/model-5600-highpoly/highpoly --resolution-scale 3
.venv/bin/python tools/recomp/model5600/verify_model_5600_highpoly.py assets/models/model-5600-highpoly
.venv/bin/python tools/model_viewer/build.py
```

[验收记录](../assets/models/model-5600-highpoly/acceptance.json)、[生成与内存修改记录](../assets/models/model-5600-highpoly/fixtures.json)、[实体 OBJ](../assets/models/model-5600-highpoly/5600-highpoly-solid.obj)。页面仅在高模验收记录存在且网格、GPU 图片哈希匹配时加入该版本。

本节证明 **5600 可以增加顶点和三角形并进入实际 RT64 绘制**。下节另行验证原游戏加载器和实际开场流程；单任务回放本身不提供这些运行时证据。

## 5600：实验 ROM 与实际开场流程

2026-09-10，已完成一次从空 SRAM 启动、女性主角开场对话、剧情地图位置变化，到第一话战术地图的实际运行。环境为本项目的原生重编译游戏 CPU、原游戏资源加载器、RT64/Metal，960×720 GPU 完成后读回；不是 N64 硬件验证。高模运行达到 **16,800 VI**，耗时约 **280.8 秒**，正常 exit 0。

[build_model_5600_rom.py](../tools/recomp/model5600/build_model_5600_rom.py) 把 96 面网格编译后追加到资源 5600 内，使用 segment 4 相对指针，让原游戏加载器管理分配和释放。资源由 **7,048 → 12,264 字节**，新增矩阵、顶点和子显示列表不依赖固定 RDRAM 地址。原虚线环、材质、原容器描述符保持不变；只替换原菱形的八条绘制命令并追加内容。

压缩资源写入独立实验 ROM 的 `0x1FF0000` 空白池，仅重定向 5600 的资源表项。压缩区占 2,384 字节；全 ROM 实际变化 2,340 字节，全部位于该表项和新增数据。原 ROM `rom.z64` 不变，实验 ROM 为 `build/recomp/model-5600/rom/srw64-model5600.z64`，SHA-256 为 `feeabfecc8eddb0e60d0ce04959683f07506abd58fdfcf9e7970158c2b85c2c7`。首 1 MiB 与全部 18 个程序装载区均逐字节一致，没有修改 guest 程序或几何渲染钩子。

验证结果：

- 只读监测通过完整资源内容识别加载结果，并确认根显示列表调用了资源入口。25 次异步任务观察记录到调用；另保存并复核了 8 组完整 RDRAM／任务，其中含 4 种旋转矩阵、2 种位置。全部观察记录含 7 种矩阵、3 种位置。观察时 VI 不代表与某一 GPU 图片严格同步。
- 同为 VI 7073 的实际游戏原版与高模图片，差异 **1,032 像素**，包围框 `[465,323,496,363)`；菱形检查区外为 **0**。虚线环、地图、头像和文字在这对图片中逐像素一致。
- 高模另一角度与地图位置变化可见于 `live-1/present-4920.png`。`present-6240.png` 已切入战术地图，`present-8400.png` 显示单位和行动范围。末尾任务快照已不再包含该完整资源或其根调用。
- 对照使用相同的宿主源码、已生成 guest 代码、渲染器源码和输入脚本；分别重建所得二进制哈希不同，**不标记为同一二进制 A/B**。原版对照在 VI 11564 收到 SDL window-quit，正常退出，但未到请求的 16800；仅将其较早完成的帧用于对照。高模完成全段。
- `tests/test_model_5600_resource.py` 独立遍历编译后的显示列表，在三个模拟加载地址验证 96 个新三角形及原环的 8 次绘制、矩阵栈平衡、原描述符保留和压缩往返。运行证据由 [verify_model_5600_live.py](../tools/recomp/model5600/verify_model_5600_live.py) 检查并生成[验收记录](../build/recomp/model-5600/acceptance.json)。

运行记录：[高模实际游戏](../build/recomp/model-5600/live-1/report.json)、[原版对照](../build/recomp/model-5600/original-1/report.json)、[ROM 构建记录](../build/recomp/model-5600/rom/build.json)。页面只有在限定范围的运行验收记录及 GPU 图片哈希匹配时，才显示“开场已验证”。 本次浏览器实测已确认该状态、两组共八张图片加载完成、对照窗口可打开，未记录 JavaScript 错误。

从项目根目录手动试玩：

```sh
.venv/bin/python tools/recomp/run/play_native.py --model-5600 --new-game
```

选 New Game → 女性超级系，保持默认姓名进入开场。方向键移动，Z 确认，X 取消，Enter 为 Start，Esc 关闭。实验使用独立 game ID `srw64-model5600-experiment`，历史和 SRAM 仅放在 `build/recomp/model-5600/play/`。省略 `--new-game` 会使用该实验历史，首次则复制配置中的既有检查点；不会覆盖通常试玩存档。

重新构建与验证（ROM 输出目录须不存在；使用其他目录时同步 variant 路径，ROM 内容哈希相同）：

```sh
.venv/bin/python tools/recomp/model5600/build_model_5600_rom.py --output build/recomp/model-5600/rom
.venv/bin/python tools/recomp/toolchain/audit_rom_variant.py --variant model5600 --output build/recomp/model-5600/compatibility.json
SRW64_BACKGROUND=1 .venv/bin/python tools/recomp/run/run_host_probe.py \
  --variant model5600 --graphics --resolution-scale 3 \
  --input config/recomp/inputs/female-to-map.json --vis 16800 \
  --output build/recomp/model-5600/live-1
```

实时采样须在运行过程中另启 `watch_model_5600_runtime.py --directory build/recomp/model-5600/live-1 --resource build/recomp/model-5600/rom/resource-5600.bin`。原版对照用相同输入、渲染配置及 `--variant jp`，输出 `original-1`。完成后运行 `verify_model_5600_live.py build/recomp/model-5600`，再构建资源页。

本次验证了 **新增网格经游戏原加载路径使用、旋转、位置更新和离开剧情地图**。未覆盖全部路线、再次返回该场景、存档恢复后的重新加载、长期性能或 N64 硬件；也还没有通用 OBJ／glTF 导入器。

## 5584：找到静态入口，仍需剧情触发证据

`load_000A7EC0` 中 ROM `0xAAF30` / VRAM `0x801C5670` 为模型对象资源表，索引 0 是 5584、索引 15 是 5600。该表还包含此前提取的舰船形状资源与地图资源。

`0x801C3490` 按第二参数的低字节选择表项；`0x801C34EC` 读取资源 ID，随后 `0x801C3560` 调用 `0x8008B4F4`，将资源 ID 写入参数区 `sp+0x1C`。另有 `0x801C4DC0` 通过 `0x801C58BC` 中的选择值比较／更新对象，并在 `0x801C4E30` 调用同一构建路径。

后续验证方法为：在确认这个 overlay 身份后，跟踪 `0x801C3490` 的索引 0 调用，记录调用者、脚本位置和场景，再取实际 GPU 帧。当前只定位了静态表与调用路径，没有确认章节、舰名或该资源在已测路线中实际出现。结构化记录见 [model-5584-reference.json](../assets/models/3d-2026-09-09/model-5584-reference.json)。
