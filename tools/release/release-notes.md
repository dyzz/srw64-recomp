An unofficial native port of **Super Robot Wars 64** with full English and Chinese translations. No ROM included: you need your own original Japanese ROM. 中文说明见下方。

> **HD pack {hd_version}** (optional; the Switch has a pack of its own, in the same release): **[Download {hd_name} ({hd_size})]({hd_download})**. {hd_status_en} How to install it and where its images come from: [the HD {hd_version} release]({hd_url}).

## Downloads

| File | Size | What it is |
| --- | --- | --- |
{package_rows_en}
| [`{hd_name}`]({hd_download}) | {hd_size} | Optional HD images (HD {hd_version}) for macOS, Windows, Linux and Android. |
{switch_hd_row_en}

## What's new in 0.5.0

### Nintendo Switch (experimental)

- **A Switch version**, for a Switch with custom firmware (Atmosphère and hbmenu). It runs in application mode only: hold R while starting any game to reach hbmenu.
- Unzip it to the SD card's root and put your ROM in `/switch/Marchwind64/` as `rom.z64`. The first launch imports it on the console in about half a minute.
- Fills a 16:9 screen, docked or handheld. The Switch has an HD pack of its own (in the HD 1.2 release); the desktop pack does not work on it.

### HD pack 1.2

- Every map unit icon redrawn in an HD-2D style: 320 of them, each side but yours with a ring in its colours.
- The story world map's two repeated tiles are now seamless HD.
- Touch-ups to some unit poses and portraits.
- GoShogun's Go Saber and Space Bazooka showed as white silhouettes in battle; they now have the original's colours.

### Battle viewer and Library

- **Combination attacks say their own lines**, with the partners the attack needs brought in for it.
- **Pilots are listed by character**: a pilot the game keeps more than one record for (Shishiris-Kiba's AI and others) has an entry of its own with its own portrait, and speaks its own lines in battle; before, it could fall back to whoever was shown last (Domon).
- **Every song sits under its work**: Ai no Kinjitou under Godmars, the game's own map and story music under Original, the Getter Robo theme under Getter Robo rather than G.
- **A controller reaches the Library and the battle viewer from the title**: down from the title menu, left and right along the corner buttons, A to open.

### Fixes

- **After a GAME OVER, retrying brought back reinforcements already on the map**: the same enemy twice, sharing its HP, and the game crashed once one was shot down. This had been there since 0.3.3.
- **With the original intermission screens, the Link screen was empty and nothing could be linked.**
- **Story choices sat on the window frame, left of the green cursor bar**, and a long option shrank the whole block. Each option is now indented as in the original, one per row.
- **A full-upgrade bonus weapon sometimes stayed locked**: after a machine swap with levels past the original cap (Mazinger Z upgraded to 13–15 becoming the JS type), or levels carried over by unified upgrades. Entering the intermission unlocks it, older saves included.
- **Pilot pages: a skill not learned yet showed as another skill at L9** (Fa Yuiri joined with Shield Defense L9).
- **Pilot list: the Getter team's units were off by one form** (Hayato on Getter-3, Musashi on Getter Poseidon).
- At 1080p a scrollbar showed beside the spirit tags on the pre-battle page.
- A few long notes in the settings window ran past its edge in Japanese and Chinese.
- Android: on x86 devices or emulators (ARM translation) the game stopped at "No CJK font found".
- Windows: some AMD integrated GPUs crashed after a driver reset under D3D12. Setting the environment variable SRW64_GRAPHICS_API=vulkan switches to Vulkan.

### New features

- **3× and 4× speed**: the speed key (Tab, R4, the phone's speed button) steps through original, 2×, 3× and 4× without a restart. 3× and 4× need the device to draw 90 and 120 frames a second and run slower where it cannot. Interpolation on high-refresh screens is smoother too.
- **The pilot panels in battle show a buff bar**: only the spirit effects in effect, your buffs in gold and the debuffs the other side cast in red; with none the row takes no room.
- **Combination attacks are always listed** in the intermission weapon list and the map's unit data; one that cannot be used now is grey, with a red line naming the partners it needs and how close they must stand. The Library names the partners too.
- **Return to Title is the second-to-last row of the map and intermission menus**, on the original intermission screens as well as the new ones.
- **EXP and money multipliers are separate cheats**, each ×1, ×1.5, ×2, ×3, ×5 or ×10. A multiplier set in an earlier version applies to both.
- **Touch control opacity goes down to 0%**: the controls are invisible but still work.
- **A lighter UI**: pages no longer redo their work every frame and update only what changed, so the UI thread on a phone uses about a third of what it did, and the pages and the pre-battle page answer keys faster.

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

### 本版更新（0.5.0）

#### Nintendo Switch 版（试验）

- **新增 Switch 版**，需要破解主机（Atmosphère＋hbmenu）。只支持应用模式：按住 R 启动任意游戏进入 hbmenu。
- 解压到 SD 卡根目录，把 ROM 命名为 `rom.z64` 放进 `/switch/Marchwind64/`。首次启动会在主机上导入，约半分钟。
- 底座和掌机都是 16:9 全屏。Switch 有自己的一份 HD 包（在 HD 1.2 发布页），电脑版的 HD 包在 Switch 上不能用。

#### HD 包 1.2

- 地图上的机体图标全部重画成 HD-2D 风格，共 320 个，除我方外都带阵营色描边。
- 剧情世界地图上两块重复的图块改为无缝 HD。
- 部分机体立绘和头像做了局部修补。
- 豪将军的豪军刀和太空火箭筒在战斗中显示成白色剪影，现在是原版的颜色。

#### 战斗查看器与图鉴

- **合体技会说合体技专用台词**，搭档按合体技的要求自动配上。
- **驾驶员按角色列出**：同一角色在游戏里有多条记录的（如シシリス・キバ 的 AI），各自单列、用自己的头像，战斗中说自己的台词；以前会变成上一个显示的驾驶员（如ドモン）。
- **BGM 都归到对应作品下**：愛の金字塔归ゴッドマーズ，游戏原创的地图和剧情音乐归「オリジナル」，ゲッターロボ 主题曲归ゲッターロボ 而不是 G。
- **手柄可以从标题画面进入图鉴和战斗查看器**：在标题菜单按下，左右选择角落的按钮，A 打开。

#### 修复

- **GAME OVER 后选择重试，已经登场的增援又出现一次**，同一个敌人在地图上有两个、共用 HP，击坠其中一个后游戏闪退。0.3.3 起就有这个问题。
- **场间画面设为原版时，联动画面是空的，无法联动。**
- **剧情中的选项压在窗口边框上、在绿色光标条左边**；选项较长时整块文字被缩小。现在每个选项和原版一样缩进、各占一行。
- **改造满段的隐藏武器有时不解锁**：段数超过原版上限后换机（如マジンガーZ 改到 13–15 段再换成 JS 型），或统一改造同步过去的段数。进入场间画面时会自动补上，旧存档也适用。
- **驾驶员页：还没学会的技能显示成别的技能 L9**（ファ・ユイリィ 一入队就有 S防御L9）。
- **驾驶员列表：盖塔组的机体名错了一档**（隼人显示ゲッター3、武藏显示ゲッターポセイドン）。
- 1080p 下战前确认页的精神标签旁出现滚动条。
- 设置窗口里几段较长的说明在日文和中文下超出窗口。
- 安卓：在 x86 设备或模拟器上（ARM 转译）启动停在「找不到中文字体」。
- Windows：部分 AMD 核显在 D3D12 下驱动重置后闪退。可以设置环境变量 SRW64_GRAPHICS_API=vulkan 改用 Vulkan。

#### 新功能

- **3 倍速和 4 倍速**：倍速键（Tab、R4、手机上的倍速按钮）在原速、2、3、4 倍速之间依次切换，不用重启。3 倍速和 4 倍速要求设备每秒画出 90、120 帧，达不到时实际速度会低一些。高刷新率屏幕上画面插帧也更顺。
- **战斗中驾驶员面板改为增益栏**：只显示正在生效的精神效果，我方增益为金色、对方施加的减益为红色；没有时不占地方。
- **合体技一直列出**：场间的武器一览和地图上的机体资料都会列出合体技；暂时用不了的显示为灰色，并用红字写明需要哪些搭档、要站在几格以内。图鉴里也会写明搭档。
- **「返回标题」移到地图菜单和场间菜单的倒数第二项**，场间菜单在原版和新画面里都有这一项。
- **经验倍率和资金倍率分成两项金手指**，各自可选 ×1、×1.5、×2、×3、×5、×10。旧版设过的倍率会同时用在两项上。
- **触屏按钮不透明度可以调到 0%**：按钮完全看不见，但照样能按。
- **界面更省资源**：各个页面不再每帧重复计算、翻页时只更新变化的部分，手机上界面线程的占用降到原来的约三分之一，翻页和战前确认页的按键响应更快。

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
