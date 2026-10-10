非官方的《超级机器人大战64》原生移植，附全游戏中文与英文翻译。不含 ROM：需要自备日版原版 ROM。

## 本版更新（0.4.5）

- **修复战斗画面天空上出现红黄色块。** 0.4.4 引入的问题，速度线、火焰之类的特效被错当成天空画了出来，所有平台都有。
- **修复战斗演出刚开始就按结束键会闪退。** 演出一开始马上按结束键（X/B 或 R2），现在会正常结束。
- **Windows：修复用户名或游戏文件夹带中文时打不开。** 适用于 Windows 10（1903 及以后）和 Windows 11。
- **新增抗锯齿开关**（选项 → 通用）。关掉可以减轻显卡负担；部分 Intel 核显上界面花屏时也可以关掉试试。重启游戏后生效。
- **关闭战斗动画时，没打中会显示「未命中」**，地图上的战斗结算也更快了。
- **对白字号可以在设置里直接选**（选项 → 界面，五档），会记住选择。
- **开着 HD 时，地图上的单位图标可以单独换回原版**（选项 → 通用）。
- **安卓：对白时的「回看」按钮回来了**（0.4.4 不小心去掉了），平时在原「返回」键的位置。
- **Windows：游戏闪退时会留下诊断文件**，用「选项 → 反馈」导出问题报告时会一起带上，方便我排查。

## 0.4.4 的更新

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

## 安卓已测试的芯片

以下芯片的真机都能正常进入游戏（安卓 12–16）。天玑 6100+ 这类入门芯片约 14 FPS，其余基本是 30 FPS。没列出的芯片不代表不能玩，只是还没测过；欢迎在 Issues 反馈。希望天玑 8200 的玩家反馈一下是否能正常运行。

- 高通：骁龙 865/870（Adreno 650，Galaxy Tab S7、POCO F3）、骁龙 888（Adreno 660，Galaxy S21 Ultra）、骁龙 8 Gen 1（Adreno 730，Galaxy S22、小米 12）、骁龙 8 Gen 2（Adreno 740，小米 13、Galaxy S23）、骁龙 8 Gen 3（Adreno 750，Galaxy S24 Ultra）、骁龙 8 至尊版（Adreno 830，Galaxy S25）
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

An unofficial native port of Super Robot Wars 64 with a full Chinese and English translation. No ROM included: you need your own original Japanese ROM. New in 0.4.5: red and yellow patches across the battle sky (a 0.4.4 problem on every platform) are gone; ending the battle animation right after it starts no longer crashes; on Windows the game starts when the user name or the game folder is in Chinese (Windows 10 1903 or later, Windows 11); Options → General has an anti-aliasing switch, which lightens the GPU load and may help if the interface looks garbled on some Intel graphics (applies after a restart); with the battle animation off, a missed attack shows MISS on the map, and the map's battle results play faster; Options → Interface offers five dialogue text sizes and remembers the choice; with HD on, the map unit icons can stay original (Options → General); on Android the dialogue Back button is the history button again, which 0.4.4 lost; on Windows a crash leaves a diagnostic file that the problem report from Options → Report includes. From 0.4.4: in widescreen, battle close-ups no longer show a black strip with building pieces above the sky (the sky is drawn a little larger; 4:3 keeps the original); with HD 1.1 the battle skies are in HD, and so are the Battle Viewer's scene pictures; closing the game no longer sometimes hangs; the weapon upgrade screen no longer sometimes crashes or freezes on opening; F12 or Print Screen saves a screenshot to the screenshots folder, and Alt+Enter toggles full screen on Windows and Linux; on Android, one touch skips a story scene (the dialogue buttons become Skip, Auto and Hold to fast-forward), the D-pad stays in the bottom-left corner with adjustable button opacity, and saves and problem reports are exported to Download/Marchwind64 without any storage permission; held fast-forward turns a page every 0.3 seconds; "Cleared" follows the stage title on one line; in English, the victory, defeat and deployment windows' titles are shorter and no longer squeezed. Android chips tested on real phones: Snapdragon 865/870, 888, 8 Gen 1, 8 Gen 2, 8 Gen 3 and 8 Elite; Dimensity 7300 and 6100+ (about 14 FPS); Exynos 1380; Tensor G2 and G3. If you play on a Dimensity 8200 phone, please let us know whether it runs well. Earlier changes are on the [website blog](https://srw64.dreamquest.club/en/blog/). 0.4 is the first public release and a feature preview: the whole game is playable, but it is still rough. The Chinese and English translations are mostly machine-translated with one polishing pass and have not been hand-edited line by line yet; expect plenty of bugs; testing beyond macOS has been limited to a few devices. Please report problems on [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues), with the platform info and problem report from Options → Report, and suggest text corrections on the [website](https://srw64.dreamquest.club/en/story/)'s Story and Library pages. Full feature list: [release notes on the website](https://srw64.dreamquest.club/en/blog/v0-4-0/). {packages_en}. The optional HD image pack for every platform is released separately with its own version, currently [HD {hd_version}]({hd_url}). {hd_status_en} The game's update check says when a newer HD pack is out; see its release page for how to install it and where its images come from. This is an unofficial fan work, and the original characters, art and trademarks belong to their owners. The macOS app is not notarized: allow it under System Settings → Privacy & Security. Z+START in battle is the original game's soft reset.

## 校验 / Checksums

```
{checksums}
```

构建自提交 / Built from `{commit}`.
