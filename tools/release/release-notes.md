非官方的《超级机器人大战64》原生移植，附全游戏中文与英文翻译。不含 ROM：需要自备日版原版 ROM。

## 本版更新（0.4.3）

- **安卓：修复部分手机打开就退出。** 显卡驱动较旧的 Mali 手机（如三星 Galaxy A15、A35，部分天玑 8200、安卓 14 机型）提示「找不到兼容的图形设备」后退出，现已修复。安卓包去掉调试断言，体积也小了一点。
- **安卓：触屏按钮恢复成固定的一整套。** 每个画面都有摇杆、确定、返回、L1/R1、L2/R2、设置和 START，不会再缺按钮；PRESS START 画面的 L2/R2 位置是「图鉴」「战斗查看器」。
- **改造可以一次改多段。** 五项能力改造画面用 ←→ 给各项规划段数，确认一次付清；武器改造的确认画面用 ←→ 选要升几段。价格、上限和联动规则都照原版。
- **Windows：程序有了图标，窗口始终带菜单栏**（设置、重载台词、检查更新、显示选项），全屏时也有；macOS 应用也换成新图标。
- **重复启动更友好。** 刚关掉游戏马上再开时会等它退干净；另一个游戏还开着时，用游戏语言提示，可以选择结束它再开始。
- **Windows：界面内部检查不再弹出对话框卡住游戏。**
- 菜单栏的「设置…」在图鉴、战斗查看器、MOD 页面里也能打开设置。
- 英文界面：「Counterattack Settings」「Weapon Stats」两个标签缩短，不再超出边框。

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

An unofficial native port of Super Robot Wars 64 with a full Chinese and English translation. No ROM included: you need your own original Japanese ROM. New in 0.4.3: Android phones with older Mali drivers (such as the Galaxy A15 and A35) no longer quit at start with "Unable to find compatible graphics device", and the Android build is a little smaller; touch controls are again one fixed full set on every screen, with the Library and Battle Viewer on L2/R2 at the title; the upgrade screens plan several levels with left/right and pay them with one confirm, under the original's prices and rules; Marchwind64.exe has an icon and the Windows window always has a menu bar, full screen included, and the Mac app has a new icon; starting again right after closing waits for the old game, and when another game holds the user folder it offers to end it and start; on Windows, internal UI checks no longer stop the game with a dialog; Settings… from the menu bar works in the Library, Battle Viewer and MOD pages; two English labels that ran past their frames are shorter. From 0.4.2: the game starts on Windows again; 0.4.0 and 0.4.1 closed at once on every launch, from Marchwind64.exe or Marchwind64.cmd, without leaving a log. From 0.4.1: Android no longer crashes on Snapdragon phones (Adreno GPUs) once the game starts drawing; the opening prologue pages have a touch Skip button; on Windows, double-clicking Marchwind64.exe starts the game as Marchwind64.cmd does. 0.4 is the first public release and a feature preview: the whole game is playable, but it is still rough. The Chinese and English translations are mostly machine-translated with one polishing pass and have not been hand-edited line by line yet; expect plenty of bugs; testing beyond macOS has been limited to a few devices. Please report problems on [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues), with the platform info and problem report from Options → Report, and suggest text corrections on the [website](https://srw64.dreamquest.club/en/story/)'s Story and Library pages. Full feature list: [release notes on the website](https://srw64.dreamquest.club/en/blog/v0-4-0/). {packages_en}. The optional HD image pack for every platform is released separately with its own version, currently [HD {hd_version}]({hd_url}). {hd_status_en} The game's update check says when a newer HD pack is out; see its release page for how to install it and where its images come from. This is an unofficial fan work, and the original characters, art and trademarks belong to their owners. The macOS app is not notarized: allow it under System Settings → Privacy & Security. Z+START in battle is the original game's soft reset.

## 校验 / Checksums

```
{checksums}
```

构建自提交 / Built from `{commit}`.
