# 相近 N64 recomp 项目与 SRW64 增强可行性

核查日期：2026-09-10。

本次核查四个游戏项目、五个牧场物语 Mod 的公开源码，并对照发布说明及 SRW64 当前宿主实现。外部项目没有在本机编译或运行；“源码已实现”和“项目方发布/验证”分别描述。当前分支可能包含尚未进入发行版的修改。SRW64 的运行依据沿用已有验收记录，本次没有新增游戏运行或修改游戏实现。

结论：可借鉴的成熟方向集中在原生设置与 Mod、2D 运动插值、分场景宽屏、状态信息和资源编辑。SRW64 已有原生中文对白、局部高清贴图和独立 GPU 模型原型，下一步可以将它们整合为可配置版本，再扩展信息侧栏、战斗节奏和场景画面。外部项目的代码不能直接替代 SRW64 的资源、对象与脚本映射工作。

## 1. 核查的源码版本

| 项目 | 核查提交 | 范围 |
| --- | --- | --- |
| [Harvest Moon 64 Recompiled](https://github.com/HarvestMoon64Recomp/HarvestMoon64Recomp) | `399a4f2b82a8b8dde4bb033e81f8a9b41d796a32` | 地图/精灵插值、宽屏、启动器和 Mod 接口 |
| [Trouble Makers](https://github.com/ThiagoLira/trouble-makers-pc-recomp) | `765b179c6f4bfbbb65dc7b371035798ca573202b` | 宽屏、场景回退、快进、已知显示问题；对应 v0.8.2 预发布 |
| [Dr. Mario 64 Recomp Plus](https://github.com/theboy181/drmario64_recomp_plus) | `af91e3bf56b1ffc329ff4327fdc2380515463de7` | 胶囊绘制补丁与公开功能说明 |
| [Paper Mario ReCut](https://github.com/SMCGames/Paper-Mario-ReCut) | `098be0a501eecd5bb894a47964061d05eeedc3a2` | 纹理导出、替换热更新、图集编辑器 |
| [HM64 Stats Display](https://github.com/SrBananaMan/HarvestMoon64StatsDisplayMod) | `2d4b700434002a55611d4bf2e075c0f6301f9db8` | 游戏字段读取、分页状态 UI、刷新回调 |
| [HM64 Enhanced](https://github.com/HarvestMoon64Recomp/HarvestMoon64EnhancedMod) | `b13995f5662fa5068958f3361c5136a8e67e297f` | 文字速度、音乐延续、地图加载优化 |
| [HM64 Custom Audio](https://github.com/harvestwhisperer/HarvestMoon64RecompAudioMod) | `2c5ad56aaff0239a09d76f464a10545cfb2ef3b2` | 外部 sequence 文件、曲目映射和音频调用补丁 |
| [HM64 FOV Config](https://github.com/SrBananaMan/HarvestMoon64FOVConfigMod) | `3ff901b4f916acf596f73deed58727d4edad80bd` | 正交视域缩放与 UI 尺寸补偿 |
| [HM64 Clock Speed](https://github.com/SrBananaMan/HarvestMoon64ClockSpeedMod) | `73e8bd781807b5411d79f354162464b47cb6447f` | 游戏内时钟步进倍率 |

## 2. 他们具体做了什么

### 牧场物语64：混合场景与原生扩展

- **插值有对象身份和分类策略。** 精灵通过 matrix group ID 对应帧间对象；部分背景禁止插值。地图构建尽量保持地块顺序稳定；拓扑或贴图区域发生一次性变化时，跳过顶点/贴图插值，保留整体变换插值。不能把两帧里相同序号的顶点自动视为同一对象。[精灵实现](https://github.com/HarvestMoon64Recomp/HarvestMoon64Recomp/blob/399a4f2b82a8b8dde4bb033e81f8a9b41d796a32/patches/sprites.c) · [地图构建](https://github.com/HarvestMoon64Recomp/HarvestMoon64Recomp/blob/399a4f2b82a8b8dde4bb033e81f8a9b41d796a32/patches/culling.c) · [插值策略](https://github.com/HarvestMoon64Recomp/HarvestMoon64Recomp/blob/399a4f2b82a8b8dde4bb033e81f8a9b41d796a32/patches/patches.h)
- **宽屏需要扩大原游戏提交范围。** 当前地图补丁取消 tile 可见性剔除，并扩大双缓冲显示列表与顶点容量；另有 2D 背景扩展标记。v1.2.0/1.2.1 的发布记录还单独修复了雨雪范围、教程背景平铺和 NPC 等对象的插值。[发布记录](https://github.com/HarvestMoon64Recomp/HarvestMoon64Recomp/releases) · [2D 宽屏分类](https://github.com/HarvestMoon64Recomp/HarvestMoon64Recomp/blob/399a4f2b82a8b8dde4bb033e81f8a9b41d796a32/patches/widescreen.c)
- **设置和 Mod 是产品的一部分。** 主程序接入 RecompUI，注册 UI 导出、纹理包启停和更新回调；游戏循环提供 UI 回调执行时机。[启动与 Mod 注册](https://github.com/HarvestMoon64Recomp/HarvestMoon64Recomp/blob/399a4f2b82a8b8dde4bb033e81f8a9b41d796a32/src/main/main.cpp)

其独立 Mod 提供了更适合 SRW64 借鉴的例子：

| Mod | 源码中的实际行为 | 对 SRW64 的启发 |
| --- | --- | --- |
| [Stats Display](https://github.com/SrBananaMan/HarvestMoon64StatsDisplayMod/blob/2d4b700434002a55611d4bf2e075c0f6301f9db8/src/stats_display.c) | 从游戏状态读取体力、时间、资金、人物好感、进度等；在游戏回调中按间隔刷新分页 UI | 选中单位的 HP/EN、气力、行动状态、地形和武器侧栏 |
| [FOV Config](https://github.com/SrBananaMan/HarvestMoon64FOVConfigMod/blob/3ff901b4f916acf596f73deed58727d4edad80bd/src/fov_config.c) | 修改相机左右上下正交边界，同时补偿不随场景变换的 UI 节点 | 地图缩放与 UI 分离；需先恢复 SRW64 摄像机、tile 范围和光标关系 |
| [Enhanced 文字配置](https://github.com/HarvestMoon64Recomp/HarvestMoon64EnhancedMod/blob/b13995f5662fa5068958f3361c5136a8e67e297f/src/message_box.c) | 改文字出现、滚动速度和文字音效设置 | 我们的标准对白已具备可调速度、分页和回看，主要缺统一设置和更多界面覆盖 |
| [Enhanced 地图优化](https://github.com/HarvestMoon64Recomp/HarvestMoon64EnhancedMod/blob/b13995f5662fa5068958f3361c5136a8e67e297f/src/map_loading.c) | 批量载入时减少重复重建，缓存复用资源和地形高度 | 用测量找到加载停顿的具体重复工作，再优化相应函数 |
| [Clock Speed](https://github.com/SrBananaMan/HarvestMoon64ClockSpeedMod/blob/73e8bd781807b5411d79f354162464b47cb6447f/src/clock_speed.c) | 只调整游戏内时间的步进倍率 | 可以分别控制特定流程的速度；这不是全游戏倍速或战斗跳过的证明 |
| [Custom Audio](https://github.com/harvestwhisperer/HarvestMoon64RecompAudioMod/blob/2c5ad56aaff0239a09d76f464a10545cfb2ef3b2/src/custom_music.c) | 从宿主加载 `.seq`，映射地图/剧情音乐，交回原音频引擎播放；工具将 MIDI 转成 seq | 支持按曲目 ID 替换音乐的路线可参考；直接播放 OGG/FLAC 需要另建宿主播放与同步路径 |

### Mischief Makers：逐场景宽屏、插值和快进

代码与工程文档显示，项目扩展了精灵/背景 tile 的绘制、裁剪和部分对象的生成/消失范围；对固定画布的过场保留 4:3，并在稳定进入操作场景后恢复宽屏。[渲染分析](https://github.com/ThiagoLira/trouble-makers-pc-recomp/blob/765b179c6f4bfbbb65dc7b371035798ca573202b/docs/README.md)

高帧率是基于原生 60 Hz 游戏帧的显示插值。导弹尾焰等快速更换的精灵会出现错误匹配，项目对特定场景和选关界面关闭插值，离开后恢复用户选择。v0.8.2 还针对雪山背景做了单独的全景扩展；README 对普通滚动场景的“不拉伸”描述不能外推到所有背景。[场景策略](https://github.com/ThiagoLira/trouble-makers-pc-recomp/blob/765b179c6f4bfbbb65dc7b371035798ca573202b/src/game/presentation.h) · [v0.8.2](https://github.com/ThiagoLira/trouble-makers-pc-recomp/releases/tag/v0.8.2)

按住 Tab 的 3 倍快进通过宿主运行库速度倍率实现，修改 VI/计时速率。它不同于“只加速战斗演出、保持音乐正常”的功能；移到 SRW64 前仍需验证计时连续性、音频队列和输入边界。[快进入口](https://github.com/ThiagoLira/trouble-makers-pc-recomp/blob/765b179c6f4bfbbb65dc7b371035798ca573202b/src/game/main.cpp) · [运行库补丁](https://github.com/ThiagoLira/trouble-makers-pc-recomp/blob/765b179c6f4bfbbb65dc7b371035798ca573202b/patches/N64ModernRuntime/0004-Add-ultramodern-set_speed_multiplier-for-host-fast-f.patch)

项目明确保留两类问题：多部件角色在插值时可能模糊或错位，精灵/地形条带拼缝也不会自动被抗锯齿消除。全流程验证仍在待办中。[已知问题](https://github.com/ThiagoLira/trouble-makers-pc-recomp/blob/765b179c6f4bfbbb65dc7b371035798ca573202b/KNOWN_ISSUES.md)

### 马里奥医生64：局部 2D 绘制改造

“胶囊改用 GPU 绘制”的具体实现，是替换原 CPU 绘制函数，装载 CI4 胶囊图集、为左右半颗装载调色板，并发出纹理矩形命令，继续由 RT64 绘制。它证明局部绘制函数可以重写，并不表示已经使用独立高模或现代 PBR 材质。[胶囊补丁](https://github.com/theboy181/drmario64_recomp_plus/blob/af91e3bf56b1ffc329ff4327fdc2380515463de7/patches/theboy181_workspace.c)

README 另列出高帧率插值、四人手柄、CRT 效果，同时注明整体测试很少。本次没有将模板继承的其他源文件视为已完成的游戏功能。[项目说明](https://github.com/theboy181/drmario64_recomp_plus/blob/af91e3bf56b1ffc329ff4327fdc2380515463de7/README.md)

### Paper Mario ReCut：把素材替换做成编辑流程

Paper Atlas Tool 支持将导出的零碎 PNG 拖放拼成图集、保存布局、交给外部图像编辑器修改，再按原布局切回替换目录。游戏渲染上下文每 750 ms 检查替换目录的最新修改时间，变化后重新加载。[图集工具](https://github.com/SMCGames/Paper-Mario-ReCut/blob/098be0a501eecd5bb894a47964061d05eeedc3a2/tools/PaperAtlasTool/README.md) · [图集编辑代码](https://github.com/SMCGames/Paper-Mario-ReCut/blob/098be0a501eecd5bb894a47964061d05eeedc3a2/tools/PaperAtlasTool/MainForm.cs) · [替换加载](https://github.com/SMCGames/Paper-Mario-ReCut/blob/098be0a501eecd5bb894a47964061d05eeedc3a2/src/paper_rt64_context.cpp)

SRW64 已有地图拼图构建、头像切块和资源查看器，适合把现有脚本组织成“查看使用场景 → 导出完整画面 → 导入修改 → 自动切片 → 游戏内对照”的编辑流程。热更新首版可以只限开发模式，正式包继续保留身份校验与明确版本。

## 3. 对照 SRW64：可以做到什么

以下难度是根据已知入口作出的工程判断，不是工期承诺。

| 目标 | 当前基础 | 需要补的工作 | 相对难度 |
| --- | --- | --- | --- |
| 统一增强设置 | 已有高清、原生对白、模型实验开关，但处在不同试玩配置 | 合并兼容配置，加入设置面板、持久化选项及实体手柄映射；联合验证 | 中 |
| 素材编辑与包管理 | RT64 贴图替换、地图/头像构建脚本、3D 查看器 | 整图导入导出、切片映射、场景关联、A/B 开关、包依赖与冲突管理 | 中 |
| 战术信息侧栏 | 已有 Core Text/Metal UI 与游戏运行钩子 | 恢复当前选中单位及字段，发布一致快照；与原菜单核对 | 中到高 |
| 战斗演出 2×/4× | 已有对白快读和开场跳过，战斗完整流程可运行 | 定位演出步进、等待与结算边界；选择时恢复正常速度；核对 HP/EN、随机推进、奖励、剧情及声音 | 中到高 |
| 地图、镜头和部分精灵平滑移动 | RT64 已接入；当前仍使用原始刷新模式 | 稳定对象身份、前后帧变换、场景失效、单独的插值策略；原生模型也要使用相同呈现时刻 | 高 |
| 实际 16:9 视野 | 当前为居中 4:3 | 地图/战斗分别扩展投影、tile 提交、裁剪、背景和特效；光标与选择保持一致 | 高 |
| 高清战斗背景与现代模型材质 | 5600 独立 GPU 网格、光照、遮挡原型已验证 | 找到具体战斗背景的资源和生命周期；制作模型/材质；验证精灵叠加、相机与各效果 | 单场景中到高；通用系统高 |
| 可选音乐包 | 原音频链路可运行 | 曲目 ID、切换/循环/淡入淡出语义；选 sequence 替换或新增宿主播放器 | 中到高 |

**插值能让整张机体图的位移、缩放、旋转更平滑；机体从一张动作图切换到下一张时，中间的手臂姿态不会凭空生成。** 补充动作帧、重做分件动画或改为 3D 骨骼动画属于进一步的素材和动画系统工作。

对于原生 3D，当前几个项目可参考的代码主要是 RT64 适配、矩形/精灵绘制和资源流程；本次没有发现可直接搬入 SRW64 的通用高模/PBR 替换方案。5600 的宿主模型路径仍需按我们自己的场景语义扩展。

## 4. 现有实现核对与建议顺序

- [`native-host/graphics.cpp`](../tools/recomp/native-host/graphics.cpp) 当前显式设置 `AspectRatio::Original`、`RefreshRate::Original`；不能把 RT64 本身有宽屏/插值能力当作 SRW64 已完成适配。
- [`play_native.py`](../tools/recomp/play_native.py) 已通过 `--profile` 统一语言、高清美术和原生模型；当前验收见[内容架构](native-content-foundation.md)。
- [实时对白](native-dialogue-ui.md) 已实现 Unicode 排版、分页、字号、回看和阅读速度；全 UI 覆盖与实体手柄仍有缺口。
- [原生 5600](native-model-replacement.md) 已有实际游戏与受控遮挡证据；当前仅 Metal raster，不含透明折射、动态投影和跨后端实现。
- 战术信息、战斗节奏与宽屏依赖沿用[整体实施方案](native-enhancements-plan.md)；本报告补充外部案例，不把计划项改标为已完成。

建议按可验收结果推进：

1. **整合现有增强与设置。** 同一版本可开关中文 UI、高清包和模型替换，保持明确的保存配置，完成开场至首关及存读档联合验证。
2. **交付实用增强样板。** 先建立选中单位状态快照，做 4:3 游戏区旁的信息侧栏；战斗速度作为独立功能验证。素材编辑流程可在不改玩法的情况下逐步完善。
3. **交付画面增强样板。** 选一个实际战斗背景制作现代模型/材质；地图或镜头先试局部插值，再逐类扩展。真实宽屏按场景验收，固定构图的剧情保留对应呈现策略。

本次新增内容仅为调研记录；未启动他人项目、未复制外部实现进 SRW64、未更改已有实验或存档。
