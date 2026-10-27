# 数值 MOD：格式与 ID 表

给 MOD 作者看，也可以把整个文件夹交给你的 AI 助手，让它照着表写配置。实现与原版机制在 [两个 MOD 的规划](../../design/mod-names-art-data.md) §2.3 和 [MOD 包](../../design/mod-packages.md) §5，包的通用规则在 MOD 包 §2。名字和头像另见 [名字与头像 MOD](../../../web/src/content/docs/zh/mod-names.md)。

**现状（2026-10-27）**：机体、驾驶员、武器的数值都能改。地形适应 0.5.1 起生效，其余字段 0.5.2 起。`data/` 里别的文件会被忽略并在日志里报「暂不支持」。

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
  - 机体：`hp` `en` `move` `mobility` `armor` `limit` `repair_cost` `upgrade_cap` `terrain` `size` `shield` `sword` `parts_slots` `abilities`；
  - 驾驶员：`melee` `ranged` `evade` `hit` `reaction` `skill` `sp` `double_move` `terrain` `skills` `spirits`；
  - 武器：`power`（100 的倍数）`range_min` `range_max` `hit` `critical` `ammo`（-1 不用弹数）`en` `morale` `terrain`。
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
| [pilots.csv](pilots.csv) | `pilot` 人物号，三语名字，`record` 能力记录号，`shares_record_with`，四个地形等级，六项能力值、`sp`、`double_move`，`skills`（`键:等级/等级/…`），`spirits`（`键:等级`） |
| [weapons.csv](weapons.csv) | `weapon` 武器号，三语名字，四个地形等级，`power` `range_min` `range_max` `hit` `ammo` `en` `morale` `critical`，`units`（带它的机体号） |

人物表只列有能力记录的人物；副驾驶员、妖精也有记录，四项多是 `-`。同名的条目是不同的编号（形态、敌我版本、剧情专用），改之前对一下数值。

表由 `tools/content/export_data_tables.py` 从 ROM 生成，同时写官网的 ID 表数据（`web/src/data/data-mods.json`）。

## 7. 示例

仓库里的 [`config/recomp/mods/space-a`](../../../config/recomp/mods/space-a/)：把我方主力里宇宙不到 A 的 20 名驾驶员与 21 台机体的宇宙改成 A。
