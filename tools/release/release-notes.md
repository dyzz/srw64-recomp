非官方的《超级机器人大战64》原生移植，附全游戏中文与英文翻译。不含 ROM：需要自备日版原版 ROM。

## 本版更新（0.4.4）

- **宽屏：战斗中画面上方不再露出楼块和黑带。** 镜头拉近时天空会往下移，原版靠上方的黑色遮幅条挡住从底部环绕上来的远景；宽屏去掉遮幅条后，两块 HP 窗之间会露出一条黑带和楼群碎块（所有平台都有）。现在天空整张略微放大（约 7%），顶部露出的是天空本身。4:3 设置保持原版不变。
- **HD：战斗天空换成高清。** 战斗背景的全部 42 张天空图（91 种配色）放大到 4 倍，保留原版的颗粒感。战斗鉴赏选场景时的缩略图也换成了游戏里实拍的 HD 画面。需要 HD 1.1。
- **修复关闭游戏时偶尔卡住、窗口关不掉。** 图形库退出时着色器编译线程可能收不到停止信号，现已修复，所有平台都有效。
- **修复整备时进入「武器改造」偶尔闪退或卡死**（0.4.2／0.4.3 的报告）。
- **截图快捷键：F12 或 Print Screen**，所有画面都能用，保存到数据文件夹的 `screenshots`。Windows／Linux 另外可以用 Alt+Enter 切换全屏（原有 F11）。
- **安卓：对白时一根手指就能跳过剧情。** 对白和序章页上，触屏按钮会换成「跳过」「自动」「按住快进」，不用再同时按 R1 和 START。
- **安卓：触屏方向键固定在左下角，按钮透明度可以在设置里调。**
- **安卓：导出存档和问题报告放进公共「下载」文件夹**（Download/Marchwind64/saves、reports），普通文件管理器就能找到，不需要任何存储权限。
- 按住快进时对白改为每 0.3 秒翻一页，不会一下子跳过太多。
- 存档列表和场间关卡栏里，「通关」紧跟在关卡标题后面，不再被拆成两行。
- 英文界面：胜利条件、失败条件、出击确认窗口的标题缩短，不再被压窄。

## 0.4.3 的更新

- **修复开着 HD 图片时游戏随机卡死。** 所有平台自 0.3.1 起，开着 HD 图片时偶尔会在某个画面永久卡住（有时声音也停），连 Esc 都关不掉，经过多张 HD 地图的开场后更容易遇到。原因是渲染线程释放过期的 HD 地图时把自己锁死了，现已修复。
- **战斗：在战前确认页施放的精神现在真正生效。** 原先在页面上用了精神后只刷新了显示的命中率：「必中」显示 100%，战斗却仍按施放前掷好的结果演；热血、魂、气合、铁壁、毅力、大毅力、幸运、努力也都没算进这一战。现在施放后按原版流程重新结算，保留已选的应对和反击武器。
- **安卓：修复部分手机打开就退出。** 显卡驱动较旧的 Mali 手机（如三星 Galaxy A15、A35）提示「找不到兼容的图形设备」后退出，现已修复。安卓包去掉调试断言，体积也小了一点。
- **安卓：修复骁龙 865/870/888 手机闪退和画面缺失。** Adreno 650（骁龙 865/870，如 POCO F3、Galaxy Tab S7）开始绘制约一秒后闪退；Adreno 660（骁龙 888，如 Galaxy S21 Ultra）一打开就闪退；现已修复。另外修复了安卓上新材质刚出现的头几帧画不出来的问题，在 Adreno 660 上表现为战斗背景、HP/EN 边框缺失和机体拖影。
- **安卓：游戏卡住时会在日志里记下各线程的位置**，方便通过「选项 → 反馈」导出问题报告后定位。
- **安卓：触屏按钮恢复成固定的一整套。** 每个画面都有摇杆、确定、返回、L1/R1、L2/R2、设置和 START，不会再缺按钮；PRESS START 画面的 L2/R2 位置是「图鉴」「战斗查看器」。
- **改造可以一次改多段。** 五项能力改造画面用 ←→ 给各项规划段数，确认一次付清；武器改造的确认画面用 ←→ 选要升几段。价格、上限和联动规则都照原版。
- **原版画面的翻译不再超出窗口。** 机体指令菜单和场间菜单加宽，译文标签按所在列和整行排版，英文先压窄再缩小。
- **大屏幕上界面跟着窗口放大。** 以前 1080p、4K 屏上战前确认页、设置窗口、战斗查看器会缩成 Deck 上的一半甚至四分之一挤在中间；现在「界面大小」按窗口高度排版（标准、大、特大），Deck 不变。
- **Windows：缩放屏幕上画面清晰。** 150%、200% 缩放的屏幕以前整个窗口被系统拉伸发糊，现在按屏幕实际像素绘制。
- **Windows：程序有了图标，窗口带菜单栏**（设置、重载台词、检查更新、显示选项）；全屏时菜单栏隐藏，鼠标移到屏幕顶边才出现。macOS 应用也换成新图标。
- **战斗：能力横幅**（暴击、盾牌防御、分身、护罩等）去掉黑底板，透出后面的战斗画面，旁边的数字改用高清绘制。
- **重复启动更友好。** 刚关掉游戏马上再开时会等它退干净；另一个游戏还开着时，用游戏语言提示，可以选择结束它再开始。
- **Windows：界面内部检查不再弹出对话框卡住游戏。**
- 菜单栏的「设置…」在图鉴、战斗查看器、MOD 页面里也能打开设置。
- 英文界面：「Counterattack Settings」「Weapon Stats」两个标签缩短，不再超出边框。

## 安卓已测试的芯片

以下芯片的真机都能正常进入游戏（安卓 12–16）。天玑 6100+ 这类入门芯片约 14 FPS，其余基本是 30 FPS。没列出的芯片不代表不能玩，只是还没测过；欢迎在 Issues 反馈。希望天玑 8200 的玩家反馈一下是否能正常运行。

- 高通：骁龙 865/870（Adreno 650，Galaxy Tab S7、POCO F3）、骁龙 888（Adreno 660，Galaxy S21 Ultra）、骁龙 8 Gen 1（Adreno 730，Galaxy S22、小米 12）、骁龙 8 Gen 2（Adreno 740，小米 13、Galaxy S23）、骁龙 8 Gen 3（Adreno 750，Galaxy S24 Ultra）、骁龙 8 至尊版（Adreno 830，Galaxy S25）
- 联发科：天玑 7300（Mali-G615，Solana Seeker）、天玑 6100+（Mali-G57，Galaxy A15）
- 三星：Exynos 1380（Mali-G68，Galaxy A35）
- 谷歌：Tensor G2（Mali-G710，Pixel 7）、Tensor G3（Mali-G715，Pixel 8）

## 0.4.2 的更新

- **Windows：修复程序一打开就退出。** 0.4.0 和 0.4.1 在 Windows 上无论双击 `Marchwind64.exe` 还是运行 `Marchwind64.cmd`，窗口都一闪就关、不留任何日志，现已修复，并在 Windows 11 上验证过。0.4.1 说的「双击 exe 开始游戏」从这一版起才真正可用。

## 0.4.1 的更新

- **安卓：修复骁龙手机一进游戏就闪退。** 高通 Adreno 显卡（骁龙 8 Gen 2、8 Gen 3 等，如红米 K70、iQOO 12、vivo Neo9）在游戏开始绘制时崩溃，现已修复，并在 Adreno 740、750、830 的真机上验证过。
- **安卓：开场序章文字页有了「跳过」按钮**；PRESS START 画面重新显示「图鉴」「战斗查看器」按钮。
- **Windows：双击 `Marchwind64.exe` 就能开始游戏**，和 `Marchwind64.cmd` 一样找 ROM，找不到时弹出选择框。

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

An unofficial native port of Super Robot Wars 64 with a full Chinese and English translation. No ROM included: you need your own original Japanese ROM. New in 0.4.4: in widescreen, battle close-ups no longer show a black strip with building pieces above the sky (the sky is drawn a little larger; 4:3 keeps the original); with HD 1.1 the battle skies are in HD, and so are the Battle Viewer's scene pictures; closing the game no longer sometimes hangs; the weapon upgrade screen no longer sometimes crashes or freezes on opening; F12 or Print Screen saves a screenshot to the screenshots folder, and Alt+Enter toggles full screen on Windows and Linux; on Android, one touch skips a story scene (the dialogue buttons become Skip, Auto and Hold to fast-forward), the D-pad stays in the bottom-left corner with adjustable button opacity, and saves and problem reports are exported to Download/Marchwind64 without any storage permission; held fast-forward turns a page every 0.3 seconds; "Cleared" follows the stage title on one line; in English, the victory, defeat and deployment windows' titles are shorter and no longer squeezed. From 0.4.3: a random freeze with HD images on (every platform since 0.3.1; the render thread locked itself when it released an expired HD map) is fixed; spirits cast on the pre-battle page now count in the battle (before, Bullseye showed 100% while the battle played the earlier rolls, and Valor, Soul, Spirit, Wall, Vigor, Guts, Fortune and Gain did not apply); Android phones with older Mali drivers (such as the Galaxy A15 and A35) no longer quit at start with "Unable to find compatible graphics device", and the Android build is a little smaller; Snapdragon 865/870 (Adreno 650, such as the POCO F3 and Galaxy Tab S7) no longer crash a second into drawing and Snapdragon 888 (Adreno 660, Galaxy S21 Ultra) no longer crashes at start, and new materials draw from their first frame on Android (on the Adreno 660, battle backgrounds and HP/EN frames were missing); a stuck game on Android writes every thread's position into its log for problem reports; touch controls are again one fixed full set on every screen, with the Library and Battle Viewer on L2/R2 at the title; the upgrade screens plan several levels with left/right and pay them with one confirm, under the original's prices and rules; translated labels keep inside the original windows, and the unit command and intermission menus are wider; on large screens the pages grow with the window instead of shrinking to half or a quarter of their Deck size; on Windows the picture is sharp on scaled (150%, 200%) screens, Marchwind64.exe has an icon and the window has a menu bar that hides in full screen until the mouse reaches the top edge, and the Mac app has a new icon; the ability banners in battle (Critical, Shield Defense and others) let the battle show through and draw their numbers in HD; starting again right after closing waits for the old game, and when another game holds the user folder it offers to end it and start; on Windows, internal UI checks no longer stop the game with a dialog; Settings… from the menu bar works in the Library, Battle Viewer and MOD pages; two English labels that ran past their frames are shorter. Android chips tested on real phones: Snapdragon 865/870, 888, 8 Gen 1, 8 Gen 2, 8 Gen 3 and 8 Elite; Dimensity 7300 and 6100+ (about 14 FPS); Exynos 1380; Tensor G2 and G3. If you play on a Dimensity 8200 phone, please let us know whether it runs well. From 0.4.2: the game starts on Windows again; 0.4.0 and 0.4.1 closed at once on every launch, from Marchwind64.exe or Marchwind64.cmd, without leaving a log. From 0.4.1: Android no longer crashes on Snapdragon phones (Adreno GPUs) once the game starts drawing; the opening prologue pages have a touch Skip button; on Windows, double-clicking Marchwind64.exe starts the game as Marchwind64.cmd does. 0.4 is the first public release and a feature preview: the whole game is playable, but it is still rough. The Chinese and English translations are mostly machine-translated with one polishing pass and have not been hand-edited line by line yet; expect plenty of bugs; testing beyond macOS has been limited to a few devices. Please report problems on [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues), with the platform info and problem report from Options → Report, and suggest text corrections on the [website](https://srw64.dreamquest.club/en/story/)'s Story and Library pages. Full feature list: [release notes on the website](https://srw64.dreamquest.club/en/blog/v0-4-0/). {packages_en}. The optional HD image pack for every platform is released separately with its own version, currently [HD {hd_version}]({hd_url}). {hd_status_en} The game's update check says when a newer HD pack is out; see its release page for how to install it and where its images come from. This is an unofficial fan work, and the original characters, art and trademarks belong to their owners. The macOS app is not notarized: allow it under System Settings → Privacy & Security. Z+START in battle is the original game's soft reset.

## 校验 / Checksums

```
{checksums}
```

构建自提交 / Built from `{commit}`.
