Marchwind64 的可选 HD 图片包，版本 HD {hd_version}，各平台通用，配 Marchwind64 {version} 或更新的应用使用。HD 包有自己的版本号，内容变了才发新版；有新版时游戏的更新检查会提示。

| 文件 | 大小 |
| --- | --- |
| `{hd_zip}` | {hd_size} |

## 安装 HD 包

1. 解压 `{hd_zip}`，得到 `hd` 文件夹。
2. 放进用户目录：
   - macOS：`~/Library/Application Support/SRW64Recomp/hd`
   - Linux／Steam Deck：`~/.local/share/srw64-recomp/hd`
   - Windows：`%LOCALAPPDATA%\SRW64Recomp\hd`

   也可以放在游戏所在的文件夹里，和 `Marchwind64.app`、`marchwind64.sh` 或 `Marchwind64.exe` 放在一起；两处都有时用用户目录里的。macOS 上放在应用旁边时，应用要先在访达里移动过一次（比如拖进「应用程序」），否则系统从别处运行一份副本，看不到旁边的 `hd`。
3. 打开应用即为 HD。F6 或设置窗口里的“图片”可以随时切回原版。

Android 不用手动解压：长按应用图标选「导入 HD 包」，或用 SRW64 打开下载的 zip。

更新时整个替换旧的 `hd` 文件夹。

## 图片来源

HD 包里的头像、场间背景、宇宙物件和标题图，由阿里云百炼的通义千问图像模型（qwen-image-3.0 / qwen-image-3.0-pro）以原版画面为参考生成，场间背景扩展到 16:9 时两侧由通义万相（wanx2.1-imageedit）补画；剧情世界地图和战术地图的底图，以及部分战斗 cut-in 与战斗中的机体额外图，由 OpenAI 的图像模型（通过 Codex 的 image_gen）以原版画面为参考绘制；都属于 AI 生成内容。机体立绘、地图上的机体图标、战斗 cut-in 与道具、战斗地面贴图用本地放大模型（4x-UltraSharpV2、4x-PixelPerfectV4）从原版像素图放大重绘，其余机体额外图由 HD 立绘推导，战术地图的色号图由原版地图像素放大而来，这几类图由原版画面衍生。边框由代码绘制；舰船、地标与地点标记是自制模型。BANPRESTO 标志、GAME OVER 和窗口边框由游戏运行时从原版画面算出，不在包里。

这是非官方的粉丝作品，原作角色、美术与商标的权利归各自的权利人所有。

## English

The optional HD image pack for Marchwind64, version HD {hd_version}, for every platform and Marchwind64 {version} or later. The pack has versions of its own and only gets a new one when its contents change; the game's update check says when there is. Unzip `{hd_zip}` and put the `hd` folder in the user directory (`~/Library/Application Support/SRW64Recomp/` on macOS, `~/.local/share/srw64-recomp/` on Linux, `%LOCALAPPDATA%\SRW64Recomp\` on Windows), or in the game's own folder next to `Marchwind64.app`, `marchwind64.sh` or `Marchwind64.exe` (on macOS once the app has been moved in Finder), replacing an older `hd` folder. On Android, import the zip in the app. F6 switches between HD and the original images. The HD images are AI-generated (Alibaba Cloud Qwen and Wanx image models; the world map, tactical maps and some battle cut-ins and extra unit images with OpenAI's image model) from the original graphics, and the unit poses, map unit icons, battle cut-ins, props and ground textures and the tactical maps' palette-index maps are upscaled from the original pixel art. This is an unofficial fan work, and the original characters, art and trademarks belong to their owners.

## 校验 / Checksum

```
{checksum}
```

构建自提交 / Built from `{commit}`.
