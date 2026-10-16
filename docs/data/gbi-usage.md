# 本作用到的 F3DEX2 指令与组合器

2026-10-16，为 [PS Vita 移植方案](../design/psv-port.md)的 V0 探针统计。Vita 没有 RT64，N64 画面要换一个 GL2 级的显示列表解释器来画；这页回答它要支持多少东西：本作的显示列表用到哪些 F3DEX2/RDP 指令、几种组合器与 RDP 模式、什么贴图格式、每帧多少绘制调用。

## 怎么测的

- 宿主的计数钩子 `src/host/gbi_stats.cpp`：桌面 RT64 版在 `SRW64_DEBUG=1` 且设了 `SRW64_GBI_STATS=<文件>` 时，每个图形任务送进 RT64 之前把显示列表走一遍（顶层列表取对白合成器改过的副本、子列表取真 RDRAM，与 RT64 一致；跟随 `G_DL`，不跟随 `G_BRANCH_Z`），只计数不改渲染。计的是**送进 RT64 的列表**：对白合成器已把原版文字绘制改成 NOP，宽屏时宿主插入的 RT64 扩展指令（0x64）也在里面。
- 场景脚本 `tools/recomp/debug/check_gbi_usage.py`：九个场景各起一次宿主，原版图片、原版画面（标题列表、幕间、战前确认、姓名输入的原生页面都切回原版），自动按键，每场 1–3 分钟。合并解码：`tools/recomp/analysis/gbi_usage.py`，输入 `build/recomp/gbi-usage/<场景>.json`，下面的表就是它的输出。
- 场景覆盖：`title` 标题（飞字、PRESS START、环形菜单、放着不动后的演示战斗，再回到环）；`start` 从环上スタート到序章放大字幕与姓名输入；`map` 迷你关 battle-ui 的战术地图（移光标、机体指令窗、地图菜单）；`battle` 同一关里敌方来攻的原版战前确认与一场战斗演出；`intermission` 读第 1 话通关存档进原版幕间菜单，依次打开九项（含存档的记忆卡画面、前往下一关）；`worldmap` 迷你关 worldmap 的剧情世界地图（3D 地球、地标）；`act`/`ending` 章节标题卡与结局用的两个迷你关；`flow` 带开场对白、胜利与结局事件的脚本关。没有覆盖：第 33 话以后的关卡、全部战斗演出（只有一场）、卡拉OK 与音乐鉴赏。
- 每场的截图与 `gbi-stats.json` 在各自的运行目录 `build/recomp/debug/<时间>/`（本地，不进仓库）。

## 结果

### 各场景的显示列表

| 场景 | 显示列表数 | 指令数 | 每帧绘制调用 均值 / p90 / 最大 | 每帧三角形 均值 / 最大 | 三角形 | 纹理矩形 | 填充矩形 | 顶点 | G_DL 最深 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| act | 1352 | 5609118 | 434 / 525 / 627 | 12 / 148 | 3220 | 548665 | 21829 | 51614 | 1 |
| battle | 2893 | 13518846 | 423 / 517 / 643 | 41 / 196 | 90208 | 1013149 | 90319 | 427362 | 2 |
| ending | 1349 | 5889637 | 370 / 525 / 618 | 25 / 148 | 20792 | 444039 | 20845 | 85556 | 1 |
| flow | 3223 | 11572482 | 351 / 494 / 666 | 39 / 276 | 99564 | 903892 | 102244 | 290498 | 2 |
| intermission | 3545 | 12276638 | 297 / 494 / 699 | 4 / 148 | 0 | 742678 | 296043 | 47864 | 2 |
| map | 1723 | 6983868 | 426 / 487 / 627 | 9 / 148 | 3220 | 692670 | 26009 | 46574 | 1 |
| start | 2651 | 13598258 | 394 / 481 / 618 | 5 / 148 | 0 | 1022185 | 10061 | 66556 | 1 |
| title | 3696 | 21950787 | 644 / 1326 / 1985 | 246 / 1646 | 802356 | 1369583 | 99281 | 2010664 | 1 |
| worldmap | 1790 | 4276120 | 307 / 562 / 1492 | 187 / 1408 | 295140 | 200382 | 15424 | 597972 | 1 |

### 指令

| 指令 | 次数 | 场景 |
| --- | --- | --- |
| G_SETTILE | 14407683 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_SETTILESIZE | 13575392 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_RDPPIPESYNC | 10012422 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_RDPLOADSYNC | 7473309 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_SETTIMG | 7473309 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_RDPHALF_1 | 6937243 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_RDPHALF_2 | 6937243 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_TEXRECT | 6937243 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_LOADTILE | 6571273 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_NOOP | 2055549 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_SETOTHERMODE_L | 1967976 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_SETOTHERMODE_H | 1418663 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_MTX | 1188259 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_SETCOMBINE | 753868 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_TEXTURE | 718258 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_SETPRIMCOLOR | 689707 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_FILLRECT | 682055 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_GEOMETRYMODE | 644438 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_TRI2 | 577405 | act, battle, ending, flow, map, title, worldmap |
| G_EX (RT64) | 541529 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_LOADTLUT | 538935 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_RDPTILESYNC | 538935 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_SETBLENDCOLOR | 520478 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_VTX | 444632 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_LOADBLOCK | 363101 | act, battle, ending, flow, map, title, worldmap |
| G_MOVEWORD | 341621 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_POPMTX | 203390 | act, battle, ending, flow, map, title, worldmap |
| G_ENDDL | 171695 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_TRI1 | 159690 | act, battle, ending, flow, map, title, worldmap |
| G_SPNOOP | 155157 | battle, flow, title |
| G_DL | 149473 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_QUAD | 134629 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_MOVEMEM | 133332 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_CULLDL | 108172 | act, battle, ending, flow, map, title, worldmap |
| G_SETCIMG | 44444 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_SETFILLCOLOR | 38580 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_RDPFULLSYNC | 22222 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_SETSCISSOR | 22222 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| G_SETZIMG | 22222 | act, battle, ending, flow, intermission, map, start, title, worldmap |

宿主插入的 RT64 扩展指令（0x64，宽屏用）：

| 扩展指令 | 次数 | 场景 |
| --- | --- | --- |
| SETRECTALIGN | 238233 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| SETSCISSOR | 95624 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| PUSHSCISSOR | 95624 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| POPSCISSOR | 95624 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| SETRECTASPECT | 16424 | act, battle, ending, flow, intermission, map, start, title, worldmap |

### 组合器

G_SETCOMBINE 的不同取值：7 种；绘制时 (othermode_h, othermode_l, combine, geometry) 的不同组合：35 种。

| w0 w1 | 次数 | 场景 | 周期 1 颜色 | 周期 1 透明 | 周期 2 颜色 | 周期 2 透明 |
| --- | --- | --- | --- | --- | --- | --- |
| 00119623 FF2FFFFF | 385550 | act, battle, ending, flow, intermission, map, start, title, worldmap | (TEXEL0 - 0) * PRIMITIVE + 0 | (TEXEL0 - 0) * PRIMITIVE + 0 | (TEXEL0 - 0) * PRIMITIVE + 0 | (TEXEL0 - 0) * PRIMITIVE + 0 |
| 00FFFFFF FFFCF279 | 121326 | act, battle, ending, flow, intermission, map, start, title, worldmap | (0 - 0) * 0 + TEXEL0 | (0 - 0) * 0 + TEXEL0 | (0 - 0) * 0 + TEXEL0 | (0 - 0) * 0 + TEXEL0 |
| 00FFFFFF FFFDF6FB | 108135 | act, battle, ending, flow, intermission, map, start, title, worldmap | (0 - 0) * 0 + PRIMITIVE | (0 - 0) * 0 + PRIMITIVE | (0 - 0) * 0 + PRIMITIVE | (0 - 0) * 0 + PRIMITIVE |
| 0011FE23 FFFFF3F9 | 71430 | act, battle, ending, flow, map, title, worldmap | (TEXEL0 - 0) * PRIMITIVE + 0 | (0 - 0) * 0 + TEXEL0 | (TEXEL0 - 0) * PRIMITIVE + 0 | (0 - 0) * 0 + TEXEL0 |
| 00121824 FF33FFFF | 35937 | battle, title | (TEXEL0 - 0) * SHADE + 0 | (TEXEL0 - 0) * SHADE + 0 | (TEXEL0 - 0) * SHADE + 0 | (TEXEL0 - 0) * SHADE + 0 |
| 00FFFFFF FFFE793C | 24514 | act, battle, ending, flow, intermission, map, start, title, worldmap | (0 - 0) * 0 + SHADE | (0 - 0) * 0 + SHADE | (0 - 0) * 0 + SHADE | (0 - 0) * 0 + SHADE |
| 00FFFFFF FFFCF67B | 6976 | act, battle, ending, flow, intermission, map, start, title, worldmap | (0 - 0) * 0 + TEXEL0 | (0 - 0) * 0 + PRIMITIVE | (0 - 0) * 0 + TEXEL0 | (0 - 0) * 0 + PRIMITIVE |

### RDP 模式（绘制时的 othermode）

| h l | 次数 | 场景 | 周期 | 滤波 | 调色板 | 透视 | Alpha 比较 | 渲染模式标志 | Z 模式 | 覆盖目标 | 混合器 周期1; 周期2 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0084ACF0 00504240 | 2818039 | act, battle, ending, flow, intermission, map, start, title, worldmap | 1CYCLE | BILERP | RGBA16 | 否 | NONE | IM_RD FORCE_BL | OPA | FULL | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |
| 0084ACF0 00551809 | 2579652 | act, battle, flow, intermission, map, title | 1CYCLE | BILERP | RGBA16 | 否 | THRESHOLD | AA_EN CVG_X_ALPHA | XLU | CLAMP | CLR_IN*A_IN + CLR_MEM*A_MEM; CLR_IN*A_IN + CLR_MEM*A_MEM |
| 008CACF0 005041C8 | 572880 | act, battle, ending, flow, intermission, map, start, title, worldmap | 1CYCLE | BILERP | RGBA16 | 是 | NONE | AA_EN IM_RD CLR_ON_CVG FORCE_BL | OPA | WRAP | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |
| 0084ACF0 0F0A4000 | 550620 | battle, flow, intermission, title, worldmap | 1CYCLE | BILERP | RGBA16 | 否 | NONE | FORCE_BL | OPA | CLAMP | CLR_IN*0 + CLR_IN*1; CLR_IN*0 + CLR_IN*1 |
| 00842CB0 00504B50 | 523200 | act, battle, ending, flow, intermission, map, start, title, worldmap | 1CYCLE | BILERP | NONE | 否 | NONE | Z_CMP IM_RD FORCE_BL | XLU | SAVE | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |
| 008CACF0 00553078 | 343083 | act, battle, ending, flow, map, title, worldmap | 1CYCLE | BILERP | RGBA16 | 是 | NONE | AA_EN Z_CMP Z_UPD IM_RD CVG_X_ALPHA ALPHA_CVG_SEL | OPA | CLAMP | CLR_IN*A_IN + CLR_MEM*A_MEM; CLR_IN*A_IN + CLR_MEM*A_MEM |
| 008CACF0 0F0A7008 | 322138 | act, battle, ending, flow, map, title, worldmap | 1CYCLE | BILERP | RGBA16 | 是 | NONE | AA_EN CVG_X_ALPHA ALPHA_CVG_SEL FORCE_BL | OPA | CLAMP | CLR_IN*0 + CLR_IN*1; CLR_IN*0 + CLR_IN*1 |
| 0084ACF0 0F0A7008 | 252213 | title | 1CYCLE | BILERP | RGBA16 | 否 | NONE | AA_EN CVG_X_ALPHA ALPHA_CVG_SEL FORCE_BL | OPA | CLAMP | CLR_IN*0 + CLR_IN*1; CLR_IN*0 + CLR_IN*1 |
| 00848CF0 005049D9 | 157888 | act, battle, ending, flow, intermission, map, start, title, worldmap | 1CYCLE | POINT | RGBA16 | 否 | THRESHOLD | AA_EN Z_CMP IM_RD CLR_ON_CVG FORCE_BL | XLU | WRAP | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |
| 008CACF0 005049D8 | 71874 | battle, title | 1CYCLE | BILERP | RGBA16 | 是 | NONE | AA_EN Z_CMP IM_RD CLR_ON_CVG FORCE_BL | XLU | WRAP | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |
| 008CACF0 00504240 | 66727 | act, battle, ending, flow, intermission, map, start, title, worldmap | 1CYCLE | BILERP | RGBA16 | 是 | NONE | IM_RD FORCE_BL | OPA | FULL | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |
| 008CACF0 005049D9 | 61772 | act, battle, ending, flow, intermission, map, start, title, worldmap | 1CYCLE | BILERP | RGBA16 | 是 | THRESHOLD | AA_EN Z_CMP IM_RD CLR_ON_CVG FORCE_BL | XLU | WRAP | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |
| 0084ACF0 005049D9 | 44345 | act, battle, ending, flow, intermission, map, start, title, worldmap | 1CYCLE | BILERP | RGBA16 | 否 | THRESHOLD | AA_EN Z_CMP IM_RD CLR_ON_CVG FORCE_BL | XLU | WRAP | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |
| 008CACF0 00504B71 | 42276 | battle, title | 1CYCLE | BILERP | RGBA16 | 是 | THRESHOLD | Z_CMP Z_UPD IM_RD FORCE_BL | XLU | SAVE | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |
| 00BCACF0 00000001 | 19899 | act, battle, ending, flow, intermission, map, start, title, worldmap | FILL | BILERP | RGBA16 | 是 | THRESHOLD | - | OPA | CLAMP | CLR_IN*A_IN + CLR_IN*1MA; CLR_IN*A_IN + CLR_IN*1MA |
| 0084ACF0 00553078 | 19344 | flow, worldmap | 1CYCLE | BILERP | RGBA16 | 否 | NONE | AA_EN Z_CMP Z_UPD IM_RD CVG_X_ALPHA ALPHA_CVG_SEL | OPA | CLAMP | CLR_IN*A_IN + CLR_MEM*A_MEM; CLR_IN*A_IN + CLR_MEM*A_MEM |
| 00B42CB0 00000000 | 13948 | act, battle, ending, flow, intermission, map, start, title, worldmap | FILL | BILERP | NONE | 否 | NONE | - | OPA | CLAMP | CLR_IN*A_IN + CLR_IN*1MA; CLR_IN*A_IN + CLR_IN*1MA |
| 008CACF0 005041C9 | 5556 | act, battle, ending, flow, intermission, map, title, worldmap | 1CYCLE | BILERP | RGBA16 | 是 | THRESHOLD | AA_EN IM_RD CLR_ON_CVG FORCE_BL | OPA | WRAP | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |
| 008C2CF0 00504240 | 5283 | act, battle, ending, flow, map, title, worldmap | 1CYCLE | BILERP | NONE | 是 | NONE | IM_RD FORCE_BL | OPA | FULL | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |
| 00BCACF0 00000000 | 4715 | act, battle, ending, flow, intermission, map, title, worldmap | FILL | BILERP | RGBA16 | 是 | NONE | - | OPA | CLAMP | CLR_IN*A_IN + CLR_IN*1MA; CLR_IN*A_IN + CLR_IN*1MA |
| 00848CF0 00504241 | 4341 | battle, flow, intermission, map | 1CYCLE | POINT | RGBA16 | 否 | THRESHOLD | IM_RD FORCE_BL | OPA | FULL | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |
| 00848CF0 00551809 | 4245 | battle, flow, intermission, title | 1CYCLE | POINT | RGBA16 | 否 | THRESHOLD | AA_EN CVG_X_ALPHA | XLU | CLAMP | CLR_IN*A_IN + CLR_MEM*A_MEM; CLR_IN*A_IN + CLR_MEM*A_MEM |
| 0084ACC0 00504240 | 2700 | act, battle, ending, flow, intermission, map, start, title, worldmap | 1CYCLE | BILERP | RGBA16 | 否 | NONE | IM_RD FORCE_BL | OPA | FULL | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |
| 008CACC0 005041C8 | 2160 | act, battle, ending, flow, intermission, map, start, title, worldmap | 1CYCLE | BILERP | RGBA16 | 是 | NONE | AA_EN IM_RD CLR_ON_CVG FORCE_BL | OPA | WRAP | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |
| 008CACF0 00504241 | 1592 | act, battle, flow, map | 1CYCLE | BILERP | RGBA16 | 是 | THRESHOLD | IM_RD FORCE_BL | OPA | FULL | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |
| 008C2CF0 0F0A4000 | 505 | title | 1CYCLE | BILERP | NONE | 是 | NONE | FORCE_BL | OPA | CLAMP | CLR_IN*0 + CLR_IN*1; CLR_IN*0 + CLR_IN*1 |
| 00300000 00000000 | 18 | act, battle, ending, flow, intermission, map, start, title, worldmap | FILL | POINT | NONE | 否 | NONE | - | OPA | CLAMP | CLR_IN*A_IN + CLR_IN*1MA; CLR_IN*A_IN + CLR_IN*1MA |
| 008CACC0 00504240 | 9 | act, battle, ending, flow, intermission, map, start, title, worldmap | 1CYCLE | BILERP | RGBA16 | 是 | NONE | IM_RD FORCE_BL | OPA | FULL | CLR_IN*A_IN + CLR_MEM*1MA; CLR_IN*A_IN + CLR_MEM*1MA |

### 贴图

| G_SETTIMG 格式 | 次数 | 场景 |
| --- | --- | --- |
| CI8 | 6048073 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| RGBA16 | 1062135 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| CI16 | 363101 | act, battle, ending, flow, map, title, worldmap |

| G_SETTILE 格式 | 次数 | 场景 |
| --- | --- | --- |
| CI8 | 7961387 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| CI4 | 4497860 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| RGBA16 | 1046400 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| RGBA4 | 538935 | act, battle, ending, flow, intermission, map, start, title, worldmap |
| CI16 | 363101 | act, battle, ending, flow, map, title, worldmap |

| G_SETCIMG 格式 宽 | 次数 | 场景 |
| --- | --- | --- |
| RGBA16 320 | 44444 | act, battle, ending, flow, intermission, map, start, title, worldmap |

| G_LOADTLUT 颜色数 | 次数 |
| --- | --- |
| 00000010 | 511823 |
| 00000100 | 27112 |

| G_SETTILESIZE 宽×高（前 20） | 次数 |
| --- | --- |
| 16×16 | 8427098 |
| 32×32 | 1507467 |
| 33×33 | 1009512 |
| 64×16 | 523200 |
| 65×17 | 523200 |
| 64×64 | 303530 |
| 9×15 | 270198 |
| 8×14 | 265432 |
| 33×17 | 113855 |
| 9×9 | 109936 |
| 8×8 | 107662 |
| 32×16 | 94569 |
| 18×18 | 94496 |
| 17×17 | 86134 |
| 15×15 | 47371 |
| 14×14 | 45825 |
| 34×34 | 42598 |
| 8×16 | 2205 |
| 128×32 | 1080 |
| 17×33 | 18 |

贴图块尺寸共 21 种，最宽 128，最高 64。

### RSP 状态

| 几何模式位 | 曾置位 | 曾清除 |
| --- | --- | --- |
| ZBUFFER | 是 | 是 |
| SHADE | 是 | 否 |
| CULL_FRONT | 否 | 是 |
| CULL_BACK | 是 | 是 |
| FOG | 否 | 是 |
| LIGHTING | 是 | 是 |
| TEXTURE_GEN | 否 | 是 |
| TEXTURE_GEN_LINEAR | 否 | 是 |
| SHADING_SMOOTH | 是 | 是 |
| CLIPPING | 否 | 否 |

| G_MOVEWORD | 次数 |
| --- | --- |
| LIGHTCOL | 143748 |
| CLIP | 88888 |
| SEGMENT | 64541 |
| NUMLIGHT | 22222 |
| PERSPNORM | 22222 |

| G_MOVEMEM | 次数 |
| --- | --- |
| LIGHT | 111110 |
| VIEWPORT | 22222 |

| G_MTX 参数 | 次数 |
| --- | --- |
| MODELVIEW LOAD | 940425 |
| MODELVIEW MUL PUSH | 203390 |
| PROJECTION MUL | 22222 |
| PROJECTION LOAD | 22222 |

| G_VTX 顶点数 | 次数 |
| --- | --- |
| 4 | 225215 |
| 8 | 164373 |
| 32 | 25371 |
| 16 | 14443 |
| 31 | 3930 |
| 30 | 2275 |
| 29 | 2160 |
| 10 | 1950 |
| 20 | 1141 |
| 27 | 1080 |
| 24 | 969 |
| 12 | 805 |
| 6 | 690 |
| 18 | 115 |
| 22 | 115 |

| G_TEXTURE 层级 | 次数 |
| --- | --- |
| 0 | 718258 |

G_BRANCH_Z：0 次（统计时不跟随分支）；G_LOAD_UCODE：0 次；未识别指令：0；未识别扩展指令：0；被截断的列表：0。

## 对 Vita 渲染器的意义

- **组合器只有 7 种取值，全是单周期**：贴图×PRIMITIVE、贴图×SHADE、直接 TEXEL0、直接 PRIMITIVE、直接 SHADE，透明通道同样。没有两周期模式（`othermode_h` 的周期类型只见 1CYCLE 与 FILL），没有 TEXEL1、LOD_FRACTION、NOISE、K4/K5、ENVIRONMENT。
- **RDP 模式 28 种、绘制时 (othermode_h, othermode_l, combine, geometry) 的组合 35 种。** 混合器只有四种式子：`CLR_IN*A_IN + CLR_MEM*1MA`（普通透明）、`CLR_IN*A_IN + CLR_MEM*A_MEM`（覆盖率抗锯齿的不透明）、`CLR_IN*0 + CLR_IN*1`（直接写）与 FILL。贴图滤波只有 BILERP 与 POINT，没有 AVERAGE；Alpha 比较只有 NONE 与 THRESHOLD，没有 DITHER；没有雾（`FOG` 位只被清过）、没有 texture gen、`G_TEXTURE` 层级恒为 0（没有 mipmap/LOD）、没有 `G_LOAD_UCODE`（整场只有 F3DEX2，没有 S2DEX）、没有 `G_BRANCH_Z`、`G_DL` 最深 2 层。
- **Z 缓冲在用**：战斗演出的 3D 地面与机体、世界地图走 `Z_CMP`/`Z_UPD`（`00553078`、`00504B71` 等），2D 场景不用。`IM_RD`（读回帧缓冲做混合）几乎处处有，但只是 N64 的普通透明混合，GL 的 alpha blend 就是它；真正需要读回上一帧的只有战斗残影（[战斗演出渲染机制](../design/battle-animation-rendering.md)），这里统计不到。
- **贴图格式 3 种**：CI4、CI8（调色板 16 或 256 色，RGBA16 调色板）、RGBA16。`G_SETTIMG` 的 CI16 是 `G_LOADBLOCK` 按 16 位块搬运 CI 数据的写法，`G_SETTILE` 的 RGBA4 是装调色板时的约定，不是真格式。没有 I/IA、没有 32 位、没有 YUV。贴图块 21 种尺寸，最大 128×32 与 64×64，16×16 占 62%。颜色缓冲恒为 320 宽 RGBA16。
- **每帧绘制调用多**：2D 场景均值 300–650 次，标题演示战斗 p90 1,326、最大 1,985，世界地图最大 1,492；几乎都是纹理矩形（`G_TEXRECT` 共 694 万次；三角形 131 万个加四边形 13 万个、顶点 362 万个，集中在演示战斗与世界地图）。一个 GL2 后端如果一条 texrect 一次 draw call，Vita 上跑不动；解释器要把连续、同贴图同状态的矩形合并成一个顶点缓冲再画，并把 TMEM 装载按内容哈希做贴图缓存（`G_SETTILE` 1,440 万次、`G_LOADTILE` 657 万次是列表里最多的指令，每帧重复装同样的小贴图）。
- **RSP 侧**：矩阵只有 LOAD 与 MUL/PUSH 两种用法，`G_MOVEMEM` 只有光源与视口，光照在用（世界地图、战斗 3D），`G_VTX` 一次最多 32 个顶点。
- **取舍**：这个子集比 libultraship Fast3D 覆盖的小得多（Fast3D 为两周期、雾、LOD、S2DEX、多种贴图格式都带了实现）。自写解释器只要实现上面列出的指令、7 种组合器、4 种混合式、3 种贴图格式和 Z 缓冲，着色器可以按 35 种状态预先写成固定集合、启动时全部预热，正好避开 Sony 运行时着色器编译器不稳定的问题；需要从 Fast3D 借鉴的只有 TMEM 装载与贴图解码的细节。方案见[PS Vita 移植方案](../design/psv-port.md)的「待定」。
