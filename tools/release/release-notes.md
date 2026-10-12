非官方的《超级机器人大战64》原生移植，附全游戏中文与英文翻译。不含 ROM：需要自备日版原版 ROM。

## 本版更新（0.4.7）

- **Windows：可能修复部分 AMD 显卡在标题画面闪退。** 有玩家的 Apex（Radeon 8060S）在 0.4.6 标题画面闪退。从闪退诊断文件看，正式版误开了显卡驱动的调试检查（只在装了 Windows「图形工具」或 Vulkan SDK 的电脑上生效），界面绘制时一处设置不匹配就被它中断。这一版关掉了调试检查，界面也改成不再走那条有问题的绘制路径。我手上没有这款显卡，还请遇到的玩家帮忙确认。
- **部分 Intel 核显界面花屏，很可能也一起修好了**，原因和上一条是同一处界面绘制问题。抗锯齿开关现在只影响游戏画面。
- **修复剧情短跳过后按键像被一直按住。** 有些剧情的短跳过结束后，触屏按钮停在「停止跳过」，方向键和确定键也像被按住一样。
- **修复地图上的「作战目标」画面让按键像被一直按住。** 它被误当成了对白。
- **安卓：触屏方向键在边缘晃动不再算成按两次**（战前确认页会跳行）；点在方向键的缝隙里也不会再抢走页面的焦点。
- **战前确认页：点按钮旁边（驾驶员面板、图片、数字）不再让黄色焦点框消失。**
- **关闭战斗动画时，目标在宽屏左侧边缘，HP 窗口不再跑到屏幕右边。**
- **宽屏：剧情选择肢的绿色光标条不再超出窗口左侧。**
- **安卓：最近 3 次运行的日志放进 Documents/Marchwind64/logs。** 黑屏进不了设置、导不出问题报告时，也能把这些日志发给我。
- **红米 K60 等机型黑屏：暂时还没能解决。** 遇到的玩家请把 logs 文件夹里的日志发给我。

## 0.4.6 的更新

- **新增 2 倍速**（选项 → 通用 →「游戏速度」，默认原速）。战斗演出、世界地图、地图上的剧情、敌方回合和单位移动都加快一倍；我方的菜单、光标、战前确认和场间画面保持原速，免得光标跑太快。音乐速度不变。键盘按 Tab 一键切换，手柄是 R4 背键，安卓在左上角有 2× 按钮。
- **Windows：修复双击启动时偶尔闪退。** 0.4.5 双击 `Marchwind64.exe` 大约每三次会闪退一次，现已修复。
- **修复战斗天空上的红黄斑点。** 废墟等场景的高楼窗户位置会透出机体或光束的颜色，看起来像红黄斑点；现在按原版画法画成不透明。
- **HD：修复机体下半身被桥面挡住。** 夜间大桥等场景里，后画的桥面会盖住 HD 机体的下半身（原版画面正常），现已修复。
- **中文译名统一采用《超级机器人大战Z》中文版的叫法**，剧情、界面和攻略同步，例如：赞波特3、斯派扎、灵·格斯，精神「必闪」。
- **安卓：存档和文件改放在「文档/Marchwind64」。** 第一次启动时（或在 选项 → 存档 里）选一次文件夹，存档、导入导出、问题报告、滤镜和框体都在这里，文件管理器能直接看到，卸载游戏也不会删掉。以前的存档会自动复制过去。
- **安卓启动图标换成 M64 徽标**；Steam Deck 添加到 Steam 时的封面图和图标也换成了新的品牌图。

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

An unofficial native port of Super Robot Wars 64 with a full Chinese and English translation. No ROM included: you need your own original Japanese ROM. New in 0.4.7: on Windows, a crash at the title on some AMD graphics (a Radeon 8060S in an Apex) may be fixed: releases had the graphics driver's debug checks on wherever Windows Graphics Tools or a Vulkan SDK is installed, and one mismatch in drawing the menus stopped the game there; the checks are off and the menus no longer take that path, which most likely also fixes garbled menus on some Intel graphics, and the anti-aliasing switch now applies to the game picture only. After some short story skips, keys no longer stay held and the touch buttons no longer stay on Stop skip; the map's objectives screen no longer holds keys either. On Android, a thumb rocking on the edge of the touch D-pad is one press, and a touch between its arms no longer takes the page's focus; on the pre-battle page, a tap beside the buttons no longer drops the yellow focus frame. With battle animations off in widescreen, the HP window no longer jumps to the right edge for a target on the left; a story choice's green bar stays inside its window. On Android, the last three runs' logs go to Documents/Marchwind64/logs, for phones that cannot reach the settings. The black screen on the Redmi K60 and similar phones is not solved yet; please send those logs. From 0.4.6: a 2x game speed (Options → General → Game speed, off by default) speeds up battle animations, the world map, story events on the map, the enemy phase and units moving, while your menus, the cursor, the pre-battle screens and the intermission keep the original speed, and the music its tempo; Tab, the R4 back button or the 2x button at the top left on Android toggles it. On Windows, double-clicking Marchwind64.exe no longer crashes about one start in three, as 0.4.5 did. Red and yellow specks in some battle skies (the ruins' tower windows) are gone. With HD, units are no longer hidden behind the night bridge's deck. The Chinese names now follow the Chinese edition of Super Robot Wars Z. On Android, saves, imports, exports, problem reports, filters and bezels live in Documents/Marchwind64, a folder you choose once, which file managers see and an uninstall keeps; the launcher icon is the M64 badge, and the Steam art for the Steam Deck is new. Android chips tested on real phones: Snapdragon 865/870, 888, 8 Gen 1, 8 Gen 2, 8 Gen 3 and 8 Elite; Dimensity 7300 and 6100+ (about 14 FPS); Exynos 1380; Tensor G2 and G3. If you play on a Dimensity 8200 phone, please let us know whether it runs well. Earlier changes are on the [website blog](https://srw64.dreamquest.club/en/blog/). 0.4 is the first public release and a feature preview: the whole game is playable, but it is still rough. The Chinese and English translations are mostly machine-translated with one polishing pass and have not been hand-edited line by line yet; expect plenty of bugs; testing beyond macOS has been limited to a few devices. Please report problems on [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues), with the platform info and problem report from Options → Report, and suggest text corrections on the [website](https://srw64.dreamquest.club/en/story/)'s Story and Library pages. Full feature list: [release notes on the website](https://srw64.dreamquest.club/en/blog/v0-4-0/). {packages_en}. The optional HD image pack for every platform is released separately with its own version, currently [HD {hd_version}]({hd_url}). {hd_status_en} The game's update check says when a newer HD pack is out; see its release page for how to install it and where its images come from. This is an unofficial fan work, and the original characters, art and trademarks belong to their owners. The macOS app is not notarized: allow it under System Settings → Privacy & Security. Z+START in battle is the original game's soft reset.

## 校验 / Checksums

```
{checksums}
```

构建自提交 / Built from `{commit}`.
