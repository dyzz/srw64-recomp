An unofficial native port of **Super Robot Wars 64** with full English, Chinese and Korean translations. No ROM included: you need your own original Japanese ROM. 中文说明见下方。

> **HD pack {hd_version}** (optional; the Switch has a pack of its own, in the same release): **[Download {hd_name} ({hd_size})]({hd_download})**. {hd_status_en} How to install it and where its images come from: [the HD {hd_version} release]({hd_url}).

## Downloads

| File | Size | What it is |
| --- | --- | --- |
{package_rows_en}
| [`{hd_name}`]({hd_download}) | {hd_size} | Optional HD images (HD {hd_version}) for macOS, Windows, Linux and Android. |
{switch_hd_row_en}

## What's new in 0.5.3

### Korean

- **Korean is the fifth language**, translated by 테오 ([jung5441](https://github.com/jung5441)), the author of the Korean patch for the N64 game, who contributed the whole translation under CC BY 4.0: the story and the battle lines, the interface and the names, with a Korean title logo and Korean faces for the chapter title cards and the battle banners. The first launch picks it on a Korean system, and it is in the language menu; what is not translated yet shows in English.
- **Language packages**: a MOD package can add a reading language over English or another built-in one, with its own fonts and pictures. The website has a template tool and a guide.

### MOD packages

- **Story MODs change what happens in the original stages.** A package adds events to a stage: a persuasion, a unit leaving or joining at a chosen level, a replaced or removed unit, new lines, long-term flags, and conditions on the roster or a pilot's kills. The game triggers and tracks them itself; their state is kept in suspend and intermission saves and cleared by a new game. The first one is on the MOD page: **Master Asia Lives**. On the Independence Army and OZ routes, once Domon has talked with Master Asia in battle he can persuade him; with 50 kills or more Master Asia withdraws instead of falling and joins after the stage at Domon's level, in the Master Gundam, with little time left; with fewer the persuasion fails once and the story goes as the original's.
- **Data MODs can add new units**, with their weapons, battle scenes, icon and pose on a free number. The example is **Final Dancouga**: after the stage where Dancouga learns the Dancou Kougaken, Dancouga and Black Wing combine into Final Dancouga, Alan rides along as the fifth pilot and the upgrades carry over. Its pictures need HD 1.3. A save made with a package's new unit and loaded without the package goes back to the title with a banner naming the package, instead of breaking.
- **Number MODs reach a weapon's kind**: melee or ranged, usable after moving (P), beam (B), parryable, the terrain the attacker and the target need, the skill it requires, its upgrade type and whether it is locked. The 格/射, P and B markers in the weapon lists follow in every language. Map weapons and combination attacks cannot be made.
- **MOD battle voices follow the game speed**: at 2× and above a line is time-stretched with its pitch kept, or, by a new setting, stays at the original speed or is muted while accelerated. A line cut off by the next one, or by aborting the animation, fades out instead of clicking.
- The save slots show a name MOD's portrait of the protagonist; the MOD manager breaks Chinese and Japanese paragraphs into lines; the MOD page no longer says extra scenarios do not take effect (they have since 0.5.1). The original characters' AI battle voices are on the website as a voice package.

### Text updates between releases

- **Text fixes arrive without a new program.** The update check (the title prompt and the About page) now also fetches the newest text from the website: the dialogue, names and interface labels fixed since this release, only the files that changed, each checked by hash before it is installed. The About page shows the text in use and can go back to the bundled text; a player's own dialogue files still win.
- **The update check works on Android and the Switch** too: the title prompt, the About page's check and the new-version notice. Only the Vita has none.

### Map and screens

- **Ground units cast a shadow**: their own silhouette under the icon, as in the later games; a flying unit's shadow sits closer to its icon. With the HD images every shadow is soft.
- **A pilot who acts twice a phase shows the crown on the pilot pages**, not only in the map's unit window: the map's pilot page and the intermission pages, drawn from the ROM's glyph with the original images and from a redrawn W mark with the HD images.
- **HD map icons** (in HD 1.3): the enemy's and the third party's rings are gone, now that each side faces its own way; acted units grey at 60% instead of 85%, so a white or grey unit visibly changes; the GP03 Dendrobium and the Qubeley are brighter before they act.
- **Translated labels stay inside their window frames**: a value the original draws against a panel's right edge, like the weapon list's required skill, now starts further left instead of running out of the frame; split sentences end at their own full stop; a label the original draws twice at one place is drawn once.
- **The title demo's units fade in correctly with the HD images**: the palette animation recoloured the whole sprite, so highlights and eyes went dark and the picture jumped between HD and original; it now recolours only the entries the frame shows.
- The Start Campaign box sits bottom left, clear of the ring's START and of the Library and MOD row; the Extra Scenarios page sees a campaign's first save at once; the stage info page's text is a readable size on phones; the whole-map view (C-right; H on the keyboard) is back on the Controls page and can be rebound.

### Touch controls

- **Hold Back for the whole-map view** on the tactical map, since phones have no C button. A short press is still B.
- **The title's Library, Battle Viewer and MOD corner show with touch controls** and are tapped where they are, so a phone has the MOD entry on the title; the first tap of a run is no longer lost.

### Fixes

- **Banjo and Daitarn 3 join on the real-robot OZ route** as in the original: they were missing from the join event of the stage where they arrive. An old save that should have them is repaired when it is loaded.
- **Battle viewer**: each Getter form has its own pilot (Hayato in the 2-forms, Musashi or Benkei in the 3-forms), and a viewer battle shows every protagonist's own face and name instead of an empty label.
- **Text**: the 13 original characters' lines were reviewed against their profiles for tone and forms of address, in Chinese and English; in Chinese Aisha calls Manami {{HeroName}}小姐 throughout, Dancouga's ground units are 豹战车, 狮战车 and 象战车, ダイガン is 断空步枪 and ムゲ is 穆格 in every line; Traditional Chinese writes 週目 and 龍捲風 throughout.
- **The map's figures sit on their unit in the widened view**: a spirit's +10, the damage and MISS were drawn offset at 16:9.
- The title's press and a quick load wait for every button to be released, so a held key no longer repeats; the intermission pages close when the game leaves the intermission; labels the original centres are centred in translation too.

### HD 1.3

- **The battle animations are in HD** for the first time: weapon effects, beams (the original shapes kept strictly, Super Robot Wars Z's look inside), hits, follow-on frames including Gundam Rose's bits in their turning angles, shields, barriers and the other defence effects, kill explosions, the map's combination and transformation movies, the special moves' brush characters and their scrolling backdrops. 29 unit poses are fixed or redrawn (seven whole redraws, Master Asia, Dancouga's body and icon), Giant Robo's frames are added, and the map icon changes above are in it. **HD 1.3 needs 0.5.3**: identical pictures now share one file and the program recolours them per row; on 0.5.2 several colour variants would come out alike.

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

### 本版更新（0.5.3）

#### 韩文

- **新增韩文**，由 N64 版韩文补丁作者 테오（[jung5441](https://github.com/jung5441)）翻译并以 CC BY 4.0 授权收录：剧情与战斗台词、界面和各类名字，配韩文 logo，章节标题卡和战斗横幅用韩文字体。韩文系统首次启动会选它，语言菜单里也能切；还没译到的部分显示英文。
- **语言包**：MOD 包可以在英文或其他内置语言之上加一种阅读语言，自带字体和图片；官网有模板工具和制作说明。

#### MOD 包

- **剧情 MOD 可以改原版关卡里发生的事。** 包给关卡加事件：说服、人物离队或以指定等级加入、替换或移除机体、新台词、跨关卡的旗标，以及按名册或驾驶员击坠数判定的条件。触发和记录都由游戏自己做，状态随中断存档和场间存档保存，开新游戏时清空。第一个剧情 MOD 已在 MOD 页：**东方不败存活**。独立军和 OZ 两条路线上，多蒙在战斗里与东方不败交谈过之后可以说服他；击坠数 50 以上时东方不败撤退而不是战死，过关后以与多蒙相同的等级驾驶尊者高达加入，但时日无多；不够则说服失败一次，之后与原作相同。
- **数值 MOD 可以新增机体**，连武器、战斗场景、图标和立绘一起放在空闲编号上。示例是**终极断空我**：学会断空光牙剑的那一关通关后，断空我与黑翼合体为终极断空我，阿兰作为第五名驾驶员同乘，改造段数继承。它的图片需要 HD 1.3。带着包里新机体的存档，在没装这个包时读取，会回到标题并提示缺哪个包，而不是出错。
- **数值 MOD 能改武器的种类**：格斗或射击、移动后可用（P）、光束（B）、能否被切り払い、攻守双方要求的地形、需要的技能、改造类型，以及是否锁定。武器列表里的 格/射、P、B 标记各语言都跟着变。MAP 武器和合体技不能新造。
- **MOD 战斗语音跟随游戏速度**：2 倍速及以上时台词按比例加快而音调不变；新设置可以改成保持原速或加速时静音。被下一句打断或中止演出时，语音淡出而不是戛然而止。
- 存档栏显示名字 MOD 给主角换的头像；MOD 管理页的中日文段落会按行折行；MOD 页不再写「追加剧本不生效」（0.5.1 起就生效了）。原创人物的 AI 战斗语音作为语音包放在官网。

#### 版本之间的文本更新

- **文本修正不用等新版程序。** 更新检查（标题提示和「关于」页）现在还会从官网取最新文本：这一版发布之后修过的台词、名字和界面词条，只下载有变化的文件，每个文件装前都按哈希核对。「关于」页显示正在用的文本，可以退回随包的文本；玩家自己的台词文件仍然优先。
- **安卓和 Switch 也有更新检查了**：标题提示、「关于」页的检查和新版本提醒。只有 Vita 还没有。

#### 地图与画面

- **地面机体有影子了**：图标下方是自己的剪影，像后来的机战；飞行机体的影子离图标更近。HD 画面下影子都是柔边的。
- **能二回行动的驾驶员在驾驶员页也显示皇冠**，不再只在地图的机体窗口里：地图的驾驶员页和场间的驾驶员页都有，原版画面用 ROM 里的字形，HD 画面用重画的 W 标记。
- **HD 地图图标**（在 HD 1.3 里）：敌方和第三方的描边去掉了，因为各阵营已经朝向不同；已行动的灰化从 85% 改为 60%，白色和灰色机体行动前后看得出区别；GP03石斛兰和卡碧尼行动前更亮。
- **翻译文字不再超出窗口框线**：原版贴着面板右边画的数值（比如武器列表的必要技能），现在往左挪而不是冲出框线；被拆开的句子在自己的句号处结束；原版在同一位置画两遍的文字只画一遍。
- **HD 画面下标题演示的机体正常渐显**：调色板动画原来把整张精灵重新着色，高光和眼睛变暗，画面还在 HD 和原版之间跳；现在只按这一帧用到的调色板项换色。
- 开始战役的框放到左下角，不再压着转盘的 START 和图鉴、MOD 那一行；追加剧本页能立刻看到战役的第一个存档；本关情报页的文字在手机上是能看清的大小；全图视图（C→，键盘 H）回到操作页，可以改键。

#### 触屏

- **长按返回键看全图**：手机没有 C 键，战术地图上按住返回 0.5 秒打开全图视图。短按仍是 B。
- **标题画面右下角的图鉴、战斗查看器和 MOD 在触屏下也显示**，点哪个就是哪个，手机上标题画面也有 MOD 入口了；一局里的第一次点击不再被吞掉。

#### 修复

- **万丈和泰坦3 在真实系 OZ 路线正常加入**，和原版一样：原来登场那一关的加入事件里漏了这两人。本该有他们的旧存档读取时会补上。
- **战斗查看器**：盖塔各形态用各自的驾驶员（2 形态是隼人，3 形态是武藏或弁庆）；从标题进的战斗显示每位主角自己的头像和名字，不再是空标签。
- **文本**：13 位原创人物的台词按人物设定逐句核对了口吻和称呼（中英文）；中文里艾夏对玛娜米一律称「{{HeroName}}小姐」，断空我的地面机体是豹战车、狮战车、象战车，ダイガン 是断空步枪，ムゲ 每一句都是穆格；繁体中文的「週目」「龍捲風」全文统一。
- **宽屏下地图上的数字回到机体上**：気合的 +10、伤害和 MISS 在 16:9 时画偏了。
- 标题的按键和快速读档会等所有按键松开再接着响应，按住的键不再连按；离开场间时场间页面会一起关掉；原版居中画的文字翻译后也居中。

#### HD 1.3

- **战斗演出第一次有了 HD**：武器特效、光束（严格保持原版外形，内部照第二次机战 Z 的画法）、命中、招式的后续画面（含玫瑰高达浮游炮的各个转向角度）、盾牌、护罩和其他防御特效、击坠爆炸、地图上的合体和变形动画、必杀技的毛笔字和它背后的滚动背景。机体立绘修了或重画了 29 张（7 张整张重画、东方不败、断空我的身体和头像），补上大铁人的帧，上面说的地图图标改动也在里面。**HD 1.3 要配 0.5.3**：相同的图现在只存一份、由程序按行分别换色，在 0.5.2 上几种换色会混成一种。

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
