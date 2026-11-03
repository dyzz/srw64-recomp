# 内置韩文（第五种阅读语言）

2026-11-03 开工，在 `ko` 分支。韩文补丁作者 jung5441 同意把翻译交给我们内置（[他们的 issue #1](https://github.com/jung5441/MARCHWIND64_Korean_patch/issues/1)），用户定：做成第五种内置语言，不走语言包。翻译到手、全文接进来之前分支不合并。

## 1. 要做到的

- 韩文和另外四种一样：设置和 F7 里直接有「한국어」，不用装 MOD，MOD 列表里看不到它；首次启动系统语言是韩文时直接选它。
- 文字放在仓库里，和英文同样的位置与格式：界面词条和名称记录在 `content/locales/ko.json`，台词在 `content/dialogue/ko/`。没译的地方显示英文，英文也没有的显示日文原文。
- 字体随游戏带：正文 Droid Sans Fallback（Apache-2.0），放在 `content/fonts/` 和其他字体一起打包。标题卡字体等译者定了、核过许可证再加，之前用正文字体。
- 语言包照样能改韩文：包里 `dialogue/ko/` 不写 `language.json`，和改英文一样盖在上面。0.5.3 的韩文演示包写了 `language.json`，内置之后也照样能用，只是声明不再起作用。

## 2. 「站在英文上」的内置语言

韩文的界面和名称记录不会一开始就译全，所以不能像另外三种那样缺的退回日文。做法是给内置语言加一个可选的退回语言（`content/locales/ko.json` 的 `"fallback": "en"`）：

- **导入 ROM 时**（`src/srw64_native/profile.py`）：界面词条按 日文 → 英文 → 韩文 叠，名称记录按 英文 → 韩文 叠，编进内容缓存的 `locale_catalogs.ko`，`config` 里记下 `fallback`。
- **宿主登记时**（`localization::initialize`）：带 `fallback` 的行登记成内置语言，排版跟英文走（`localization::style("ko")=="en"`，和语言包同一张表），断行另外按韩文（`portable_text.cpp` 的 keep-all，语言包时已做）。
- **读台词时**（`native_dialogue.cpp` 的 `load_text`）：韩文的台词层是 英文随包 → 英文文本更新 → 韩文随包 → 韩文文本更新 → 语言包 → 玩家目录；文字页（序章、结局）先取英文的再盖韩文的。
- 代码里问「是不是语言包的语言」的地方改问 `localization::is_package(locale)`，不能再拿 `style(locale)!=locale` 当答案。

## 3. 写死了四种语言的地方

| 地方 | 改法 |
| --- | --- |
| 首次启动语言（`runtime.cpp`，Steam 的 `koreana`，Switch） | `ko`／`ko-*` → `ko` |
| 字体（`game_fonts.cpp`、`ui_fonts.hpp`） | 韩文：Droid Sans Fallback 在前，后面接英文那一套 |
| 世界地图名牌（`native_marker.cpp`） | 韩文名牌已在模型包里（`PLATE_LOCALES`），内置后按韩文取 |
| 启动失败、ROM 选择、存档同步的系统弹窗 | 先用英文，译文到了再加 |
| 宿主内的语言判断（`graphics.cpp` 等） | 认 `ko` |
| 语言包（`mod_languages.hpp`、`validate.py`、`language_template.py`） | 内置表加 `ko`；包里声明 `ko` 时当覆盖 |
| 剧情 MOD（`story.py`） | 台词可以写 `ko`，没写的照现在的规则退回 |
| 调试接口、`srw64ctl`、排版审计、界面探针 | 语言表加 `ko` |
| 文本更新包、关于页的一句话说明 | 韩文退回英文 |
| 官网、图鉴名称、关卡情报 | 官网不做韩文版，链接去英文版；图鉴、情报按退回链取 |

## 4. 译文到了以后

jung5441 的 0.5.2 补丁是我们自己的格式（占英文的位置）：393 个台词文件、935 条界面词条、4,674 条 ROM 文字。转换脚本按记录号和日文原文行对齐，生成 `content/dialogue/ko/` 和 `content/locales/ko.json`，再列出 0.5.2 之后新增、改动的界面词条和台词给对方补。每次发版前导出韩文缺失清单。

## 5. 进度

**11-03 第一步（未实机）**：

- 文字：`content/locales/ko.json`（`"fallback": "en"`）和 `content/dialogue/ko/` 先放第一关演示包的译文（954 条界面词条、1,024 条名称记录、第一关剧情和 18 个人物的战斗台词），等译文到了整份替换。`play-profile.json` 登记 `ko`，排在英文后面。
- 导入：`profile.py` 的 `layered_locale()`，`build_import_spec.py` 与 `rom_import.cpp` 把 `fallback` 带进内容缓存；`catalog.cpp` 登记时查退回语言必须是不站在别人上的语言。
- 宿主：`localization::is_package()`；`load_text` 的台词层；`game_fonts.cpp` 韩文默认字体（游戏文字和共享界面都走它）；名牌槽 3 给韩文；名字 MOD 按退回链取（`mod_names.hpp`、`default_names.hpp`）；首次启动（系统 `ko*`、Steam `koreana`、Switch）。
- 工具：语言包（宿主与 `validate.py` 都把 0.5.3 演示包的 `ko` 声明当覆盖）、`language_template.py`、`story.py`、调试接口、界面探针、排版审计、文本更新包。
- 测试：`make recomp-mod-languages-test`（改用 fr 当语言包的语言，另加内置韩文一段）、`recomp-content-test`、`recomp-mod-names-test`、`recomp-stage-info-test`；Python 的导入元数据、MOD 校验、英文、文本更新等测试。改过的宿主文件对照主工作区的编译命令做过语法检查，宿主没有整体编译、没有实机。

**11-03 Windows 实机（`cafe1017`，GTX 1080 Ti）**：存了 ko 的设置重启直接是韩文；没有设置、系统语言 `ko-KR` 的首次启动选了韩文；装着 0.5.3 演示包时 `SRW64_MOD_LANGUAGES - layers=1 problems=0`，语言菜单里只有一个「한국어」。台词报告 ko 413 个文件（英文 393＋韩文 20）、0 个问题。看过开场序章、标题菜单、主角选择两页、第一关第一句对白、设置页；F7 按 ja → zh-Hans → zh-Hant → en → ko 循环。测试机的 HD 包是旧的、没有韩文名牌，名牌没看。

**已知不足**：英文的文本更新包只更新英文；韩文没译、显示英文的界面词条和名称记录还是随包的英文（台词文件不受影响，英文的更新台词会垫在韩文下面）。

**合并前要做**：

- 实机：Mac、Windows、安卓、Switch 各开一次韩文；首次启动按系统语言；设置与 F7 切换；名牌；排版审计带 `ko`。
- 换上 jung5441 的译文（§4），定标题卡字体和韩文标题 logo（按语言的场景图现在只有语言包能给）。
- 文档：官网语言包页和 `docs/guide/language-mods/` 的例子从韩文换成别的语言；官网 MOD 下载页撤下韩文演示包；issue #17 跟进。
- 小处：Switch 启动画面的提示（`tools/release/switch/splash.py`）、Steam 快捷方式名字（`add_to_steam.py`、`steam_art.py`）、系统弹窗的韩文。
