# 原生试玩

更新：2026-09-18。只支持 macOS（Apple Silicon，Metal）。构建准备见[原生开发指南](native-development.md)。

## 启动

先在仓库目录运行一次 `make`（见[原生开发指南](native-development.md#构建与日常检查)），然后双击 `scripts/Play SRW64 Native.command`，或在终端运行：

```sh
scripts/Play\ SRW64\ Native.command --language zh-Hans
```

脚本即 `tools/recomp/run/play_native.py --profile config/recomp/profiles/play-profile.json`，后面的参数原样传入。程序会检查 ROM 和生成代码身份，源码有变化时重新编译宿主，然后打开 macOS Metal 窗口。还没有任何试玩会话（也没有冻结备份）时直接从开场剧情开始新游戏；以后自动读取最近一次正常退出、可核验的试玩会话的 SRAM，`--new-game` 重新开始。已有会话但全部无法核验时会报错并停下，不会悄悄改成新游戏。

| 参数 | 作用 |
| --- | --- |
| `--language ja｜zh-Hans｜en` | 初始语言；游戏中 F7 循环切换，选择会被记住 |
| `--images original｜hd` | 初始画面；HD 需要本地 `assets/` 里的实验素材，F6 切换 |
| `--rules original｜fixed｜all`、`--rule-fixes IDS` | 原版规则、缺陷修正（首次默认）或加上难度调整；会被记住，见[可选规则修正](../gameplay/rule-fixes.md) |
| `--upgrade-rules PATH` | 改造增量、价格与上限的规则文件，见[改造段数与上限](../gameplay/upgrade-limits.md) |
| `--resolution-scale 1..8` | 内部分辨率倍数，字号与布局不变 |
| `--mute` | 关闭声音 |
| `--list-saves`、`--restore-session ID` | 查看或指定恢复的试玩会话 |
| `--mini-stage FILE` | 用自制迷你关卡替换第一话，主菜单按 F8 进入，见[迷你关卡](../script/mini-stage.md) |

`scripts/Play SRW64.command`（不带 `--profile`）是早期试玩入口：不加载语言目录与 profile，存档历史在 `build/recomp/play/`，首次运行依赖开发者本地冻结的第一话通关档，新克隆无法直接使用。

## 按键

字母键按物理键位映射。窗口失去焦点时释放全部键；带 Command、Option 或 Control 的系统快捷键不会送入游戏。键盘以外的手柄尚未接入。

| 键盘 | N64 输入 / 用途 |
| --- | --- |
| 方向键 | 十字键：光标、菜单；标题环形菜单左右旋转 |
| Z | A：确认、推进对话 |
| X | B：取消、返回 |
| Enter | START；标题环形菜单用 Enter 确认 |
| Q / E | L / R；地图上可切换我方机体 |
| 空格 | Z 扳机 |
| I / K / J / L | C 上 / 下 / 左 / 右 |
| W / S / A / D | 模拟摇杆上 / 下 / 左 / 右 |
| F6 | 切换 Original／HD（需要本地 HD 素材） |
| F7 | 日文 → 中文 → 英文循环切换语言，无弹窗、不重启 |
| F8 | 带 `--mini-stage` 时在主菜单进入迷你关卡 |
| Esc 或窗口关闭按钮 | 退出程序 |

开机画面出现前不要按住 Enter：原版会进入 Controller Pak 管理画面，其中的 `osPfsIsPlug` 尚未实现，宿主会中止。

剧情对白与开场的阅读操作（详见[对白 UI](../native/native-dialogue-ui.md)）：

| 操作 | 键盘 |
| --- | --- |
| 下一阅读页 | Z |
| 自动阅读加速 / 减速（0 为手动） | ↑ / ↓ |
| 关闭自动阅读、取消跳过 | X |
| 按住快进，松开停止 | E + Z |
| 跳过当前脚本段；开场缩放文字与路线序章同样适用 | E + Enter |
| 打开／关闭回看；回看中 ↑↓ 滚动 | Q |
| 正文字号 10–18（默认 13） | I / K |

## 原生界面

- **主角选择页**：新游戏跳过公共序章后出现，四张卡片（超级系／真实系 × 男／女）并排；←→ 切换，Enter／Z 确定，也可以点击卡片。
- **姓名页**：选主角后出现在游戏窗口内（主角 → 搭档 → 确认三步），可用鼠标和键盘直接输入，支持输入法；Tab 切换字段、Enter 下一项／确认、Esc 返回。原字库范围和 7/7/5 字数上限不变，见[姓名输入](../native/native-name-entry.md)。
- **联动页**：整备画面选「リンク」时出现，不需要 Transfer Pak 和 Link Battler 卡带。三张作品卡片（ガンダムF91、ゴーショーグン、ザンボット3）并排，←→ 切换，空格／Z 或点击卡片勾选，Enter 继续进入原版联动画面，Esc／X 返回整备菜单。勾选的作品在下一场战斗前以特别关卡加入；已加入的作品变灰，已排定的锁定为勾选。见 [Link Battler 联动](../gameplay/link-battler.md) §10。
- **菜单栏「选项」**：「游戏性调整」逐项开关可选规则，「设置…」（⌘,）打开设置窗口，可切换规则、语言和画面，立即生效并记住，见[设置窗口](../native/settings-window.md)。
- **提示条**：开启难度调整「离队退款」后，剧情让机体离开部队时退回其改造资金，窗口顶部显示约 6 秒的提示，对话回看里也留一行，见[可选规则修正](../gameplay/rule-fixes.md) §2.6。

## 存档

游戏内保存后再退出。每次运行写入独立的会话目录（`build/recomp/profile-play/sessions/`），以后按运行报告的 ROM 身份、正常退出和最终摘要筛选最近可核验副本；损坏会话会报告原因并回退到更早的可核验副本。可用 `--list-saves` 查看、`--restore-session SESSION_ID` 指定恢复。文件完整性与游戏内槽位有效性分别判断，详见[存档恢复](native-save-recovery.md)。窗口不设自动退出时限，不会自动操作游戏。试玩模式只保留最近一张 GPU 截图和对应元数据，音频与控制诊断保存在该次目录。

## 验证状态

当前音频修复已验证设备队列不会在已测战斗中持续累积，实际扬声器音画同步仍需人工试听。2026-09-12 已从冻结通关档冷启动恢复到整备，核对总回合 7、资金 14,500 和玛娜米等级 2 / SP 102/102；没有进入第二话。2026-09-18 用[调试接口](debug-interface.md)从冷启动驱动到男主路线对白，复核了序章跳过、字号、快进与姓名页。
