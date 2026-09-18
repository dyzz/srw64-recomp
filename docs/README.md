# 技术文档索引

更新：2026-09-12。仓库只维护统一日版 ROM 的原生 recomp、内容工具与运行验证。旧 ROM 汉化流水线已移除；关卡脚本已完成静态指令解析。

## 从这里开始

| 目的 | 文档 |
| --- | --- |
| 查看内置 MOD 首发/后续范围、多语种、Original/HD、存档兼容与验收门槛 | [内置 MOD 路线图](mod-roadmap.md) |
| 接手代码、构建、验证、定位日志与清理测试进程 | [原生开发指南](native-development.md) |
| 查看 Original 在 HD 资源缺失时的启动行为及验证 | [Original 回退](native-original-fallback.md) |
| 查看当前语言、图片、5600 模型配置及扩展方式 | [内容架构第一批实现](native-content-foundation.md) |
| 查看内容分层与语义接口，以及暂缓的外部 MOD 扩展研究 | [扩展架构方案](native-extensibility-architecture.md) |
| 重跑文本／资源／机体／驾驶员／场景地图提取，浏览整合档案、全体特殊能力／技能持有者、原始字节和引用链 | [原始数据目录](original-data-catalog.md) |
| 查看事件脚本的完整指令、条件块、主角段落、说话人、触发类型、路线流向和出击记录；按章节阅读日文原文剧情（剧情查看器） | [关卡脚本完整解析](stage-script-exploration.md) |
| 查看隐藏机体／驾驶员、说服与分歧条件在脚本中的实现，及与社区攻略的对照和出入 | [隐藏要素](hidden-elements.md)（2026-09-17，静态分析，未运行验证） |
| 连续阅读剧情、全文搜索、逐句定位、主角路线与阅读设置 | [剧情审阅站](story-reader.md) |
| 确认剩余指令效果、安排静音跟踪与单参数实验 | [剩余指令语义确认](script-semantics-confirmation.md) |
| 查看第一话实际执行的 123 条指令、地图转换和部署观察 | [静音脚本运行观察](script-runtime-observation.md) |
| 查看男性超级系开场 56 条指令与 3D3C 单位移动的代码／画面证据 | [3D3C 静音运行观察](script-3d3c-runtime.md) |
| 对照 3D3C 目标位置改动、逻辑坐标写回及固定二进制实验 | [3D3C 单参数实验](script-3d3c-experiment.md) |
| 用自定义脚本在运行中的游戏里验证指令效果（淡入淡出、卷动、切换地图、登场／退场、移动、资金／改造／气力、对白） | [脚本注入调试](script-debug-injection.md) |
| 用自制迷你关卡（开场、对白、战场、事件、结束）替换一话来验证脚本指令与关卡流程 | [迷你关卡](mini-stage.md) |
| 查看原版人物头像、机体地图图标、战场底图及武器属性标记 | [原版图片与武器标记](original-images.md) |
| 查看关窗崩溃修复、线程回收和退出验收边界 | [退出生命周期问题](native-window-close.md) |
| 列出/恢复历史存档、了解完整性回退与通关档冷启动证据 | [原生存档恢复](native-save-recovery.md) |
| 语言设置与覆盖报告、保存集合原型、冷启动及随机状态差异 | [三项底座验证](native-foundations-verification.md) |
| 了解提交与本地资源边界 | [贡献约定](../CONTRIBUTING.md)、[来源与版本](provenance.md) |

## 当前原生功能

| 模块 | 用法、实现与验收 |
| --- | --- |
| 启动、输入、独立 SRAM 历史 | [试玩指南](native-playtest.md)；统一 profile 的新入口见[内容架构](native-content-foundation.md) |
| 标准剧情双框对白、原生字体、分页与回看 | [对白 UI](native-dialogue-ui.md)、[高清对白实现](native-dialogue-runtime-hd.md) |
| 自动四档、下一句进度、当前说话框 | [阅读指示器](native-reading-indicators.md) |
| 间歇性画面/底栏消失问题与修复证据 | [对白闪烁](native-dialogue-flicker.md) |
| 窗口内现代姓名页面、原校验、姓名写回 | [姓名输入](native-name-entry.md) |
| 开场缩放文字跳过与资源提取 | [开场文字](native-intro.md) |
| 5600 原生水滴替换与 Original/HD 切换 | [模型替换](native-model-replacement.md) |
| 浏览原游戏 3D 资源 | [模型查看器](native-model-viewer.md)、[3D 资源分析](3d-model-replacement-analysis.md) |

## 设计与历史实验

这些文档记录当时的配置和验收，不能替代当前 profile 的结果。ROM 内 5600 高面数实验与原始 JP profile 使用不同身份与存档目录。

- recomp 基础：[分阶段计划](recomp-plan.md)、[早期进度与可重跑探针](recomp-progress.md)。
- 架构研究：[同类项目比较](recomp-peer-comparison.md)、[原生增强规划](native-enhancements-plan.md)。
- 原生高清美术：[世界地图 HD](native-worldmap-hd.md)。
- 字体探针：[字库替换](native-font-probe.md)、[Core Text 固定帧验证](native-coretext-probe.md)。
- 美术实验：[AI 探索](hd-ai-exploration.md)、[基准比较](hd-ai-benchmark.md)、[对话框预览](dialogue-frame-ai-preview.md)。

## 原始 ROM 与数据

- `src/srw64_rom/` 提供身份校验、文本表和资源编解码。
- [原始数据目录](original-data-catalog.md)说明 `make recomp-data` 的导出与浏览。
- [来源记录](provenance.md)固定原 ROM、日文字形表及工具链身份。

静态检查、组件测试、固定帧回放、原生游戏运行、模拟器运行和人工检查各有独立范围；“有截图”或“退出码为 0”不自动代表整条流程已验收。
