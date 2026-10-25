An unofficial native port of **Super Robot Wars 64** with full English and Chinese translations. No ROM included: you need your own original Japanese ROM. 中文说明见下方。

> **HD pack {hd_version}** (optional; the Switch has a pack of its own, in the same release): **[Download {hd_name} ({hd_size})]({hd_download})**. {hd_status_en} How to install it and where its images come from: [the HD {hd_version} release]({hd_url}).

## Downloads

| File | Size | What it is |
| --- | --- | --- |
{package_rows_en}
| [`{hd_name}`]({hd_download}) | {hd_size} | Optional HD images (HD {hd_version}) for macOS, Windows, Linux and Android. |
{switch_hd_row_en}
## What's new in 0.4.10

### Fixes

- **Windows: the game stopped responding after the window was minimised** with the settings window or the pre-battle page open.
- **Loading the suspend save from the pre-battle page left the page over the map, holding every button.**
- **A wrong ROM closed the game without a word, like a crash.** Every platform now shows a message saying what is wrong with the file, in the game's language; the Android launcher lets you choose again, and a Steam Deck in Game Mode says where to put rom.z64.
- **A line at the 4:3 edges of battle skies on a widescreen.** Every sky layer is drawn whole from its HD picture; the HD pack itself has not changed.
- **With 4:3 kept in a wider window, the title's load menu and other pages sat off to the right.**
- **Unified weapon upgrades: at a machine swap in a stage opening, the new machine's extra weapons started at level 0.** They now take the old machine's level.
- **A five-stat upgrade that reached the EW swap dropped the rest of the plan.** It now goes on with the new machine, and the unit list says which machine it became.

### New features

- **Portable mode (Windows and Linux)**: create an empty folder named user-data beside Marchwind64.exe (srw64 on Linux) and saves and settings live there. The first launch copies in what is already in the system folder; a normal exit syncs back, asking first when the saves differ and backing up the old ones.
- **The first launch follows the system language** on every platform; a game started from Steam follows Steam's language.
- **Menu bar (Windows and macOS)**: Language (a globe), Tools (Screenshot, 2× Speed, Stage Info, Load Suspend Save, Export Logs) and Help (website, source code, issue tracker, Copy Platform Info) menus; View gains HD Pictures. Each entry shows its key.
- **Three new cheats**: real damage past 65535, EXP and money ×2/×5/×10, and an ally morale cap of 150/200/300/999.
- **Pilot pages show kills** (not for enemies).
- **Chapter title cards in brush lettering**: Japanese traced from the original cards; Chinese and English in Ma Shan Zheng.
- **Return to the title screen** from the map's menu, the intermission menu, the application menu, F10 or the stage info page; it asks first.

## From 0.4.9

- Unified weapon upgrades (on by default), loading the suspend save from the map (F9), carrying finished hidden elements into the next playthrough, unlimited actions, the spirits still to learn on pilot pages, a new Link page and smaller downloads.
- More Snapdragon phones start (855/860, 778G, 765G, 730/732G); a crash with 友情 on the battle page fixed.

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

> **HD 包 {hd_version}**（可选；Switch 用单独的一份，在同一个发布页）：**[下载 {hd_name}（{hd_size}）]({hd_download})**。{hd_status}安装方法和图片来源见 [HD {hd_version} 发布页]({hd_url})。以后 HD 包出新版本时，游戏的更新检查会提示。

### 下载

| 文件 | 大小 | 内容 |
| --- | --- | --- |
{package_rows}
| [`{hd_name}`]({hd_download}) | {hd_size} | 可选的 HD 图片包（HD {hd_version}），macOS、Windows、Linux、安卓通用。 |
{switch_hd_row}
### 本版更新（0.4.10）

#### 修复

- **Windows：设置窗口或战前确认页开着时最小化，游戏无响应。**
- **从战前确认页读取中途存档后，页面留在地图上，所有按键失灵。**
- **ROM 文件不对时游戏直接退出，看起来像闪退。** 现在各平台都弹窗说明原因，用游戏的语言显示；安卓启动器可以重新选择，Steam Deck 游戏模式下会提示把 rom.z64 放在哪里。
- **宽屏下战斗背景在 4:3 边缘有一道竖线。** 所有天空层都改用整张 HD 图绘制；HD 包本身没有变化。
- **宽屏保留 4:3 时，标题的读档菜单等页面向右偏。**
- **武器统一改造：在关卡开场换机时，新机体多出来的武器从 0 段起算。** 现在继承原机的段数。
- **五项一次改造碰上 EW 换机时，剩下的改造被丢弃。** 现在换机后在新机上继续，机体列表会提示换成了哪台机体。

#### 新功能

- **便携模式（Windows、Linux）**：在 Marchwind64.exe（Linux 是 srw64）旁边新建空文件夹 user-data，存档和设置就放在里面。第一次启动会把系统目录里已有的设置和存档复制进来；正常退出时同步回系统目录，两边存档不一致会先询问，覆盖前备份旧的。
- **首次启动跟随系统语言**，各平台都是；从 Steam 启动的按 Steam 的界面语言。
- **菜单栏（Windows、macOS）**：新增「语言」（地球图标）、「工具」（截图、2 倍速、本关情报、读取中途存档、导出日志）、「帮助」（官网、源码、问题反馈、复制平台信息）菜单，「显示」菜单加了 HD 图片开关。每项旁边显示当前按键。
- **金手指新增三项**：伤害照实显示（超过 65535 也照算）、经验与资金倍率 ×2／×5／×10、我方气力上限 150／200／300／999。
- **驾驶员页显示击坠数**（敌方不显示）。
- **章节标题卡改为毛笔字**：日文照原版标题卡字形制作，中文和英文用马善政字体。
- **返回标题画面**：地图菜单、场间菜单、系统菜单、键盘 F10、本关情报页都能进，会先确认。

### 0.4.9 的更新

- 武器统一改造（默认开）、在地图上读取中途存档（F9）、周目继承、我方无限行动、驾驶员页显示还没学会的精神、联动画面改版、安装包变小。
- 更多骁龙手机能启动（855/860、778G、765G、730/732G）；修复战斗页使用「友情」时闪退。

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
