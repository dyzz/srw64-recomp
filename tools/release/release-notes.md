An unofficial native port of **Super Robot Wars 64** with full English and Chinese translations. No ROM included: you need your own original Japanese ROM. 中文说明见下方。

> **HD pack {hd_version}** (optional, every platform): **[Download {hd_name} ({hd_size})]({hd_download})**. {hd_status_en} How to install it and where its images come from: [the HD {hd_version} release]({hd_url}).

## Downloads

| File | Size | What it is |
| --- | --- | --- |
{package_rows_en}
| [`{hd_name}`]({hd_download}) | {hd_size} | Optional HD images (HD {hd_version}), the same for every platform. |

## What's new in 0.4.8

- **Stage info.** A page lists the current stage's hidden elements and how far you are with each (between stages, the next stage's), and each enemy's parts and retreat condition. Press Y on a controller or I on the keyboard on the map or between stages; on Android, tap Info at the top left; it is also in Options → General.
- **A Frame Rate setting (experimental).** The game stays at the original 30 by default; to try it, choose 60 or Display in Options → UI, which adds frames in between.
- **A new cheat: enemy and recruit levels** (Options → Cheats): original, match the party, or by chapter.
- **Android: the black screen on the Redmi K60 and similar phones is fixed**, confirmed by a K60 player.
- **Windows: a PC with no Vulkan driver starts again** and runs on D3D12.
- **Android: Hold ▶▶ keeps fast-forwarding again**; since 0.4.5 it often stopped after one line.
- **After a restart, the settings page and the button hints show your own key bindings**, not the defaults.

## From 0.4.7

- Windows: a likely fix for a crash at the title on some AMD graphics, and most likely for garbled menus on some Intel graphics.
- Keys no longer stay held after some short story skips or the map's objectives screen.
- Android: a thumb on the edge of the touch D-pad is one press; the pre-battle page keeps its focus frame; the HP window and story choices stay in place in widescreen.
- Cheats for 4 part slots on every unit and 15 of each part.
- Android keeps the last three runs' logs in Documents/Marchwind64/logs.

## Android chips tested

The game starts and plays on real phones with these chips (Android 12–16): Snapdragon 865/870, 888, 8 Gen 1, 8+ Gen 1, 8 Gen 2, 8 Gen 3 and 8 Elite; Dimensity 7300 and 6100+ (about 14 FPS; the others mostly 30); Exynos 1380; Tensor G2 and G3. A chip that is not listed has simply not been tested yet. If you play on a Dimensity 8200 phone, please let us know whether it runs well.

Earlier changes are on the [website blog](https://srw64.dreamquest.club/en/blog/).

## Feature preview

0.4 is the first public release series and a feature preview: the whole game is playable, but it is still rough.

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

> **HD 包 {hd_version}**（可选，各平台通用）：**[下载 {hd_name}（{hd_size}）]({hd_download})**。{hd_status}安装方法和图片来源见 [HD {hd_version} 发布页]({hd_url})。以后 HD 包出新版本时，游戏的更新检查会提示。

### 下载

| 文件 | 大小 | 内容 |
| --- | --- | --- |
{package_rows}
| [`{hd_name}`]({hd_download}) | {hd_size} | 可选的 HD 图片包（HD {hd_version}），各平台通用。 |

### 本版更新（0.4.8）

- **新增「本关情报」**：列出当前关（在场间时是下一关）的隐藏要素和完成情况，以及敌人带的强化零件和撤退条件。在地图或场间按手柄 Y、键盘 I 打开，安卓点左上角的「情报」，选项 → 通用里也有入口。
- **新增「帧率」设置（试验性）**：默认仍是原版 30 帧，想试的话可以在 选项 → 界面 里改成 60 或跟随显示器，会在两帧之间补出中间画面。
- **金手指新增「敌人与新同伴等级」**（选项 → 作弊）：原版、跟我方、按话数三选一。
- **安卓：红米 K60 等机型黑屏已修复**，K60 玩家已确认能正常游戏。
- **Windows：没有 Vulkan 驱动的电脑现在也能启动**，会用 D3D12 运行。
- **安卓：修复「按住快进」只能翻一句。** 0.4.5 起按住快进经常只翻一句就停，现在会一直快进。
- **修复改键后重启，设置页和按键提示显示成默认键位。**

### 0.4.7 的更新

- Windows：可能修复部分 AMD 显卡在标题画面闪退，部分 Intel 核显界面花屏很可能也一起修好了。
- 修复剧情短跳过后、地图「作战目标」画面之后按键像被一直按住。
- 安卓：触屏方向键在边缘晃动不再算成按两次；战前确认页的焦点框、宽屏下的 HP 窗口和剧情选项光标的修复。
- 金手指新增「全部机体零件槽 4 个」，「强化零件全部」改为 15 个。
- 安卓：最近 3 次运行的日志放进 Documents/Marchwind64/logs。

### 安卓已测试的芯片

以下芯片的真机都能正常进入游戏（安卓 12–16）。天玑 6100+ 这类入门芯片约 14 FPS，其余基本是 30 FPS。没列出的芯片不代表不能玩，只是还没测过；欢迎在 Issues 反馈。希望天玑 8200 的玩家反馈一下是否能正常运行。

- 高通：骁龙 865/870（Adreno 650，Galaxy Tab S7、POCO F3）、骁龙 888（Adreno 660，Galaxy S21 Ultra）、骁龙 8 Gen 1（Adreno 730，Galaxy S22、小米 12）、骁龙 8+ Gen 1（Adreno 730，红米 K60）、骁龙 8 Gen 2（Adreno 740，小米 13、Galaxy S23）、骁龙 8 Gen 3（Adreno 750，Galaxy S24 Ultra）、骁龙 8 至尊版（Adreno 830，Galaxy S25）
- 联发科：天玑 7300（Mali-G615，Solana Seeker）、天玑 6100+（Mali-G57，Galaxy A15）
- 三星：Exynos 1380（Mali-G68，Galaxy A35）
- 谷歌：Tensor G2（Mali-G710，Pixel 7）、Tensor G3（Mali-G715，Pixel 8）

更早版本的更新见[官网博客](https://srw64.dreamquest.club/zh/blog/)。

### 功能预览

0.4 是第一个公开版本系列，定位是功能预览：从头玩到尾的主要功能都已具备，但还很粗糙。

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
