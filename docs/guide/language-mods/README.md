# 语言包 MOD：翻译流程与规则清单

给语言包作者看，也可以把这个文件夹交给你的 AI 助手，让它照着翻。官网版的说明是[语言包 MOD](../../../web/src/content/docs/zh/mod-language.md)（三语），设计与宿主实现在[语言包设计](../../design/mod-language-packs.md)，台词文件的完整格式在[台词纯文本](../dialogue-text.md)。

**现状（2026-11-03，已合并进 main，随 0.5.3 发布，`game` 写 `">=0.5.3"`）**：能加新的阅读语言，也能改现有语言的文字。完整示例是韩文第一关演示包 [`config/recomp/mods/lang.ko`](../../../config/recomp/mods/lang.ko/)，官网 MOD 下载页可以直接下载；它当时用的专名表在同一文件夹的 [`glossary-ko.md`](glossary-ko.md)。

## 1. 包的结构

```text
<包 id>/
  mod.json
  dialogue/<语言>/language.json   语言声明：代码、菜单里的名字、退回语言、字体
  dialogue/<语言>/terms.json      界面词条（ui）和名称记录（entries）
  dialogue/<语言>/story/…         剧情台词，按场景分文件
  dialogue/<语言>/battle/…        战斗台词，按说话人分文件
  dialogue/<语言>/intro.txt …     文字页（开场序章、结局）
  dialogue/<语言>/fonts/…         字体，和字体的许可证（.md）
  art/<语言>/scene-images/…       按语言换的场景图（可选）
  art/<语言>/plates/…             世界地图名牌（可选）
```

包的根目录只能放 `mod.json`；说明、许可证之类放进对应的子文件夹，写成 `.md`（`dialogue/` 下的 `.txt` 都会被当成台词读）。

## 2. 起步

```bash
.venv/bin/python -B tools/content/export_text.py
```

```bash
python3 tools/mods/language_template.py --locale ko --name 한국어 --fallback en --id yourname.korean -o korean-source
```

第一条从你的 ROM（`assets/rom.z64`）导出原文到 `assets/text-export/`，可以不跑；跑过之后模板会在 `terms.json` 每条记录旁边附上日文原文 `ja`，翻译时有原文可对。第二条把退回语言（这里是英文）的全部文字导出成起步包：`terms.json` 是全部界面词条和名称记录，台词文件和我们附带的一样，每条带日文原文行（`>`）。

只翻一部分也能用，没翻的地方显示退回语言。只做某一关时要的文件：

| 要翻的 | 在哪 |
| --- | --- |
| 这关的剧情 | `story/scene-<场景号 4 位>.txt`，场景号见 [`../story-mods/scenes.csv`](../story-mods/scenes.csv) |
| 这关出场人物的战斗台词 | `battle/speaker-<人物号 3 位>.txt`，人物号见 [`../data-mods/pilots.csv`](../data-mods/pilots.csv)；每条上方的 `# 触发：` 说明什么时候说 |
| 界面、人名、机体名、武器名、地形、能力、系统提示 | `terms.json` 全部翻；名称记录量大但短 |
| 开场序章、结局 | `intro.txt`、`ending.txt` |

不翻的文件直接删掉，不要留着退回语言的原样：以后我们更新了这些文字，你包里的旧副本还会盖住新版。

## 3. 翻译流程（给 AI 助手）

1. **先定专名表**：人名（短称、全名）、机体、势力、地名、系统用语（精神指令、能力、指令菜单）各一行，对照日文原文。查这种语言里机战、原作动画的正式译名；没有的按日文读音转写。参考 [`glossary-ko.md`](glossary-ko.md) 的格式。
2. **按文件分工**：几个助手同时翻时，每个拿一组文件和同一张专名表，不要两个人改同一个文件。`terms.json` 太大时可以按 `ui`、`entries` 拆开翻、再合并。
3. **以日文原文为准**：`>` 行和 `ja` 是原文，退回语言的译文只作参考。人物口吻照原文：谁用敬语、谁说话粗。
4. **每翻完一份就检查**（§5），不要等全部翻完。

## 4. 规则清单

台词文件（`.txt`）：

- `@记录号 备注` 一行原样保留，`>` 原文行可留可删（留着时程序会和 ROM 比对，对不上的那条不生效）。
- `---`（翻页处）的个数、`*` 选项的个数和原文一样。
- `{HeroName}`、`{PartnerSurname}` 这类占位符每页的个数、种类和原文一样，顺序可以按语序调。
- 不能出现半角 `<`，用全角 `＜`。行首要写 `>`、`*`、`#`、`@` 时前面加 `\`。
- 引号用这种语言自己的写法（韩文演示包用 “ ”），原文的「」不要照搬。
- 一页写成一行，游戏按对白框宽度自动折行；手动换行会强制另起一行。
- 战斗台词不分页，放不下就缩小字号，越短越好。
- 文字页（`intro.txt`、`ending.txt`）里的 `---` 是空一行，不是翻页。

`terms.json`：

- `ui`：`{A}`（按键图标）、`{n}`（数字）这类记号和原文一样多，不翻的词条删掉，不要留空字符串。
- `entries`：值以 `<END>` 结尾，`<BR>`、`<STOP>` 等控制码和原文一样，不一样的那条不用。可以写成 `{"text": "…", "ja": "…"}`，`ja` 只作对照。
- 武器名开头的「格」「射」保留，游戏里显示成格斗、射击图标。
- 主角和搭档的默认名字也在 `entries` 里，要一起翻；玩家不能改名，名字按阅读语言显示。
- 名称尽量短：地图窗口、指令菜单、能力页的宽度按原版。

`language.json`：

- `fallback` 也决定排版：退回英文按英文排（词间空格、空格处换行、窄体字号），退回中日文按中日文排。韩文、法文这类用空格分词的语言选 `en`。韩文另外只在空格和标点处断行，不在词中间断开。
- 我们的字体只有拉丁、中日文字。韩文、泰文这类要自带字体放进 `fonts/`，并附上字体许可证；字体要允许随包分发（示例用 Apache-2.0 的 Droid Sans Fallback）。

## 5. 检查

```bash
python3 tools/mods/validate.py korean-source
```

```bash
python3 tools/mods/pack.py korean-source -o build/mods
```

`validate.py` 查结构、语言代码、字体和 `entries` 的写法；`pack.py` 打包并再查一遍。`ui` 的记号，以及控制码、页数、占位符和原文对不对，要在游戏里查：

1. 装上包、重启游戏、选这种语言。日志里一行 `SRW64_MOD_LANGUAGES ko<en layers=… problems=…`，有问题的逐条写 `SRW64_MOD_LANGUAGE_PROBLEM`。
2. 台词的问题写在用户目录的 `dialogue/dialogue-report.txt`（文件名、行号、记录号、原因）。
3. **边改边看**：把正在改的台词文件放进用户目录的 `dialogue/<语言>/`，它盖在包上面；游戏里按 **F5** 重新读取，当前这句马上按新文本显示。改好了再抄回包里。
4. 战斗台词用标题画面的「战斗鉴赏」看最快：选人物和对手直接开打，不用玩到那一关。
