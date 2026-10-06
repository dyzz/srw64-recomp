---
title: 迷你关卡
summary: 用一份 JSON 描述地图、出击单位和事件脚本，借用一话的位置直接进入，用来复现战斗、画面或剧情指令。只能通过 MCP 加载。
section: tools
order: 2
updated: 2026-10-06
---

迷你关卡是一份自己写的小关卡：选一张原版地图，摆上要用的我方和敌方单位，再写几段事件脚本（开场对白、回合开始、增援、胜利条件等）。加载后游戏会借用一话的位置直接进入这个关卡，不经过新游戏、序章和主角选择，几秒钟就能站到地图上。

我们开发时用它来复现特定的战斗和画面：比如让两台指定机体相邻，检查战前确认的命中率；把敌人放到画面外，检查 L2／R2 切换；或者单独跑一条剧情指令，看它到底做了什么。仓库里有六十多个现成的关卡可以参考。

迷你关卡是调试工具，**游戏里没有进入它的菜单**：只能在打开调试接口后，通过 MCP 加载。怎么连接见[用 AI 智能体操作游戏（MCP）](../mcp/)。

## 需要什么

- 已经按 [MCP 那一篇](../mcp/) 连上游戏（`srw64_attach` 能用）。
- [源代码仓库](https://github.com/dyzz/srw64-recomp)和 Python 3，用来把关卡定义编译成游戏读的镜像。
- 关卡里引用了原版的出击记录或事件（下文的 `template`、`deployments_from`、`copy_from`）时，还要在仓库里放好你自己的 ROM（`rom.z64`），运行一次 `make recomp-data`，从 ROM 提取原版数据。只用自己写的字段时不需要。

## 1. 写关卡定义

关卡定义是一个 JSON 文件，`schema` 固定为 `srw64.mini-stage.v1`。下面是仓库里 `move-jump.json` 的简化版：第 20 号地图上放一台我方机体、一台敌方机体，开场把镜头对准我方，然后进入我方回合。

```json
{
  "schema": "srw64.mini-stage.v1",
  "name": "my-test",
  "note": "随便写，说明这个关卡测什么",
  "map": 20,
  "deployments": [
    {"template": "base:stage_deployments:001f0f1c", "group": 0, "x": 8, "y": 8, "faction": 0},
    {"template": "base:stage_deployments:001f2720", "group": 1, "x": 12, "y": 8, "faction": 1}
  ],
  "events": [
    {"name": "opening", "type": 12, "header": [0, 0, 0, 0], "commands": [
      {"op": "3DD0"},
      {"op": "3D32", "args": [4]},
      {"op": "3D4D"},
      {"op": "3D65", "args": [17, 31]},
      {"op": "3D3B", "args": [1]},
      {"op": "3D45", "args": [0]},
      {"op": "3D45", "args": [1]},
      {"op": "3D35", "args": [16384, 0]},
      {"op": "3D48"}
    ]},
    {"name": "ending", "type": 14, "header": [0, 0, 0, 0], "commands": [
      {"op": "3DD0"},
      {"op": "3D4B", "args": [4]}
    ]}
  ]
}
```

各字段：

| 字段 | 说明 |
|---|---|
| `map` | 原版地图编号（0–255）。 |
| `slot` | 可选。借用哪一话的位置（场景编号）；不写时借用加载后游戏登记的那一话。 |
| `deployments_from` | 可选。整块复制一话原版的出击记录，例如 `base:stage_auxiliary:001e4268` 是第一话的。 |
| `deployments` | 追加的出击记录。`group` 是登场组（对应事件里的 `3D45 组号`），`x`／`y` 是格子坐标，`faction` 0 为我方、1 为敌方；`template` 拿一条原版记录做底板，再用 `unit`、`actor`、`upgrade`、`behavior` 等字段改掉想改的部分。 |
| `initial_resources` | 可选。进入地图时把某些单位的 HP、EN 设成上限的百分比：`{"side": 1, "slot": 0, "hp_percent": 30, "en_percent": 100}`。 |
| `events` | 事件列表，最多 63 个。每个事件有类型 `type`（0–14，例如 12 开场、13 初始出击、0 回合开始、7 敌数条件、14 结束）、四个触发参数 `header`，以及指令列表 `commands`；指令写成 `{"op": "3D45", "args": [1]}`。也可以用 `copy_from` 原样复制一个原版事件。 |

每条指令、条件指令、事件类型和出击记录字段见[关卡脚本指令参考](../script-commands/)；更早的研究过程在仓库里（中文）：[关卡脚本与事件类型](https://github.com/dyzz/srw64-recomp/blob/main/docs/script/stage-script-exploration.md)、[指令写法](https://github.com/dyzz/srw64-recomp/blob/main/docs/script/script-debug-injection.md)、[迷你关卡研究记录](https://github.com/dyzz/srw64-recomp/blob/main/docs/script/mini-stage.md)。最省事的办法是从 [`config/recomp/mini-stages/`](https://github.com/dyzz/srw64-recomp/tree/main/config/recomp/mini-stages) 里挑一个接近的关卡改，每个文件的 `note` 都写了它测什么。

## 2. 编译成镜像

在仓库目录里运行：

```bash
python3 tools/recomp/script_lab/mini_stage.py compile my-test.json --out my-test.image.json
```

生成的镜像（`srw64.mini-stage-image.v1`）就是游戏直接读取的格式。超过 63 个事件、事件或出击记录超出游戏的缓冲区时，编译会报错并说明原因。

镜像里含有从你的 ROM 复制来的原版数据，只在自己的电脑上用，不要分发。

## 3. 加载并进入

让游戏停在标题画面的主菜单（「ニューゲーム／コンティニュー」那一层），然后让智能体加载镜像：

> 等游戏到标题主菜单，用 srw64_mini_stage_load 加载 /你的路径/my-test.image.json，等关卡就绪后截图。

- `srw64_mini_stage_load` 的 `path` 是**游戏所在那台机器上**的路径。连 Steam Deck 时，镜像要先拷到 Deck 上。
- 加载后游戏立刻进入关卡：先播开场事件，然后停在地图上等你操作。`srw64_status` 里的 `mini_stage.ready` 变成 `true` 表示已经可以操作了。
- 不在标题主菜单、文件读不到或格式不对时，加载会被拒绝，返回原因。
- 已安装的游戏只能加载编译好的镜像。用 `srw64_launch` 从源码启动的开发会话还可以直接加载关卡定义，游戏会自己调用编译器；`srw64_launch` 的 `mini_stage` 参数则在启动时就带上关卡，到主菜单后按 F8 进入。

之后就可以按[工具一览](../mcp/#工具一览)里的工具操作：选单位、攻击、看战前确认、截图或录像。

## 注意事项

- **用的是你自己的存档目录。** 关卡里如果手动存档，或者开着自动存档，进度会写进平常的存档栏。测试前先备份用户目录下的 `saves/`，或者按 MCP 那一篇的做法用 `--user-dir` 另开一套目录。
- **不经过主角选择。** 主角与搭档的名字和路线都是空的；需要它们的事件请在关卡里自己设好，或者先正常开新游戏再测。
- **随机数和原版不同。** 直接进入比正常开新游戏早了几百帧，敌方的行动和命中判定会跟着变化。要复现某次的结果，就从同一个镜像、同样的操作重新开始。
- **一次只有一个关卡。** 再加载一个会替换掉前一个。测完用 `srw64_quit` 退出，或者回到标题再加载下一个。
