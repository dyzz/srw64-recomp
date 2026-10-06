非官方的《超级机器人大战64》原生移植，附全游戏中文与英文翻译。不含 ROM：需要自备日版原版 ROM。

## 下载

| 文件 | 大小 | 内容 |
| --- | --- | --- |
{package_rows}
| `{hd_zip}` | {hd_size} | 可选的 HD 图片包，各平台通用。 |

## 安装 HD 包

1. 解压 `{hd_zip}`，得到 `hd` 文件夹。
2. 放进用户目录：
   - macOS：`~/Library/Application Support/SRW64Recomp/hd`
   - Linux／Steam Deck：`~/.local/share/srw64-recomp/hd`
   - Windows：`%LOCALAPPDATA%\SRW64Recomp\hd`

   也可以放在游戏所在的文件夹里，和 `Marchwind64.app`、`marchwind64.sh` 或 `Marchwind64.exe` 放在一起；两处都有时用用户目录里的。macOS 上放在应用旁边时，应用要先在访达里移动过一次（比如拖进「应用程序」），否则系统从别处运行一份副本，看不到旁边的 `hd`。
3. 打开应用即为 HD。F6 或设置窗口里的“图片”可以随时切回原版。

HD 包只能配同一版本的应用使用，更新时整个替换。

## 说明

- 这是非官方的粉丝作品，与万代南梦宫、万普（BANPRESTO）及各作品的版权方无关。原作角色、美术与商标的权利归各自的权利人所有。
- HD 包里的头像、场间背景、宇宙物件和标题图，由阿里云百炼的通义千问图像模型（qwen-image-3.0 / qwen-image-3.0-pro）以原版画面为参考生成，场间背景扩展到 16:9 时两侧由通义万相（wanx2.1-imageedit）补画；剧情世界地图和战术地图的底图，以及部分战斗 cut-in 与战斗中的机体额外图，由 OpenAI 的图像模型（通过 Codex 的 image_gen）以原版画面为参考绘制；都属于 AI 生成内容。机体立绘、地图上的机体图标、战斗 cut-in 与道具、战斗地面贴图用本地放大模型（4x-UltraSharpV2、4x-PixelPerfectV4）从原版像素图放大重绘，其余机体额外图由 HD 立绘推导，战术地图的色号图由原版地图像素放大而来，这几类图由原版画面衍生。边框由代码绘制；舰船、地标与地点标记是自制模型。BANPRESTO 标志、GAME OVER 和窗口边框由游戏运行时从原版画面算出，不在包里。
- macOS 应用只有本地签名，没有经过 Apple 公证。第一次打开被拦下时，到“系统设置 → 隐私与安全性”里点“仍要打开”。
- 战斗中按 Z+START 是原版的软复位，会回到标题画面，未存档的进度会丢失。
- 字体使用 HarmonyOS Sans，许可全文随应用附带。

## English

An unofficial native port of Super Robot Wars 64 with a full Chinese and English translation. No ROM included: you need your own original Japanese ROM. {packages_en}. `{hd_zip}` is the optional HD image pack for every platform: unzip it and put the `hd` folder in the user directory (`~/Library/Application Support/SRW64Recomp/` on macOS, `~/.local/share/srw64-recomp/` on Linux, `%LOCALAPPDATA%\SRW64Recomp\` on Windows), or in the game's own folder next to `Marchwind64.app`, `marchwind64.sh` or `Marchwind64.exe` (on macOS once the app has been moved in Finder). F6 switches between HD and the original images. The HD images are AI-generated (Alibaba Cloud Qwen and Wanx image models; the world map, tactical maps and some battle cut-ins and extra unit images with OpenAI's image model) from the original graphics, and the unit poses, map unit icons, battle cut-ins, props and ground textures and the tactical maps' palette-index maps are upscaled from the original pixel art; this is an unofficial fan work, and the original characters, art and trademarks belong to their owners. The macOS app is not notarized: allow it under System Settings → Privacy & Security. Z+START in battle is the original game's soft reset.

## 校验 / Checksums

```
{checksums}
```

构建自提交 / Built from `{commit}`.
