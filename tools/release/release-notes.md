非官方的《超级机器人大战64》原生移植，附全游戏中文与英文翻译。不含 ROM：需要自备日版原版 ROM。

## 本版更新（0.4.8）

- **新增「本关情报」**：列出当前关（在场间时是下一关）的隐藏要素和完成情况，以及敌人带的强化零件和撤退条件。在地图或场间按手柄 Y、键盘 I 打开，安卓点左上角的「情报」，选项 → 通用里也有入口。
- **新增「帧率」设置（试验性）**：默认仍是原版 30 帧，想试的话可以在 选项 → 界面 里改成 60 或跟随显示器，会在两帧之间补出中间画面。
- **金手指新增「敌人与新同伴等级」**（选项 → 作弊）：原版、跟我方（不设上限）、按话数（3＋已通关话数×0.9，最高 50）三选一。
- **安卓：红米 K60 等机型黑屏已修复**，K60 玩家已确认能正常游戏。
- **Windows：没有 Vulkan 驱动的电脑打不开的问题已修复。** 0.4.7 在这类电脑上会直接打不开，现在会用 D3D12 运行；两种都用不了时会提示更新显卡驱动。
- **安卓：修复「按住快进」只能翻一句。** 0.4.5 起按住快进经常只翻一句就停，现在剧情中按住会一直快进。
- **修复改键后重启，设置页和按键提示显示成默认键位。** 游戏操作一直用的是改过的键，只是界面上显示不对。

## 0.4.7 的更新

- **Windows：可能修复部分 AMD 显卡在标题画面闪退**，部分 Intel 核显界面花屏很可能也一起修好了。抗锯齿开关现在只影响游戏画面。
- **修复剧情短跳过后、地图「作战目标」画面之后按键像被一直按住。**
- **安卓：触屏方向键在边缘晃动不再算成按两次**，点在方向键的缝隙里也不会再抢走页面的焦点。
- **战前确认页：点按钮旁边不再让黄色焦点框消失。**
- **关闭战斗动画时，目标在宽屏左侧边缘，HP 窗口不再跑到屏幕右边**；剧情中选项的绿色光标条不再超出窗口左侧。
- **金手指新增「全部机体零件槽 4 个」，「强化零件全部」改为 15 个。**
- **安卓：最近 3 次运行的日志放进 Documents/Marchwind64/logs。**

## 安卓已测试的芯片

以下芯片的真机都能正常进入游戏（安卓 12–16）。天玑 6100+ 这类入门芯片约 14 FPS，其余基本是 30 FPS。没列出的芯片不代表不能玩，只是还没测过；欢迎在 Issues 反馈。希望天玑 8200 的玩家反馈一下是否能正常运行。

- 高通：骁龙 865/870（Adreno 650，Galaxy Tab S7、POCO F3）、骁龙 888（Adreno 660，Galaxy S21 Ultra）、骁龙 8 Gen 1（Adreno 730，Galaxy S22、小米 12）、骁龙 8+ Gen 1（Adreno 730，红米 K60）、骁龙 8 Gen 2（Adreno 740，小米 13、Galaxy S23）、骁龙 8 Gen 3（Adreno 750，Galaxy S24 Ultra）、骁龙 8 至尊版（Adreno 830，Galaxy S25）
- 联发科：天玑 7300（Mali-G615，Solana Seeker）、天玑 6100+（Mali-G57，Galaxy A15）
- 三星：Exynos 1380（Mali-G68，Galaxy A35）
- 谷歌：Tensor G2（Mali-G710，Pixel 7）、Tensor G3（Mali-G715，Pixel 8）

更早版本的更新见[官网博客](https://srw64.dreamquest.club/zh/blog/)。

## 功能预览

0.4 是第一个公开版本系列，定位是功能预览：从头玩到尾的主要功能都已具备，但还很粗糙。

- **翻译尚未精翻。** 中英文以机器翻译为主，经过一轮去直译腔的润色、专名统一，还没有逐句人工校对，语气、用词和误译都会有不少问题。
- **bug 应该会很多。** 新加的界面、翻译排版、HD 画面和宽屏都可能出错，请多留一个存档栏。
- **各平台测试还不充分。** macOS 测得最多；Steam Deck、Windows、Linux 和 Android 只在少数设备上试过。

程序问题欢迎到 [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues) 反馈：游戏里「选项 → 反馈」可以复制平台信息、导出问题报告，按表单填写并附上；文本意见请在[官网](https://srw64.dreamquest.club/zh/story/)的剧情和图鉴页面上直接提交。功能一览见[官网的版本说明](https://srw64.dreamquest.club/zh/blog/v0-4-0/)。

## 下载

| 文件 | 大小 | 内容 |
| --- | --- | --- |
{package_rows}

## HD 包

可选的 HD 图片包单独发布，有自己的版本号，当前是 [HD {hd_version}]({hd_url})，各平台通用。{hd_status}以后 HD 包出新版本时，游戏的更新检查会提示。安装方法和图片来源见 HD 包的发布页。

## 说明

- 这是非官方的粉丝作品，与万代南梦宫、万普（BANPRESTO）及各作品的版权方无关。原作角色、美术与商标的权利归各自的权利人所有。
- macOS 应用只有本地签名，没有经过 Apple 公证。第一次打开被拦下时，到“系统设置 → 隐私与安全性”里点“仍要打开”。
- 战斗中按 Z+START 是原版的软复位，会回到标题画面，未存档的进度会丢失。
- 字体使用 HarmonyOS Sans，许可全文随应用附带。

## English

An unofficial native port of Super Robot Wars 64 with a full Chinese and English translation. No ROM included: you need your own original Japanese ROM. New in 0.4.8: a Stage info page lists the current stage's hidden elements and how far you are with each, and the enemies' parts and retreat conditions (Y on a controller, I on the keyboard, the Info button at the top left on Android, or Options → General; between stages it shows the next one). A Frame Rate setting (experimental): the game stays at the original 30 by default; to try it, choose 60 or Display in Options → UI, which adds frames in between. A new cheat sets enemy and recruit levels: original, match the party (no cap), or by chapter (3 + 0.9 × chapters cleared, at most 50). The black screen on the Redmi K60 and similar phones is fixed, confirmed by a K60 player. On Windows, a PC with no Vulkan driver starts again (0.4.7 did not open there) and runs on D3D12; with neither available, it asks you to update the graphics driver. On Android, Hold ▶▶ keeps fast-forwarding through a story again instead of stopping after one line, as it had since 0.4.5. After a restart, the settings page and the button hints show your own key bindings, not the defaults (the game itself always used yours). From 0.4.7: a likely fix for a crash at the title on some AMD graphics and for garbled menus on some Intel graphics; keys no longer stay held after some short story skips or the map's objectives screen; on Android, a thumb on the edge of the touch D-pad is one press; the pre-battle page keeps its focus frame; the HP window and story choices stay in place in widescreen; cheats for 4 part slots on every unit and 15 of each part; Android keeps the last three runs' logs in Documents/Marchwind64/logs. Android chips tested on real phones: Snapdragon 865/870, 888, 8 Gen 1, 8+ Gen 1, 8 Gen 2, 8 Gen 3 and 8 Elite; Dimensity 7300 and 6100+ (about 14 FPS); Exynos 1380; Tensor G2 and G3. If you play on a Dimensity 8200 phone, please let us know whether it runs well. Earlier changes are on the [website blog](https://srw64.dreamquest.club/en/blog/). 0.4 is the first public release and a feature preview: the whole game is playable, but it is still rough. The Chinese and English translations are mostly machine-translated with one polishing pass and have not been hand-edited line by line yet; expect plenty of bugs; testing beyond macOS has been limited to a few devices. Please report problems on [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues), with the platform info and problem report from Options → Report, and suggest text corrections on the [website](https://srw64.dreamquest.club/en/story/)'s Story and Library pages. Full feature list: [release notes on the website](https://srw64.dreamquest.club/en/blog/v0-4-0/). {packages_en}. The optional HD image pack for every platform is released separately with its own version, currently [HD {hd_version}]({hd_url}). {hd_status_en} The game's update check says when a newer HD pack is out; see its release page for how to install it and where its images come from. This is an unofficial fan work, and the original characters, art and trademarks belong to their owners. The macOS app is not notarized: allow it under System Settings → Privacy & Security. Z+START in battle is the original game's soft reset.

## 校验 / Checksums

```
{checksums}
```

构建自提交 / Built from `{commit}`.
