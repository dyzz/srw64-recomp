Marchwind64 的可选 HD 图片包，版本 HD {hd_version}，macOS、Windows、Linux、安卓通用（Switch 用下表里单独的一份），配 Marchwind64 {version} 或更新的应用使用。HD 包有自己的版本号，内容变了才发新版；有新版时游戏的更新检查会提示。

| 文件 | 大小 |
| --- | --- |
| `{hd_zip}` | {hd_size} |
{switch_rows}
## 本版更新（HD {hd_version}）

### 地图机体图标

- **图标顶满原版的格子**：HD 图标原来四周各空出约 8%，看起来比原版小；现在按原版图标的外框等比放大，面积大多大了约两成，像素不混色。
- 高达Mk-Ⅱ和超级夺地者的图标重画。

### 机体立绘

- 超级夺地者的立绘重画。

### 战斗

- 补齐还缺的 15 种战斗天空配色：地球和月球的第二层远景，以及 13 种已有 HD 图的换色版。
- 多蒙、东方不败和阿尔伯特的生身战斗动作换成各自的图和配色；多蒙和东方不败的未命中动作原来配错成爆炸的图。
- 战斗查看器的「地球」场景有了缩略图和预览背景。

没变的：人物头像、其余机体立绘、场间背景、剧情世界地图和战术地图的底图。

**HD {hd_version} 要配 Marchwind64 {version} 或更新的版本**：生身战斗动作的第一帧由 {version} 的程序提前准备，「地球」场景也是 {version} 才有的。

## 安装 HD 包

1. 解压 `{hd_zip}`，得到 `hd` 文件夹。
2. 放进用户目录：
   - macOS：`~/Library/Application Support/SRW64Recomp/hd`
   - Linux／Steam Deck：`~/.local/share/srw64-recomp/hd`
   - Windows：`%LOCALAPPDATA%\SRW64Recomp\hd`

   也可以放在游戏所在的文件夹里，和 `Marchwind64.app`、`marchwind64.sh` 或 `Marchwind64.exe` 放在一起；两处都有时用用户目录里的。macOS 上放在应用旁边时，应用要先在访达里移动过一次（比如拖进「应用程序」），否则系统从别处运行一份副本，看不到旁边的 `hd`。
3. 打开应用即为 HD。F6 或设置窗口里的“图片”可以随时切回原版。

Android 不用手动解压：长按应用图标选「导入 HD 包」，或用 Marchwind64 打开下载的 zip。

更新时整个替换旧的 `hd` 文件夹。

Nintendo Switch 用 `{switch_hd_zip}`（贴图已转成主机显卡直接读取的格式，桌面版的包在 Switch 上不能用）：解压到 SD 卡根目录，得到 `/switch/Marchwind64/hd/`，下次启动即为 HD。

## 图片来源

HD 包里的头像、场间背景、宇宙物件和标题图，由阿里云百炼的通义千问图像模型（qwen-image-3.0 / qwen-image-3.0-pro）以原版画面为参考生成，场间背景扩展到 16:9 时两侧由通义万相（wanx2.1-imageedit）补画；剧情世界地图和战术地图的底图、地图上的机体图标，部分战斗 cut-in 与战斗中的机体额外图，部分机体立绘的整张重画，以及机体立绘和头像的局部修补，由 OpenAI 的图像模型（通过 Codex 的 image_gen）以原版画面为参考绘制；都属于 AI 生成内容。机体立绘、战斗 cut-in 与道具、战斗特效（武器、命中、防御、击坠爆炸、地图上的合体与变形动画、必杀技毛笔字）与必杀技背后的滚动背景、战斗地面贴图与战斗天空用本地放大模型（4x-UltraSharpV2、4x-PixelPerfectV4）从原版像素图放大重绘，其余机体额外图由 HD 立绘推导，战术地图的色号图由原版地图像素放大而来，这几类图由原版画面衍生。光束的内部和着地阴影由程序按原版形状绘制，边框由代码绘制；舰船、地标与地点标记是自制模型。BANPRESTO 标志、GAME OVER 和窗口边框由游戏运行时从原版画面算出，不在包里。

这是非官方的粉丝作品，原作角色、美术与商标的权利归各自的权利人所有。

## English

### What's new in HD {hd_version}

- **Map icons fill the original's square**: the HD icons left about 8% empty on each side and looked smaller than the original's; they are now scaled up to the original icon's outline, typically about a fifth larger, without blending pixels. The Gundam Mk-II's and Super Earthgain's icons are redrawn.
- Super Earthgain's unit pose is redrawn.
- The 15 battle sky colourings still missing are added: the Earth's and the Moon's second layer, and recolourings of 13 skies that had HD pictures.
- Domon's, Master Asia's and Albert's battle actions on foot use their own pictures and palettes; Domon's and Master Asia's evades had an explosion's pictures.
- The Battle Viewer's Earth scene has its thumbnail and preview.
- Unchanged: portraits, the other unit poses, intermission backgrounds, and the world and tactical map bases.
- **HD {hd_version} needs Marchwind64 {version} or later**: the {version} program prepares the first frames of the actions on foot, and the Earth scene is new in {version}.

The optional HD image pack for Marchwind64, version HD {hd_version}, for macOS, Windows, Linux and Android and Marchwind64 {version} or later; the Switch takes `{switch_hd_zip}`, extracted to the SD card's root (`/switch/Marchwind64/hd/`), in the formats its GPU reads. The pack has versions of its own and only gets a new one when its contents change; the game's update check says when there is. Unzip `{hd_zip}` and put the `hd` folder in the user directory (`~/Library/Application Support/SRW64Recomp/` on macOS, `~/.local/share/srw64-recomp/` on Linux, `%LOCALAPPDATA%\SRW64Recomp\` on Windows), or in the game's own folder next to `Marchwind64.app`, `marchwind64.sh` or `Marchwind64.exe` (on macOS once the app has been moved in Finder), replacing an older `hd` folder. On Android, import the zip in the app. F6 switches between HD and the original images. The HD images are AI-generated (Alibaba Cloud Qwen and Wanx image models; the world map, tactical maps, map unit icons, some battle cut-ins and extra unit images, some redrawn unit poses and touch-ups to unit poses and portraits with OpenAI's image model) from the original graphics, and the unit poses, battle cut-ins, props, battle effects and the special moves' scrolling backdrops, ground textures and skies and the tactical maps' palette-index maps are upscaled from the original pixel art; the insides of the beams and the landing shadow are drawn by a program in the original shapes. This is an unofficial fan work, and the original characters, art and trademarks belong to their owners.

## 校验 / Checksum

```
{checksum}
```

构建自提交 / Built from `{commit}`.
