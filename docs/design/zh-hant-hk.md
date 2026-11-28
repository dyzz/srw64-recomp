# 繁体中文的香港版（zh-Hant-HK）

2026-11-27。现有的繁体中文（zh-Hant，见 [srw64-zh-hant 的做法](../../tools/translation/make_zh_hant.py)）是台湾的用字、用语和译名。这次加香港版。

## 1. 用户定的事（2026-11-27）

- **做进游戏**，随下一个版本发布，不做成语言包 MOD。
- **底层是独立语言 `zh-Hant-HK`，界面上是繁体中文的两个地区**，不像两种语言：语言列表只有一个「繁體中文」，下面再选「台灣／香港」。
- **译名参考机战 Z 的港台成果**（srwz-zh 仓库里 zh-hant 配置下的港台专名表、用语规则表、港字形表）。
- **字体沿用 HarmonyOS Sans TC**，不另加香港字形的字体。

## 2. 界面

- **设置 › 一般**：「语言」一行有五个按钮（日本語、简体中文、繁體中文、English、한국어）。阅读繁中时，下面多一行「地區：台灣｜香港」（词条 `settings_region`、`region_tw`、`region_hk`）。点「繁體中文」时，进入上次选的地区。
- **菜单栏的语言菜单**（Mac、Windows）：「繁體中文」是一个子菜单，里面是台灣、香港两项，当前地区打勾。
- **F7、手柄语言键**：一次换一种语言，繁中只经过当前选的那个地区。
- **记住地区**：设置文件里存 `regions: {"zh-Hant": "zh-Hant-HK"}`，切到别的语言再切回来，下次启动后也一样。
- **首次启动**：系统语言是香港、澳门（zh-HK、zh-MO、zh-Hant-HK，脚本是 Hans 的除外）时用港版；台湾、Hant 用台版。Steam 只有一个 tchinese，Switch 没有香港代码，这两处都用台版。
- **RecompLauncher 的语言列表**：它只能平铺，所以写成「繁體中文（台灣）」「繁體中文（香港）」两项，挨在一起。
- **启动前的提示**（选 ROM、显卡失败、存档同步、重复启动）：港版用台版的文字。

## 3. 目录与退回

- `content/locales/zh-Hant.json` 加 `"region": "TW"`；`content/locales/zh-Hant-HK.json` 写 `"fallback": "zh-Hant", "region": "HK"`，**只存与台版不同的界面词条和数据条目**。
- `content/dialogue/zh-Hant-HK/` 也只存与台版不同的台词条目；宿主先铺台版的台词文件，再铺港版的（与韩文在英文之上相同，`native_dialogue.cpp load_text`；主机版 `console_data.cpp` 同样处理）。
- 退回链：`zh-Hant-HK → zh-Hant → zh-Hans → ja`（`localization::fallbacks`）。排版按台版（`style()` 是 `zh-Hant`），字体、章节标题卡毛笔字、世界地图名牌（繁中本来就用简体名牌）都跟台版。
- **地区的定义**：一个带 `region` 的语言，退回到另一个带 `region` 的语言，就是它的一个地区（`localization::family/region/regions/chosen_region/language_options`，`catalog.cpp`）。语言包不能退回到港版。
- 图鉴的特殊名字（`library-names.json`）加 `zh-Hant-HK`，查不到时用台版那一条。

## 4. 港版文字怎么来

照机战 Z 的做法，**不经过台版的 s2twp 台湾用词**，从简中直接派生（`src/srw64_native/zh_hant_hk.py`）：

1. **专名**：`content/translation/zh-Hant-HK/names.json`，与台版专名表一一对应（按日文名）。依据（`basis`）：
   - `srwz`：机战 Z 已定的港名；
   - `hk_dub`／`hk_release`／`hk_common`：这次调研的结果（TVB／亚视港配、香港官方如 hk.gundam-official.com、香港常用如维基 zh-hk 转换）；
   - `derived`：台版名字里套上已知港名与作品名（Z鋼彈 → Z高達）；
   - `tw`／`keep_tw`：没找到香港写法，沿用台版名字的港字形。
2. **用语**：`regional.json` 的 `terms`。机战 Z 的 45 条（高达 高達、机器人 機械人、导弹 導彈 等，部分只在日文含某词时生效），加上我们补的 `group: srw64` 规则（界面、电脑用语：窗口 視窗、手柄 手掣、分辨率 解像度 等）。
3. **字形**：OpenCC s2tw（只转字形）后，按 `regional.json` 的 `characters` 换成香港字形（說→説、裡→裏、著→着 等，机战 Z 人工挑过的 27 组）。
4. **章节标题卡**：毛笔字体是台版标题的子集，缺的港字形（啓、衞、裏）在标题里保留台版字形，和机战 Z 的贴图沿用台湾字形一样。
5. 引号用「」『』，与台版一样。
6. **OpenCC 选错字**（2026-11-28 加）：一简对多繁的字（只 隻、干 乾/幹、里 裡、发 髮、别 彆、出 齣、回 迴、复 覆），OpenCC 按自己的词组表选字，会把句子切错（总是只能→總是隻能、资料里根本 里“根”成了人名）或把音译名里的 里 当成 裡。`content/translation/zh-Hant/phrases.json` 列出这些词的正确写法，台港共用（港版再套港字形）：
   - `phrases`：在专名之后、OpenCC 之前锁住（只能、干得、组织里、弗里德、乞力马扎罗 等）；
   - `guard`：在专名之前锁住，防止专名把另一个名字切开（迪斯盖亚 不是“迪斯”加台版的 盖亚）；
   - `dialogue_guard`：只在台词里、专名之前锁住，让同时是游戏术语的常用词在叙述里保持原词（集中火力不变成 專注火力）；
   - `dialogue_words`：只对台版台词生效，s2twp 在界面里对、在叙述里错的词（设置 設置、保存、支持）。
   港版另外把简中原有的“著”锁住（简中的著只读 zhù：显著、著），不让港字形把它换成“着”。`import_srwz_hk.py` 不再对已经是繁体的台版名再跑一次 s2tw（奇里 曾被写成 奇裏、岩浆兽 写成 巖漿獸、游击队员 写成 遊擊隊員），没有台版名的行取台版转换器写出的名字。

**工具**：

- `tools/translation/import_srwz_hk.py --srwz <srwz-zh 目录>`：从机战 Z 重新导入，记下提交号和文件哈希。`srwz/derived/tw` 行会重算；调研过、用户定过的行保留；`srw64` 用语保留。
- `tools/translation/make_zh_hant.py`：同时写出台版和港版（港版只写差异），并更新 `library-names.json` 与 `src/host/library_names_table.hpp`。改简中或译名表后重跑。

## 5. 字体覆盖

HarmonyOS Sans TC 缺香港字形（説、着、温、脱、户、悦、啓、衞 等），游戏文字和共享界面都会接着用 HarmonyOS Sans SC 显示这些字。两者设计一致，不会出现方框。

## 6. 待定

- 译名审核页：https://claude.ai/artifact/9h6C8fNnQPhon2By3bcE3d （数据库集合 `choices`）。内容包括总体取舍（港配中式人名、0079 旧名、毫米、长名字）、23 条新加用语和 93 条有备选的译名；用户选完后应用到 `names.json`／`regional.json`，再重跑生成器。
- 2026-11-28 重算派生行时，「マーグ司令船」（港版人名マーグ是「馬克」，派生规则不替换三个字以内的人名，结果会是台版的「馬可司令船」）和「シャトル」（会变成台湾用语「太空梭」）两行保留原值（瑪古司令船、航天飛機），行内 note 有说明，等用户定。
- 实机验证：设置页的地区行、菜单子菜单、F7、记住地区、首次启动按系统地区选择。
- 官网没有香港版页面；`frontend.cpp` 里的官网链接按台版（`tw`）。
