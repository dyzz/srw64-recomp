An unofficial native port of **Super Robot Wars 64** with full English and Chinese translations. No ROM included: you need your own original Japanese ROM. 中文说明见下方。

> **HD pack {hd_version}** (optional; the Switch has a pack of its own, in the same release): **[Download {hd_name} ({hd_size})]({hd_download})**. {hd_status_en} How to install it and where its images come from: [the HD {hd_version} release]({hd_url}).

## Downloads

| File | Size | What it is |
| --- | --- | --- |
{package_rows_en}
| [`{hd_name}`]({hd_download}) | {hd_size} | Optional HD images (HD {hd_version}) for macOS, Windows, Linux and Android. |
{switch_hd_row_en}

## What's new in 0.5.2

### Traditional Chinese

- **Traditional Chinese (Taiwan) is the fourth language**, after Japanese, English and Simplified Chinese: the whole game, from the menus and the story to the stage info and the chapter title cards, with the Taiwan names of the units, people, weapons and series. It is converted from the Simplified Chinese text with a list of Taiwan names researched work by work, so the two stay in step. The first launch picks it on a Traditional-script system, in Taiwan, Hong Kong or Macau, and for Steam's Traditional Chinese; it is in the language menu too. The world map's name plates are still Simplified. The website has Traditional pages as well, at [/tw/](https://srw64.dreamquest.club/tw/).

### MOD packages

- **Android installs MOD packages now.** Put the `.srw64mod` (or `.zip`) in `mods/` inside the Marchwind64 folder under Documents, in the Files app, and it installs when the game starts or the MOD page opens; or press Install from file on the MOD page and pick it. A file that does not install is listed with the reason, on the desktop too.
- **Number mods set the stats**: a unit's HP, EN, movement, mobility, armor, limit, repair cost, upgrade cap, abilities, size, shield and sword and part slots; a pilot's six stats, SP, double move, skills and spirits; a weapon's power, range, hit, ammo, EN, morale, critical rate and terrain ranks. Saves keep the original values, so a package can be turned off again.
- **Name and portrait mods**: rename the four routes' heroes and partners and the unit (for display only; saves keep the game's names, so the package can be turned off), rename any pilot, unit or weapon, and replace any portrait with `art/portraits/<pilot>.png`, with or without the HD pack.
- **Map icon mods**: `art/icons/<unit>.png` replaces a unit's map icon with one picture of 16 to 128 pixels; the enemy and third-party rings, the acted grey and the facing are added as for the HD icons.
- **Battle voice mods**: `audio/voice/battle/<record>.ogg` plays as that battle line's box opens, over the game's own sound. Settings → General has a switch and a volume for MOD voices. We ship no voices, only the maker.
- **Makers on the website** for all of these, in your browser with nothing uploaded: the [number](https://srw64.dreamquest.club/en/mods/data/), [name and portrait](https://srw64.dreamquest.club/en/mods/names/) (with a framing window for the portrait), [map icon](https://srw64.dreamquest.club/en/mods/icons/) and [battle voice](https://srw64.dreamquest.club/en/mods/voices/) makers join the BGM maker, which now plays a song from its row and seeks on a click in the waveform. The [ID tables](https://srw64.dreamquest.club/en/mods/ids/units/) are three pages (units, pilots, weapons) with the original stats, abilities, skills and spirits. Guides: [Number mods](https://srw64.dreamquest.club/en/docs/mod-data/), [Name, portrait and map icon mods](https://srw64.dreamquest.club/en/docs/mod-names/), [Music mods](https://srw64.dreamquest.club/en/docs/mod-music/) (battle voices included).

### Fixes

- **Karaoke, the title demo and the battle viewer keep the original speed** at 2× and above: the lyrics counted frames while the song kept its tempo. Only a stage's battle animation runs at the chosen speed.
- **The battle viewer offers the forms and units no stage deploys**: Getter-2 and 3, Getter Liger, Shin Getter-2 and 3, Zambo-Ace and the other transformed forms, and the units only the story puts pilots in.
- **English names**: Gundam Nataku, and the Titans' Gundam Mk-II in the Library.

### Other changes

- **Android: import a new HD pack from the About page.** It shows the installed HD version; Import HD pack asks twice, since unsaved progress is lost, then closes the game and opens the pack picker in a fresh start.
- **Ready for [RecompLauncher](https://recomplauncher.com)**: the desktop packages carry its `recomplauncher.json`, and a game installed from the launcher gets the HD pack during setup. The launcher itself is not out yet.
- **The Linux package's folder is `Marchwind64-linux-x64`** whatever the version, like the Windows one, and the Windows zip again holds the `Marchwind64-windows-x64` folder, as every release before 0.5.1 did. If you extracted 0.5.1 over an old folder and the title still says a new version is out, the 0.5.1 files landed one level up: delete them and extract 0.5.2 over the folder.

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

### 本版更新（0.5.2）

#### 繁体中文

- **新增繁体中文（台湾）**，和日文、英文、简体中文并列：菜单、剧情、本关情报到章节标题卡全都有，机体、人物、武器和作品用台湾译名。它由简体中文文本转换而来，加一份逐部作品查过的台湾译名表，两边始终同步。系统是繁体中文、身在台湾、香港、澳门，或 Steam 设为繁体中文时，首次启动会选它；语言菜单里也能切。世界地图的地名牌暂时还是简体。官网也有繁体页面：[/tw/](https://srw64.dreamquest.club/tw/)。

#### MOD 包

- **安卓版可以安装 MOD 包了。** 把 `.srw64mod`（或 `.zip`）放进「文件」里 Marchwind64 文件夹的 `mods/`，游戏启动或打开 MOD 页时自动安装；也可以在 MOD 页点「从文件安装」直接选文件。装不上的文件会列在页面上并说明原因，桌面版也是。
- **数值 MOD 可以改数值了**：机体的 HP、EN、移动力、运动性、装甲、限界、修理费、改造上限、特殊能力、尺寸、盾与剑、部件槽；驾驶员的六项能力、SP、二回行动、技能和精神；武器的攻击力、射程、命中、弹数、EN、气力、暴击和地形适应。存档里保存的仍是原版数值，所以包随时可以停用。
- **名字与头像 MOD**：改四条路线主角、搭档和部队的名字（只改显示，存档里仍是游戏自己的名字，停用就恢复），改任意人物、机体、武器的名字，用 `art/portraits/<人物号>.png` 换任意头像，有没有 HD 包都行。
- **地图图标 MOD**：`art/icons/<机体号>.png` 换掉机体的地图图标，一张 16 到 128 像素的图就够，敌方和第三方的描边、已行动的灰化和朝向都按 HD 图标的做法自动加。
- **战斗语音 MOD**：`audio/voice/battle/<台词号>.ogg` 在战斗演出里这句台词框打开时播放，叠在游戏自己的声音上。设置 → 通用 有 MOD 语音的开关和音量。我们不提供语音，只提供制作工具。
- **官网的制作工具**覆盖以上全部，都在浏览器里完成、不上传任何东西：[数值](https://srw64.dreamquest.club/zh/mods/data/)、[名字与头像](https://srw64.dreamquest.club/zh/mods/names/)（头像可以拖动缩放裁切）、[地图图标](https://srw64.dreamquest.club/zh/mods/icons/)、[战斗语音](https://srw64.dreamquest.club/zh/mods/voices/)四个制作页，加上 BGM 制作页，现在可以在列表里直接试听、点波形跳转。[ID 表](https://srw64.dreamquest.club/zh/mods/ids/units/)分成机体、人物、武器三页，带原版数值、能力、技能和精神。说明见[数值 MOD](https://srw64.dreamquest.club/zh/docs/mod-data/)、[名字、头像与地图图标 MOD](https://srw64.dreamquest.club/zh/docs/mod-names/)、[音乐 MOD](https://srw64.dreamquest.club/zh/docs/mod-music/)（含战斗语音）。

#### 修复

- **卡拉OK、标题演示和战斗查看器不再跟着加速**：开着 2 倍速时歌词按帧走、歌却是原速。现在只有关卡里的战斗演出按所选速度跑。
- **战斗查看器能选没有关卡出击过的形态和机体**：盖塔2、盖塔3、盖塔狮虎、真盖塔2、真盖塔3、赞波ACE 等变形形态，以及只在剧情里配了驾驶员的机体。
- **英文译名**：Gundam Nataku，以及图鉴里泰坦斯的 Gundam Mk-II。

#### 其他

- **安卓版可以在「关于」页导入新 HD 包**：显示已装的 HD 版本，「导入 HD 包」会确认两次（未存档的进度会丢），然后退出游戏、重新启动并打开选包窗口。
- **为 [RecompLauncher](https://recomplauncher.com) 做好了准备**：三个桌面包里都带它的 `recomplauncher.json`，从启动器安装的游戏会在安装过程中自动下载 HD 包。启动器本身还没发布。
- **Linux 包解压出的文件夹固定叫 `Marchwind64-linux-x64`**，不再带版本号，和 Windows 包一样；Windows 的 zip 恢复为里面是 `Marchwind64-windows-x64` 文件夹，和 0.5.1 之前的历代一样。如果你把 0.5.1 解压覆盖到旧文件夹后标题仍提示有新版本，那是 0.5.1 的文件落在了旧文件夹的上一层：删掉它们，把 0.5.2 解压覆盖到文件夹上即可。

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
