# 技术文档索引

更新：2026-09-18。仓库只维护统一日版 ROM 的原生 recomp（只支持 macOS）、内容工具与运行验证。旧 ROM 汉化流水线已移除。文档按主题分目录；带日期的文档记录当时的结论，`build/` 下的证据链接只在本地存在。

| 目录 | 内容 |
| --- | --- |
| [`guide/`](#使用与开发guide) | 试玩、构建与开发、调试接口、存档、来源记录 |
| [`gameplay/`](#玩法规则与原版缺陷gameplay) | 原版 Bug 登记、基础修复、可选规则、改造上限与继承、隐藏要素 |
| [`native/`](#原生界面与呈现native) | 对白、阅读控制、姓名页、开场、设置窗口、画面与模型替换、退出生命周期 |
| [`script/`](#关卡脚本script) | 事件脚本解析、指令语义、运行观察、脚本注入、迷你关卡、剧情审阅站 |
| [`data/`](#原始数据data) | 原始数据目录、图片与武器标记、3D 资源与模型查看器 |
| [`design/`](#规划与历史实验design) | 路线图、recomp 方案与实施记录、架构研究、字体与美术实验 |
| `media/` | README 使用的截图与视频，来源见 `media/manifest.json` |

## 从这里开始

| 目的 | 文档 |
| --- | --- |
| 启动试玩、按键、阅读操作、姓名页与「选项」菜单 | [原生试玩](guide/native-playtest.md) |
| 构建、源码责任、验证开关、证据与清理 | [原生开发指南](guide/native-development.md) |
| 不靠人工按键驱动实机：启动隔离会话、按键、截图、读状态、操作原生界面（命令行与 MCP） | [调试接口与 MCP](guide/debug-interface.md) |
| 原版有哪些 Bug、我们修了哪些、怎么开关 | [原版 Bug 登记](gameplay/original-bug-register.md) → [基础修复](gameplay/base-fixes.md)、[可选规则修正](gameplay/rule-fixes.md) |
| 首发与后续功能范围 | [内置 MOD 路线图](design/mod-roadmap.md) |

## 使用与开发（guide/）

| 文档 | 内容 |
| --- | --- |
| [原生试玩](guide/native-playtest.md) | 启动参数、完整按键表、阅读操作、姓名页与「选项」菜单、存档历史 |
| [原生开发指南](guide/native-development.md) | 当前能力与限制、源码与工具目录、构建与组件测试、验证用环境变量与控制文件、证据与清理 |
| [调试接口与 MCP](guide/debug-interface.md) | 宿主 JSON-RPC 方法、输入覆盖范围、命令行 `srw64ctl.py`、MCP 工具、实测与限制 |
| [原生存档恢复](guide/native-save-recovery.md) | 历史存档列表与显式恢复、完整性回退、通关档冷启动证据 |
| [本地输入与来源记录](guide/provenance.md) | 原 ROM 身份、日文字形表、固定工具链与参考资料 |

## 玩法：规则与原版缺陷（gameplay/）

| 文档 | 内容 |
| --- | --- |
| [原版 Bug 登记](gameplay/original-bug-register.md) | 网上 Bug 报告、来源分歧、已确认原因（BUG01–05）与建议复现步骤 |
| [基础修复](gameplay/base-fixes.md) | 默认生效、没有开关的缺陷修复（BUG05 五飞假身） |
| [可选规则修正](gameplay/rule-fixes.md) | 超能力、圣战士、限界、底力、武器改造继承、奥拉斩威力等修正与难度调整：原因、依据、开关与实机核对 |
| [改造段数与上限](gameplay/upgrade-limits.md) | “丑小鸭”上限、每段增量与价格、上限突破、升级规则文件 |
| [改造继承分析](gameplay/upgrade-inheritance.md) | 换机时改造段数如何搬运、前任表与武器映射、疑似漏项（静态分析） |
| [隐藏要素](gameplay/hidden-elements.md) | 隐藏机体／驾驶员、说服与分歧条件在脚本中的实现，与攻略对照（静态分析） |

## 原生界面与呈现（native/）

| 文档 | 内容 |
| --- | --- |
| [对白 UI](native/native-dialogue-ui.md) | 双框对白、原生字体、分页、回看、自动阅读、快进与跳过 |
| [阅读指示器](native/native-reading-indicators.md) | 自动档位、下一句进度、当前说话框 |
| [对白闪烁](native/native-dialogue-flicker.md) | 间歇性画面／底栏消失的原因与修复证据 |
| [高清对白实现](native/native-dialogue-runtime-hd.md) | 实际地图／边框资源高清替换与蓝色人名 |
| [姓名输入](native/native-name-entry.md) | 窗口内现代姓名页、原校验、姓名写回、关闭后的按键释放 |
| [开场文字](native/native-intro.md) | 开场缩放文字跳过与资源提取 |
| [设置窗口](native/settings-window.md) | 菜单栏「选项」与设置窗口的结构、元数据与验收 |
| [Original 回退](native/native-original-fallback.md) | HD 资源缺失时的启动行为 |
| [世界地图 HD](native/native-worldmap-hd.md) | 对话世界地图高清资源 |
| [模型替换](native/native-model-replacement.md) | 5600 原生水滴与 Original/HD 切换 |
| [退出生命周期](native/native-window-close.md) | 关窗崩溃修复、线程回收与退出验收边界 |
| [内容架构第一批实现](native/native-content-foundation.md) | 语言、图片、5600 模型配置及扩展方式 |
| [三项底座验证](native/native-foundations-verification.md) | 语言设置与覆盖报告、保存集合原型、冷启动及随机状态差异 |

## 关卡脚本（script/）

| 文档 | 内容 |
| --- | --- |
| [关卡脚本完整解析](script/stage-script-exploration.md) | 事件脚本的完整指令、条件块、主角段落、说话人、触发类型、路线流向和出击记录 |
| [剩余指令语义确认](script/script-semantics-confirmation.md) | 尚待确认的指令效果、静音跟踪与单参数实验安排 |
| [静音脚本运行观察](script/script-runtime-observation.md) | 第一话实际执行的 123 条指令、地图转换和部署 |
| [3D3C 静音运行观察](script/script-3d3c-runtime.md)、[3D3C 单参数实验](script/script-3d3c-experiment.md) | 男性超级系开场的单位移动证据与目标位置对照 |
| [脚本注入调试](script/script-debug-injection.md) | 在运行中的游戏里执行自定义脚本验证指令效果 |
| [迷你关卡](script/mini-stage.md) | 用自制关卡替换一话来验证指令与关卡流程 |
| [剧情审阅站](script/story-reader.md) | 连续阅读剧情、全文搜索、逐句定位、主角路线 |

## 原始数据（data/）

| 文档 | 内容 |
| --- | --- |
| [原始数据目录](data/original-data-catalog.md) | 文本／资源／机体／驾驶员／场景地图提取、整合档案、特殊能力与技能持有者、引用链 |
| [原版图片与武器标记](data/original-images.md) | 人物头像、机体地图图标、战场底图及武器属性标记 |
| [3D 资源分析](data/3d-model-replacement-analysis.md) | 原始 3D 资源与模型替换可行性 |
| [模型查看器](data/native-model-viewer.md) | 本地模型资源浏览与 5600 验证 |

## 规划与历史实验（design/）

这些文档记录当时的配置和验收，不能替代当前 profile 的结果。

| 文档 | 内容 |
| --- | --- |
| [内置 MOD 路线图](design/mod-roadmap.md) | 首发／后续范围、多语种、Original/HD、存档兼容与验收门槛 |
| [分阶段计划](design/recomp-plan.md)、[实施记录](design/recomp-progress.md) | recomp 基础方案、早期进度与可重跑探针 |
| [同类项目比较](design/recomp-peer-comparison.md)、[原生增强规划](design/native-enhancements-plan.md) | 架构研究与增强方案 |
| [扩展架构方案](design/native-extensibility-architecture.md) | 内容分层与语义接口，暂缓的外部 MOD 扩展 |
| [字库替换](design/native-font-probe.md)、[Core Text 固定帧验证](design/native-coretext-probe.md) | 字体探针 |
| [AI 探索](design/hd-ai-exploration.md)、[基准比较](design/hd-ai-benchmark.md)、[对话框预览](design/dialogue-frame-ai-preview.md) | 美术高清化实验 |

## 验证层次

静态检查、组件测试、固定帧回放、原生游戏运行、模拟器运行和人工检查各有独立范围；“有截图”或“退出码为 0”不自动代表整条流程已验收。`tests/test_docs.py` 检查所有 Markdown 链接与文档中引用的仓库路径。
