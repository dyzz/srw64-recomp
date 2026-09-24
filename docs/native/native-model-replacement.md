# 原生 3D 模型替换：5600 标记

同一套接入后来扩展到世界地图过场的舰船、地标与航迹，见[世界地图过场模型 HD](native-ship-model.md)。

## 2026-09-24：保留原版棱角的 HD 标记

HD 造型从圆润水滴改回原版的形状。原版 5600 是上短下长的双四棱锥：腰部方环在 Y = 6，四个角为 (±5, 6, ±5)；上尖在 Y = 12，下尖在 Y = −12，共 8 个面，由 ROM 资源的两条 `G_VTX` 和八条 `TRI1` 解出。

- **网格**：由 [`prepare_native_marker.py`](../../tools/recomp/model5600/prepare_native_marker.py) 生成。做法是把双锥各面向内平移 0.5 个单位，再与半径 0.5 的球做闵可夫斯基和：
  - 8 个面仍是原来的平面，平面着色；
  - 12 条棱变成圆柱面，6 个尖角变成球面，法线连续；
  - 两个尖顶向外补偿圆角缩进的量，所以 Y 仍是 −12…12，X/Z 在 ±5 以内；
  - 共 510 个顶点、1,016 个三角形，是封闭曲面。
- **材质**（`native_marker.cpp` 的 5600 片元着色器）：
  - 底色从橙金 (1.0, 0.57, 0.045) 改为接近原版明黄的 (1.0, 0.82, 0.16)，漫反射减弱；
  - 按反射方向在天光 (1.0, 0.96, 0.80) 与暖地面 (0.55, 0.38, 0.08) 之间取色，再加一道地平线高光带；
  - 平面按朝向明暗分明，游戏转动它时，高光带会依次扫过各个面和倒角棱线。
- **实机**（第 8 话迷你关卡世界地图，HD 模式）：金黄色八面体，棱线发亮；F6 仍可切回原版菱形。
- 资源包在 `build/recomp/native-marker/assets` 重新生成，旧的水滴包移到 `assets-waterdrop-20260910`。模型查看器显示为「原生 HD · 1,016 面」。原有验收证据是为水滴录的，网格摘要不符时不再显示。
- profile 的取值仍叫 `model_5600: waterdrop`，只是名称沿用，实际画的是这个棱角标记。

以下是第一版（水滴）的记录。

2026-09-10 的第一版将剧情地图上的黄色菱形替换为圆润金色水滴。网格、法线和材质由宿主 GPU 渲染，原游戏继续提供位置、旋转、相机和剧情时序。使用原始日版 ROM。早期把 96 面网格编进资源的 ROM 实验已于 2026-09-24 删除。

## 试玩

统一入口 `scripts/Play SRW64 Native.command` 的 **F6** 已支持图片和 5600 模型一起切换：HD 为原生 HD 标记（2026-09-24 前为水滴），Original 恢复原版八面菱形；可以在剧情运行中来回切换。若 profile 的 `presentation.model_5600` 设为 `original`，HD 下也保持原模型。

在仓库根目录执行：

```sh
.venv/bin/python tools/recomp/run/play_native.py --native-waterdrop --new-game
```

选择 New Game → 女性超级系，默认姓名进入第一话开场。方向键移动，Z 确认，X 取消，Enter 为 Start，Esc 关闭窗口。此入口使用 `build/recomp/native-marker/play/` 下的独立试玩记录和存档；首次运行会在缺少资源包时生成水滴网格。使用其他试玩入口可回到原版。

本地资源页的 5600 支持原版 8 面与原生水滴 3,968 面两个选项。页面使用同一份几何和连续法线，金色材质为网页近似；游戏中的光照以 GPU 对照图为准。

## 渲染接入

`tools/recomp/toolchain/native_model_hook_patches.py` 通过 `prepare_rt64.py` 对锁定的 RT64 版本应用窄补丁：

1. F3DEX2 的 TRI1 处理器调用宿主识别函数。识别以当前 segment 4 为基址，核对完整的 7,048 字节原版 5600 资源，并检查八条立体三角形命令的偏移。堆地址变化不影响识别，虚线环不被标记。
2. 原本体的八条命令分别形成独立 draw call。首条携带原生绘制标记，另外七条携带抑制标记。标记随 draw call 复制进不可变的 Workload。
3. RT64 提交该 raster draw 时调用宿主渲染器，从对应 Workload 获取世界变换、视图投影、RSP viewport、scissor 和屏幕缩放。GPU 回调不读取最新一帧的 RDRAM。
4. `src/host/native_marker.cpp` 在同一个 command buffer 中，以 Load/Store 方式使用当时的场景 color/depth attachment 绘制浮点网格，遵循原 draw 的深度比较和写入设置。随后恢复 RT64 的图形状态，继续绘制场景及头像、文字。

水滴包含 1,986 个顶点、3,968 个三角形，使用 32 位索引。CPU 在启动时上传宿主缓冲区，不经过 N64 顶点缓存或整型顶点格式。局部高度保持 Y = −12…12，最大横向半径约 5.5；虚线环仍使用原几何和贴图。

材质使用逐像素法线插值、柔和主光、补光、高光和边缘反射近似。当前为不透明金色，不包含折射、动态投影或真实环境反射。水滴绕轴对称，因此即使继承了原旋转，外形也不会像菱形那样明显变化。

## 验证证据

2026-09-11 的模式联动验收在 `build/recomp/model-mode-check/acceptance.json`：四次相同任务 GPU 回放确认 Original 加载水滴包与完全不加载模型包逐像素一致，HD 的模型差异只在 `[461,320,500,363]` 标记区域内；同一原生游戏运行完成 Original → HD → Original → HD，对白状态保持一致，头像／地图静态区域往返逐像素恢复。图形与内容检查、60 项 Python 测试通过，实机测试全程静音。模型包仍驻留 GPU，仅在构建新任务时选择是否替换。

实际运行截图：[Original](../../build/recomp/model-mode-check/live-1/profile-checks/original.png)、[HD](../../build/recomp/model-mode-check/live-1/profile-checks/hd.png)。联动测试的输入使用与 F6 共用的请求路径；未模拟实体 F6 键。可在带 `SRW64_WINDOW_CONTROL=1`、`--original-name-entry` 和 `intro-skip-female.json` 的新运行中执行 `verify_profile_images.py RUN_DIRECTORY --start-vi 3400 --model-5600` 复测。

`build/recomp/native-marker/acceptance.json` 由 `tools/recomp/model5600/verify_native_marker.py` 检查并生成，资源页构建时再次校验资源包及证据文件哈希。

- **同任务开关对照**：`replay-original-2` 与 `replay-3` 使用相同程序和原始任务快照。差异仅在 5600 本体周围，地图、虚线环、头像和文字保持一致。
- **识别失败回退**：`replay-rejected-1` 仅改变资源尾部一个未执行的元数据字节，完整资源身份核对失败；原生绘制数为零，GPU 输出与原版一致。
- **受控遮挡**：`replay-visible-control-1` 将宿主几何移到头像上方，水滴可见；`replay-occluded-1` 将它移到不透明头像后方，水滴被遮挡。此项是捕获任务的定向测试。
- **实际游戏运行**：`live-1` 从空 SRAM 使用 `female-to-map.json` 输入运行 16,800 VI，约 280.8 秒，正常退出，经过欧洲大陆、撒丁岛剧情并进入第一话战术地图。记录 5,882 次原生绘制以及 26 种采样变换；RT64 分别绘制原分辨率与放大画面，绘制次数不等于游戏帧数。诊断日志早期字段 `world` 实际存储的是 `world * viewProj`。
- **资源包检查**：完整文件集合、原资源身份、二进制网格与网页几何一致性、有限浮点值、单位法线及索引范围均在启动前检查。

重跑验证：

```sh
.venv/bin/python -m unittest discover -s tests -p 'test_native_marker.py'
.venv/bin/python tools/recomp/model5600/verify_native_marker.py
.venv/bin/python tools/model_viewer/build.py
```

原始 ROM 的 SHA-256 为 `ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e`。统一 profile 的默认 HD 模式启用水滴；上述独立模型实验仍按显式资源包选择，不依赖图像开关。当前适配 macOS Metal 的 raster 路径，验证范围是女主开场与第一话战术地图。其他资源、多部件动画、透明材质、不同图形后端仍需分别接入和验证。
