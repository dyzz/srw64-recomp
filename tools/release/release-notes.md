非官方的《超级机器人大战64》原生移植，附全游戏中文与英文翻译。不含 ROM：需要自备日版原版 ROM。

## 本版更新（0.4.6）

- **新增 2 倍速**（选项 → 通用 →「游戏速度」，默认原速）。战斗演出、世界地图、地图上的剧情、敌方回合和单位移动都加快一倍；我方的菜单、光标、战前确认和场间画面保持原速，免得光标跑太快。音乐速度不变。键盘按 Tab 一键切换，手柄是 R4 背键，安卓在左上角有 2× 按钮。
- **Windows：修复双击启动时偶尔闪退。** 0.4.5 双击 `Marchwind64.exe` 大约每三次会闪退一次，现已修复。
- **修复战斗天空上的红黄斑点。** 废墟等场景的高楼窗户位置会透出机体或光束的颜色，看起来像红黄斑点；现在按原版画法画成不透明。
- **HD：修复机体下半身被桥面挡住。** 夜间大桥等场景里，后画的桥面会盖住 HD 机体的下半身（原版画面正常），现已修复。
- **中文译名统一采用《超级机器人大战Z》中文版的叫法**，剧情、界面和攻略同步，例如：赞波特3、斯派扎、灵·格斯，精神「必闪」。
- **安卓：存档和文件改放在「文档/Marchwind64」。** 第一次启动时（或在 选项 → 存档 里）选一次文件夹，存档、导入导出、问题报告、滤镜和框体都在这里，文件管理器能直接看到，卸载游戏也不会删掉。以前的存档会自动复制过去。
- **安卓启动图标换成 M64 徽标**；Steam Deck 添加到 Steam 时的封面图和图标也换成了新的品牌图。

## 0.4.5 的更新

- **修复战斗画面天空上出现红黄色块。** 0.4.4 引入的问题，速度线、火焰之类的特效被错当成天空画了出来，所有平台都有。
- **修复战斗演出刚开始就按结束键会闪退。** 演出一开始马上按结束键（X/B 或 R2），现在会正常结束。
- **Windows：修复用户名或游戏文件夹带中文时打不开。** 适用于 Windows 10（1903 及以后）和 Windows 11。
- **新增抗锯齿开关**（选项 → 通用）。关掉可以减轻显卡负担；部分 Intel 核显上界面花屏时也可以关掉试试。重启游戏后生效。
- **关闭战斗动画时，没打中会显示「未命中」**，地图上的战斗结算也更快了。
- **对白字号可以在设置里直接选**（选项 → 界面，五档），会记住选择。
- **开着 HD 时，地图上的单位图标可以单独换回原版**（选项 → 通用）。
- **安卓：对白时的「回看」按钮回来了**（0.4.4 不小心去掉了），平时在原「返回」键的位置。
- **Windows：游戏闪退时会留下诊断文件**，用「选项 → 反馈」导出问题报告时会一起带上，方便我排查。

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

An unofficial native port of Super Robot Wars 64 with a full Chinese and English translation. No ROM included: you need your own original Japanese ROM. New in 0.4.6: a 2x game speed (Options → General → Game speed, off by default) speeds up battle animations, the world map, story events on the map, the enemy phase and units moving, while your menus, the cursor, the pre-battle screens and the intermission keep the original speed, and the music its tempo; Tab, the R4 back button or the 2x button at the top left on Android toggles it. On Windows, double-clicking Marchwind64.exe no longer crashes about one start in three, as 0.4.5 did. Red and yellow specks in some battle skies (the ruins' tower windows) are gone. With HD, units are no longer hidden behind the night bridge's deck. The Chinese names now follow the Chinese edition of Super Robot Wars Z. On Android, saves, imports, exports, problem reports, filters and bezels live in Documents/Marchwind64, a folder you choose once, which file managers see and an uninstall keeps; the launcher icon is the M64 badge, and the Steam art for the Steam Deck is new. From 0.4.5: red and yellow patches across the battle sky (a 0.4.4 problem on every platform) are gone; ending the battle animation right after it starts no longer crashes; on Windows the game starts when the user name or the game folder is in Chinese (Windows 10 1903 or later, Windows 11); Options → General has an anti-aliasing switch, which lightens the GPU load and may help if the interface looks garbled on some Intel graphics (applies after a restart); with the battle animation off, a missed attack shows MISS on the map, and the map's battle results play faster; Options → UI offers five dialogue text sizes and remembers the choice; with HD on, the map unit icons can stay original (Options → General); on Android the dialogue Back button is the history button again, which 0.4.4 lost; on Windows a crash leaves a diagnostic file that the problem report from Options → Report includes. Android chips tested on real phones: Snapdragon 865/870, 888, 8 Gen 1, 8 Gen 2, 8 Gen 3 and 8 Elite; Dimensity 7300 and 6100+ (about 14 FPS); Exynos 1380; Tensor G2 and G3. If you play on a Dimensity 8200 phone, please let us know whether it runs well. Earlier changes are on the [website blog](https://srw64.dreamquest.club/en/blog/). 0.4 is the first public release and a feature preview: the whole game is playable, but it is still rough. The Chinese and English translations are mostly machine-translated with one polishing pass and have not been hand-edited line by line yet; expect plenty of bugs; testing beyond macOS has been limited to a few devices. Please report problems on [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues), with the platform info and problem report from Options → Report, and suggest text corrections on the [website](https://srw64.dreamquest.club/en/story/)'s Story and Library pages. Full feature list: [release notes on the website](https://srw64.dreamquest.club/en/blog/v0-4-0/). {packages_en}. The optional HD image pack for every platform is released separately with its own version, currently [HD {hd_version}]({hd_url}). {hd_status_en} The game's update check says when a newer HD pack is out; see its release page for how to install it and where its images come from. This is an unofficial fan work, and the original characters, art and trademarks belong to their owners. The macOS app is not notarized: allow it under System Settings → Privacy & Security. Z+START in battle is the original game's soft reset.

## 校验 / Checksums

```
{checksums}
```

构建自提交 / Built from `{commit}`.
