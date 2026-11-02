# 剧情 MOD：给原版关卡加事件

给 MOD 作者看，也可以把这个文件交给你的 AI 助手，让它照着写。设计与原版机制在 [MOD 包](../../design/mod-packages.md) §7，包的通用规则在同一文档 §2。

**现状（2026-11-02，worktree story-mod，未合并、未发布）**：能在原版任意一关加事件——什么时候触发、要满足什么、做什么、说哪些新台词。不占原版的事件位，原版事件一个不动。示例包 [`config/recomp/mods/master-asia`](../../../config/recomp/mods/master-asia/)（独立军线大屿山（下）说服东方不败，过关后加入）已在 Mac 实机走通。

## 1. 包的结构

```text
<包 id>/
  mod.json
  story/
    <任意名>.json     剧情脚本（schema srw64.story.v1），可以有几份
  data/
    pilots.json       需要时顺便改数值（见数值 MOD）
```

`tools/mods/pack.py <源目录> -o <输出目录>` 打包时把 `story/` 里的剧情脚本编译成游戏读的格式（也可以单独 `python tools/mods/story.py compile story/x.json -o out.json` 看编译结果和每个事件的指令清单）。写错了编译就会报哪个事件、哪一项。

## 2. 写法

```json
{
  "schema": "srw64.story.v1",
  "events": [
    {
      "id": "persuade",
      "scene": 129,
      "when": { "persuade": { "by": 4, "target": 18 } },
      "if": { "vars": { "131": 0 } },
      "do": [
        { "say": 4, "text": { "ja": "師匠！", "zh-Hans": "师父！", "en": "Master!" } },
        { "say": 18, "mode": 2, "text": { "ja": "……", "zh-Hans": "……", "en": "..." } },
        { "leave": 18 },
        { "set": { "131": 3 } }
      ]
    },
    {
      "id": "joins",
      "scene": 129,
      "when": { "after": "end" },
      "if": { "fired": ["persuade"] },
      "do": [ { "join": { "pilot": 18, "unit": 27 } } ]
    }
  ]
}
```

- **`scene`**：场景号（0–255），即这关在 ROM 里的编号，可以查攻略数据或 `content/stages/scene-*.json`。
- **`id`**：包里唯一；`if.fired` 用它引用别的事件。
- **默认每关只触发一次**，游戏自己记、跟着中断存档走，**不用像原版那样自己写变量防重放**。要能反复触发写 `"repeat": true`（开场／配置／结束之后的不行）。
- `note`：随便写说明，游戏不读。

### 2.1 `when`：什么时候

| 写法 | 意思 |
| --- | --- |
| `{"turn": {"turn": 3, "side": "player"}}` | 第 3 回合起、该阵营的回合开始（`player`／`enemy`／`third`） |
| `{"defeated": 18}` | 某人物的单位被击破（离场不算） |
| `{"hp_below": {"who": 18, "percent": 30}}` | 某人物 HP 降到 30% 以下 |
| `{"battle": {"between": [4, 18], "moment": "after"}}` | 两人交战后（`before` 是交战前；0 表示任何人） |
| `{"enemies_gone": true}` | 敌方全灭；可写 `{"by_turn": 5}` 限第 5 回合内 |
| `{"remaining": {"side": "enemy", "at_most": 3}}` | 敌方（或 `third` 第三方）只剩 3 台以下 |
| `{"persuade": {"by": 4, "target": 18}}` | 4 号站在 18 号旁边时，菜单出现「说服」，选了就触发 |
| `{"after": "opening"}`／`"setup"`／`"end"` | 接在这关原版的开场／初期配置／结束事件之后 |

原版事件优先：同一时刻原版也有事件要触发时先跑原版的，下一次轮询再看你的。

### 2.2 `if`：还要满足什么

- `"vars": {"131": 0}`：剧情变量（0–199，值 0–3）。原版用它们记剧情分支，例如场景 129 的 131 是「多蒙在战斗中和东方不败说过话」（说过是 0）。
- `"fired": ["persuade"]`：本包的这些事件本关已经触发过。
- `"not_fired": [...]`：还没触发过。

### 2.3 `do`：做什么

| 动作 | 意思 |
| --- | --- |
| `{"say": 人物号, "text": {...}, "mode": 0–5}` | 一句台词。`text` 必须有 `ja`，其余语言没写的用 `ja`（繁体没写用简体）。一句最多 200 字，阅读器自己分页，不要写 `<STOP>`。`mode` 是原版对白的显示方式（3D3E–3D43），地图上一般 0（我方）、2（敌方）；世界地图上 0／1 上下交替 |
| `{"join": {"pilot": 18, "unit": 27}}` | 人物和机体加入我方 |
| `{"remove": {"pilot": 18, "unit": 27}}` | 从名册删掉（单位要先离场） |
| `{"leave": 18}` | 人物的单位离场（不算击破） |
| `{"leave_group": 11}`／`{"deploy": 11}` | 原版这关的出击组离场／登场 |
| `{"set": {"131": 3}}` | 设剧情变量 |
| `{"if": {"var": 128, "is": 2}, "then": [...], "else": [...]}` | 按变量分支 |
| `{"focus": 18}` | 镜头切到人物 |
| `{"bgm": 11}`、`{"sound": 124}`、`{"fade": "out"}`、`{"wait": 30}` | 音乐、音效、淡出／淡入、等待（帧） |
| `{"close": "window"}` | 关掉对白窗口 |
| `{"op": "3D35", "args": [2335, 1]}` | 原版指令照写（指令表在原版数据目录 `script_opcodes`），可以直接引用原版台词号，例如 `{"op": "3D40", "args": [43215]}` |

人物号、机体号查 [数值 MOD 的表](../data-mods/)（`pilots.csv`、`units.csv`）。

## 3. 要注意的

- **原版事件只认原版变量**。想让原版的某段事件不播或换一段，就得改它判断的变量。示例里说服之后写 `131 = 3`，原版结束事件只在 131 为 1 或 2 时播东方不败的死别。
- **单位离场（`leave`）不会触发原版的「击破」事件**。原版击破后要做的事（示例里是王登场、打开过关条件），要在你的事件里自己做一遍。
- **加入的人物的数值**：敌方人物的 SP、精神常常是空的，用 `data/pilots.json` 补（示例补了 SP）。
- 自制追加剧本（`campaign/`）里不跑剧情 MOD。

## 4. 自己验证

开发环境里 `tools/recomp/debug/check_story_mod.py` 是示例包的实机检查：装包、直接进入场景 129 的验证关卡（`config/recomp/mini-stages/story-master.json`）、说服、过关、看名册。写自己的包时可以照它改一个。运行时宿主把触发、开始、结束记在运行目录的 `story-mod-events.jsonl`，读不了的包在日志里报 `SRW64_STORY_MOD_PROBLEM`。
