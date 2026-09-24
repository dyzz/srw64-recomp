# 原生内容架构：第一批实现

2026-09-12 范围更新：后续按[内置 MOD 路线图](../design/mod-roadmap.md)推进我们自己的功能模块；本文“下一批工作”中的外部内容类型注册、包依赖与公开 SDK 接入暂缓。已有目录、profile、语言和美术切换继续复用。

2026-09-11 时序修复：已修复下一帧对白快照被误清空的问题；普通试玩默认关闭周期性截图和内存导出，完整探针保持可用。详见 [对白闪烁修复](native-dialogue-flicker.md)。

日期：2026-09-11。此批把统一 JP 基线、外部语言目录、独立美术包与图像切换接入真实宿主。整体设计见 [架构方案](../design/native-extensibility-architecture.md)。

## 可运行入口

双击仓库根目录的 `scripts/Play SRW64 Native.command`，或：

```sh
.venv/bin/python tools/recomp/run/play_native.py \
  --profile config/recomp/profiles/play-profile.json --new-game

# 同一个原始 ROM、同一个存档目录；启动时选择日文和原图。
.venv/bin/python tools/recomp/run/play_native.py \
  --profile config/recomp/profiles/play-profile.json --language ja --images original --new-game
```

默认配置为中文对白、高清图片、原生水滴、4 倍内部渲染分辨率。运行中按 **F6** 在 Original 与 HD 之间来回切换，窗口标题显示当前模式。2026-09-11 起，5600 模型也跟随切换：Original 使用原版菱形，HD 使用配置选定的模型。`presentation.model_5600: waterdrop` 表示 HD 模式启用水滴；设为 `original` 时两种模式均保持原模型。F6 只改变本次运行；下次启动按 profile/命令行选择。语言、原生字体和渲染分辨率不随此开关变化。

新入口的保存历史统一放在 `build/recomp/profile-play/sessions/`，日中与两种图片模式共用同一 JP 游戏身份。2026-09-12 起，未指定 `--new-game` 时按已完成运行报告、ROM 身份和最终摘要选择最近可核验的保存，损坏副本会报告并回退；首次使用原有、已锁定的 JP 第一话通关存档副本。支持列出历史及显式选择，详见[存档恢复](../guide/native-save-recovery.md)。旧补丁 ROM 入口已移除，历史存档不自动迁移。

此入口仍使用本地原始 `rom.z64`、已生成的 recomp 构建与已核验美术文件。资源缺少单张替换时 RT64 使用原图；2026-09-12 起，Original 遇到 HD 包/头像文件缺失可继续启动，并显示 HD 不可用；显式 HD 或摘要不匹配仍报错。详见[Original 回退](native-original-fallback.md)。发布用包分发尚未实现。

## 已拆出的责任

| 模块 | 现阶段实际职责 |
| --- | --- |
| `src/srw64_native/catalog.py` | 从固定原始 ROM 与字形映射生成本地源目录；TextKey、原文摘要、脚本屏障和参数验证 |
| `src/srw64_native/profile.py` | 分别解析语言、图像、模型、字体与分辨率；编译不可变运行配置 |
| `src/srw64_native/assets.py` | 校验纯美术允许清单及每个文件摘要，输出独立 RT64 包 |
| `src/native/localization/` | 宿主 TextKey 查找、对应源文回退、字体/locale/原生 UI 字符串 |
| `src/native/game_adapter/dialogue_source.hpp` | 明确标准对白来自 table 0；识别绘制时实际绑定的字库图集 |
| `src/native/presentation/image_mode.hpp` | 窗口线程只提交图像模式请求，由渲染线程确认应用 |
| `src/host/` | 继续承载已有游戏桥接、读取控制和平台后端；逐步迁移，保留已有验收入口 |

没有移动自动生成的 recomp C 文件，也没有引入第二个代码 Mod 加载器。`gameplay_mods` 在本批必须为空，防止配置看起来接受了一个实际未加载的玩法 Mod。

## 多语言内容

游戏原文从用户本地 ROM 生成。译文分两部分，都使用 Unicode，不占用或扩展 N64 字库：
- **数据文本**（名称、标签、提示，记录 0–5643）：由词条表 `content/locales/terms/` 经 `tools/content/apply_terms.py` 展开进 `content/locales/<语言>.json`，中英文各 4,674 条，见[数据文本词条表](localization-terms.md)。
- **剧情与战斗台词**：玩家可改的台词文本文件 `content/dialogue/<语言>/`，见[台词文本文件](../guide/dialogue-text.md)。

缺译时按完整 TextKey 回退相应的日文源记录。

TextKey 示例为 `base:t00_17412`；另一个表的 `base:t01_17412` 是不同记录。当前标准对白适配器来自原函数 `8008C9C0`，其 `8008CA5C..8008CA6C` 调用固定传入 table 0。其他 UI/文字消费者仍须逐一适配，不能仅因目录包含其记录就宣称已经汉化。

译文允许改变长度、换行和原生阅读分页，必须保留 STOP/END 顺序，以及每段里的动态姓名/专用字形 token。原文摘要变化、重复键、未知键或非法控制符会阻止加载。内容编译只读取原 ROM；不进行 ROM 文本注入。

增加语言：在 `content/locales/terms/` 加一份与中英文同键的词条表并运行 `apply_terms.py`，在 `content/dialogue/<语言>/` 放台词文件，再在 profile 的 `locales` 里登记，用 `--language <语言>` 启动。语言标签无 C++ 枚举，不需因新增语言重编宿主。

可先编译校验而不启动：

```sh
.venv/bin/python tools/content/compile_profile.py \
  --language zh-Hans --images original \
  --output build/recomp/content-preview
```

输出必须是新目录，其中记录原始 ROM、全部注册语言目录、美术清单及生成文件摘要。注册目录在启动前验证并冻结；**运行中 F7 热切换语言，F6 切换图片与 5600 模型**。热切换采用不可变目录和逐帧引用，当前对白不推进到下一片段。设置、覆盖报告和实际验证见[三项底座验证](native-foundations-verification.md)。

当前接入：剧情与战斗对白（原生阅读 UI）和全部原生页面（场间各画面、战前页、姓名页、设置）。原版菜单、开场与结局的图片文字等仍显示原图。玩家姓名保留游戏值；默认主角名还没有本地化。

## 原图与高清图

`content/art/stage1-hd.json` 从已核验包中明确列出 **70 张**纯美术替换：57 张世界地图切片、13 张对话边框切片；头像另列在 `portraits` 段。4842 张字库图和其余未纳入本批清单的贴图不进入这个包；筛选在离线编译时完成，运行时不凭文件名猜资源类别。

来源包是 `assets/hd-ai/portrait-matte/v2/pack`：它在 [v6 包](native-worldmap-hd.md)的基础上只换掉 36 张头像切片，其余文件逐字节相同。现在清单只从中取 70 条世界地图和对白边框替换；头像改为整张 768×768 图，由清单的 `portraits` 段列出，宿主整张绘制，见[人物头像 HD](native-portraits-hd.md)。原头像透明处理有三个缺陷，替换原图后头像边缘出问题（2026-09-23 查明）：

- 透明像素的颜色被存成黑色。RT64 和 RmlUi 页面都按非预乘 Alpha 做双线性采样，于是轮廓外沿出现暗边。
- 模型输出相对原图有偏移和缩放（劳伦斯右偏约 1.5 个原像素，玛娜米缩小约 1%），而遮罩沿用了偏移后的轮廓。结果是一侧被吃掉、另一侧外扩，轮廓内侧 2 个原像素以内出现 13–519 个透光像素。
- 灰底从图像四边一起泛洪，被画框截断的衣服和头发（底边、顶边）出现透光点。

[`portrait_matte.py`](../../tools/hd_ai/portrait_matte.py) 的改法：

- 先把模型输出配准到原图；
- 轮廓只允许在原图遮罩 ±1.5 个原像素内移动；
- 灰底只从原图透明的像素开始泛洪；
- 透明像素填入最近的实色；
- 缩小时颜色与 Alpha 分开重采样。

改后四张头像的轮廓 IoU 为 0.984–0.994，画框截断处 100% 不透明，轮廓内透光像素为 0–3（Lanczos 振铃，Alpha ≥ 245）。模拟非预乘双线性放大 3 倍后，边缘偏差超过 16 级的像素从 203 / 74 / 97 / 0 降为 0。重建命令（输出目录必须不存在）：

```sh
.venv/bin/python -m tools.hd_ai.portrait_matte \
  --portraits assets/hd-ai/stage1-zh/portraits-ai \
  --pack assets/hd-ai/worldmap-runtime/pack-v6/pack \
  --output assets/hd-ai/portrait-matte/v2
```

F6 不卸载 GPU 正在使用的纹理。窗口线程提交请求；渲染提交线程等待已提交 workload/present 完成并空闲后，在 RT64 的 texture-map mutex 内改变替换开关。这样 UV 缩放与纹理描述符会在同一模式下构建；纹理仍由 RT64 管理。5600 的原生替换标记也按已应用模式构建：Original 不添加原生绘制／抑制标记，保留完整八个原版面；HD 才标记水滴替换。此开关目前作用于纯美术包和 5600，不能在以后接入语言贴图时直接混装使用。

`image-mode.json` 记录已应用模式及实际 `model_5600`，`image-mode-events.jsonl` 记录切换；完整诊断模式的 GPU 截图元数据包含应用模式。测试用 `SRW64_WINDOW_CONTROL=1` 文件请求与 F6 共用同一请求/应用路径，不修改游戏内存或存档。

## 验证与后续边界

Python 校验、C++ TextKey/回退/模式请求测试和实际 Core Text 排版测试分别运行。`tools/recomp/verify/verify_profile_images.py` 在真实新游戏剧情中到达 `base:t00_17412` 的同一段，执行原图→高清→原图→高清，核对对白状态不变及静态头像/地图区域往返像素一致。实际运行结果见本文件末尾的验收记录。

下一批工作：

1. 继续把姓名、菜单等消费者纳入 TextKey 服务；默认显示名与自定义名分别处理，完成日中语言选择 UI。
2. 注册 N64ModernRuntime 的 SRW64 内容类型，补充包依赖、版本与加载冲突；当前外部 JSON 是开发用输入，尚非 `.nrm`/公开 SDK。
3. 提取机体、人物、武器的已知字段 schema，先无修改往返，再选一个字段验证资料页、实际结算和保存一致。
4. 关卡部署与事件先做结构化提取/往返；新增单位或关卡需要容量与存档映射验证后再开放。


## 2026-09-11 实际验收

- `make check`：60 项 Python 测试、compileall、依赖检查通过；`make recomp-content-test`：C++ 内容/适配测试及实际 Core Text 对话排版/阅读控制测试通过。
- 四种启动配置（日/中 × 原图/高清）编译通过，均为同一锁定 JP ROM。两次实机的手写宿主源码摘要一致，语言通过数据选择。
- `build/recomp/profile-check/live-zh-3/`：7848 VI 正常退出；`live-ja-3/`：8064 VI 正常退出。两次均从新游戏运行至 `base:t00_17412` 的 segment 1。
- 每种语言实际截取原图→高清→原图→高清四帧。头像 ROI `[48,45,330,335]` 有 70680 个像素发生变化，地图 ROI `[0,0,100,45]` 有 4500 个像素变化；两个区域切回原图及高清均逐像素一致。对白事件、owner、页码、揭示进度与内容在切图期间保持不变。
- 两种语言均确认 57 张世界地图纹理在 RT64 实际缓存中达到 512×512、UV 缩放 8 倍；原生水滴分别绘制 1398 / 1608 次。人工检查完成的 GPU 截图，确认中文/日文、原图/高清和现代模型同时生效。
- 已确认绘制中的字库为运行时 504×504 图集；原始 ROM 资源 1 的 504×252 文件头不能直接作为绘制绑定的判断条件。适配层按实际图集识别。

汇总证据：[acceptance.json](../../build/recomp/profile-check/acceptance.json)。截图：[中文原图](../../build/recomp/profile-check/live-zh-3/profile-checks/original.png)、[中文高清](../../build/recomp/profile-check/live-zh-3/profile-checks/hd.png)、[日文高清](../../build/recomp/profile-check/live-ja-3/profile-checks/hd.png)。本次未覆盖全游戏、旧中文存档迁移或实体键盘自动化；F6 与测试请求共用同一个应用路径。
