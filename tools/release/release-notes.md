非官方的《超级机器人大战64》原生移植，附全游戏中文与英文翻译。不含 ROM：需要自备日版原版 ROM。

## 0.5.0 更新

- 应用改名为 Marchwind64。
- 中文与英文翻译全部重新润色：全部剧情和战斗台词去掉直译腔，专名统一。
- 首次提供 Android 版（试验）：可从 zip 导入 HD 包，数据文件夹能在「文件」App 里打开。
- 支持 RetroArch 滤镜和框体：macOS 用 Metal，Linux 与 Steam Deck 用 Vulkan，Windows 用 D3D12 或 Vulkan，Android 也有。除内置的以外，还能用你自己的或 RetroArch 的滤镜和框体文件夹。
- 电脑版标题画面新增战斗查看器：自选攻防双方、武器、场景，回看战斗演出。
- 新增金手指，在设置里单独一页，不写进存档。
- 新增更新检查：启动时问一次，对照官网，不会自动下载。HD 包改用独立的版本号。
- HD 包也可以直接放在游戏所在的文件夹里。
- 对白下方的按键提示 5 秒后自动隐藏，按方向键会再显示 3 秒；也可以在设置里改成一直显示。
- 标题画面飞过的作品名、演示战斗的机体名，改按阅读语言显示。
- ROM 除 .z64 外也接受 .v64 和 .n64。
- 新增给开发用的 AI 调试接口（MCP），各平台都有，在「设置 → 关于」里打开。
- 项目代码改用 GPL-3.0-or-later 许可证。

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

An unofficial native port of Super Robot Wars 64 with a full Chinese and English translation. No ROM included: you need your own original Japanese ROM. New in 0.5.0: the app is now Marchwind64; every story and battle line of the Chinese and English translations has been rewritten to read naturally; a first, experimental Android build; RetroArch filters and bezels on every platform; a battle viewer on the title screen (computers); cheats (never saved); an update check that asks once and never downloads, with HD packs versioned on their own; the HD pack can also sit in the game's own folder; the controls bar under dialogue hides itself; .v64 and .n64 ROM dumps are accepted; the project's own source is now GPL-3.0-or-later. {packages_en}. The optional HD image pack for every platform is released separately with its own version, currently [HD {hd_version}]({hd_url}). {hd_status_en} The game's update check says when a newer HD pack is out; see its release page for how to install it and where its images come from. This is an unofficial fan work, and the original characters, art and trademarks belong to their owners. The macOS app is not notarized: allow it under System Settings → Privacy & Security. Z+START in battle is the original game's soft reset.

## 校验 / Checksums

```
{checksums}
```

构建自提交 / Built from `{commit}`.
