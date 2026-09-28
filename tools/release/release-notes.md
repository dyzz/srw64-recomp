非官方的《超级机器人大战64》原生移植，附全游戏中文与英文翻译。不含 ROM：需要自备日版原版 ROM。

## 下载

| 文件 | 大小 | 内容 |
| --- | --- | --- |
| `{app_zip}` | {app_size} | 应用本体。Apple Silicon，macOS 14 起。首次启动时选择你的 ROM。 |
| `{hd_zip}` | {hd_size} | 可选的 HD 图片包。 |

## 安装 HD 包

1. 解压 `{hd_zip}`，得到 `hd` 文件夹。
2. 放进 `~/Library/Application Support/SRW64Recomp/`，也就是 `…/SRW64Recomp/hd`。
3. 打开应用即为 HD。F6 或设置窗口里的“图片”可以随时切回原版。

HD 包只能配同一版本的应用使用，更新时整个替换。

## 说明

- 这是非官方的粉丝作品，与万代南梦宫、万普（BANPRESTO）及各作品的版权方无关。原作角色、美术与商标的权利归各自的权利人所有。
- HD 包里的头像、场间背景、宇宙物件和标题图，由阿里云百炼的通义千问图像模型（qwen-image-3.0 / qwen-image-3.0-pro）以原版画面为参考生成；剧情世界地图和战术地图的底图由 OpenAI 的图像模型（通过 Codex 的 image_gen）以原版画面为参考绘制；都属于 AI 生成内容。机体立绘和地图上的机体图标用本地放大模型（4x-UltraSharpV2、4x-PixelPerfectV4）从原版像素图放大重绘，战术地图的色号图由原版地图像素放大而来，这几类图由原版画面衍生。边框由代码绘制；舰船、地标与 5600 标记是自制模型。BANPRESTO 标志、GAME OVER 和窗口边框由游戏运行时从原版画面算出，不在包里。
- 应用只有本地签名，没有经过 Apple 公证。第一次打开被拦下时，到“系统设置 → 隐私与安全性”里点“仍要打开”。
- 战斗中按 Z+START 是原版的软复位，会回到标题画面，未存档的进度会丢失。
- 字体使用 HarmonyOS Sans，许可全文随应用附带。

## English

An unofficial native port of Super Robot Wars 64 with a full Chinese and English translation. No ROM included: you need your own original Japanese ROM. `{app_zip}` is the app (Apple Silicon, macOS 14 or later); `{hd_zip}` is the optional HD image pack: unzip it and put the `hd` folder in `~/Library/Application Support/SRW64Recomp/`. F6 switches between HD and the original images. The HD images are AI-generated (Alibaba Cloud Qwen image models; the world map and tactical maps with OpenAI's image model) from the original graphics, and the unit poses, map unit icons and the tactical maps' palette-index maps are upscaled from the original pixel art; this is an unofficial fan work, and the original characters, art and trademarks belong to their owners. The app is not notarized: allow it under System Settings → Privacy & Security. Z+START in battle is the original game's soft reset.

## 校验 / Checksums

```
{app_sha}  {app_zip}
{hd_sha}  {hd_zip}
```

构建自提交 / Built from `{commit}`.
