<p align="center">
  <img src="web/public/brand/title-zh.webp" alt="三月风64" width="640">
</p>

<p align="center"><b>超级机器人大战64 —— 1999 年 N64 游戏的原生重编译版。</b></p>

<p align="center">
  <a href="README.md">English</a> · 简体中文 · <a href="README.ja.md">日本語</a>
</p>

<p align="center">
  <a href="https://srw64.dreamquest.club/zh/">官网</a> ·
  <a href="https://srw64.dreamquest.club/zh/install/">下载与安装</a> ·
  <a href="https://srw64.dreamquest.club/zh/faq/">常见问题</a> ·
  <a href="https://srw64.dreamquest.club/zh/story/">剧情</a> ·
  <a href="https://srw64.dreamquest.club/zh/library/">图鉴</a> ·
  <a href="https://srw64.dreamquest.club/zh/guide/">攻略</a>
</p>

Marchwind 64（“三月风”）是《超级机器人大战64》的非官方原生重编译项目。支持电脑、Steam Deck
和安卓手机，提供中英文全文翻译和宽屏显示，另有可选的 HD 美术包。

> **需要自备日版 ROM**（スーパーロボット大戦64，Rev 0）。本仓库和下载包都不含任何游戏数据，
> 见 [ROM 说明](https://srw64.dreamquest.club/zh/faq/#rom)。

| 原生运行 | 中 · 英 · 日 | HD 与宽屏 | 原版 bug 修正 |
| --- | --- | --- | --- |
| 安装后添加 ROM 即可开始 | 全文翻译，游戏中随时切换 | 原版与 HD 美术随时切换 | 默认开启，支持逐项设置 |

## 原版与 HD 画面对比

| 原版 · 4:3 | MARCHWIND 64 · HD 宽屏 |
| --- | --- |
| ![原版 4:3 画面](web/public/media/compare-original-zh.webp) | ![HD 宽屏画面](web/public/media/compare-hd-zh.webp) |

左侧为原版 4:3 画面，使用 N64 分辨率的头像和地图。右侧为安装 HD 包后的 Marchwind 64，以 16:9
显示重绘的头像和世界地图。游戏中可随时切换原版与 HD 美术，两种模式共用存档。

## 界面与操作改进

在 N64 原作的基础上，加入新的界面、全文翻译和操作改进，并提供独立的 bug 修正选项。以下均为游戏内截图。

### 对白阅读

| | |
| --- | --- |
| ![自动播放](web/public/media/auto-zh.webp) | ![对白回看](web/public/media/history-zh.webp) |

全部对白用高清字体重新排版：整条台词连续排列，按字号自动分行分页，尽量不在半句话处翻页。读过的对白可以随时回看；
自动播放、快进和跳过都有专门的操作，屏幕底栏显示当前的阅读模式和对应按键。

- 回看最近 256 条对白，按说话人区分颜色
- 自动播放有 4 档速度，字号可在 10–18 之间调整
- 快进和跳过照现代机战的做法：按住快进，松手即停；跳过会一次走完整段剧情，停在下一个选择或战斗前，结果和逐句读完相同
- 随时切换语言或对照日文原文，当前这句立即重新排版
- 台词保存为纯文本文件，可以自己修改

### 现代机战式的界面与操作

| | |
| --- | --- |
| ![战前确认](web/public/media/prebattle-zh.webp) | ![一步跳到最远格](web/public/media/move-jump.webp) |

战前确认画面按现代机战的样式重新设计：攻守双方的伤害、命中率、暴击率，以及盾防和护罩的效果并排显示，
精神指令、更换武器和战斗动画开关集中在下方。战术地图上也补上了现代机战常见的便捷操作。

- 选择移动位置时，可以一步跳到移动范围最外圈的格子
- L1／R1 依次切换我方机体，L2／R2 依次查看敌方机体
- 战斗演出可随时中止，战斗结果照常结算
- 设置中也提供原版界面选项

### 游戏内设置

![设置](web/public/media/settings-zh.webp)

游戏中可调整语言、原版或 HD 美术、画面比例、全屏和界面大小，也可逐项设置规则修正、自定义键盘与手柄按键。

- 游戏中随时可以打开，标题画面右下角也有入口
- 界面大小有标准、大、特大三档

### RetroArch 滤镜与框体

![RetroArch 框体加 crt-lottes 滤镜](web/public/media/filters-tv-crt.webp)

可以直接使用 RetroArch 的 slang 着色器预设（.slangp），给画面加上 CRT 扫描线、NTSC 色彩等效果；画面比例为 4:3 时
还能套上 RetroArch 的框体，透明窗口自动对准游戏画面。在「选项 → 通用」里设置。

- 自带 crt-lottes、crt-geom、zfast-crt（适合掌机）、ntsc-adaptive、xbrz-freescale 等 9 个常用预设
- 自动找到已安装的 RetroArch 的全部 slang 着色器；自己的预设和框体放进数据文件夹即可
- 可按原版 240 行（最接近 CRT）、480、960 行或窗口分辨率处理
- 只作用于游戏画面和对白，选项窗口、提示和框体保持清晰
- 滤镜支持 macOS 和 Linux／Steam Deck，Windows 版暂不支持

<sub>框体为 RetroArch 自带的 tv-integer（libretro/common-overlays，CC BY 4.0），滤镜为 crt-lottes（480 行）。安装包不内置任何框体。</sub>

### 图鉴：353 台机体、293 名人物

![图鉴](web/public/media/library-zh.webp)

按作品分类，收录机体能力值、地形适应、武器及满改攻击力、改造上限，以及驾驶员成长、精神指令和特殊技能。数据直接取自游戏。

- 标题画面和游戏中都能打开
- 官网提供同一份[图鉴](https://srw64.dreamquest.club/zh/library/)

### 战斗鉴赏

![战斗鉴赏](web/public/media/viewer-zh.webp)

选择攻守双方的机体和驾驶员，设置武器、防御方式、伤害和场景，即可播放完整战斗演出。

- 支持查看所有机体的武器演出
- BGM 沿用原版规则，播放攻击方的主题曲

## HD 战斗画面

机体、cut-in、头像和背景采用高清素材，沿用原版的演出时序和镜头。

| | |
| --- | --- |
| ![cut-in](web/public/media/cutin-zh.webp) | ![战斗](web/public/media/spin-zh.webp) |
| ![光束攻击](web/public/media/beam-zh.webp) | ![战斗](web/public/media/sekiha-zh.webp) |

## 原版 bug 修正与可选规则

每项规则都有独立开关，调整后立即生效，不改变存档格式。「全部关闭」可统一使用原版规则。

**原版 bug 修正**（默认开启）：修正原版中的数值计算错误和遗漏。

- 超能力：命中、回避按实际等级计算（原版固定按 64 级计算）
- 圣战士：回避按实际等级计算（原版固定按 32 级计算）
- 限界：命中、回避分别加上运动性后，以限界为上限
- 底力：修正 HP 档位，满血时不触发加成
- 底力：命中、回避加成减半，暴击不变
- 换乘：补上表中遗漏的 3 件武器，使其可以继承改造
- 圣战士：超级奥拉斩的威力随等级提升

**便利与难度选项**（默认关闭）：可按需要开启，调整改造、资金和战斗难度。

- 改造上限突破：所有机体都能改到 15 段
- 离队退款：机体因剧情离队时，退还其改造花费
- 部件随行：换乘时，强化部件随驾驶员转移到新机体
- 头目假身：可将次数减半，或完全取消

**画面与界面**（新旧可选）：可按画面分别选择新版或原版界面。

- 战前确认：新版、高清原版、原版三选一
- 场间画面、主角选择、标题菜单：新版或原版
- 美术：HD 或原版，随时切换
- 画面比例：铺满屏幕，或保持 4:3
- 战斗动画、自动存档：分别设置开关

## 更多功能

- **原生运行。** N64 程序重编译为本机代码，图形由 RT64 渲染。安装后按说明添加 ROM，即可启动游戏。
- **中英日三语。** 游戏全文提供中文、英文翻译及日文原文。人名、机体名和武器名采用各语言的常用译名。
- **多存档栏与自动存档。** 增加存档栏，并在关键节点自动存档。卡带存档可导出，供 ares、Project64、RetroArch 等模拟器使用。
- **宽屏不拉伸。** 支持 4:3 到 16:9 的屏幕比例。原版画面居中，两侧扩展场景，以保持比例的方式铺满屏幕。
- **掌机、手机与手柄。** 自动识别 Steam Deck，并默认使用对应键位和大号界面。Android 提供触屏操作，仅显示当前画面需要的按键，
  并标注「确定」「快进」「下个单位」等功能。
- **可选 HD 美术包。** HD 美术包单独下载，所有平台通用；未安装时使用原版像素画面，安装后可在游戏中随时切换。

## 支持平台

| 平台 | 要求 |
| --- | --- |
| Windows（实验性） | 64 位 Windows |
| macOS | Apple 芯片，macOS 14 及以上 |
| Linux | x86-64，glibc 2.35 及以上，需要 Vulkan |
| Steam Deck（实验性） | 运行脚本加入 Steam 库，在游戏模式启动 |
| Android（实验性） | arm64，Android 9 及以上，触屏操作 |

各平台的安装步骤见官网的[安装页](https://srw64.dreamquest.club/zh/install/)。

## 一起改进译文

官网把[全部剧情](https://srw64.dreamquest.club/zh/story/)按关卡整理，日文原文与译文逐句对照。发现语句不顺或译名不一致，
可在对应台词下提交修改建议；每条建议的处理结果公开可查。程序问题请提交到 [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues)。

## 从源码构建

仓库不含 ROM、存档、提取出的游戏数据、生成的游戏代码、HD 美术或预编译的应用。请把自己的日版 Rev 0 ROM
以 `rom.z64` 放在仓库根目录（身份校验见[来源记录](docs/guide/provenance.md)）。

在 macOS（Apple 芯片）上，准备好 Python 3.11+ 和 Xcode 命令行工具：

```sh
brew install python cmake ninja sdl2 freetype harfbuzz icu4c
make                                                    # 工具链、生成的游戏代码与宿主程序
scripts/Play\ SRW64\ Native.command --language zh-Hans  # 开始游戏
```

- [开发指南](docs/guide/native-development.md)：构建步骤、检查与模块边界
- [Linux 与 Steam Deck 构建](docs/guide/linux-build.md) · [发布流程](docs/guide/release.md)
- [调试接口与 MCP](docs/guide/debug-interface.md)：从命令行或 AI 代理操作游戏
- [技术文档索引](docs/README.md) · [贡献约定](CONTRIBUTING.md)

## 致谢

本项目基于 [N64Recomp](https://github.com/N64Recomp/N64Recomp)、
[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) 与 [RT64](https://github.com/rt64/rt64)；
其他来源与固定版本见[来源记录](docs/guide/provenance.md)。界面与对白使用 HarmonyOS Sans 字体，按其许可原样随软件分发
（`content/fonts`）；按键图标改编自 Yukari “Shinmera” Hafner 的 PromptFont（SIL 开放字体许可证）。

本项目为非官方粉丝作品。超级机器人大战及相关角色、机体与商标归各自权利人所有。

## 许可证

本项目自有的源码与工具以 [GNU 通用公共许可证第 3 版](LICENSE)（或你选择的任何更新版本）授权。它只涵盖本项目自己写的部分，
不涵盖《超级机器人大战64》本身、其 ROM 以及从中提取或衍生的任何内容（游戏代码、文本、数据、图像、音乐），也不涵盖各权利人的
角色、机体与商标。`content/fonts` 里的字体、HD 图片包（见其 NOTICE）与第三方组件遵守各自条款。
