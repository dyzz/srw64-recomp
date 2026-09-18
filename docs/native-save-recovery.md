# 原生存档历史选择与通关档恢复

2026-09-12。本轮实现的是历史 SRAM 副本的完整性筛选、显式恢复与读取验证；不是新的游戏存档格式，也不是安全节点自动保存或任意时刻即时存档。

## 玩家入口

```sh
# 列出统一原生入口的历史副本和冻结初始备份；不启动游戏、不创建试玩目录。
.venv/bin/python tools/recomp/run/play_native.py \
  --profile config/recomp/profiles/play-profile.json --list-saves

# 恢复指定会话。ID 使用上一步列出的完整时间戳。
.venv/bin/python tools/recomp/run/play_native.py \
  --profile config/recomp/profiles/play-profile.json --restore-session SESSION_ID

# 显式回到冻结的第一话通关备份。
.venv/bin/python tools/recomp/run/play_native.py \
  --profile config/recomp/profiles/play-profile.json --restore-session initial
```

`--new-game`、`--restore-session`、`--list-saves` 互斥。不指定时从新到旧选择第一个通过完整性校验的历史副本；都不可用时尝试配置中有固定摘要的初始备份。跳过原因及实际选择均打印出来。指定的会话无效时直接拒绝，不悄悄选择另一份进度；没有任何可核验来源时要求检查历史或显式新游戏。

统一 profile、普通日文、原生模型和 ROM 模型实验继续使用各自既有历史目录。日中及 Original/HD 在统一 profile 下共用同一 JP 身份；ROM 模型实验的身份单独核对。两个 `.command` 入口均使用项目 `.venv` 并转发参数。

## 校验与保护边界

实现：`src/srw64_native/save_history.py`；接入：`tools/recomp/run/play_native.py` 与 `run_host_probe.py`。

候选会话必须有可识别且完成的运行报告、宿主退出码 0、匹配的 ROM 版本与 SHA-256，以及最终 SRAM 的既有大小和摘要记录。当前只接受原版玩法配置；涉及未支持玩法配置的记录不自动恢复。文件必须恰为 32 KiB、与记录摘要一致且非全零/全 FF。报告缺失/损坏、异常退出、同长度内容损坏、截断、错误身份和越界/链接存档都会被拒绝。内容编译目录和输入副本不参加历史排序。

**这证明文件与既有记录一致，不证明每个游戏内槽位都有效。** 本轮没有解析原游戏的 SRAM 内部校验和，也不能用一个新计算的摘要为没有既有记录的文件背书。游戏内 Load/Continue 仍由原游戏读取和校验；需要修复或导入未记录的旧档时另设流程。

选择后再次核对摘要，将同一份字节以独占创建方式写入新会话旁的 `.source.sram`，刷新到磁盘后才启动宿主；`.save-selection.json` 记录原来源、选择理由及跳过列表。探针的 `--save-sha256` 校验该既有摘要，并继续保留构建后对输入变化的检查。游戏只写自己的新运行目录，原会话与冻结备份不覆盖、不删除。

启动中断留下的来源副本或选择记录不会成为下一次自动恢复候选。它们是恢复输入的审计记录，不是“原子提交的新游戏存档”；安全节点保存集合和轮转淘汰尚未实现。

## 实际冷启动读取

证据：[intermission-cold-1/reload-verification.json](../build/recomp/save-recovery-check/intermission-cold-1/reload-verification.json)。使用同一 JP Rev 0 ROM、日文/Original profile、独立 SRAM 副本，全程静音。

原始冻结档为 `build/recomp/gfx-probes/first-map-turn5-reload-1/stage1-clear-turn7.sram`，摘要为 `0c6ded15fdf60c6b0064b2260a335d17a4ff77386d14d634bfd7d3bcb8de7484`。

实际流程：冷启动→标题 Load→ROM 卡带→存档 1→确认→整备→驾驶员能力→玛娜米详情→正常退出。

| 观测位置 | 已核对结果 | GPU 画面 |
| --- | --- | --- |
| 读取槽位列表 | 第一话 CLEAR、玛娜米等级 2、总回合 7、资金 14,500 | `present-2460.png` |
| 实际恢复后的整备菜单 | 第一话 CLEAR、总回合 7、资金 14,500 | `present-3000.png` |
| 恢复后的驾驶员详情 | 玛娜米・哈米尔、等级 2、气力 100、SP 102/102 | `present-3960.png` |

三个画面均已查看，哈希和元数据记录于上述证据文件。宿主退出码 0；4 个游戏线程全部 join，RDRAM 释放前后观测为 0 个。原始冻结档、传给宿主的来源副本和运行结束后的 SRAM 摘要均保持原值。没有购买改造、重新保存或进入第二话。

`config/recomp/inputs/load-intermission-check.json` 从本轮实际确认过的 VI 输入记录导出，可用于后续重跑（建议 `--vis 9000`）。本轮验收的是实际冷启动与运行中控制；导出的固定输入尚未单独重放，不能标记为确定性回放已验证。运行报告保留原输入脚本和全部已应用控制事件。

## 检查与剩余工作

- `make check`：108 项 Python 测试、compileall 与依赖检查通过。10 项新增测试覆盖新旧副本回退、显式恢复拒绝、报告/身份检查、空档、符号链接、来源冻结、二次摘要校验、只读列表和启动参数传递。
- 原生宿主使用上一轮已核验的相同二进制，实际完成本轮冷启动；本轮没有修改 C++ 或新增组件验收结论。
- 尚未完成：SRAM 内部格式校验、任意安全节点序列化、多手动槽管理 UI、自动保存/轮转备份、分歧书签、完整随机/事件状态对照，以及多语言/美术组合下的存读档矩阵。
- 没有新增参考模拟器对照，现有战术中断档的历史对照证据仍属于各自记录。
