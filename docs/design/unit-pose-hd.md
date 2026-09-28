# 机体立绘 HD：规模与首轮试做

2026-09-26。战前确认、改造、能力、のりかえ 等原生页面画的机体大图来自 `battle_assets.units`（机体基本姿势，按 场景/图集/调色板 三元组去重），页面最多放大 6 倍，像素块很明显。本文记录用 `qwen-image-3.0-pro` 重画的规模估算和 3 台机体的试做结果。工具：[`unit_pose_hd.py`](../../tools/hd_ai/unit_pose_hd.py)，输出在 `assets/hd-ai/unit-poses/`（不进 git）。

## 规模

```sh
.venv/bin/python -m tools.hd_ai.unit_pose_hd scale
```

| 项 | 数 |
| --- | ---: |
| 机体 | 363 |
| 去重后的基本姿势 | 332 |
| 96×96 | 249 |
| 128×128 | 57 |
| 128×96 | 16 |
| 其余（32×32 ×3、128×64 ×3、96×97、160×128、144×144、130×228） | 10 |

费用按单张一次请求、一个候选、原价计：

| 做法 | 请求数 | 费用（元） |
| --- | ---: | ---: |
| 单张，`qwen-image-3.0-pro` | 332 | 约 173 |
| 单张，`qwen-image-3.0` | 332 | 约 66 |
| 2×2 拼图，Pro（机体上未验证） | 83 | 约 43 |
| 3×3 拼图，Pro（未验证） | 37 | 约 19 |

被 IP 过滤器拒绝的请求不计费（见下）。`aliyun.py` 每个输出目录有 18.12 元的预留上限，全量跑之前要放开。

## 试做

三台：3 シャイニングガンダム（96）、177 真・ゲッター1（128）、156 暗黒大将軍（128）。每张：灰底 (100,100,112)，预放大 6 倍，输出 2048²，配准与抠图沿用 `portrait_matte`，母版 8 倍（96 → 768）。

| 目录 | 预放大 | 提示词 | 结果 |
| --- | --- | --- | --- |
| test-1 | 最近邻 | `faithful`（写明是"像素画立绘"） | 两台都通过；Pro 把它当像素画照抄，只是把线条理顺，锯齿全部保留。配准 IoU 0.995 / 0.987 |
| test-2 | 最近邻 | `smooth`（要求消除锯齿，画成赛璐璐插画） | シャイニング 半像素半线稿，横向缩了 3.5%，配准超阈值；ゲッター 被拒 |
| **test-3** | **双三次** | **`smooth`** | **シャイニング 是干净的赛璐璐线稿，结构忠实，配准 x1.000/y0.998、IoU 0.986，边缘滤波误差均值 0.86。定为基线。** ゲッター 被拒 |
| test-4 | 双三次 | `smooth-plain`（去掉 SD/二头身 用词） | ゲッター 仍被拒 |
| test-5 | 双三次 | `faithful` | ゲッター 被拒（推理 45 秒后，输出侧）；暗黒大将軍 5.7 秒被拒（输入侧） |

**结论**

- 双三次预放大 + `smooth` 提示词是可用的做法；最近邻输入会引导模型保留像素风。
- **IP 过滤是主要风险**：真・ゲッター1 只有像素风的那一版通过，画成平滑线稿就被输出侧过滤器拦下；暗黒大将軍 连输入都不收。3 台里只有 1 台按基线做法成功，全量的通过率还没有数据，估计前先抽 10–20 台跑一遍（被拒不计费，只付成功的）。
- 被拒的机体没有替代方案时，可以退回 `faithful`+最近邻（test-1 那种"理线不去锯齿"的结果），或本地算法放大。

## 接入

已做，见下文「定案与接入」。HD 图按 `battle_assets` 的键命名（`unit-<scene>-<atlas>-<palette>.png`），页面里 `unit_art` 按 alpha 裁 `rect`，母版的透明区域用最近实色填充，不会出暗边。

## 本地 ESRGAN 对照（2026-09-26）

[`esrgan_pose.py`](../../tools/hd_ai/esrgan_pose.py) 用 spandrel 跑社区 4x ESRGAN 模型：颜色图先把透明像素填成最近实色，alpha 当灰度图单独过模型，两趟（16 倍）再 Lanczos 缩到 8 倍母版。环境 `build/esrgan-venv`（python3.14 + torch + spandrel），模型 `build/esrgan-models/`（4x-PixelPerfectV4 WTFPL；4x-AnimeSharp CC-BY-NC-SA 4.0，Kim2091），都不进 git、不进公开包。输出 `assets/hd-ai/unit-poses/esrgan-1/`，对比页每行：原图、AnimeSharp ×1/×2 趟、PixelPerfectV4 ×1/×2 趟、Pro（有的话）。

| 项 | 结果 |
| --- | --- |
| 速度 | MPS 上 96² 一趟 0.6 秒、两趟 1.3 秒；128² 约两倍。332 张几分钟跑完，零费用，无 IP 过滤 |
| 忠实度 | 逐像素对应，没有任何结构漂移，alpha 边缘平滑 |
| AnimeSharp | 硬边、深色描线，最接近赛璐璐线稿；两趟比一趟干净。适合 シャイニング、ゲッター 这类硬边像素画 |
| PixelPerfectV4 | 更柔和、偏绘画感，色阶过渡顺；适合 暗黒大将軍 这类本来就带明暗渐变的图 |
| 对比 Pro | Pro 的线条更像人手勾的、块面更整洁，但细节有小改动（胸口、手指）；ESRGAN 线条略有"抖"感，细节全保留 |

结论：ESRGAN 可以作为全量的底，两台被 IP 过滤拒掉的机体也都处理了；Pro 只作主要机体的加分项。

## 定案与接入（2026-09-26）

14 个本地模型（[`esrgan_pose.py`](../../tools/hd_ai/esrgan_pose.py)，输出 `assets/hd-ai/unit-poses/esrgan-2`）在三台机体上对比后，**定案：4x-UltraSharpV2 与 4x-PixelPerfectV4 各半混合**。UltraSharpV2（DAT）细节最多、线条最干净，但用户觉得稍锐；六种减锐做法（一趟加 Lanczos、1.5 / 2.5 px 高斯、与 BS-Deviance / PixelPerfectV4 各半混合，`esrgan-5`）里选了 PixelPerfectV4 混合。淘汰：AnimeSharpV4_RCAN、HFA2k_realplksr（发灰重影）、Drawimation（糊）、NumericFrames（过黑）、Faithful-Lite（保留像素感）、两个 2x AnimeSharp（三趟叠加发软或出颗粒）。2026-09-27 起 `build/esrgan-models/` 只留定案用的 4x-UltraSharpV2 与 4x-PixelPerfectV4（图标流水线也只用后者），其余对比模型已删，要重做对比时从 OpenModelDB 重新下载。

流水线：

```sh
.venv/bin/python -m tools.hd_ai.unit_pose_hd prepare --output assets/hd-ai/unit-poses/all-1 --all --prescale bicubic
build/esrgan-venv/bin/python tools/hd_ai/esrgan_pose.py --samples assets/hd-ai/unit-poses/all-1 \
    --models build/esrgan-models --output assets/hd-ai/unit-poses/all-1 --blend 4x-UltraSharpV2 4x-PixelPerfectV4
.venv/bin/python -m tools.hd_ai.build_unit_images --run assets/hd-ai/unit-poses/all-1 --output assets/hd-ai/unit-poses/whole-v1 --bind
```

- `esrgan_pose.py --blend` 对每张姿势跑两个模型（各两趟到 16 倍，Lanczos 回 8 倍；alpha 单独过模型），写 `hd/unit-<scene>-<atlas>-<palette>.png`。332 张在 MPS 上约 1 小时。
- [`build_unit_images.py`](../../tools/hd_ai/build_unit_images.py) 生成包目录 `whole-v1/`（PNG 保留 alpha）和 `units.json`（`srw64.unit-images.v1`，按三元组索引），`--bind` 把 `units` 段写进 `content/art/stage1-hd.json`。
- 接入沿用头像的路子：`compile_art` 复制成 `art/units/` 和 `srw64-units-hd.json`；`assets.unit_lookup` 按三元组找文件；`profile.py` → `prepare_battle_assets(..., hd_unit)` 给 `battle_assets.units[n]` 加 `hd`；发布版启动器 `launch.cpp` 读同一索引按 `resources` 三元组挂 `hd`（C++ 导入器的机体条目新增 `resources`）。
- 页面：战前确认页 `battle_unit` 与改造页用 `portrait_path()`（HD 模式且带 `hd` 时取 HD 文件），能力／のりかえ／存档页原本就走 `portrait_path`，无需改。确认页放大上限按 ROM 像素算（6 × ROM 宽 ÷ 文件宽），HD 文件按显示宽度重采样。
- 实机核对：[`check_unit_pose_hd.py`](../../tools/recomp/debug/check_unit_pose_hd.py)，HD 进战前确认页截图，F6 切原图对照。
- 版权：模型的许可见 [`esrgan_pose.py`](../../tools/hd_ai/esrgan_pose.py) 的说明；产物由原版画面衍生，随 HD 包发布（公开包与自用相同，2026-09-28 用户定），NOTICE 写明所用模型。发布包里存 6 倍 ROM 像素的 JPEG 加透明 PNG（`compress_hd.py`）。

## 战斗动画能否照此处理

可以，而且就是盘点里定的"图集母版 + 零件切片"路线，但不是把整张图集丢进模型那么简单，有三处要做：

1. **零件是按块加载的贴图。** 每个零件由 `8009761C` 用 LoadTile 从 CI8 图集里装一块（多为 32×32，最大 2 KB TMEM），RT64 对装入的那一块算哈希做替换。所以 HD 要按零件切片，而不是按整张图集；同一区域被不同尺寸装载算不同贴图。CI8 块的 RT64 哈希算法还没有像 CI4 那样对过 TMEM 转储（`rt64_hash.py` 只有字库和地图两种），要先验证。
2. **接缝。** 图集里相邻的零件在画面上未必相邻，整张图集放大会把邻块的颜色混进边缘。稳妥做法是像姿势那样先合成整帧再放大，然后按零件在帧里的位置切回去（导出器已经能按零件记录合成每一帧）；模式 1 的缩放旋转零件用未变形的帧。
3. **按调色板分别生成。** ESRGAN 只认 RGB，296 张图集配 305 套调色板（敌我配色差分），每套都要单独出图；切片按 4 倍存（32×32 → 128×128）就够，8 倍的体积没必要。

范围限于机体图集、武器零件、盾和 cut-in；特效走调色板动画，哈希每跳都变，不适用（战术地图那节已有同样结论）。这条路线不碰战斗逻辑和时序，只换贴图，符合"战斗演出不改结算"的约束。

**首次实机的两个错（已修）**：ESRGAN 的 alpha 在整张画布上留有零星微弱值，页面按 alpha 算的 `rect` 变成整张文件，机体缩小并在透明画布里飘；`clean_alpha`（`build_unit_images.py`，`esrgan_pose.run_pose` 也调用）把 alpha 限制在原掩码外扩 12 px 内并去掉小于 8 的值，页面算边界也只数 alpha ≥ 16 的像素。另外确认页的 `<img>` 不能按显示宽度重采样，`rect` 以文件像素为单位。修后 `check_unit_pose_hd.py` 截图与原图版尺寸、位置一致。

## 机体大图按尺寸等级缩放（2026-09-26，评估中）

用户提出机体大图应按"体积"缩放，而不是每台都撑满区域。数据来源是机体记录的尺寸等级：ROM 机体表（`0x71B80`，36 字节一条）**+4 的低 5 位** 是 1/2/4/8/0x10 → SS/S/M/L/LL（运行时记录 `+0x0C`，能力页 `801D1680` 同一算法；高位 0x80 另有含义）。363 台按等级和姿势像素统计：

| 等级 | 台数 | 姿势画布 | 像素包围盒高（最小／中位／最大） |
| --- | ---: | --- | --- |
| SS | 5 | 32² ×3（ドモン、マスターアジア、アルベルト 生身）、96² ×2 | 18 / 25 / 87 |
| S | 57 | 96² ×55、128×96 ×2 | 39 / 84 / 96 |
| M | 205 | 96² ×173、128² ×20、128×96 ×10、130×228 ×1 | 40 / 87 / 122 |
| L | 58 | 96² ×46、128² ×10、144² ×1、128×96 ×1 | 65 / 88 / 126 |
| LL | 38 | 128² ×28、160×128、128×64 ×3、128×96 ×3、96² ×3 | 53 / 94 / 128 |

结论：**精灵像素大小不反映尺寸等级**。S、M、L 三级绝大多数都画在 96² 画布、包围盒 85–90 px 上下（ダンバイン 系 S 级也是 96 px），只有 LL 才普遍用 128²；同一等级内又有 Gフォートレス（96×40）、バトルクラフト（60×39）这类扁平或小图。所以"按 ROM 像素统一比例"行不通，要按机体数据的等级来。

草案（已在工作区实现，未提交）：战前确认页快照带 `size`（0–4），页面按等级取区域的一个份额再按包围盒适配，仍保留 6 倍 ROM 像素上限。第一版 LL 100%、L 86%、M 72%、S 58%、SS 46%；用户看后要求驾驶员面板再压缩、机体再大、SS/S 更小、LL 更有压迫感，第二版：能力效果区 70dp → 44dp，机体区高度常数 462 → 436、上限 320 → 360dp（960×720 逻辑窗口下机体框 258 → 300dp），份额 LL 100%、L 84%、M 70%、S 52%、SS 30%。第三版（定案）：LL 提到 115%，上顶横幅、下抵驾驶员面板，刚好不压别的元素；试验关卡 `config/recomp/mini-stages/battle-ui-ll.json`（敌方换成 デビルガンダム），`check_unit_pose_hd.py` 可传关卡路径。示意图（每级三台，从 whole-v1 离线合成）和实机截图（ゼーロン L 对 ミニフォー S）已给用户看。待定：份额数值；32² 的生身角色受 6 倍上限只有约 120 px，是否要放宽；362 シュバルツ 等占位记录标 SS 但借用 シャイニング 的图。
