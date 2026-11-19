# 数值 MOD：格式与 ID 表

给 MOD 作者看，也可以把整个文件夹交给你的 AI 助手，让它照着表写配置。实现与原版机制在 [两个 MOD 的规划](../../design/mod-names-art-data.md) §2.3 和 [MOD 包](../../design/mod-packages.md) §5，包的通用规则在 MOD 包 §2。名字和头像另见 [名字与头像 MOD](../../../web/src/content/docs/zh/mod-names.md)。

**现状（2026-11-03）**：机体、驾驶员、武器的数值都能改。地形适应 0.5.1 起生效，其余字段 0.5.2 起，武器的属性类字段（`attack`、`post_move`、`beam`、`parryable`、`terrain_at`、`requires`、`upgrade_type`、`locked`）0.5.3 起。`data/` 里别的文件会被忽略并在日志里报「暂不支持」。

## 1. 包的结构

```text
<包 id>/
  mod.json
  data/
    units.json      按机体号改机体
    pilots.json     按人物号改驾驶员
    weapons.json    按武器号改武器
```

三个文件写哪几个都行。`mod.json` 的写法见 [MOD 包](../../design/mod-packages.md) §2；发布时把文件夹压成 zip，扩展名改成 `.srw64mod`，拖进游戏窗口就会安装。

## 2. 写法

```json
// data/units.json
{ "262": { "hp": 6000, "armor": 1500, "terrain": { "space": "A" }, "abilities": ["i_field"] } }
// data/pilots.json
{ "196": { "melee": 150, "terrain": { "space": "A" }, "skills": { "newtype": [1, 5, 10] },
           "spirits": [["focus", 1], ["valor", 10], ["soul", 35]] } }
// data/weapons.json
{ "731": { "power": 3600, "range_max": 5, "ammo": -1 } }
```

- 键是编号，十进制、不补零：机体号 0–362（[units.csv](units.csv)），人物号 0–359（[pilots.csv](pilots.csv)），武器号 0–1328（[weapons.csv](weapons.csv)）。
- 只写要改的字段，没写的保持原版。字段、范围和键的完整列表在官网文档「[数值 MOD](../../../web/src/content/docs/zh/mod-data.md)」，这里只列一览：
  - 机体：`hp` `en` `move` `mobility` `armor` `limit` `repair_cost` `upgrade_cap` `terrain` `size` `shield` `sword` `parts_slots` `move_types` `swap_class` `funds` `exp` `abilities`；
  - 驾驶员：`melee` `ranged` `evade` `hit` `reaction` `skill` `sp` `double_move` `swap_class` `terrain` `skills` `spirits`；
  - 武器：`power`（100 的倍数）`range_min` `range_max` `hit` `critical` `ammo`（-1 不用弹数）`en` `morale` `terrain`；属性类：`attack`（`melee`／`ranged`，格斗或射击）`post_move`（`true`／`false`，移动后能不能用，即名字后的 P；例：`{ "209": { "post_move": true } }` 让 νガンダム 的フィンファンネル 移动后也能用）`beam`（光束，名字后的 B）`parryable`（能被切落）`terrain_at`（`attacker`／`target`，武器地形适应按哪一格判定）`requires`（必要技能：`null`、`ally`、`hyper_mode`、`super_mode`、`level_10`…`level_45`、`newtype`、`newtype_4`、`holy_warrior`、`holy_warrior_3`、`v_max`、`v_max_red`）`upgrade_type`（0–4）`locked`（剧情解禁前隐藏）。名字前后的 格／射、P、B 跟着字段走。MAP 武器和合体技不能加减。
- `move_types`（`land` 陆、`air` 空、`sea` 海、`space` 宇、`underground` 地中）也是写全的；`swap_class` 是换乘类别（0 不能换乘；驾驶员与机体同类才能换。原版用了 1–7，8 留给 MOD 当专用类别，例如只让一名驾驶员在两台机体间换），`funds`／`exp` 是被击坠时给的资金与经验，这三项在建机体实例和读档时才进实例（`800A6E68`、`800A8CE8`），能力重算不重读。
- `abilities`、`spirits` 是写全的：数组里没有的就去掉。`skills` 只改写到的技能，`null` 去掉。
- `terrain` 里写要改的通道：`air`（空）、`land`（陸）、`sea`（海）、`space`（宇），值是 `A`、`B`、`C`、`D` 或 `-`。
- 规则很严：不认识的字段、键或等级，范围外的值或编号，没有能力／精神记录的人物，都算错。**一个文件有错就整份不用**，游戏日志里有一行 `SRW64_MOD_DATA_PROBLEM` 写明哪个包、哪个文件、什么原因。`tools/mods/validate.py` 按同样的规则检查。
- 几个包改了同一项时，MOD 管理列表上面的包优先。

## 3. 实战里怎么算地形

- **机体侧的适应是机体和驾驶员两个等级相加再分档**：A=4、B=3、C=2、D=1、-=0，和 0 是 `-`，1–3 是 D，4–5 是 C，6–7 是 B，**8 以上才是 A**。所以机体本身已经是 A 时，要改驾驶员才能让实战变成 A。例：ゴーショーグン（184）宇 A，真吾（133）宇 B，合起来 7，在宇宙只按 B 算。
- 倍率：A 120%、B 100%、C 80%、D 60%、- 0%。攻击力乘攻方的这个倍率，装甲乘守方的。
- 武器的地形适应只看武器自己，另乘一次。

## 4. 什么时候生效

- 装上、启用或停用包后不用重启。
- 这些表不常驻内存，每次用到都从 ROM 重读一条记录，宿主在读完时打上包里的字节。
- **读档时**（场间存档、关内中断存档）全部机体、武器、驾驶员都按当前启用的包重建：装着包读档就是新数值，停用包再读档回到原版，存档本身不变。
- 机体数值和武器的威力、最大射程、空中适应在进地图、进场间时更新；武器其余字段和驾驶员要读一次档（或新开），驾驶员升级时也更新。
- 图鉴显示改过的数值。

## 5. 共用记录

- 一部分人物共用同一条能力记录（多为同一人物的另一份编号，比如ドモン 4、273、274），改其中一个，共用的人一起变。[pilots.csv](pilots.csv) 的 `shares_record_with` 列出了共用者。精神是另一套记录，也可能共用。
- 一件武器可能挂在好几台机体上，改它会让它们一起变。[weapons.csv](weapons.csv) 的 `units` 列出了带它的机体。

## 6. 表

| 文件 | 列 |
| --- | --- |
| [units.csv](units.csv) | `unit` 机体号，`name_ja`／`name_zh`／`name_en`，四个地形等级，`hp` `en` `move` `mobility` `armor` `limit` `repair_cost` `upgrade_cap` `size` `parts_slots`，`shield`／`sword`（1 有 0 无），`abilities`（空格分隔的键） |
| [pilots.csv](pilots.csv) | `pilot` 人物号，三语名字，`record` 能力记录号，`shares_record_with`，四个地形等级，六项能力值、`sp`、`double_move`、`swap_class`，`skills`（`键:等级/等级/…`），`spirits`（`键:等级`） |
| [weapons.csv](weapons.csv) | `weapon` 武器号，三语名字，四个地形等级，`power` `range_min` `range_max` `hit` `ammo` `en` `morale` `critical`，`attack`（melee／ranged），`beam` `post_move` `parryable` `locked`（1 是 0 否），`terrain_at`（attacker／target），`requires`（条件键，空为无），`upgrade_type`，`units`（带它的机体号） |

人物表只列有能力记录的人物；副驾驶员、妖精也有记录，四项多是 `-`。同名的条目是不同的编号（形态、敌我版本、剧情专用），改之前对一下数值。

表由 `tools/content/export_data_tables.py` 从 ROM 生成，同时写官网的 ID 表数据（`web/src/data/data-mods.json`）。

## 7. 示例

仓库里的 [`config/recomp/mods/space-a`](../../../config/recomp/mods/space-a/)：把我方主力里宇宙不到 A 的 20 名驾驶员与 21 台机体的宇宙改成 A。

[`config/recomp/mods/super-earthgain`](../../../config/recomp/mods/super-earthgain/)：数值文件给布拉德（27）、夺地者（34）、超级夺地者（306）同一个换乘类别 8，剧情文件在「了断之时」（场景 124）招回卡茨后让布拉德换到 306、34 留在部队里，于是两台能在换乘里互换。

## 新机体与新武器（`data/new_units.json`）

新机体、新武器、新战斗场景都用名字写，号码由游戏给：机体在 400–998，武器在 1400–2399，场景接在原版 1053 项之后，原版的号一个都不占。包里写照抄谁、站姿、地图图标、武器表、改造从谁继承、战斗动画记录，宿主把它们的行排进表里，和上面的数值文件走同一条补丁链（数值文件改的是抄过来之后的记录）。

```json
{
  "schema": "srw64.new-units.v2",
  "scenes":  { "cannon-pose": [2560, 1716, 2118] },
  "weapons": { "cannon": { "copy": 875, "inherits_from": 875,
                           "animation": { "camera": 1, "hit": 59, "action": 84, "defender": 125, "sounds": [219],
                                          "actors": [ { "scene": "@cannon-pose", "h4": 1, "h5": 255, "behavior": 359 } ] } } },
  "units":   { "final": { "copy": 224, "pose": [2324, 1724, 2016], "icon": 978,
                          "weapons": [870, 871, "@cannon"], "inherits_from": 224,
                          "crew": [171, 168, 170, 169, 166] } }
}
```

- 名字只用小写字母、数字和 `-` `_` `.`。包里其他文件用 `@名字` 指本包的新机体、新武器、新场景（`data/units.json` 写 `"@final": {"hp": 8000}`，`dialogue/names.json` 的 `units` 写 `"@final"`，地图图标文件叫 `art/icons/@final.png`，`art/icons.json` 的朝向写 `"@final"`，剧情 MOD 写 `"with": "@final"`、`"unlock": "@cannon"`）；指别的包写 `包id@名字`。
- 号码按「包 id@名字」算出来，同样的包在哪台机器上、按什么顺序装都一样；只有两个名字算到同一个号时，哈希小的那个先占，另一个往后挪一位。存档旁会记下每个号码是哪个包的哪个名字：后来装的包让号码挪了，读档时自动改成现在的号码；停用了这个包，读这种存档会被拦下、回标题并提示需要哪个包。
- 照抄的机体要有自己的战斗记录（原版 0–353，或别的新机体）。抄过来时去掉変形、分離、合体：新号码不在形态表和合体表里。
- `inherits_from`：机体换成这台新机体时（剧情 MOD 的 `replace`），机体改造段数从这台旧机体搬过来；武器的 `inherits_from` 指出新武器的段数从旧机体的哪件武器来，旧机体本来就有的武器照原样搬。新机体台数没有上限。
- `crew`：1–5 个人物号，第一个是主驾驶。这台机体登记给主驾驶时（`replace`、`join`），其余人物成为它的乘员；原版的乘员表没有空位，由宿主写。
- `combine`：由部件合体的新机体，`{"parts": [2–5 台部件], "over": [原版合体机]}`。部件的顺序就是乘员顺序（每台部件的主驾驶）。地图上原版合体机（`over`）的「合体」成立、其余部件（例如黑翼）也在旁边且未行动时，合出的是这台新机体；「分离」放回全部部件。这台机体的实例由剧情 MOD 的 `build` 步骤建（[剧情 MOD 指南](../story-mods/README.md)），自己不会在关开始时出击。
- `forms`：变形。写在基本形态上，`"forms": ["@wr"]` 列出 1–2 个别的形态（同一文件里的新机体），各形态照旧写自己的 `weapons`、`pose`、`icon`。宿主把各形态的武器合成一张共用列表，每件武器只在列了它的形态下能用；全族共用改造段数、武器段数和乘员（和原版一样）。地图菜单出「变形」。一台机体只能在一个变形族里，原版机体不能加进来。
- `work`：图鉴详情里写的原作，原版作品号 0–24（文本 60＋号，和原版「ロボットリスト」同一套）；不写就跟照抄的机体。包加的机体和人物在图鉴里都排在最后的「MOD」组。图鉴里的型号行写在 `dialogue/names.json` 的 `models`：`{"models": {"@final": {"ja": "…", "zh-Hans": "…", "en": "…"}}}`，最多 48 字，只有图鉴用；不写就跟照抄的机体（原版超级系多数没有型号）。
- **新人物**（`pilots` 分区）：`"pilots": {"kai": {"copy": 166, "voice": 166}}`。`copy` 是照抄的原版人物（要有能力记录），能力、技能、精神、头像、战斗曲都从它抄；`voice` 是战斗里借谁的台词（不写就是 `copy`；包里给了它自己的战斗台词时，没写的情境仍说这个人的，见下）；`portrait` 写包里的头像：`{"image": "art/kai.png", "hd": "art/kai-hd.png"}`，`image` 是 96×96、最多 63 种颜色的像素图（`python tools/mods/sprite_pixels.py 大图.png kai.png --size 96 96 --colors 63` 可以从高清图缩出来），`hd` 是 96 的整数倍的正方形高清图（可不写）；也可以写两个原版资源号 `[图, 调色板]`；不写就用照抄人物的头像。号码在 400–511（存档只存 9 位，所有包合计最多 112 名）。数值、技能、精神用 `data/pilots.json` 的 `"@kai"` 改，名字用 `dialogue/names.json` 的 `pilots."@kai"`，入队用剧情 MOD 的 `{"join": {"pilot": "@kai", "unit": 9}}`。停用这个包后读有它的存档，会回标题并提示需要哪个包。另有两个可选项：`"love": [{"with": 166, "mutual": true}]` 是恋爱／友情补正（相隔 2 格内攻击 +30%、受伤 −30%，`mutual` 让对方也有），`"combo_as": 196` 让它在合体技的驾驶员条件里算作那个原版人物。
- 新武器照抄的武器若在 `data/weapons.json` 里改了格斗／射击、移动后、光束，菜单名前后的「格／射」「P」「B」跟着改。
- 每个键写进哪张表、宿主怎么读，见 [`docs/design/new-unit-ids.md`](../../design/new-unit-ids.md)；示例包 [`config/recomp/mods/final-dancouga`](../../../config/recomp/mods/final-dancouga/)。

### 新人物自己的战斗台词（`dialogue/battle_quotes.json`）

```json
{
  "schema": "srw64.battle-quotes.v1",
  "pilots": {
    "@kai": {
      "attack":  [{"ja": "「カイ、行くぞ!」", "zh-Hans": "「凯，上了！」", "en": "\"Kai, here I come!\"", "voice": "kai-attack.ogg"},
                  {"ja": "「これでも食らえ!」", "zh-Hans": "「吃我这招！」"}],
      "evade":   [{"ja": "「見切った!」", "zh-Hans": "「看穿了！」"}],
      "special": [
        {"weapon": 49, "lines": [{"ja": "「全力の一撃だ!」", "zh-Hans": "「全力一击！」"}]},
        {"opponent": 298, "situation": "evade", "lines": [{"ja": "「そんな攻撃が当たるか!」", "zh-Hans": "「这种攻击打不中我！」"}]}
      ]
    }
  }
}
```

- 只能给包里的新人物（`@名字`，也可以是别的包的 `其他包@名字`）写；原版人物的台词不能改。
- 九种情境，和原版一样每次从这一组里随机说一句：`attack` 攻击、`shot_down` 被击坠、`heavy` 重伤（剩余 HP 不足 30%）、`medium` 中伤（30–90%）、`light` 轻伤（90% 以上）、`evade` 回避、`no_damage` 攻击无效（护罩、装甲）、`no_ammo` 弹药／EN 不足不能反击、`out_of_range` 射程外不能反击。**没写的情境说 `voice`（借台词的人）的台词**，台词框里也是那个人的名字和头像。
- 每句是一个对象：各语言的台词（`ja` 必须有，最多 42 字，正好一页三行；其他语言最多 200 字，读者按台词框宽度排）；可选 `voice` 是 `audio/voice/battle/` 里的 `.ogg` 文件名，这句显示时放（同 [战斗语音](../audio-mods/README.md)，只认 Ogg Vorbis）。
- `special` 是有条件的台词，条件成立时一定说它，不再从情境里抽（原版人物挨打、回避时的条件台词常被普通台词顶掉，包里的新人物不会）：
  - `"weapon": 武器`：用这件武器攻击时（原版武器号或 `@新武器`）。同名武器（各种 バルカン 等）算同一件，新武器算它照抄的那件。加 `"sometimes": true` 就和原版的部分武器台词一样，常被普通攻击台词顶掉（攻击台词只有 1 句时总被顶掉，2 句时三次里留一次）。
  - `"opponent": 原版人物`：对手是这个人时，`situation` 写九种情境之一（默认 `attack`）。
  - 每个人物的条件台词合计最多 119 句；同时成立的几条随机选一条。
- 台词号由宿主分配（所有包合计最多 2048 句），存档里不记，停用包后照原样回到借来的台词。

### 自己的战斗图（`sprites`）

新机体的站姿、招式里的场景、cut-in 都可以用包里的 PNG：

```json
"sprites": {
  "stand":  { "image": "art/battle/stand.png", "origin": [59, 90], "hd": "art/battle/stand-hd.png" },
  "cutin":  { "image": "art/battle/cutin.png", "origin": [56, 48], "mirror": false, "ticks": [22], "hd": "art/battle/cutin-hd.png" }
},
"scenes": { "cutin": "@cutin" },
"units":  { "final": { "pose": "@stand", ... } }
```

- `image` 是游戏里实际大小的像素图（站姿约 128×96），最多 127 种颜色（按 RGBA5551 算），半透明以下当透明。可以用 `python tools/mods/sprite_pixels.py 大图.png 小图.png --size 128 96` 从大图缩小、减色得到。
- `origin` 是图里对应场景原点的像素：站姿写脚下中间。
- `mirror` 默认 true：机体和招式里的东西会按敌我翻转；cut-in 这类不该翻的写 false。
- 多帧写 `"frames": [...]`，配 `"ticks"`（每帧几拍）和 `"loop"`（播完回到第几帧）。
- `hd` 是每帧一张的高清图，HD 画面下画它，原版画面下画 `image`。改造、能力等页面和图鉴也用这两张图。
- 站姿 `pose`、场景总表 `scenes` 写 `@图片名`，也可以照旧写原版的 [场景, 图集, 调色板] 三个资源号。

借原版图、靠 HD 包换图的包在 `mod.json` 写 `"requires": {"srw64.hd": ">=1.3"}`：没装 HD 包或图像设成原版时整包不加载，MOD 页显示「需要 HD 画面」；游戏里切换图像要重启才算。

