# assets/：本地导出资源

这里放从 ROM 导出、或由实验生成、需要长期复用的资源。整个目录不进 git（只有本说明文件入库），
也不随仓库分发。与 `build/` 的分工：

| 目录 | 放什么 | 删掉会怎样 |
| --- | --- | --- |
| `build/` | 工具链、编译产物、生成代码、测试输出、运行与探针记录 | 可以用 `make`／工具重建；运行记录只作证据 |
| `assets/` | 导出的数据、HD 素材包、模型等要反复使用的资源 | 需要重新导出，HD 素材可能无法重现 |

## 目录

| 路径 | 内容 | 来源／使用者 |
| --- | --- | --- |
| `original-data/` | 原始数据目录：文本、资源、机体／驾驶员、事件脚本等记录与数据查看器页面 | `make recomp-data`（`tools/content/extract_original.py`）生成，每次整体替换；`tools/data_viewer/serve.py`、迷你关卡与脚本工具读取 `records/` |
| `hd-ai/worldmap-runtime/pack-v6/` | 当前 HD 美术包（F6） | `content/art/stage1-hd.json` |
| `hd-ai/stage1-zh/pack-7/` | 第一话 HD 素材，含姓名页头像 | `content/ui/name-entry.json` |
| `hd-ai/dialogue-runtime/v3/`、`hd-ai/dialogue-type/medium-pack/` | 对白框 HD 素材，世界地图包的构建基础 | `tools/hd_ai/build_*_runtime_pack.py` |
| `hd-ai/terrain-runtime/pack-v5/` | 地形素材最终版 | — |
| `hd-ai/2026-09-08/` | HD 素材的来源：导出样本、AI 生成结果（`runs/`）、评审与图库 | `tools/hd_ai/` 的样本、基准与打包脚本 |
| `hd-ai/dialogue-polish/`、`hd-ai/dialogue-type/` | 字体（`fonts/`、`*-fonts/`）与对白排版实验素材 | `tools/hd_ai/build_stage1_gallery.py`、`tools/recomp/probes/run_coretext_probe.py` |
| `hd-ai/jp-emulator-reference/` | 日版在参考模拟器上的对照截图 | HD 对照与图库 |
| `hd-ai/research/` | 云端模型可用性调研记录 | 文档引用 |
| `models/` | 3D 模型导出：资源 5584–5602 的几何、5600 高模与探针 | `tools/model_viewer/build.py`、`tools/recomp/model5600/` |

每条 HD 素材线只保留当前使用或最终的版本；更早的版本、实验运行和日志已在 2026-09-18 清理，
清单见 `build/cleanup-2026-09-18.tsv`。
