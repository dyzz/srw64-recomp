# 语言包 MOD：增加别的阅读语言

2026-11-03。用户要求「允许增加别的语种」。接在[MOD 包](mod-packages.md)的格式后面（§2 的 `dialogue/` 目录「台词与语言」）。在这之前游戏只有日文、简体中文、繁体中文、英文四种阅读语言，韩文补丁只能占用英文的位置（`--content` 另建内容目录）。

## 1. 用户定的事（2026-11-03）

- **缺译时退回哪种语言**：作者在包里指定，四种内置语言之一，不写就用英文。界面词条、名称、剧情台词，哪条没译就显示退回语言的那条；退回语言自己也缺的，照现在的规则显示日文原文。
- **生效时机**：启用、停用、更新语言包后要**重启游戏**才生效。语言列表在启动时建好。
- **带文字的图片**：第一版就能按语言换图。
- **字体**：包可以自带字体；不带就用退回语言的字体。西文类语言（法、西、德等）直接用我们的字体；韩文、泰文等我们字体里没有的字，作者要放字体文件。

## 2. 包里怎么写

一个包可以带一种或几种语言，每种语言一个文件夹：

```text
mod.json
dialogue/ko/language.json     这种语言的声明（新语言必须有）
dialogue/ko/terms.json        界面词条与名称（可选）
dialogue/ko/**/*.txt          剧情、战斗台词、文字页（与 content/dialogue/<语言>/ 同格式）
dialogue/ko/fonts/*.ttf|otf   字体（可选）
art/ko/scene-images/…         按语言替换的场景图（可选）
art/ko/plates/<编号>.png       按语言替换的世界地图名牌（可选，如 5590.png、5599.6.png）
```

`language.json`：

```json
{
  "schema": "srw64.language.v1",
  "locale": "ko",
  "name": "한국어",
  "fallback": "en",
  "fonts": { "text": ["fonts/NanumGothic.ttf"], "title": "fonts/NanumBrush.ttf" }
}
```

- `locale`：语言代码，BCP-47 写法（`ko`、`fr`、`pt-BR`、`es-419`），小写字母开头 2–3 个，后面每段 2–8 个字母或数字。不能是四种内置语言。
- `name`：语言菜单里显示的名字，用这种语言自己写。1–24 个字。
- `fallback`：`ja`、`zh-Hans`、`zh-Hant`、`en` 之一，默认 `en`。**排版规则也跟它走**：退回英文的语言按英文排（词与词之间留空格、可以在空格处换行、用窄体字号），退回中日文的按中日文排。韩文、法文等用空格分词的语言应该选 `en`。
- `fonts.text`：正文和界面字体，按顺序排在退回语言的字体前面（缺的字往后找）。`fonts.title`：章节标题卡的毛笔字体，排在正文字体前面；不写时标题卡用正文字体。路径相对这个语言的文件夹，只能在 `fonts/` 里，`.ttf`／`.otf`，每个不超过 32 MB。
- 未知字段报错，和 `mod.json` 一样严格。

`terms.json`：

```json
{
  "schema": "srw64.language-terms.v1",
  "ui": { "title_start": "시작" },
  "entries": { "base:t00_00000": "평지<END>" }
}
```

- `ui`：界面词条，键与 `content/locales/<语言>.json` 的 `ui` 相同；游戏没有的键忽略。`{A}` 这类按键记号、`{n}` 这类占位符要和原文一样多。
- `entries`：记录号 0–5643 的名称、地形、能力名、系统提示（词条表那一部分），键是 `base:t00_<5 位记录号>`，值是目录写法（`<BR>`、`<END>` 等控制码），或 `{"text": …, "ja": …}`（`ja` 是给作者对照的原文，游戏不读）。控制码签名必须和原文一样，不一样的那条不用、记一条问题。
- 5644 以后的台词写进 `.txt`，规则见[台词纯文本](../guide/dialogue-text.md)。

**内置语言的文件夹**：`dialogue/en/`（没有 `language.json`）表示改英文：`.txt` 和 `terms.json` 盖在我们的英文上。这就是 mod-packages.md §4「台词按记录号多层覆盖」，规则相同，所以一起做了。

**几个包给了同一种语言**：上面的包赢，按条目叠；`language.json` 取最上面那个包的（名字、退回语言、字体）。

## 3. 退回顺序

所有按语言写的地方（`mod.json` 的 `name`／`description`、追加剧本的名字与标题、剧情 MOD 的台词、名字 MOD、曲名、本关情报）统一用一条链：

- 内置：`zh-Hant` → `zh-Hans` → `ja`；`zh-Hans`、`en` → `ja`。
- 语言包：本语言 → 退回语言（及它的链）→ `ja`。

`localization::fallbacks(locale)` 给出这条链。名字 MOD 照旧只在本语言（繁中再查简中）里找，链上的其他语言不顶替：没写就显示这种语言的译名，而译名本身已经按退回语言补齐。

## 4. 宿主

- **语言目录**：`mod_languages.hpp` 在 `native_dialogue::configure` 里读启用的包，`localization::add_locale` 把新语言登记到语言列表末尾（F7 循环、设置的语言菜单都自动出现）。新语言的目录从退回语言的目录复制：先把退回语言的台词文件（附带的、文本更新的）叠好，再叠包的 `terms.json`、包的 `.txt`、玩家用户目录里这种语言的台词文件（`dialogue/<locale>/`）。F5 重新载入一样处理。
- **选定的语言**：设置里存的是 `ko`。启动器不认识它时不再报错：照常用默认语言建会话，把 `SRW64_LOCALE=ko` 交给宿主；宿主登记语言包后切过去。包停用或删除后，下次启动退回到默认语言（首次启动的规则：系统语言，否则内容的默认语言）。
- **排版**：代码里所有按 `en`／`ja`／`zh-Hans` 区分排版的地方改问 `localization::style(locale)`：内置语言是自己，语言包是退回语言。ICU 断行和 HarfBuzz 用真正的语言代码；韩文（`ko`）另外只在空格和标点处断行（§7）。
- **字体**：`text::set_locale_fonts` 登记包的字体；`game_font_sources`／`title_font_sources` 把它们排在退回语言的字体前面。共享界面（RmlUi）在第一次用到这种语言时载入包的正文字体，族名 `srw64-ui-<locale>`。
- **换图**：场景图（`srw64-scene-images.json` 里的 `file`，如标题 logo）在阅读这种语言时先找 `art/<locale>/scene-images/<file>`；世界地图名牌找 `art/<locale>/plates/<编号>.png`（编号是我们名牌文件名去掉 `.plate.ja.png`：舰船 `5590`，宇宙区域 `5599.6`），尺寸要和我们的一样；包没给就用模型包里这种语言的名牌（清单 `textures` 有这个键时），再没有就用退回语言的。**模型包自带韩文名牌**（2026-11-03 用户要求提前做好）：`tools/models/build_native_models.py` 的 `PLATE_LOCALES` 加了 `ko`，译名写在脚本 `KOREAN_PLATE_NAMES`（리브라、라비앙로즈、바르지、액시즈、피프스 루나、사이드 N、스위트워터；バルジ 暂按日文读音转写），字体用 Droid Sans Fallback（Apache 2.0，AOSP android-4.4_r1，本地放 `assets/fonts/`，脚本核对 SHA-256）。重新生成后日中英名牌和模型逐字节不变，只多 12 张 `*.plate.ko.png`；旧宿主不认 `ko` 键，不受影响。要随下一个 HD 包发布。场景图替换需要装着 HD 包（位置、尺寸取自 HD 包的清单）。
- **问题**：语言包有错时那种语言不登记（或那一条不用），日志写 `SRW64_MOD_LANGUAGE_PROBLEM <包> dialogue/<语言>/…：<原因>`，登记结果一行 `SRW64_MOD_LANGUAGES ko<en layers=… problems=…`；字体 FreeType 打不开时整种语言不登记。MOD 管理里包的内容统计显示「语言 ko」，安装、启用／停用、删除带语言的包时提示要重启（`mod_language_restart`）。
- **启动前的提示**（ROM 选择、显卡失败、存档同步）：读包之前就要显示，语言包的语言一律用英文（`desktop.cpp saved_locale`、`graphics.cpp graphics_failure`）。
- **主机版**（Switch、Vita 的 `run_prepared`）没有 MOD 目录，不涉及。

## 5. 不支持的

- 需要词典断行的语言（泰文、老挝文、高棉文、缅甸文）：ICU 数据为了包体积裁掉了词典（package-size-icu），这些语言只能在空格处换行。
- 从右往左书写的语言（阿拉伯文、希伯来文）：对白排版没有双向文本。
- 原版的日文贴图（必杀技毛笔汉字 4519 等）不在可换的图里；F6 原版图模式下场景图照原版。

## 6. 工具

- `tools/mods/language_template.py --locale ko --name 한국어 --fallback en -o <目录>`：从我们的退回语言导出一个起步包：`mod.json`（不带 `license`，作者自己选）、`language.json`、`terms.json`（全部 `ui` 与 `entries`，值是退回语言的译文；有 ROM 文本导出 `assets/text-export/records.jsonl` 时每条附日文原文 `ja`）、全部 `.txt`（去掉我们生成器的文件头）和空的 `fonts/`。作者改完直接 `tools/mods/pack.py` 打包。
- `tools/mods/validate.py` 检查 `language.json`、`terms.json` 的结构，字体路径与大小，语言代码，字体文件只能在 `dialogue/<语言>/fonts/`；`names.json` 可以写包里声明的语言。terms 的控制码签名要 ROM，只在游戏里查。摘要多一项 `languages`。

## 7. 测试与验证

- `make recomp-mod-languages-test`（`tests/native_mod_languages.cpp`）：`language.json`／`terms.json` 的各条规则、包的上下顺序、换图查找、目录登记（F7 循环、语言菜单、`style`、`fallbacks`、`pick`、重复登记与退回语言限制、`initialize` 清空）。
- `tests/test_mod_validate.py`：语言文件夹的检查与 `names.json` 的语言。
- **实机（2026-11-03，Mac，调试会话 `20261103T052712.972401Z`）**：韩文测试包（`language_template.py` 的结构，AppleGothic 字体只在本机，104 条界面词条加「[한]」前缀、两条地形、@17410 一句台词、换了颜色的标题 logo 和 5590 名牌）：日志 `SRW64_MOD_LANGUAGES ko<en layers=1 problems=0`；`SRW64_LOCALE=ko` 启动即是韩文；演示战斗、序章文字页、主角选择页没译的都退回英文；真美路线第一句 @17410 显示包里的韩文、说话人名退回英文，韩文字形正常；F7 从 ko 绕回 ja 并写进设置，再按 4 次回到 ko；界面通知与标题「PRESS START」用包的字体画出韩文；标题 logo 换成了包里的图。
- **韩文第一关试做包**（2026-11-03，`config/recomp/mods/lang.ko/`，`lang.ko` 0.1.0）：真实系男主第一关「流転」——剧情 117 条、序章 30 页、阿克/艾尔利希/夸特罗/阿波利/罗伯特/汉密尔顿和特种部队/游击队战斗台词约 700 条、界面词条 954 条、ROM 系统文字与这一关的人名/机体/武器 1024 条；8 个子代理按同一张专名表翻译，用与游戏同规则的检查脚本自查。字体 Droid Sans Fallback（本地，`.git/info/exclude`，打包前要放进 `dialogue/ko/fonts/`）。实机（`20261103T065556.215917Z`）：序章、主角选择与确认页、阿克路线序章、章节卡「제1화 유전」、剧情与说话人、地图指令菜单、能力页、武器页都是韩文。实机发现默认名字（主角名、说话人）在包的名称词条叠上去之前就建好、显示英文：`register_languages` 改为登记时就叠上包的 `entries`。战斗台词另用战斗查看器实测（`20261103T070458.001730Z`）：아크「제발, 맞아 줘!」「해 주겠어…… 나도 죽고 싶진 않다고!」、스페셜즈「이런 대미지쯤!」、에를리히「얕보면 곤란하지!」，说话人与 HUD 徽章（방／반）都是韩文。没测：作战目标页。
- **名牌实机**（`20261103T063148.395401Z`，迷你关卡 worldmap-libra 拉长停留）：韩文下リーブラ显示模型包里的「리브라」。
- **没验证到**：设置里的语言菜单（列表来自 `locale_options`，与 F7 同源）、正式启动器路径下重启后记住 ko（`launch.cpp`，调试会话不经过它）、Windows／安卓。
- **韩文断行（2026-11-03 修）**：ICU 按 UAX #14 允许任意两个韩文音节之间断行，第一次实机「오늘|은」被拆开（占用英文位置的韩文补丁也一样）。用户要求先只管韩文：`portable_text.cpp` 在语言代码为 `ko`／`ko-*` 时去掉「两边都是字母或数字、且至少一边是韩文」的断点（`keep_korean_words`，即 CSS `word-break: keep-all`），空格与标点处照常断，放不下的长词走应急断行。共享界面（RmlUi）本来就只在空格处断，不用改。`tests/portable_text` 加了韩文用例（几种宽度下 `ko`／`ko-KR` 都不在词中断，`en` 下同一句会断，保证用例有效）。重跑实机（`20261103T054554.248046Z`）：@17410 三行都断在空格处。
