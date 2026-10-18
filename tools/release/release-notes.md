An unofficial native port of **Super Robot Wars 64** with full English and Chinese translations. No ROM included: you need your own original Japanese ROM. 中文说明见下方。

> **HD pack {hd_version}** (optional, every platform): **[Download {hd_name} ({hd_size})]({hd_download})**. {hd_status_en} How to install it and where its images come from: [the HD {hd_version} release]({hd_url}).

## Downloads

| File | Size | What it is |
| --- | --- | --- |
{package_rows_en}
| [`{hd_name}`]({hd_download}) | {hd_size} | Optional HD images (HD {hd_version}), the same for every platform. |

## What's new in 0.4.9

### Fixes

- **Android: Snapdragon 855/860, 778G, 765G and 730/732G phones no longer close at start** (Poco X3 Pro, Pixel 4/4a/5, Galaxy A73); on the older Snapdragon phones still on Android 9 or 10, the game asks you to update Android instead of closing.
- A crash when casting 友情 or similar spirits on the battle page during the enemy's turn.
- A flash when choosing Evade or Defend on the battle page.
- An extra confirm sound in dialogue with nothing pressed.
- Misaligned weapon ranges and negative hit modifiers in the original-style screens.

### New features

- **Unified weapon upgrades** (Options → UI → Weapon upgrades; on by default): all of a unit's weapons, every form included, go up together to one level, for the same total price as one by one; weapons that join later catch up for free. The full-upgrade bonuses show one after another, and levels past the original cap show as yellow triangles. Choose Per weapon for the original's way.
- **Load the suspend save from the map**: the Load button beside Info on Android, a button on the stage info page, or F9 on the keyboard. It asks first, showing the save's turn and funds, and works in the middle of a battle too.
- **Carry finished hidden elements into the next playthrough** (a new difficulty rule, off by default): a hidden element finished in an earlier playthrough joins right after the stage where it would have joined.
- **A new cheat: unlimited actions** for your units.
- **Pilot pages show the spirits still to learn**, with their levels. An enemy's pilot page shows its learned spirits and SP, and every enemy's numbers can be seen from the start of the stage.
- **The Link page is a list of works with a detail panel** for the chosen one's units and pilots.
- **Smaller downloads**: every package is about 30 MB smaller, and the Android APK about 40 MB more.

## From 0.4.8

- Stage info: hidden elements, enemy parts and retreat conditions for the current stage.
- An experimental Frame Rate setting (30 by default).
- A cheat for enemy and recruit levels.
- The Redmi K60 black screen fixed; Windows starts without a Vulkan driver; Hold ▶▶ on Android keeps fast-forwarding; your own key bindings show after a restart.

## Android chips tested

The game starts and plays on real phones with these chips (Android 11–16): Snapdragon 730/732G, 765G, 778G (14–22 FPS), 855/860, 865/870, 888, 8 Gen 1, 8+ Gen 1, 8 Gen 2, 8 Gen 3 and 8 Elite; Dimensity 7300 and 6100+ (about 14 FPS; the others mostly 30); Exynos 1380; Tensor G2 and G3. A chip that is not listed has simply not been tested yet. If you play on a Dimensity 8200 phone, please let us know whether it runs well.

Earlier changes are on the [website blog](https://srw64.dreamquest.club/en/blog/).

## Feature preview

0.4 is the first public release series and a feature preview: the whole game is playable, but it is still rough.

- **The translations are not final.** English and Chinese are mostly machine-translated with one polishing pass and consistent names, and have not been hand-edited line by line yet.
- **Expect plenty of bugs.** The new screens, the translated layout, HD and widescreen can all go wrong; keep a spare save slot.
- **Testing beyond macOS is limited** to a few devices.

Please report problems on [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues), with the platform info and problem report from Options → Report, and suggest text corrections on the [website](https://srw64.dreamquest.club/en/story/)'s Story and Library pages. Full feature list: [the 0.4.0 release notes](https://srw64.dreamquest.club/en/blog/v0-4-0/).

## Notes

- This is an unofficial fan work, unrelated to Bandai Namco, BANPRESTO or the rights holders of the series. The original characters, art and trademarks belong to their owners.
- The macOS app is signed locally and not notarized: if it is blocked the first time, allow it under System Settings → Privacy & Security.
- Z+START in battle is the original game's soft reset: it returns to the title, and unsaved progress is lost.
- The font is HarmonyOS Sans; its license comes with the app.

---

## 中文

非官方的《超级机器人大战64》原生移植，附全游戏中文与英文翻译。不含 ROM：需要自备日版原版 ROM。

> **HD 包 {hd_version}**（可选，各平台通用）：**[下载 {hd_name}（{hd_size}）]({hd_download})**。{hd_status}安装方法和图片来源见 [HD {hd_version} 发布页]({hd_url})。以后 HD 包出新版本时，游戏的更新检查会提示。

### 下载

| 文件 | 大小 | 内容 |
| --- | --- | --- |
{package_rows}
| [`{hd_name}`]({hd_download}) | {hd_size} | 可选的 HD 图片包（HD {hd_version}），各平台通用。 |

### 本版更新（0.4.9）

#### 修复

- **安卓：骁龙 855/860、778G、765G、730/732G 的手机不再一启动就退出**（Poco X3 Pro、Pixel 4/4a/5、Galaxy A73 等）；还停在安卓 9／10 的较老骁龙手机，会提示升级系统而不是直接退出。
- 敌方回合在战斗页使用「友情」等精神时闪退。
- 战斗页选「回避」「防御」时画面闪一下。
- 对白里没按键也会多响一声确认音。
- 原版画面里武器射程和负数命中修正的数字没对齐。

#### 新功能

- **武器统一改造**（选项 → 界面 →「武器改造」，默认「统一」）：一台机体（含所有形态）的武器一起改到同一段数，费用和逐件改造相同；之后加入的武器自动免费补到同一段数。改满奖励会依次提示，超过原版上限的段数显示为黄色三角。选「逐件（原版）」就是原来的改法。
- **在地图上读取中途存档**：安卓点「情报」旁的「读档」，本关情报页里有按钮，键盘按 F9。会先确认，显示存档的回合和资金，战斗中也能用。
- **周目继承**（新的难度规则，默认关）：上周目完成过的隐藏要素，在原本入队的那一关之后直接加入。
- **金手指新增「我方无限行动」。**
- **驾驶员能力页显示还没学会的精神和学习等级。** 敌方驾驶员页显示已学会的精神和 SP，地图上的敌人从一开始就能看到数值。
- **联动画面改版**：左边是作品列表，右边显示选中作品的机体和驾驶员。
- **安装包变小**：各平台约小 30 MB，安卓 APK 再小约 40 MB。

### 0.4.8 的更新

- 本关情报：当前关的隐藏要素、敌人的强化零件和撤退条件。
- 试验性的「帧率」设置（默认 30）。
- 金手指新增「敌人与新同伴等级」。
- 红米 K60 黑屏修复；Windows 没有 Vulkan 驱动也能启动；安卓「按住快进」恢复连续快进；改键后重启显示正确。

### 安卓已测试的芯片

以下芯片的真机都能正常进入游戏（安卓 11–16）。天玑 6100+ 这类入门芯片约 14 FPS，骁龙 778G 约 14–22 FPS，其余基本是 30 FPS。没列出的芯片不代表不能玩，只是还没测过；欢迎在 Issues 反馈。希望天玑 8200 的玩家反馈一下是否能正常运行。

- 高通：骁龙 730/732G（Adreno 618，Pixel 4a）、骁龙 765G（Adreno 620，Pixel 5）、骁龙 778G（Adreno 642L，Galaxy A73）、骁龙 855/860（Adreno 640，Pixel 4、Poco X3 Pro）、骁龙 865/870（Adreno 650，Galaxy Tab S7、POCO F3）、骁龙 888（Adreno 660，Galaxy S21 Ultra）、骁龙 8 Gen 1（Adreno 730，Galaxy S22、小米 12）、骁龙 8+ Gen 1（Adreno 730，红米 K60）、骁龙 8 Gen 2（Adreno 740，小米 13、Galaxy S23）、骁龙 8 Gen 3（Adreno 750，Galaxy S24 Ultra）、骁龙 8 至尊版（Adreno 830，Galaxy S25）
- 联发科：天玑 7300（Mali-G615，Solana Seeker）、天玑 6100+（Mali-G57，Galaxy A15）
- 三星：Exynos 1380（Mali-G68，Galaxy A35）
- 谷歌：Tensor G2（Mali-G710，Pixel 7）、Tensor G3（Mali-G715，Pixel 8）

更早版本的更新见[官网博客](https://srw64.dreamquest.club/zh/blog/)。

### 功能预览

0.4 是第一个公开版本系列，定位是功能预览：从头玩到尾的主要功能都已具备，但还很粗糙。

- **翻译尚未精翻。** 中英文以机器翻译为主，经过一轮去直译腔的润色、专名统一，还没有逐句人工校对，语气、用词和误译都会有不少问题。
- **bug 应该会很多。** 新加的界面、翻译排版、HD 画面和宽屏都可能出错，请多留一个存档栏。
- **各平台测试还不充分。** macOS 测得最多；Steam Deck、Windows、Linux 和 Android 只在少数设备上试过。

程序问题欢迎到 [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues) 反馈：游戏里「选项 → 反馈」可以复制平台信息、导出问题报告，按表单填写并附上；文本意见请在[官网](https://srw64.dreamquest.club/zh/story/)的剧情和图鉴页面上直接提交。功能一览见[官网的版本说明](https://srw64.dreamquest.club/zh/blog/v0-4-0/)。

### 说明

- 这是非官方的粉丝作品，与万代南梦宫、万普（BANPRESTO）及各作品的版权方无关。原作角色、美术与商标的权利归各自的权利人所有。
- macOS 应用只有本地签名，没有经过 Apple 公证。第一次打开被拦下时，到“系统设置 → 隐私与安全性”里点“仍要打开”。
- 战斗中按 Z+START 是原版的软复位，会回到标题画面，未存档的进度会丢失。
- 字体使用 HarmonyOS Sans，许可全文随应用附带。

## Checksums / 校验

```
{checksums}
```

Built from / 构建自提交 `{commit}`.
