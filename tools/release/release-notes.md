An unofficial native port of **Super Robot Wars 64** with full English, Chinese and Korean translations. No ROM included: you need your own original Japanese ROM. 中文说明见下方。

> **HD pack {hd_version}** (optional; the Switch has a pack of its own, in the same release): **[Download {hd_name} ({hd_size})]({hd_download})**. {hd_status_en} How to install it and where its images come from: [the HD {hd_version} release]({hd_url}).

## Downloads

| File | Size | What it is |
| --- | --- | --- |
{package_rows_en}
| [`{hd_name}`]({hd_download}) | {hd_size} | Optional HD images (HD {hd_version}) for macOS, Windows, Linux and Android. |
{switch_hd_row_en}

## What's new in 0.5.4

### MOD packages: new units, characters and campaigns

The largest change in this version: a package can now add units, weapons and characters of its own and write whole campaigns, instead of only changing what the original has.

- **New units, weapons and characters have numbers of their own** instead of taking an original's place, so they join next to the whole original cast. A package can add:
  - units that **transform**, each form with its own weapons and map icon;
  - units that **combine on the map** by a recipe of their own, separate again and sortie combined. Upgrading the combined unit keeps the parts' own upgrades;
  - new weapons, **combination attacks** among them. The partners, their EN and morale are checked as in the original's, and the weapon pages, the Library and the Battle Viewer list the partners;
  - new characters with **their own battle quotes and voices**, for the usual battle moments and for a given weapon or opponent, and **battle songs** of their own;
  - new units and characters **on the enemy side**, deployed by the story;
  - **songs of its own** that replace none of the original's, and **a picture of its own** for an actor in a weapon's effect;
  - story lines in a text file for each language, and hidden elements that carry over to the next playthrough as the original's do.

  New units show their own picture and series in the Library and the Battle Viewer.
- **The website's unit maker** writes such a package in the browser:
  - copy an original character, unit or weapon, then change its names in four languages, its numbers, weapons, forms, crew and combination attack;
  - frame a portrait from a picture of your own; give a unit its stand pose, battle pictures with cut-ins and map icon, reduced to the game's colours in the browser;
  - battle quotes in four languages, each with a voice trimmed on its waveform, and battle songs;
  - weapon animations put together from the game's own effects: move each actor, borrow actors from any original weapon, add cut-ins;
  - when the unit joins and the package's own reinforcements; export as a Thunderstore package too;
  - a ready template to start from.

  A switch on the About page (off by default) lets the maker send a package to a running game and try it in the Battle Viewer. The game asks before it accepts a page.
- **The website's level editor** writes campaigns stage by stage:
  - maps painted for the campaign, deployments on either side, and enemy behaviour: when a unit starts moving, its target, what it leaves when shot down, its HP and EN on arrival;
  - conditions: reaching an area, units shot down by side, group or pilot, units remaining, turns, HP, route, the last choice and funds, alone or combined;
  - choices with their own options, stage objectives, options in the middle of a story, 「???」 speakers in silhouette, and a pilot's new lines at chosen moments of a battle;
  - the original's stage instructions in plain forms: camera, effects, moving and destroying units, changing sides, sorties, upgrades, funds, world map places, green target areas and scripted battles;
  - project files that keep unfinished work, undo and redo of every edit, songs, sounds, weapons and maps chosen by name, and a two-stage template to start from.
- **New package, Kurtz and Super Earthgain**: after "Time to Settle It" on Brad's route, Kurtz joins and Earthgain and Virose combine into Super Earthgain, with Brad as the main pilot and Kurtz as the sub pilot; it keeps Earthgain's upgrades. It sorties combined, separates on the map into Earthgain and Virose, who have the combination attack Shorai Tenryu Mametsujin, and combines again when the two stand side by side.
- **Final Dancouga** now sorties combined and separates and combines on the map the same way, and its map icon fills its square. Saves made with its 0.1.0 load in 0.5.4.
- The idea of combining and separating on the map and of sortieing combined came from w6408004 and 柯博文2016 on Bilibili.
- **Packages are read in place**: installing only puts the `.srw64mod` file in the mods folder, without unpacking it, so a voice package installs in seconds. **Thunderstore packages install** as ours do, and so does a zip that holds `.srw64mod` files. A file that does not install is reported instead of listed as a broken package, and the MOD page can delete it.
- **A move or a weapon range above 15 works**: the original's tables stop at 15, and beyond it the game works the range out itself.
- **MOD battle voices are not cut off**: at the original speed a quote's box stays until its voice has been spoken, and the next quote waits for the previous line. A line longer than its box holds the pose and camera until it ends, and a defender's quote waits for the hit.

### Battle

- **Battles fill a wide screen**: shots flying in and out, beams, special moves' backdrops, the scrolling clouds, the Earth's still sky and the Devil Gundam's tentacles spread across 16:9, instead of showing mid-screen with black sides. Dragon Claw reaches its target. The units and the ground stay where the original puts them, and the battle's results do not change.
- **The pre-battle spirit list shows one pilot at a time**: a tab per pilot with the face and SP, that pilot's commands, and the focused command's description with the SP left after casting. With a controller the focused command is always in view.
- **Battle Viewer**: the cursor is easier to follow with keys or a controller, the scene cards load together, and the title no longer shows through as a battle starts.

### Library

- **An Animations tab** plays the twelve combining, transformation and Aura Road movies of the tactical map, with the game's own sound and music, in the original pictures or HD.
- The Japanese Library keeps the game's own names, and records the game never uses are marked as such in Chinese and Korean. The weapon tables' headings fit their numbers.

### Windows

- **PCs whose GPU RT64 cannot use now run the game**: with neither Direct3D 12 shader model 6.0 nor Vulkan 1.2 (Intel before the 6th generation, older NVIDIA and AMD cards), the game draws with a compatible OpenGL renderer instead of failing to start. The same happens when RT64 fails to start after all.
  - This renderer has no frame interpolation, MSAA or RetroArch filters, so those settings cannot be chosen.
  - It draws at a lower resolution by default; **Picture detail** in the settings raises it.
- AMD Vega integrated graphics use Vulkan, since Direct3D 12 reset their driver.
- A PC with no GPU driver no longer crashes at start.
- No console window flashes open when the game starts.

### Android

- **A game the system closes in the background comes back where it was left**: once you switch to another app, the game saves at its next save point (the map waiting for orders, or the intermission's menu), apart from your saves and autosaves. If Android then closes it, the next start offers to go back to that map or intermission.
- In the background the game stops running and gives back memory. With the HD pack it takes 703 MB instead of 1118 MB at the title and 887 MB instead of 1450 MB in the background on our test phone, so the system closes it less often.

### Controls, touch and launchers

- **Phone controls can be moved and resized**: Settings → Input → Button layout. Drag the D-pad or any button, size each one or all together, then save, cancel or go back to the defaults.
- **Quit game** at the foot of the settings window (not on Android), reached by Up from the tab bar. Left and Right move between a question's answers.
- A controller reaches the title's corner entries at PRESS START. The language list shows 한국어 instead of boxes.
- The title's Library, Battle Viewer and MOD entries take a tap even under a touch button. A failed HD import on Android says what went wrong.
- **RecompLauncher**:
  - its setup asks for the ROM and installs the HD pack;
  - under it the game starts full screen in Big Picture and leaves the update check to the launcher;
  - a language chosen in it before the game's first start no longer stops the game from starting, and a start that fails leaves its log for the launcher's crash report.

### Performance

- The Library and the Battle Viewer open faster: their data is made in the background.
- The intermission pages (upgrades, parts, pilots and the rest) no longer rebuild themselves every frame when nothing changed, so moving between them stutters less.
- A battle's scene transition redraws only the parts of the screen its lines cross, not the whole picture.
- The first battle quote after starting the game no longer hitches when voices are on.
- The protagonist selection shows at once; the portraits appear as they finish loading.

### Switch

- **Smoother overall.** Measured on a docked Switch:
  - The audio takes about 5% of a CPU core instead of 27% (41% at its worst).
  - The interface is no longer redrawn when nothing on it changed; presenting a frame takes a quarter of the time it did, and the render thread half its CPU.
  - All HD pictures are read from one file instead of thousands of small ones, and no longer a small piece at a time. The first battle had 14 reads that stalled and now has none.
  - Battle textures used last time are read ahead, and new pictures are decoded on helper threads.
  - The first opening: the Library 0.96 s → 0.3 s, the Battle Viewer 0.8 s → 0.18 s, the protagonist selection 1.8 s → under 0.1 s. The upgrade screen and the Battle Viewer's unit list stall far less.
  - A voice package installs in under a second; it took minutes to unpack on the SD card.
- The Switch version still stutters in places.
- **With a voice MOD the game no longer aborts after about 20 minutes of play.** This came with 0.5.3.1.
- **Quit game** returns to the Homebrew Menu. A text update that arrives mid-game no longer ends the game.

### Text

- Chinese battle cries and drawn-out shouts read as Chinese instead of kana spelled out.
- English names follow the Japanese originals instead of the dub: Gundam Spiegel, Dokuga, and God Gundam's and Devil Gundam's attacks.
- Names from the website's suggestions, among them English Simurgh and Chinese 斩空者, 拉兹格瑞丝, 希格露恩, 龙蜥海兽 and 大扎姆. 0.5.3 has already received these as text updates; 0.5.4 carries them.

### Fixes

- **A mirrored map icon sits on its square** again instead of one texel over.
- Holding R to jump no longer swings the wide view off the map.
- Battles with Final Dancouga 0.1.0 no longer show wrong scenes or crash on the Steam Deck.
- A new character that copies a protagonist shows its own name.
- The Saves page brings an import's result into view.

### HD 1.4

- **Map icons fill the original's square**: the HD icons left about 8% empty around them and looked smaller than the original's. The Gundam Mk-II's icon is redrawn.
- The 15 battle sky colourings still missing are added, and Domon's, Master Asia's and Albert's battle actions on foot use their own pictures. The Battle Viewer's Earth scene has its picture.

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

非官方的《超级机器人大战64》原生移植，附全游戏中文、英文与韩文翻译。不含 ROM：需要自备日版原版 ROM。

> **HD 包 {hd_version}**（可选；Switch 用单独的一份，在同一个发布页）：**[下载 {hd_name}（{hd_size}）]({hd_download})**。{hd_status}安装方法和图片来源见 [HD {hd_version} 发布页]({hd_url})。以后 HD 包出新版本时，游戏的更新检查会提示。

### 下载

| 文件 | 大小 | 内容 |
| --- | --- | --- |
{package_rows}
| [`{hd_name}`]({hd_download}) | {hd_size} | 可选的 HD 图片包（HD {hd_version}），macOS、Windows、Linux、安卓通用。 |
{switch_hd_row}

### 本版更新（0.5.4）

#### MOD 包：新机体、新人物和新战役

这是本版最大的改动：MOD 包不再只能改原版已有的内容，可以加自己的机体、武器和人物，也可以写完整的战役。

- **新机体、新武器和新人物有了自己的编号**，不用再占原版的位置，和原版全部角色并存。包里可以加：
  - 能**变形**的机体，每种形态有自己的武器和地图图标；
  - 按自己的配方**在地图上合体**、能再分离、以合体状态出击的机体。改造合体机体时，各部分自己的改造保留；
  - 新武器，包括**合体技**。搭档、EN 和气力按原版的规则判断，武器页、图鉴和战斗查看器都会列出搭档；
  - 新人物，带**自己的战斗台词和语音**（常规战斗时机，以及针对某件武器或某个对手的台词）和自己的**战斗曲**；
  - **敌方**的新机体和新人物，由剧情安排登场；
  - 包**自己的曲子**（不替换原版任何一首），以及武器特效里演员**自己的图**；
  - 每种语言一个文本文件的剧情台词；像原版一样带到下一周目的隐藏要素。

  新机体在图鉴和战斗查看器里显示自己的图和所属作品。
- **官网的机体制作工具**在浏览器里直接做这样的包：
  - 复制一个原版人物、机体或武器，再改四种语言的名字、数值、武器、形态、乘员和合体技；
  - 用自己的图裁出头像；给机体配站姿、带 cut-in 的战斗图和地图图标，在浏览器里减到游戏的色数；
  - 四种语言的战斗台词，每句可配语音，在波形上剪裁；战斗曲；
  - 用游戏自带的特效拼武器演出：拖动每个演员，从任意原版武器借演员，加 cut-in；
  - 加入时机和包自己的增援；也能导出成 Thunderstore 包；
  - 有现成的模板可以直接改。

  「关于」页有一个开关（默认关），打开后制作工具能把包直接发给正在运行的游戏，在战斗查看器里试。游戏会先问是否接受这个网页。
- **官网的关卡编辑器**一关一关地写战役：
  - 为战役自己画的地图、敌我双方的部署，以及敌人的行动方式：几回合后开始行动、攻击目标、被击坠时掉落的部件、登场时的 HP 和 EN；
  - 条件：到达区域，按阵营、部队或驾驶员计的击坠数，剩余机体数，回合，HP，路线，上一次的选项，资金，可以单独用也可以组合；
  - 带自己选项文字的选择、作战目的、剧情中途的选项、显示为剪影的「???」说话人，以及驾驶员在战斗中指定时机说的新台词；
  - 原版的关卡指令都有易写的形式：镜头、特效、移动和消灭机体、改变阵营、出击、改造、资金、世界地图地点、绿色目标区域、剧情战斗；
  - 工程文件能保存没做完的工作，每一步都能撤销和重做，曲子、音效、武器和地图都按名字选，还有两关的模板可以直接改。
- **新 MOD「卡茨与超级夺地者」**：通关布拉德篇「了断之时」后，卡茨入队，夺地者与斩空者合体为超级夺地者，布拉德主驾驶、卡茨副驾驶，改造段数从夺地者继承。出击时是合体状态，在地图上分离后回到夺地者和斩空者，可以使用合体技「招雷 天龙魔灭阵」，两台相邻时可以再合体。
- **终极断空我**也改为合体状态出击，在地图上同样可以分离、合体，地图图标顶满格子。用它 0.1.0 做的存档能在 0.5.4 读取。
- 地图上合体分离和合体状态出击的思路来自 B 站用户 w6408004 和 柯博文2016。
- **MOD 包不再解压**：安装时只把 `.srw64mod` 文件放进 mods 文件夹，游戏直接从包里读，语音包几秒就装好。**Thunderstore 的包**也能照样安装，装着 `.srw64mod` 的 zip 也能直接装。装不上的文件会报告出来，不再列成一个坏掉的包，并且可以在 MOD 页删掉。
- **移动力和射程超过 15 也能用了**：原版的表只到 15，超过时由游戏自己算范围。
- **MOD 战斗语音不再被截断**：原速时台词框等语音说完才关，下一句等上一句说完。比台词框长的语音会让姿势和镜头停住等它说完，被击中一方的台词等命中那一下再出。

#### 战斗

- **战斗画面铺满宽屏**：飞进飞出的射击、光束、必杀技的背景、滚动的云层、地球的静止天空和恶魔高达的触手都铺满 16:9，不再只在画面中间、两边留黑。龙爪能够到对手。机体和地面仍在原版的位置，战斗结果不变。
- **战前的精神指令一次只显示一名驾驶员**：每名驾驶员一个页签，带头像和 SP；下面是这名驾驶员的指令，以及选中指令的说明和施放后剩余的 SP。用手柄时选中的指令总在视野里。
- **战斗查看器**：用键盘和手柄时光标更容易看清，场景卡片一起载入，开战时不再透出标题画面。

#### 图鉴

- **新增「动画」页**，播放战术地图上的 12 段合体、变形和奥拉之路动画，带游戏原有的音效和音乐，可以切换原版画面和 HD。
- 日文图鉴用游戏原有的名字，游戏里没用到的记录在中文和韩文里标为未实装。武器表的表头和数字一样宽。

#### Windows

- **RT64 用不了的显卡也能玩了**：既没有 Direct3D 12 着色器模型 6.0、也没有 Vulkan 1.2 时（6 代以前的 Intel 核显、较老的 NVIDIA 和 AMD 显卡），游戏改用兼容的 OpenGL 渲染方式，不再打不开。RT64 启动失败时也会这样切换。
  - 这种方式没有插帧、MSAA 和 RetroArch 滤镜，这几项设置不能选。
  - 默认分辨率较低，设置里的**画面精细度**可以调高。
- AMD Vega 核显改用 Vulkan，因为 Direct3D 12 会让它的驱动重置。
- 没有显卡驱动的电脑启动时不再闪退。
- 启动时不再闪出控制台窗口。

#### 安卓

- **切到后台被系统关闭后能回到原处**：切到其他应用后，游戏在下一个存档点（地图上等待指令时，或场间主菜单）另存一份，不占存档栏，也不影响自动存档。之后如果系统把游戏关掉，下次启动会问要不要回到当时的地图或场间画面。
- 在后台时游戏停止运行，并交还内存。带 HD 包时，测试手机上标题画面占用的内存从 1118 MB 降到 703 MB，后台从 1450 MB 降到 887 MB，被系统关掉的情况会少一些。

#### 操作、触屏与启动器

- **手机触屏按键可以移动和调大小**：设置 → 操作 → 按键布局。拖动方向键或任意按钮，单独或整体调大小，然后保存、取消或恢复默认。
- **设置窗口底部有「退出游戏」**（安卓没有），从页签栏按上即可选到。问答框里可以用左右键切换选项。
- 在 PRESS START 画面，手柄也能选到标题画面角落的入口。语言列表里的「한국어」不再显示成方块。
- 标题画面的图鉴、战斗查看器和 MOD 被触屏按键盖住时也能点到。安卓导入 HD 包失败时会说明原因。
- **RecompLauncher**：
  - 在设置步骤里选 ROM 并装好 HD 包；
  - 从它启动时游戏以 Big Picture 全屏启动，更新检查交给启动器；
  - 在游戏第一次启动前就在启动器里选了语言，不再导致游戏无法启动；启动失败时会留下日志，供启动器的崩溃报告使用。

#### 性能

- 图鉴和战斗查看器打开更快：资料在后台准备。
- 场间画面（改造、强化部件、驾驶员等）内容没变时不再每帧重建，切换画面时卡顿减少。
- 战斗的转场只重画线条经过的部分，不再整屏重画。
- 开启语音时，开游戏后第一句战斗台词不再卡一下。
- 主角选择页立即显示，头像载入完成后逐个出现。

#### Switch

- **整体更流畅。** 在 Switch 底座模式下实测：
  - 音频约占一个 CPU 核心的 5%，原来是 27%（最高 41%）。
  - 界面内容没变时不再重画；呈现一帧的时间降到原来的四分之一，渲染线程的 CPU 占用减半。
  - HD 图片从一个文件里读取，不再打开成千上万个小文件，也不再一小块一小块地读。第一场战斗原来有 14 次读图卡顿，现在没有了。
  - 上次用过的战斗贴图会提前读入，新图片在辅助线程上解码。
  - 第一次打开：图鉴 0.96 秒 → 0.3 秒，战斗查看器 0.8 秒 → 0.18 秒，主角选择页 1.8 秒 → 不到 0.1 秒。改造画面和战斗查看器的机体列表卡顿大大减少。
  - 语音包不到一秒就装好，原来在 SD 卡上解压要几分钟。
- Switch 版仍有卡顿。
- **装了语音 MOD 时，玩二十分钟左右游戏中止的问题修好了。** 这个问题从 0.5.3.1 开始出现。
- **「退出游戏」回到 Homebrew 菜单**。游戏中途收到文本更新不再导致游戏退出。

#### 文本

- 中文的战斗喊声和拖长的叫声改成中文写法，不再是假名的音译。
- 英文名照日文原名而不是英语配音版：Gundam Spiegel、Dokuga，以及神高达、恶魔高达的招式名。
- 采纳了官网的译名意见，其中有英文 Simurgh，中文斩空者、拉兹格瑞丝、希格露恩、龙蜥海兽、大扎姆。这些已经以文本更新的方式发给了 0.5.3，0.5.4 随包带上。

#### 修复

- **镜像显示的地图图标回到格子上**，不再偏一个像素。
- 按住 R 跳格时宽屏视野不再晃出地图。
- 用终极断空我 0.1.0 时，战斗画面不再错乱，Steam Deck 上也不再闪退。
- 复制主角的新人物显示自己的名字。
- 「存档」页导入存档后，结果会滚到看得见的地方。

#### HD 1.4

- **地图机体图标顶满原版的格子**：HD 图标原来四周各空出约 8%，看起来比原版小。高达 Mk-II 的图标重画。
- 补齐还缺的 15 种战斗天空配色；多蒙、东方不败和阿尔伯特的生身战斗动作换成各自的图。战斗查看器的「地球」场景有了图。

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
