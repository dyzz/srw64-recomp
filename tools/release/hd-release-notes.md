Marchwind64 的可选 HD 图片包，版本 HD {hd_version}，macOS、Windows、Linux、安卓通用（Switch 用下表里单独的一份），配 Marchwind64 {version} 或更新的应用使用。HD 包有自己的版本号，内容变了才发新版；有新版时游戏的更新检查会提示。

| 文件 | 大小 |
| --- | --- |
| `{hd_zip}` | {hd_size} |
{switch_rows}
## 本版更新（HD {hd_version}）

### 战斗演出整体 HD

HD 1.2 的战斗里只有 cut-in 和道具是 HD 的。这一版把战斗动画里其余的画面基本补齐，从约 740 张增加到约 6900 张，都用本地放大模型从原版像素图放大，形状和配色跟原版走，在屏幕上的大小也和原版一样：

- **武器特效**：拳脚、刀剑、飞弹、炮弹、火焰等武器本身的画面。
- **光束**：外形逐帧严格照原版，只把内部改成第二次机器人大战 Z 的画法：白色的实心芯，外面一圈由浅到饱和的窄边。光束步枪、Mega 粒子炮、浮游炮、加特林光弹、扭转激光、闪电等共 99 组；原版渐隐的帧保持原来的暗度。
- **招式的后续画面**：命中之后的连续画面和残影，包括玫瑰高达的玫瑰浮游炮在各个转向角度的样子。
- **命中特效**与**击坠爆炸**。
- **防御**：盾牌、护罩等防御特效和被击中时的反应画面。
- **地图上的合体与变形动画**。
- **必杀技**：画面上的毛笔字，以及背后滚动的气场和星云背景。
- **着地阴影**换成柔边的圆形，大小不超过原版。

### 机体立绘

- 修了 26 张有乱纹的立绘，其中钢加农、神高达H、重装高达改、汉布拉比、马拉赛、巴乌、GP03S雄蕊 整张重画。
- 东方不败整张重画。
- 断空我的身体重画，两种站姿都换了。
- 大铁人补上原来没有的画面：两张立绘，宇宙和月面上穿宇航服的驾驶员，火箭飞弹、火箭筒这两招带驾驶员的帧，以及状态页不带驾驶员的那张。
- 新画的图都压回原版的亮度，和其他立绘放在一起不会显得特别亮。

### 地图机体图标

- 去掉敌方和第三方图标的红、黄描边。0.5.3 起我方朝左、敌方朝右，敌我一眼就能分清。
- 行动完的图标从 85% 灰改成 60% 灰，白色、灰色的机体行动后也能看出变化。
- GP03石斛兰和卡碧尼在行动前调亮，GP03 的蓝边加深。
- 大铁人的图标照原版去掉驾驶员。
- 断空我的图标换成新画的头部。

### 其他

- 剧情世界地图的地名牌多了韩文版，给韩文用。
- 没变的：人物头像、场间背景、剧情世界地图和战术地图的底图。

**HD {hd_version} 要配 Marchwind64 {version} 或更新的版本。** 这一版里完全相同的图只存一份，由程序按每处的配色分别上色；旧版程序分不出来，几种配色会变成一样。图标朝向、地面机体的影子、空中机体的影子和驾驶员页的再动标记是 {version} 程序本身的改动，不装 HD 包也有。

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

- **Battle animations in HD**: besides the cut-ins and props of HD 1.2, about 6,900 pictures now cover the weapon effects, beams, follow-on frames (Gundam Rose's Rose Bits in all their turning angles), hits, kill explosions, shields, barriers and the other defence effects, the map's combination and transformation movies, and the special moves' brush characters and scrolling backdrops. All are upscaled from the original pixel art and keep the original size on screen. Beams keep the original shapes frame by frame, with Super Robot Wars Z's look inside: a solid white core and a narrow rim from pale to saturated (99 groups). The landing shadow is a soft round one, no larger than the original.
- **Unit poses**: 26 poses with scribbled patches fixed, seven of them redrawn whole (Guncannon, God Gundam H, Gundam Heavyarms Kai, Hambrabi, Marasai, Bawoo, GP03S Stamen); Master Asia redrawn whole; Dancouga's body in both stances redrawn; Giant Robo's missing frames added (two poses, the pilot in a space suit in space and on the Moon, the rocket attacks with the pilot, the status page without him). New pictures are toned back to the original brightness.
- **Map icons**: the enemy's and the third party's red and yellow rings are gone, now that each side faces its own way (0.5.3); acted units grey at 60% instead of 85%, so white and grey units visibly change; the GP03 Dendrobium and the Qubeley are brighter before they act; Giant Robo's icon drops the pilot as in the original; Dancouga has a new head.
- Korean world map name plates for the Korean language. Unchanged: portraits, intermission backgrounds, and the world and tactical map bases.
- **HD {hd_version} needs Marchwind64 {version} or later**: identical pictures are stored once and the program colours each use; an older program would show several colour variants alike. The icon facing, the ground and air shadows and the double-move mark on the pilot pages are part of the {version} program, with or without the pack.

The optional HD image pack for Marchwind64, version HD {hd_version}, for macOS, Windows, Linux and Android and Marchwind64 {version} or later; the Switch takes `{switch_hd_zip}`, extracted to the SD card's root (`/switch/Marchwind64/hd/`), in the formats its GPU reads. The pack has versions of its own and only gets a new one when its contents change; the game's update check says when there is. Unzip `{hd_zip}` and put the `hd` folder in the user directory (`~/Library/Application Support/SRW64Recomp/` on macOS, `~/.local/share/srw64-recomp/` on Linux, `%LOCALAPPDATA%\SRW64Recomp\` on Windows), or in the game's own folder next to `Marchwind64.app`, `marchwind64.sh` or `Marchwind64.exe` (on macOS once the app has been moved in Finder), replacing an older `hd` folder. On Android, import the zip in the app. F6 switches between HD and the original images. The HD images are AI-generated (Alibaba Cloud Qwen and Wanx image models; the world map, tactical maps, map unit icons, some battle cut-ins and extra unit images, some redrawn unit poses and touch-ups to unit poses and portraits with OpenAI's image model) from the original graphics, and the unit poses, battle cut-ins, props, battle effects and the special moves' scrolling backdrops, ground textures and skies and the tactical maps' palette-index maps are upscaled from the original pixel art; the insides of the beams and the landing shadow are drawn by a program in the original shapes. This is an unofficial fan work, and the original characters, art and trademarks belong to their owners.

## 校验 / Checksum

```
{checksum}
```

构建自提交 / Built from `{commit}`.
