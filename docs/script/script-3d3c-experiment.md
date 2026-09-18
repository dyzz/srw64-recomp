# 3D3C 单参数对照实验

2026-09-12。承接[五次原参数观察](script-3d3c-runtime.md)，用固定原生二进制、相同按键和独立空 SRAM 对比男性超级系第一话。运行期间关闭音频设备输出，使用日文、Original 图片与模型显示。

## 验收结果

**本次绝对坐标实例通过：目标上移一格，同时改变了精灵终点和单位名册逻辑坐标。** 两组都完成开场的 56 条普通／对白指令，18 条对白，退出码 0。

| 检查 | 原值组 `move-baseline-2` | 改值组 `move-target17-1` |
| --- | --- | --- |
| 目标参数 | `1912` | 临时 `1911`，完成后恢复 `1912` |
| 移动前逻辑位置 | `(25,28)` | `(25,28)` |
| 完成后逻辑位置 | `(25,18)` | `(25,17)` |
| 完成后精灵位置 | `(432,320)` | `(432,304)` |
| 目标指令 VI | 6994–7054 | 6995–7059 |
| 耗时 | 60 VI | 64 VI |
| 开场结束时布拉德位置 | `(25,18)` | `(25,17)` |
| 开场结束时卡兹位置 | `(23,19)` | `(23,19)` |
| 原参数核对 | 56 条全部匹配 | 55 条匹配，另 1 条仅有预定的参数差异 |

两组二进制 SHA-256 都是 `a8d361fb8683453ee8c1bc58d8f9d195d488766c026674fdd8791e168f7d12b4`，宿主源码、ROM、输入和展示配置一致。移动完成及开场结束两个边界，全部有效单位的已记录字段只出现布拉德 y 减 1、精灵 y 减 16 的差异。名册中的起点 `(25,28)` 已腾空；改值组 `(25,18)` 无残留单位、`(25,17)` 仅布拉德占用。最终 RDRAM 快照中的整段 256 字节脚本也与原 ROM 一致。

画面会随布拉德位置改变而移动镜头，以下不是固定镜头逐像素差分；可以观察布拉德与卡兹的相对间距。源帧的 VI 和文件名保存在同名 JSON 中。

![原值与目标上移一格的实际游戏画面](../../build/recomp/script-analysis/move-position-comparison.png)

最终证据：[25 项对照检查全部通过](../../build/recomp/script-analysis/move-comparison-2.json)、[原值宿主报告](../../build/recomp/script-analysis/move-baseline-2/report.json)、[改值宿主报告](../../build/recomp/script-analysis/move-target17-1/report.json)。两组的 `script-observation.json` 保留完整指令对应，改值组中的一条原值不匹配被明确保留。

首次原值组 `move-baseline-1` 与改值组虽然源码一致、状态对照符合预期，但启动器重新生成 RSP 并链接，二进制摘要不同，因此[首轮严格比较未通过](../../build/recomp/script-analysis/move-comparison-1.json)。随后增加带指纹校验的 `--reuse-build-from`，复用改值组的原文件补跑 `move-baseline-2`；最终验收使用补跑结果。旧报告没有被覆盖。

## 对关卡 Mod 的意义与边界

正常绝对坐标的 `3D3C` 可以改变实际关卡单位位置。原值组还显示：精灵先到达终点，逻辑坐标仍保留起点；直到收尾调用完成才写回。因此依赖位置的后续事件应等待该命令完成。

本轮没有验收相对位置、越界或不可通行目标、同驾驶员编号的多个单位、目标不存在、完整战斗和存档重载。正式置信度继续保留 `structure-confirmed`，但绝对坐标移动、收尾写回及这一项单参数实验已有运行证据。

## 实验设计与隔离

唯一干预是事件 `0019BF10 + 00C8` 的第二参数：`1912 → 1911`，即驾驶员 27 ブラッド的目标由 `(25,18)` 改为 `(25,17)`。原 ROM 文件不变。

`script_move_probe.hpp` 默认关闭，`SRW64_SCRIPT_MOVE_PROBE=baseline` 只记录，`target17` 才临时覆盖。固定实验会检查场景 0、路线 `3DD3`、阶段 `C1`、待执行 PC `8019B4C8`、256 字节事件指纹及原始指令/参数，全部匹配才写入 `8019B4CC`。仅执行一次，在该命令完成后恢复 `1912`；若发现参数被其他代码改写，记录冲突而不覆盖。启动器要求原 JP、静音、空 SRAM、开启脚本记录。

两次运行使用同一个[输入文件](../../config/recomp/inputs/script-move-probe-input.json)。日志中的 `SRW64_SCRIPT_TRACE` 保留实际参数，因此改值组必须产生一条明确的原值不匹配；独立比较器只接受预先指定的 `1912 → 1911`，不会隐藏差异。

## 记录的状态

每次目标命令轮询记录三阵营各 30 槽中的全部有效单位：阵营、槽、单位指针、驾驶员编号、状态字节、逻辑 x/y、精灵索引和精灵 x/y。另记录移动前、覆盖后、命令完成、恢复后、开场最后一句完成这几个边界。

逻辑坐标的写回来自 `load_000AB160:801CBEB8`：`801E510C` 由精灵索引找阵营/槽，将 `(精灵位置−32)>>4` 写入 `8015E100 + 阵营×0x258 + 槽×0x14 + 4/+5`。新的 `script_actor_movement_commit` 证据窗口锁定这段原始机器码；测试独立检查两个坐标写入指令。

这里的“占格”是依据有效名册坐标枚举，不代表已经验证游戏碰撞查询、地形通行性、阻挡规则或独立占格缓存。

## 复现命令

```sh
# 先运行改值组，保留构建指纹。
SRW64_SCRIPT_TRACE=1 SRW64_SCRIPT_MOVE_PROBE=target17 \
SRW64_FRAME_TRACE_FROM=6800 SRW64_FRAME_TRACE_TO=8100 \
.venv/bin/python -B tools/recomp/run/run_host_probe.py \
  --graphics --profile config/recomp/profiles/play-profile.json --language ja \
  --images original --original-name-entry --resolution-scale 2 \
  --input config/recomp/inputs/script-move-probe-input.json \
  --output build/recomp/script-analysis/move-target17-next --vis 9000

# 原值组复用同一文件，拒绝二进制、源码、ROM、生成报告或 ABI 指纹变化。
SRW64_SCRIPT_TRACE=1 SRW64_SCRIPT_MOVE_PROBE=baseline \
SRW64_FRAME_TRACE_FROM=6800 SRW64_FRAME_TRACE_TO=8100 \
.venv/bin/python -B tools/recomp/run/run_host_probe.py \
  --graphics --profile config/recomp/profiles/play-profile.json --language ja \
  --images original --original-name-entry --resolution-scale 2 \
  --input config/recomp/inputs/script-move-probe-input.json \
  --reuse-build-from build/recomp/script-analysis/move-target17-next \
  --output build/recomp/script-analysis/move-baseline-next --vis 9000

.venv/bin/python -B tools/recomp/analysis/analyze_move_probe.py \
  build/recomp/script-analysis/move-baseline-next \
  build/recomp/script-analysis/move-target17-next \
  --output build/recomp/script-analysis/move-comparison-next.json
```

开场结束后可通过 `control_host.py RUN --quit` 提前退出；本实验不需要完成战斗。两组的最终验收以目标指令与开场边界为准，不要求启动和退出的墙钟时间完全一致。

## 代码与验证

- [固定实验探针](../../src/host/script_move_probe.hpp)：默认关闭、身份校验、单次覆盖、收尾恢复及状态快照。
- [比较器](../../tools/recomp/analysis/analyze_move_probe.py)：精确接受一个预定参数差异，同时检查源码／二进制、指令顺序、恢复与名册变化。
- [组件测试](../../tests/native_script_move_probe.cpp)：ASan/UBSan 下验证默认无写入、身份不符拒绝、仅一个字节改变、恢复、单次执行及冲突时不覆盖。
- `PYTHONDONTWRITEBYTECODE=1 make check`：97 项通过，编译和依赖检查通过；目录重建为 83 个证据项，全部引用可解析。
- 启动器负例：开启声音的实验被拒绝；伪造二进制摘要的复用请求被拒绝。比较器也拒绝将两份原值记录当作改值实验。
