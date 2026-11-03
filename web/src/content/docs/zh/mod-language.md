---
title: 语言包 MOD
summary: 给游戏加一种新的阅读语言（韩文、法文……），或者换掉现有语言的界面文字、名称和台词。没译到的地方显示你指定的退回语言。
section: mods
order: 6
updated: 2026-11-03
---

游戏自带日文、简体中文、繁体中文、英文四种阅读语言。语言包可以再加一种：装好、启用、**重启游戏**之后，设置里的语言菜单和语言切换键（F7）都会出现它。没翻译到的界面文字、名称和台词，显示你指定的退回语言；退回语言也没有的，显示日文原文。

同一个包也可以只改现有语言，比如换一套英文译文。

包的安装和管理见 [MOD 包：安装与制作](../mods/)。需要 0.5.3 或更新的版本。

## 包里放什么

```text
yourname.korean/
  mod.json
  dialogue/ko/language.json      这种语言的声明（新语言必须有）
  dialogue/ko/terms.json         界面文字与名称
  dialogue/ko/story/…            剧情台词
  dialogue/ko/battle/…           战斗台词
  dialogue/ko/fonts/…            字体（我们的字体没有这种文字时）
  art/ko/scene-images/…          按语言换的图（可选）
  art/ko/plates/…                世界地图名牌（可选）
```

`ko` 是语言代码，文件夹名和 `language.json` 里写的必须一样。

最省事的起点是用[源代码仓库](https://github.com/dyzz/srw64-recomp)里的模板工具，它会把一种现有语言的全部文字导出成上面的结构，你直接改：

```bash
python3 tools/mods/language_template.py --locale ko --name 한국어 --fallback en -o korean-source
```

## language.json

```json
{
  "schema": "srw64.language.v1",
  "locale": "ko",
  "name": "한국어",
  "fallback": "en",
  "fonts": { "text": ["fonts/MyGothic.ttf"], "title": "fonts/MyBrush.ttf" }
}
```

| 字段 | 说明 |
| --- | --- |
| `locale` | 语言代码：`ko`、`fr`、`pt-BR`、`es-419` 这样的写法。不能是 `ja`、`zh-Hans`、`zh-Hant`、`en` |
| `name` | 语言菜单里显示的名字，用这种语言自己写，最多 24 个字 |
| `fallback` | 可选，没译到的地方用哪种语言：`ja`、`zh-Hans`、`zh-Hant`、`en`，默认 `en` |
| `fonts.text` | 可选，正文和界面用的字体，最多 4 个，排在退回语言的字体前面 |
| `fonts.title` | 可选，章节标题卡用的字体 |

**排版跟退回语言走**：退回英文的语言，词与词之间留空格、在空格处换行；退回中文或日文的，按中日文的方式排。韩文、法文这类用空格分词的语言选 `en`。

**字体**：只能放在 `fonts/` 里，`.ttf` 或 `.otf`，每个不超过 32 MB。西文字母我们的字体都有，法文、西班牙文这类不用自带；韩文、泰文等我们字体里没有的字，要放字体文件，否则显示不出来。字体的许可证要允许随包分发。

## terms.json：界面文字与名称

```json
{
  "schema": "srw64.language-terms.v1",
  "ui": { "title_start": "시작" },
  "entries": { "base:t00_00000": "평지<END>" }
}
```

- `ui`：界面文字，键和游戏自带语言的一样（模板里全有）。`{A}` 这类按键记号、`{n}` 这类数字位要原样保留，少了或多了的那条不用。
- `entries`：人物名、机体名、武器名、地形、能力名、系统提示等，记录号 0–5643。`<BR>`（换行）、`<STOP>`（翻页）、`<END>`（结尾）这些控制码要和原文一样；模板里每条旁边带着日文原文 `ja` 供对照，游戏不读它。
- 没写的条目显示退回语言的。

## 台词

剧情、战斗台词和文字页（开场、结局）写成纯文本，格式和玩家自己改台词的文件一样，见仓库里的[台词纯文本格式](https://github.com/dyzz/srw64-recomp/blob/main/docs/guide/dialogue-text.md)。文件放在 `dialogue/ko/` 下面任意位置，一条记录一段：

```text
@17410 로렌스
“아가씨, 차가 준비되었습니다. 오늘은 러시안 티입니다.
---
어디로 가져다 드릴까요?”
```

页数（`---`）、选项个数、`{HeroName}` 这类名字占位符要和原文一致，不一致的那条不用，显示退回语言的。玩家自己的台词文件夹里也可以有 `ko/`，盖在包的上面。

## 按语言换图

阅读这种语言时，下面的图换成包里的：

- `art/ko/scene-images/<文件名>`：场景图，例如标题画面的 logo `art/ko/scene-images/title-logo.png`。文件名和 HD 包里的一样，需要装着 HD 包。
- `art/ko/plates/<编号>.png`：世界地图上舰船和地点的名牌，编号是我们名牌文件名去掉 `.plate.ja.png` 的部分（例如 `5590`、`5599.6`），尺寸要和我们的一样。没给的名牌显示退回语言的。

## 别的包里的这种语言

装着语言包时，其他 MOD 也可以写这种语言的文字：`mod.json` 的名字和说明、追加剧本的名字和章节标题、名字 MOD、曲名。没写的照样按退回语言显示。

## 注意

- 启用、停用、更新语言包后都要重启游戏。正在用的语言包被停用或删除，下次启动回到系统语言。
- 韩文（`ko`）只在空格和标点处换行，词中间不断开。
- 需要词典断行的语言（泰文、老挝文、高棉文、缅甸文）只能在空格处换行；从右往左书写的语言（阿拉伯文、希伯来文）暂不支持。
- 有问题的语言包不会出现在语言菜单里，原因写在游戏日志里（`SRW64_MOD_LANGUAGE_PROBLEM`）。发布前用 `tools/mods/validate.py` 检查一遍。
