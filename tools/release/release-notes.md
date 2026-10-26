An unofficial native port of **Super Robot Wars 64** with full English and Chinese translations. No ROM included: you need your own original Japanese ROM. 中文说明见下方。

> **HD pack {hd_version}** (optional; the Switch has a pack of its own, in the same release): **[Download {hd_name} ({hd_size})]({hd_download})**. {hd_status_en} How to install it and where its images come from: [the HD {hd_version} release]({hd_url}).

## Downloads

| File | Size | What it is |
| --- | --- | --- |
{package_rows_en}
| [`{hd_name}`]({hd_download}) | {hd_size} | Optional HD images (HD {hd_version}) for macOS, Windows, Linux and Android. |
{switch_hd_row_en}

## What's new in 0.5.1

### Fixes

- **The game froze on a save or an upgrade.** Saving a second time in one visit to the save screen (overwriting slot 1), or upgrading a unit, could stop the game while the screen went on running. Reported on 0.5.0 from an iQOO 12 and a Windows desktop; 0.5.0's 120 Hz timing made a flaw of the save screens far easier to hit.
- **Counter Plan on the New and HD pre-battle pages** (#16): with any Counter Plan order other than Manual, these pages gave no way to change the response and the weapon button did nothing. They now open the response screen with the order's choice selected (counter with its weapon, evade or defend), and you can keep it or change it, another weapon included. The Original screen still follows the order without stopping.

### Translation

- **Names**: Psycho Gundam and ReGelgu by their official spellings; Battle Beast, Great General of Darkness, Great Marshal of Hell, Apolly, Roze, Devil Gundam and God Gundam from the website's suggestions. The Library names some English records by pilot or series instead of numbering them.

### MOD packages

- **Install a MOD package by dropping it on the game window**, or put it in the MOD folder. The MOD manager (the title screen's corner button, or Settings → General → MOD) lists the installed packages and enables, disables, orders and removes them. A package is a folder, or a `.srw64mod` zip of it, holding data only. Not on Android yet.
- **Music mods are supported**: a package's Ogg Vorbis files can replace any of the 49 songs, with loop points, volume and titles, and a battle song per pilot or unit. We ship no music package, only the tool to make one: the website's [BGM mod maker](https://srw64.dreamquest.club/en/mods/bgm/) builds the package in your browser from mp3, wav, flac, m4a or ogg files, nothing uploaded. Settings → General turns MOD music off or sets its volume; the sound test marks the replaced songs. Guides: [Mod packages](https://srw64.dreamquest.club/en/docs/mods/), [Music mods](https://srw64.dreamquest.club/en/docs/mod-music/).
- **Number mods change the terrain ranks** of units and pilots (`data/units.json`, `data/pilots.json`). A demo package, Main Cast: Space A, raises space to A for the 20 pilots and 21 units of the main cast below it (the Mazinger team, GoShogun and others); it is in the repository at `config/recomp/mods/space-a`. See [Number mods](https://srw64.dreamquest.club/en/docs/mod-data/) and the [unit and pilot ID table](https://srw64.dreamquest.club/en/mods/ids/).

### Other changes

- **Map unit icons face their side**: your units and friendly third parties look left, enemies and hostile third parties right, in the original images and in HD.
- **The screenshot key can be rebound** on the Controls page (F12 and Print Screen by default). On Android the picture goes to `screenshots/` in the Marchwind64 folder under Documents, and a touch key under Settings takes one; the touch controls and the frame-rate readout stay out of the shot.
- **3× and 4× on phones that cannot keep up** (#10): a phone that cannot draw 90 or 120 frames a second now runs the game every frame and draws one frame in three, so 3× and 4× are faster than 2× there (on a Dimensity 7300 an enemy phase went from 22 to 15 seconds). While hurried, the game's threads run on the big cores.

## Android chips tested

The game starts and plays on real phones with these chips (Android 11–16): Snapdragon 730/732G, 765G, 778G (14–22 FPS), 855/860, 865/870, 888, 8 Gen 1, 8+ Gen 1, 8 Gen 2, 8 Gen 3 and 8 Elite; Dimensity 7300 and 6100+ (about 14 FPS; the others mostly 30); Exynos 1380; Tensor G2 and G3. A chip that is not listed has simply not been tested yet. If you play on a Dimensity 8200 phone, please let us know whether it runs well.

Earlier changes are on the [website blog](https://srw64.dreamquest.club/en/blog/).

## Feature preview

0.5 is still a feature preview: the whole game is playable, but it is still rough.

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

### 本版更新（0.5.1）

#### 修复

- **存档或改造时游戏卡住。** 在存档画面里连存两次（覆盖 1 号存档），或者改造机体，游戏可能停住不动而画面还在走。0.5.0 发布后 iQOO 12 和一台 Windows 台式机都报告了这个问题；存档画面早有这个隐患，0.5.0 的 120 Hz 节拍让它变得很容易碰上。
- **新画面和 HD 画面的战前确认不认反击计划**（#16）：驾驶员设了「手动」以外的反击计划时，这两种画面没有改应对方式的地方，武器按钮也没反应。现在会和原版的「手动」一样打开应对画面，并预选计划里的选择（用它的武器反击、回避或防御），可以照用，也可以改，包括换武器。原版画面仍按计划直接开打。

#### 翻译

- **译名**：按官网收到的意见改为 凤翼天翔、夺地者、菲因梅露姆、艾比安高达；英文的 Psycho Gundam、ReGelgu 改为官方拼写，另有 Battle Beast、Great General of Darkness 等几处。图鉴里一部分英文条目改用驾驶员或作品名而不是编号。

#### MOD 包

- **把 MOD 包拖进游戏窗口就能安装**，也可以放进 MOD 文件夹。MOD 管理（标题画面右下角的「MOD」，或设置 → 通用 → MOD）列出已装的包，可以启用、停用、排序和删除。包是一个只放数据的文件夹，或把它压缩成的 `.srw64mod`。安卓版暂时不能安装。
- **支持音乐 MOD**：包里的 Ogg Vorbis 文件可以替换 49 首曲子中的任意几首，设循环点、音量和曲名，还能指定某个人物或机体打仗时放哪首。我们不提供现成的音乐包，只提供制作工具：官网的 [BGM MOD 制作](https://srw64.dreamquest.club/zh/mods/bgm/)在浏览器里把 mp3、wav、flac、m4a、ogg 做成音乐包，音频不会上传。设置 → 通用 可以关掉 MOD 音乐或调音量；音乐鉴赏里被替换的曲目有标记。说明见 [MOD 包](https://srw64.dreamquest.club/zh/docs/mods/)和[音乐 MOD](https://srw64.dreamquest.club/zh/docs/mod-music/)。
- **数值 MOD 可以改机体和驾驶员的地形适应**（`data/units.json`、`data/pilots.json`）。做了一个示例包「主力宇宙适应 A」，把魔神系、豪将军等我方主力里宇宙不到 A 的 20 名驾驶员和 21 台机体改成 A，在仓库的 `config/recomp/mods/space-a`。说明见[数值 MOD](https://srw64.dreamquest.club/zh/docs/mod-data/)，编号查[机体与人物 ID 表](https://srw64.dreamquest.club/zh/mods/ids/)。

#### 其他

- **地图上的机体图标按阵营朝向**：我方和友好的第三方朝左，敌方和敌对的第三方朝右，原版图和 HD 都是。
- **截图键可以改**：操作页里有「截图」一行（默认 F12 和 Print Screen）。安卓上图片存到「文件」里 Marchwind64 文件夹的 `screenshots/`，设置里还有一个触屏截图键；截图里不带触屏按钮和帧率显示。
- **跟不上 3、4 倍速的手机**（#10）：画不出每秒 90、120 帧的手机，现在游戏逻辑每帧都跑、每三帧画一帧，3 倍速和 4 倍速真的比 2 倍速快（天玑 7300 上一个敌方回合从 22 秒缩到 15 秒）。加速时游戏的线程会放到大核上。

### 安卓已测试的芯片

以下芯片的真机都能正常进入游戏（安卓 11–16）。天玑 6100+ 这类入门芯片约 14 FPS，骁龙 778G 约 14–22 FPS，其余基本是 30 FPS。没列出的芯片不代表不能玩，只是还没测过；欢迎在 Issues 反馈。希望天玑 8200 的玩家反馈一下是否能正常运行。

- 高通：骁龙 730/732G（Adreno 618，Pixel 4a）、骁龙 765G（Adreno 620，Pixel 5）、骁龙 778G（Adreno 642L，Galaxy A73）、骁龙 855/860（Adreno 640，Pixel 4、Poco X3 Pro）、骁龙 865/870（Adreno 650，Galaxy Tab S7、POCO F3）、骁龙 888（Adreno 660，Galaxy S21 Ultra）、骁龙 8 Gen 1（Adreno 730，Galaxy S22、小米 12）、骁龙 8+ Gen 1（Adreno 730，红米 K60）、骁龙 8 Gen 2（Adreno 740，小米 13、Galaxy S23）、骁龙 8 Gen 3（Adreno 750，Galaxy S24 Ultra）、骁龙 8 至尊版（Adreno 830，Galaxy S25）
- 联发科：天玑 7300（Mali-G615，Solana Seeker）、天玑 6100+（Mali-G57，Galaxy A15）
- 三星：Exynos 1380（Mali-G68，Galaxy A35）
- 谷歌：Tensor G2（Mali-G710，Pixel 7）、Tensor G3（Mali-G715，Pixel 8）

更早版本的更新见[官网博客](https://srw64.dreamquest.club/zh/blog/)。

### 功能预览

0.5 仍是功能预览：从头玩到尾的主要功能都已具备，但还很粗糙。

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
