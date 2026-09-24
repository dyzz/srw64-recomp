# assets/：本地导出资源

这里放从 ROM 导出、或由实验生成、需要长期复用的资源。整个目录不进 git（只有本说明文件入库），
也不随仓库分发。与 `build/` 的分工：

| 目录 | 放什么 | 删掉会怎样 |
| --- | --- | --- |
| `build/` | 工具链、编译产物、生成代码、测试输出、运行与探针记录 | 可以用 `make`／工具重建；运行记录只作证据 |
| `assets/` | 导出的数据、HD 素材包、模型等要反复使用的资源 | 需要重新导出，HD 素材可能无法重现 |

## 原版数据与图像

| 路径 | 内容 | 来源／使用者 |
| --- | --- | --- |
| `original-data/` | 原始数据目录：文本、资源、机体／驾驶员、事件脚本等记录与数据查看器页面 | `make recomp-data`（`tools/content/extract_original.py`）生成，每次整体替换；`tools/data_viewer/serve.py`、迷你关卡与脚本工具读取 `records/` |
| `original-graphics/` | 按机体／人物命名整理的原版图像：战斗图与图集、动画零件、地图图标、头像、特效、cut-in、合体动画、章节标题、战场底图，附 `index.html` 图库与数值总表 | `tools/content/export_graphics.py` 生成，每次整体替换；见 `docs/data/battle-graphics.md` |
| `transcriptions/` | 章节标题卡与结局页的日文转写 | 标题画面与剧情文字图（`docs/native/native-title-and-story-images.md`） |

## HD 素材（`hd-ai/`）

美术清单 `content/art/stage1-hd.json` 引用的包，以及重建它们需要的来源：

| 路径 | 内容 | 来源／使用者 |
| --- | --- | --- |
| `worldmap-surfaces/pack-v1/` | RT64 哈希替换包：世界地图地表 209 条、宇宙物件 27 条、对话框边框 13 条 | 美术清单 `source`；`tools/hd_ai/worldmap_surfaces.py`、`dialogue_frame_asset.py` 写入 |
| `worldmap-surfaces/run-5/` | pack-v1 地表的生成与合成记录（`choices.json` 等） | `worldmap_surfaces.py` |
| `worldmap-runtime/pack-v6/` | 第一话欧洲 57 块图块，pack-v1 沿用它们；同目录的 `ai-*`、`live-v6` 是当时的来源与运行证据 | `worldmap_surfaces.py` |
| `portrait-matte/v2/` | `pack/` 是世界地图打包的基础包（`--base-pack`），pack-v1 由它加上地图、宇宙图块而来；另有 4 张已审核头像母版（29、33、166、169） | `worldmap_surfaces.py`；`build_portrait_images.py --reviewed` |
| `worldmap-space/run-1/` | 宇宙星空与物件的生成记录 | `tools/hd_ai/worldmap_space.py` |
| `portrait-batch/whole-v1/` | 整张 HD 头像 | 美术清单 `portraits`；`build_portrait_images.py` 生成 |
| `portrait-batch/full-1/`、`review-site/` | 头像 2×2 拼图的生成结果与审图站 | `portrait_batch.py`、`build_portrait_review_site.py` |
| `backgrounds/` | 场间背景：`run-1` 是付费生成的原始输出，`whole-v1` 由它构建，`whole-v2` = `whole-v1` 加星空，是清单引用的整张图 | 美术清单 `backgrounds`；`tools/hd_ai/background_hd.py`、`worldmap_space.py` |
| `title/` | 标题 Logo 与火焰：`v1`、`whole-v1` 是来源，`whole-v2` 是清单引用的整帧图 | 美术清单 `scene_images`；`title_hd.py`、`flat_scene_hd.py` |
| `dialogue-runtime/v4/` | 当前对话框边框的预览与构建记录 | `dialogue_frame_asset.py` |
| `tactical-maps/` | 战术地图 HD 样板：请求清单、生成结果（`runs/`）、合成中间文件，`runtime/` 是宿主读取的底图与色号图 | `tools/hd_ai/tactical_map_hd.py`；运行时由 `SRW64_HD_MAPS` 指向 `runtime/` |
| `weapon-markers/` | 武器标记图标的 AI 描摹来源 | 武器标记符号字体 |
| `dialogue-polish/HarmonyOS-Sans.zip` | 官方字体包 | `tools/content/prepare_fonts.py` 解出游戏字体 |
| `native-support/` | RT64 哈希的本机实现（`libhash.dylib`）与 xBRZ 源码 | `tools/hd_ai/rt64_hash.py` |
| `research/` | 云端模型可用性调研记录 | 文档引用 |

## 3D 模型（`models/`）

资源 5584–5606 的几何导出（`3d-2026-09-09/`），以及世界地图过场舰船与地标的 HD 模型（每个机体一个目录）。
`tools/model_viewer/build.py` 读取前者生成本地模型浏览页；后者见 `docs/native/native-ship-model.md`。

## 清理记录

每条 HD 素材线只保留当前使用或重建需要的版本。2026-09-18 清理过一次早期版本、实验运行和日志，
清单见 `build/cleanup-2026-09-18.tsv`。2026-09-24 的 HD 遗留清理删除了实验阶段的素材
（2026-09-08 基准、第一话中文实验包、地形实验、对白字体与排版实验、旧对话包、苹方字形包、
世界地图画风试验 run-1…run-4 等），清单见 `build/cleanup-2026-09-24.tsv`。
