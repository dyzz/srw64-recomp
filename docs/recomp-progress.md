# SRW64 recomp 实施记录

2026-09-12 补充：第一话通关槽位现已通过原生冷启动读取，恢复整备的总回合 7、资金 14,500，并核对玛娜米等级 2 / SP 102/102；未进入第二话，未新增参考模拟器对照。历史 SRAM 选择已增加摘要校验与回退，详见[当前存档恢复记录](native-save-recovery.md)。下文“通关槽位读取待验证”保留为当时的证据状态。

更新：2026-09-08。工作基点：`bc93a869e99fcafaf2c836751a6d46ee9c7a14dc`。
桌面字体的最新独立渲染实验见 [原生分辨率字体实验](native-font-probe.md)。
目标仍是日版原生完成“新游戏 → 完整战斗 → 过关整备 → 保存 → 退出重启读档”，
语言接入的当前状态见[原生内容架构](native-content-foundation.md)。RT64/Metal 原生宿主已从新游戏推进至女性超级系第一话
通关，并保存为总回合 7、资金 14,500。用户要求自动测试停在这份存档，后续由用户
试玩；通关槽位的重启读取与整备操作继续保留为待验证。
键盘绑定和启动入口见 [原生试玩说明](native-playtest.md)。

## 已取得的证据

| 项目 | 实测结果 | 证据范围 |
| --- | --- | --- |
| 工具链 | macOS arm64 编译 N64Recomp、RSPRecomp、n64sym、完整 N64ModernRuntime 和 RT64 成功 | 已链接任务记录宿主及 RT64/Metal 图形宿主 |
| 启动映射 | 初始化入口处常驻装载范围与 ROM 逐字节相同，BSS 为零，栈地址吻合 | ares 运行时观察 |
| 原生 LZ | 6,436 / 6,436 个资源一致，共 57,061,848 个解码字节；AddressSanitizer 通过 | 三个原始 MIPS 函数生成 C 后在 arm64 运行；ROM I/O、分配与释放由显式实验适配器提供 |
| 游戏内 LZ | 捕获原游戏一次调用及返回，127,016 字节输出与独立解码器一致 | 原函数确实用于游戏，参数/返回/栈与解码语义已交叉确认 |
| 装载范围 | 从一组直线装载函数恢复 20 次传输、18 个不同范围 | 其中一个为零长度传输，一个是 16 字节零数据；不能把范围数叫作代码 overlay 数 |
| Overlay 内容 | ROM `0x121560..0x184730` 与 RAM `0x801C2600..0x802257D0` 全范围相同 | 405,968 字节的运行时装载内容；未给该观察命名为某个已人工验收的场景 |
| CPU 函数扫描 | 审查 text/data 边界后为 3,526 个候选；系统入口另有 3 次精确拆分 | 生成了 3,407 个保留的 CPU 函数；仍需场景运行验证边界及间接调用 |
| 系统绑定 | n64sym 的 355 个函数标签中，282 个完整归一化签名匹配；结合逐指令审查应用 122 个系统名称 | 包含运行库替代项及省略的系统内部项；11 个残留调用使用明确报错的诊断入口 |
| CPU 原生运行 | 连续 600 VI、296 个图形任务、598 个音频任务、1,186,432 个音频样本，采样率 44,100 Hz | 游戏入口和线程原生执行；图形使用任务记录器，音频样本进入诊断接收器，未验证画面/扬声器 |
| 原生 overlay 读取 | ROM `0x10DA50` → RAM `0x801C4500`，长 `0x7C50`，全部字节与 ROM 相同 | 游戏阻塞读取完成后更新函数查找表；此次观察只覆盖该 overlay |
| 原生 GPU 画面 | RT64/Metal 在 Apple M4 Max 实际运行；draw hook 通过完成后的 GPU blit 读回 PNG | 已查看开场星空、缩放日文及公共序章；首段内容与已有模拟器图片一致，尚未做逐像素/全场景对照 |
| 原生输入 | 独立的 N64 button bitmask 按 VI tick 回放，可推进公共序章 | 输入文件带 schema、源脚本哈希与编译后哈希；不混用 RetroPad 编号和原生 N64 mask |
| 女性超级系 | `gfx-probes/female-route-1` 完成 5,400 VI；查看 present-2400 确认女主角マナミ・ハミル和对手アイシャ・リッジモンド默认姓名；present-2700 为路线开场 | 已验证选择与命名，未到达战术地图 |
| 第一张战术地图 | `gfx-probes/female-map-audio-1/present-6660.png` 已显示地形、城镇与敌方单位；present-7080 显示蓝色我方单位登场 | 实际 GPU 画面已查看；玩家移动、攻击和过关仍待验证 |
| 地图操作与中断存读档 | `female-map-audio-3` 两台初始机体移动、待机、保存；退出后 `first-map-reload-2` 恢复第一回合、资金 0、单位位置与已行动状态 | 原生跨进程战术存读档通过；过关整备存档仍待验证 |
| 首次完整交战 | `first-map-reload-2` 敌机导弹攻击大泰坦 3，HP 8000 → 7990；太阳激光反击后敌机 HP 1500 → 0，爆炸后返回地图并触发下一次交战 | 完整攻击/反击/结算/返回地图已查看；不是整关完成 |
| 第一话通关保存 | `first-map-turn5-reload-1/present-34260.png` 显示第一话通关、マナミ等级 2、总回合 7、资金 14,500；已冻结 32 KiB SRAM | 原生击破全部敌人、战后剧情和通关保存通过；该槽位的重启读取与参考端对照尚未执行 |
| RSP 任务 | 捕获真实图形与音频 OSTask，记录微码、数据、命令和哈希 | 任务提交证据；不代表已接入原生画面或扬声器 |
| 原生音频任务 | 一个真实任务完成；记录的 117 次 RSP DMA 写回范围全部与参考端一致，ASan 通过 | 参考端为同一次任务之后、下一次装载之前的 RDRAM；SP HALT/BROKE/SIG2 均置位 |
| 原生音频输出 | `gfx-probes/female-map-audio-1` 已接入 SDL 音频设备；用户在该次运行中反馈“声音我听了ok” | 原生开场音频人工试听通过；不能据此覆盖尚未进入的战斗音效 |
| 仓库检查 | `make check` 的 39 项测试、Python 编译和依赖检查通过 | 静态与工具测试；不替代原生场景验收 |

日版 SHA-256：
`ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e`。
原生解压的独立参考是本仓库 `src/srw64_rom/resources.py`。
音频参考端是固定 ares v148；运行库的向量实现也源自 ares，因此输出对照证明了
这条任务重编译/接入路径的一致性，不是两个完全独立硬件实现之间的证明。

后续 `gfx-probes/female-story-1` 原计划 11,400 VI，但进程以退出码 0 在 5,473 VI
结束；不能用其旧报告中的 `native-graphics-frames-captured` 推断完成了全段输入。
退出来源当时未记录，不能断言是用户关闭窗口或游戏故障。现已增加窗口退出事件日志，
并将实际 VI 不足的运行单独标记为 `native-run-ended-before-VI-limit`。
GPU 截图现在附有绘制时 VI 的 JSON，完成 GPU command buffer 后才写入图片与元数据。

为继续同一进程的战斗操作，宿主新增 `control.txt` 原子命令读取、每秒 `live-state.json`
以及按实际 VI 记录的 `control-events.jsonl`；工具入口为 `tools/recomp/control_host.py`。
实时按键已在 `female-story-2` 中推进世界地图上的角色对话；实际 VI 与按键事件保存在
该次运行目录。此项只证明输入生效，不能替代战术地图、战斗或存读档验证。

`female-map-audio-1` 后续已在第一张战术地图操作两台我方机体移动并待机，
但在约 19,333 VI 后以 SIGBUS 退出。系统崩溃堆栈确认 `srw64_queue_audio`
复制了异常大的样本长度。原始 `0x8007E28C` 的音频调度读取 AI_LEN，按目标
736 帧减剩余帧再加 240，最后写入有符号半字；将整个 SDL 队列作为 AI_LEN
会在积压时使该长度变负。ares v148 的 `n64/ai/io.cpp` 读取的是
`dmaLength[0]`，即当前 DMA，而非当前和后继两段之和。

首次修补改为跟踪当前 DMA，但 `female-map-audio-2` 暴露了另一问题：用户反馈
声音和图像不同步，17,418 VI 正常停止后，SDL 队列峰值为 1,480,816 帧，
即 33.58 秒。当前 DMA 长度无法反映原生设备的总积压，因此该实现没有通过同步
验收。`sync-issue.json` 保留了人工反馈和测量值；不能以“没有崩溃”判定音频通过。

当前宿主重新用整个设备队列反馈合成节拍，在前端保留一个 VI 的输出余量，并将
反馈量限制在一个 VI 内，再交给运行库现有的提前量处理。这是 SRW64 原生音频
适配，不宣称完整模拟 AI 两级 FIFO。超出 AI DMA 长度范围的输入会明确报错；
设备积压超过 100 ms 时丢弃旧队列并记录恢复事件，正常播放应无需此恢复。
新增 `audio-live.json` 每秒记录队列时长及恢复次数。
`tests/native_audio_queue.cpp` 现在将原游戏长度公式与 512 帧设备消耗粒度、
延迟回调组合运行 36,000 VI，同时检查长度和积压上限，ASan/UBSan 通过。
原生重跑目录为 `female-map-audio-3`。已收集连续两分钟、120 个样本，覆盖
VI 2,924..10,064：SDL 排队时长为 24.67–38.82 ms，均值 30.98 ms；
截至采样结束，整次运行峰值为 41.00 ms，积压恢复次数为零。证据位于
`audio-timing-report.json` 和 `audio-timing-observations.jsonl`。
此项验证设备队列不再持续增长；声卡实际输出延迟和完整画声同步仍以实际播放验收。
该次进程随后运行至 23,100 VI，经控制命令正常退出；最终设备队列峰值仍为
1,808 帧（41.00 ms），积压恢复次数为零，未重现此前约 19,333 VI 的崩溃。

随后 `first-map-reload-2` 持续至 70,583 VI（约 19.61 分钟），已覆盖多轮
敌方反击、我方主动攻击及回到地图。该检查点的设备队列峰值为 2,016 帧
（45.71 ms），恢复次数仍为零；见该目录 `audio-sync-checkpoint.json`。
`make check` 的 39 项测试、Python 编译检查和依赖检查通过；原生音频
队列测试经 ASan/UBSan 通过。此处仍只证明设备队列与原生流程，不将其写成
声卡回环测量或用户对战斗画声同步的人工验收。

该进程随后在第五回合、资金 8,300 保存，运行至 100,968 VI，经控制命令
正常退出（实测 1,683.92 秒）。最终音频队列峰值仍为 45.71 ms，恢复次数零。
冻结 `first-map-turn5.sram` 的 SHA-256 为
`591c7db67fb909bc4216f16103757309332fe12e7c65cc64636559cba5579953`。
后续 `first-map-turn5-reload-1` 从该存档继续；同时启动参考模拟器的这次启动阶段
在 VI 292..536 记录了 8 次积压恢复，峰值 5,008 帧（113.56 ms）。随后直到
VI 70,770 正常退出，恢复次数未增加，典型队列约 25–40 ms。该现象与并行启动负载同时发生，尚未证明
唯一因果；保留为启动阶段短暂跳音风险，不能用此前零恢复的运行掩盖。
这次启动也说明绝对 VI 输入脚本会受初始化进度影响：固定的标题脚本没有完成
Continue，随后通过实际菜单截图和控制事件继续读档成功。

本次进程已在第七回合用「熱血」和サンアタック击破最后的戦闘獣ダンテ，
推进战后剧情并在 ROM 卡带的存档 1 保存第一话通关状态。冻结文件为
`first-map-turn5-reload-1/stage1-clear-turn7.sram`，SHA-256：
`0c6ded15fdf60c6b0064b2260a335d17a4ff77386d14d634bfd7d3bcb8de7484`。
`intermission-save-evidence.json` 记录保存画面和状态；用户随后要求自动测试
到此结束，因此没有继续第二话、整备操作、通关槽位重启读取。

为交接人工试玩，宿主新增有焦点限制的 SDL 键盘输入，跨线程使用原子状态快照。
`Play SRW64.command` / `tools/recomp/play_native.py` 首次复制上述冻结存档，以后
复制最近一轮独立试玩目录的 SRAM；一次只允许打开一个试玩进程。交互模式不设
自动退出时限，GPU 图片使用固定的最新截图文件名。新增宿主已在
`keyboard-build-smoke-1` 完成 600 VI 启动检查；物理键盘和扬声器同步仍由人工验收。

在 `female-map-audio-3/present-10500.png` 中，游戏显示地图内保存完成。
原先的 SRAM SHA-256 为 `97a08fb524d03b8f0caaf344205b1dda9f5ea18b42bd48d0627b146998e19904`，
保存后为 `b340a9c686b627d00dfaee9b4d89c896039547f8d607cb51b035d3f7bb2d756b`，
长度均为 32 KiB。独立冻结副本 `first-map-turn1.sram` 将用于新进程读档；
当前状态为第一回合、资金 0、两台初始我方机体移动并待机。这是战术中断保存证据，
完整战斗、过关整备和该阶段的保存仍需继续验证。

`first-map-reload-2` 使用该冻结 SRAM，在新进程中完成标题 → Continue →
战术地图。present-1320 显示两台机体保持移动后的位置和已行动灰色状态；
present-1740 的系统菜单显示第一回合、资金 0。证据与哈希见该目录的
`save-reload-evidence.json`。标题菜单通过左右键旋转选项，屏幕上方的 Continue
不能用上键直接选择；第一次尝试因此进入新游戏，未算作读档成功。

同一冻结 SRAM 也已由固定 Mupen64Plus-Next 核心冷启动读取。原生保存为
大端字节序；参考核心的聚合保存区在 `0x20800` 放置 32 KiB SRAM，按其
`sram.c` 的 S8 访问规则转换字内字节序后，仅写入该范围并进行完整读回检查。
参考端实际通过 Continue 恢复地图，机体位置和已行动状态吻合，菜单显示
第一回合、资金 0。导入前与参考运行后的 SRAM 哈希保持相同。证据位于
`reference-save/turn1-load-1` 和 `turn1-inspect-1`；入口为
`tools/recomp/run_reference_save.py`。这是实际参考模拟器读档及画面对照，
不是将参考端即时状态用作原生检查点，也不覆盖尚未完成的过关整备状态。

第五回合的原生存档也已在参考模拟器恢复为资金 8,300，地图和机体位置经
`reference-save/turn5-load-1`、`turn5-inspect-1` 对照。进一步从同一 SRAM
恢复出的状态重放主角向下三格、向左一格移动，并使用命中率 100% 的
デストラクションブーメラン：原生与参考端均将ゼイファー的 HP 从 3,000
降至 0，主角 HP 保持 3,881，EN 从 85 降至 45。参考端真正执行了该次
移动和完整攻击；`reference-save/turn5-attack-1/visual-comparison.json`
记录对应画面与哈希。此项不推导所有随机战斗、帧时序或后续关卡一致。

当前日版与 5600 模型实验的身份锁在 `config/recomp/rom-variants.json`，宿主的
XXH3 身份表由该文件生成。编译前校验完整 ROM SHA-256、初始 1 MiB 与全部
审查过的装载段；CPU 来自已验证的日版生成结果。原生语言切换始终使用日版 ROM。

## 平台与强化边界

当前已运行的是 macOS ARM64 + Metal 宿主。Windows/Linux 是可沿上游架构适配的目标，
本项目尚无这些平台的编译和游戏验证。浏览器版本亦尚未实现。

上游 [RT64](https://github.com/rt64/rt64) 当前列出 D3D12、Vulkan、Metal 和
Windows/Linux/macOS，没有现成 WebGPU 后端。若制作浏览器版，拟复用自动生成的游戏 C
和资源解析，另行验证 Emscripten/Wasm、线程/消息队列、内存分配、图形以及持久化存档。
固定版本 N64ModernRuntime 的 librecomp 还链接 LiveRecomp/SLJIT；网页构建必须审查并
隔离这类本机运行时生成代码路径，不能只将 CMake 编译器换成 emcc。
[Emscripten pthread 文档](https://emscripten.org/docs/porting/pthreads.html) 说明多线程
依赖 SharedArrayBuffer 与 COOP/COEP；[运行环境文档](https://emscripten.org/docs/porting/emscripten-runtime-environment.html)
说明浏览器主循环与文件持久化的差异。这些是后续适配计划，尚无 Wasm 可执行性证明。

[Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) 将静态转换、现代运行层、
RT64 与游戏专用补丁组合。已读取的固定参考版本中，`patches/sky_transform_tagging.c`
通过变换标识辅助帧间插值，`patches/ui_patches.c` 加入扩展 GBI 和界面对齐，
`patches/autosaving.c` 根据游戏状态选择保存时机。SRW64 若做强化，同样需要识别和修改
本游戏的绘制、布局、计时与保存逻辑；这些 Zelda 补丁不适用于直接替换 SRW64 函数。

## 启动、内存与装载

已观察的启动关系：

- ROM 头入口：`0x80076610`。
- 初始化入口：`0x8007F5B8`，栈：`0x8010F0B0`。
- 常驻装载：ROM `[0x1000, 0x5BC30)` → RAM `[0x80076610, 0x800D1240)`。
- 常驻 CPU text 的后面是 RSP boot，text 终点为 ROM `0x4DEA0` / RAM `0x800C34B0`。
- 启动 BSS：`[0x800D1240, 0x8018DAC0)`。
- 当前模拟器的 `osMemSize` 为 8 MiB；捕获器现默认读取该字段。早期的 4 MiB
  文件是部分 RDRAM 快照，不能拿来排除 Expansion Pak 区域的代码或数据。

`0x8007F704` 是游戏的阻塞 ROM 读取包装函数，参数为 ROM offset、RAM 目的地、
长度；内部按至多 `0x400` 字节拆分 PI DMA 并等待消息。装载函数族
`[0x8007FD80, 0x8008016C)` 的常量参数可由 `analyze_layout.py` 重建。
分析器支持实际出现的少量指令、处理 JAL 延迟槽，并在未知指令/参数上拒绝继续。

不同 ROM 范围反复装入 `0x801C2600` 或 `0x801C4500`，另有传输到 `0x80400000`。
因此，函数身份必须包含 ROM 段，运行时查找表必须随装载更新，不能只用 RAM
地址给函数取全局唯一名字。当前扫描导出 `load_ROMOFFSET_func_VRAM`，保留身份。

第一轮常驻代码生成曾因 `0x801FD020` 缺符号失败。当前已确认其一个来源为
ROM `0x15BF80`，属于 `load_00121560`。补入按装载范围区分的候选后，原先
64 个不同外部 JAL 目标的缺符号问题不再是生成器的第一个阻塞。
此结论不表示所有调用已做运行时绑定验收。

生成器可能把只出现一次的目标地址直接绑定到某个 ROM 段，但候选唯一并不证明
其他段没有该地址的代码。当前生成后明确将 7,213 个到 overlay 的具名调用改为
同一个 MIPS 地址的 `LOOKUP_FUNC`，保留函数定义及原始调用地址。
整段 ROM 读取仍执行原始 MIPS 函数；在返回后核对全部读取字节，再卸载冲突段并
注册新段。仅按 `0x400` 的单次 PI DMA 更新运行库不能代表完整 overlay 已装载。

首次真实宿主运行在游戏线程 3 的 `0x800AEBC0` 读取 `AI_LEN_REG` 时触发访问错误。
逐指令确认其为 `osAiGetLength` 后接到运行库，下一轮连续运行通过。
`osInitialize` 的 guest 时钟全局按原指令补入 `46,875,000` 和 NTSC VI clock
`48,681,812`；目前属于静态语义接入，尚未捕获原函数返回点逐字段对照。

诊断宿主的 11 个 unsupported 入口会打印函数名并终止；不会返回伪造的成功值。
当前成功运行没有触发它们，不能由此宣称 Controller Pak、Transfer Pak 或全部
系统调用已支持。每次运行使用新的独立 SRAM/配置目录，尚未做跨进程存档验证。

任务记录宿主另完成了 1,800 VI 的持续运行：896 个图形任务、1,797 个音频任务。
图形宿主的 `gfx-probes/boot-3` 完成 600 VI，并输出 5 张 GPU 图片；
`start-1/present-420.png` 的公共序章首段，与既有
`build/libretro/start-scan/screenshots/frame-000900.png` 内容一致。二者的输出像素
比例不同，这里只记录直接查看得到的场景/文字对应关系。

## 音频微码与任务对照

已观察的 boot 微码位于 ROM `0x4DEA0` / RAM `0x800C34B0`，大小 `0xD0`。
其 SHA-256 为
`5759e9bb21f2e504bfbb3e5b75173cb81aa50c60b19e77bcee1d0f6fc34e8fa4`。
boot 会装入 `0xF80` 字节到 IMEM `0x1080`；音频 OSTask 的 `ucode_size=0`
并不意味着没有微码。图形任务指向 ROM `0x4DF70`，其数据里可见 F3DEX fifo 2.08。

音频可执行前缀位于 ROM `[0x4F300, 0x50120)`，长 `0xE20`，SHA-256 为
`14e3b245e8cd4e0bdf4cbb864af82d1ccba6d3d3ff33f73cb52c5821a76f30fc`。
首轮误将末尾截在 `0xE10`，宿主编译发现缺少 `L_1E94`；检查延迟槽与尾跳转后
修正为 `0xE20`。之后的 CPU 数据也被 boot 多读进 IMEM，但当前任务没有执行它。
早期捕获中完整 `0xF80` 块的 24 字节差异从相对 `0xE21` 开始，可执行前缀一致；
不把这项差异定性为自修改微码。

音频命令跳转表位于微码数据 `+0x10`，对应 ROM `0x59ED0` 的 16 个半字。
`audio-probe.toml` 显式记录这些间接跳转目标；回放前检查快照中的值完全相同。
音频实验使用 N64ModernRuntime 的真实 RSP 向量和 DMA helpers。仅在生成代码的
DMA 写接口增加观察器，保留原有写入行为并记录目的范围。

对照流程：停止在音频 `osSpTaskLoad` 入口 → 保存描述符与 8 MiB 输入 →
继续至下一次连续的 `osSpTaskLoad` 入口 → 核对 SP 状态并保存参考内存 →
本机回放同一输入 → 对照全部记录的 DMA 目的范围。
本次样本 SP 状态为 `0x243`，117 次写回全部一致。后续 `female-map-audio-1`
已通过 SDL 输出原生 PCM，用户确认开场试听正常。完整 BGM、各类战斗音效、
长时间播放及采样率/混音时序仍需独立覆盖。

## 可重跑入口

```sh
# 隔离的固定版本分析工具；首次需要网络及本机 clang/cmake/ninja。
make recomp-bootstrap

# 静态装载范围与按段的函数候选。
make recomp-layout
make recomp-scan

# 全部资源的真实 MIPS → C → arm64 对照。
make recomp-lz

# 带系统符号复核、overlay 地址查找和明确 unsupported 诊断入口的 CPU 生成。
make recomp-cpu

# 编译完整 CPU 宿主并运行；目录必须新建，图形仍为任务记录器。
python3 tools/recomp/run_host_probe.py \
  --output build/recomp/host-probes/new-boot --vis 600

# 图形依赖与可选的运行时 Metal 源码编译适配；只改变 build/ 下的固定克隆。
python3 tools/recomp/prepare_rt64.py
python3 tools/recomp/run_host_probe.py --graphics \
  --input config/recomp/start-scan.json \
  --output build/recomp/gfx-probes/new-start --vis 960
```

ares 使用 `build/recomp/runtime/srw64-jp.z64` 的基线副本和独立设置，保存文件不会
写到原始 `rom.z64` 旁。后台行为使用 `Input/Defocus=Block`，允许模拟继续而不
接收后台按键。调试信号 `S10` 只在已检查的 COP1 指令上允许继续原异常处理器；
其他意外异常会失败并保留记录。

```sh
# 输出目录必须尚不存在；--already-halted 仅用于等待 GDB 的冷启动状态。
python3 tools/recomp/rsp_capture.py \
  --session build/recomp/runtime/session.json \
  --output build/recomp/captures/new-memory

python3 tools/recomp/analyze_layout.py \
  --capture build/recomp/captures/idle-8mb \
  --output build/recomp/layout-observed.json

python3 tools/recomp/capture_rsp_tasks.py \
  --session build/recomp/runtime/session.json \
  --output build/recomp/captures/new-audio-task --count 6 \
  --snapshot-first-type 2 --snapshot-following-task

.venv/bin/python tools/recomp/run_audio_probe.py \
  --capture build/recomp/captures/new-audio-task \
  --output build/recomp/audio-probe/new-audio-task
```

主要本地证据（全部位于忽略的 `build/` 下）：

| 路径 | 内容 |
| --- | --- |
| `recomp/toolchain-build.json` | 工具提交、编译器、依赖与二进制哈希 |
| `recomp/captures/init-entry-2/` | 初始化入口寄存器与部分 RDRAM |
| `recomp/captures/lz-first-call-3/report.json` | 游戏内解压调用及输出对照 |
| `recomp/lz-probe/report.json` | 6,436 个资源的原生结果 |
| `recomp/captures/idle-8mb/` | 8 MiB 运行时内存与哈希 |
| `recomp/layout.json` | 装载函数、范围、哈希、外部调用与当前快照对照 |
| `recomp/cpu-scan/report.json` | 3,526 个候选的分段扫描命令与结果 |
| `recomp/cpu-bound/report.json` | 系统绑定、生成文件哈希、overlay 调用适配和 unsupported 清单 |
| `recomp/host-probes/boot-2/` | 首次进入游戏线程后在 AI 硬件读取处失败的记录 |
| `recomp/host-build/boot-2-debug.log` | LLDB 捕获的原生访问错误位置 |
| `recomp/host-probes/boot-3/` | 600 VI 的成功运行与任务计数；内存快照仅用于诊断 |
| `recomp/host-probes/boot-4/` | 1,800 VI 的持续运行与任务计数 |
| `recomp/gfx-probes/boot-3/` | 开场 GPU 图片与完整运行报告 |
| `recomp/gfx-probes/start-1/` | Start 回放与公共序章首段画面 |
| `recomp/gfx-probes/prologue-1/` | A 键推进至第二段公共序章的 GPU 画面 |
| `recomp/captures/rsp-tasks-idle-2/` | 24 个图形/音频任务与微码样本 |
| `recomp/captures/audio-differential-1/` | 同一音频任务的输入和下次装载边界快照 |
| `recomp/audio-probe/differential-1/report.json` | 原生音频任务的 117 次 DMA 范围对照 |

## 下一步及完成门槛

1. 继续场景覆盖和系统兼容审查。生成、本机编译和一次持续运行已经通过；需要验证
   同址 overlay 切换、间接调用、系统全局以及启动后路径。
2. 继续 RT64 场景对照，推进地图和战斗。主角选择、命名、路线开场及开场音频已
   取得证据；GPU、输入和音频设备输出需要覆盖战斗路径。
3. 扩展统一 JP profile 的语言、目标场景与存读档验收；保持每类证据独立。

自动化负责采样、分析、生成、编译、回放和对照；每次失败保留输入身份、命令、
状态与日志，再推进具体缺口。生成成功、组件运行、目标场景和整套游戏完成分别
记录。当前不报告整体完成百分比，也不以函数数替代可玩闭环。
