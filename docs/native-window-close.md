# 关闭窗口时的宿主退出问题

2026-09-12 更新：已实现游戏线程的协作停止、唤醒和完整回收；现代姓名→剧情关窗及原版姓名页面关窗的最终版本均已通过。原先“释放 RDRAM 时还有 4 个游戏线程存活”的缺陷在这些入口中已消除。VI 自动退出的本轮结果与完整证据汇总见下方验收记录；不据此宣称全游戏、存档恢复或任意卡死场景已验收。

## 2026-09-12 修复

源码位于 `tools/recomp/toolchain/prepare_runtime_lifecycle.py` 与 `src/host/runtime-support/guest_shutdown.hpp`。生成器按固定版本校验并生成 `threads.cpp`、`mesgqueue.cpp`、`scheduling.cpp`、`timer.cpp` 和 `recomp.cpp` 的本地适配，保留上游 checkout 和生成 CPU C 不变。

- 登记 `osCreateThread` 创建的宿主线程，登记、发布线程句柄与初始化确认使用同一个锁。退出后拒绝新线程；避免线程立即退出时清理器提前销毁初始化信号量。
- 退出时唤醒调度等待和外部消息等待；游戏线程在调度/消息安全点抛出运行时已有的终止异常，退出路径不再恢复游戏逻辑或继续调度其他线程。
- 清理器不再随 `exited` 提前停止，必须逐个 join 所有登记线程。退出期间已 join 的线程上下文保留到全部游戏线程结束，防止尚在调度操作中的线程访问已释放信号量。
- 入口线程、计时器、事件线程、游戏线程清理器和保存线程全部回收后才释放 RDRAM。未到达停止检查点的游戏线程会阻止释放；不采用强杀、detach 或延迟后强行释放。
- 二进制复用指纹加入生命周期生成器和 support 源码，修改这些文件后不能继续以旧宿主作为当前修复证据。

验证区分宿主退出码、实际 join 记录和 macOS 线程观测。`shutdown_verified` 要求创建/join 数量相等，RDRAM 释放前后均有明确的零线程观测，且 join 日志在释放之前。系统线程枚举不完整时输出 `UNOBSERVED`，不能当作零线程。

本轮组件测试使用实际适配后的调度/消息/清理源码，ASan/UBSan 覆盖阻塞接收、空闲消息等待、未启动线程、运行中轮询、正常销毁/复用、退出期间保留调度对象、40 次创建/退出竞态、无游戏线程及重复 join。入口为 `make recomp-guest-shutdown-test`，已加入 `make recomp-native-check`。

### 最终验收

汇总：[guest-shutdown-check/verification.json](../build/recomp/guest-shutdown-check/verification.json)。三个入口使用相同最终宿主二进制、手写源码摘要和生命周期适配，全部静音。

| 运行 | 退出路径 | 宿主退出码 | 创建 / 已 join | 释放 RDRAM 前 / 后存活游戏线程 |
| --- | --- | --- | --- | --- |
| `name-window-final` | 完整现代姓名校验、写回、缩放、进入剧情后真实关窗 | 0 | 4 / 4 | 0 / 0 |
| `original-window-final-2` | 原版姓名选字页真实关窗 | 0 | 4 / 4 | 0 / 0 |
| `vi-stop-final` | 600 VI 上限触发运行时退出 | 0 | 4 / 4 | 0 / 0 |

两条提前关窗运行的包装器状态为 `native-run-ended-before-VI-limit`，CLI 返回 1；这是没有到达预设 VI 上限的原有策略，宿主均为正常退出。实际验收以宿主退出码、关窗动作、join 与系统线程观测综合判断。

`original-window-final` 的首次最终版本运行在释放前遇到系统线程枚举不完整；其 `shutdown_lifecycle_verified: false` 原样保留，未计入通过项。随后相同二进制的 `original-window-final-2` 获得完整观测。早期 `original-window-1` / `name-window-1` 属于补强上下文保留之前的中间版本，也不计入最终矩阵。

- `make check`：98 项 Python 测试、compileall、依赖检查通过。
- `make recomp-native-check`：9 个组件程序通过，新增游戏线程组件使用 ASan/UBSan；实际原生宿主未使用 sanitizer 构建。
- 三个图形宿主目标构建通过，上游 N64ModernRuntime checkout 无修改。
- 已查看最终现代姓名运行的 `page-exited-to-story-window.png`，自定义昵称、中文对白与 1200×800 窗口呈现正常；不扩大为全场景视觉验收。

该修复关闭已复现的“游戏线程仍存活时释放 RDRAM”缺陷。没有验证全游戏或 SRAM 冷启动恢复；对不再调用调度/消息检查点的任意死循环，协作停止可能等待不返回，此时不会越过 join 释放内存。

## 历史复现与修复前证据

以下是 2026-09-11 的原始记录。当时缺陷已确认但未修复；正常进程退出不能覆盖这些线程证据。

## 当前验证

证据汇总：`build/recomp/window-close-check/verification.json`。所有测试静音，每轮使用新运行目录。

| 运行目录 | 路径 | 进程退出码 | 释放 RDRAM 后的游戏线程 |
| --- | --- | --- | --- |
| `window-1` | 新页面完整命名 → 剧情 → 真实 Cocoa 关窗 | 0 | 未启用诊断 |
| `control-trace` | 新页面完整命名 → 剧情 → VI 脚本退出 | 0 | 4 个 |
| `window-trace` | 新页面完整命名 → 剧情 → 真实 Cocoa 关窗 | 0 | 4 个 |
| `original-ui-trace` | 禁用新姓名 UI，在原版选字界面关窗 | 0 | 4 个 |

这里的真实关窗由 `NSWindow.performClose` 调用窗口自身关闭动作，经 SDL 的窗口委托产生 `SDL_QUIT`；没有直接合成 SDL_QUIT，也没有杀进程。`window-close-events.jsonl` 记录动作与 VI。`run_host_probe.py` 对提前关窗返回非零 CLI 状态是其未达到 VI 上限的策略，不能把该包装器返回值当作宿主崩溃；以 `report.json.exit_code` 为准。

三轮完整姓名检查均确认：主角／搭档编辑、Tab／Shift-Tab、窗口键盘派发、无效输入与重名拒绝、取消重进、确认页返回修改、两人八个姓名字段、剧情中的自定义昵称。800×600 → 1200×800 缩放后，GPU／对白 raster 尺寸一致，实际窗口中的对白与框体位置已核对。最新截图在 `window-trace/page-exited-to-story-window.png`。

## 退出证据

`SRW64_SHUTDOWN_TRACE=1` 仅在诊断运行中启用。在 `recomp::start` 释放 RDRAM 前后，通过 macOS `proc_pidinfo` 检查当前宿主自己的线程。脚本退出和 Cocoa 关窗都观察到 **Game Thread 1、6、4、3** 仍然存活。禁用新姓名 UI 后结果相同。大部分采样时它们处于等待状态；一次释放前采样中 Game Thread 1 处于运行状态。

因此已确认：运行时没有等全部游戏线程结束，就释放了它们仍引用的内存；这不是新姓名界面独有的问题。本轮没有再次触发 SIGSEGV，新增诊断也可能改变竞态时序，不能把四次正常进程退出解释为已经修复。

原始崩溃发生在 `build/recomp/name-page/final-hd/`，日志最后为 `SRW64_WINDOW_QUIT event=256 vi=2855`，随后宿主退出码 `-11`。重新读取系统崩溃报告后得到直接证据：

- 主线程位于 `__munmap → recomp::start → main`。
- 故障线程为 `Game Thread 6`，位于 `resident_func_8008AE90 → load_000A7EC0_func_801C28C8`。
- 故障地址为 `0x7000025c00`，对应本机观察到的 RDRAM 基址 `0x7000000000` 加 `0x25c00`。

原报告为 `~/Library/Logs/DiagnosticReports/srw64-gfx-host-2026-09-11-165355.ips`；与此问题相关的栈和哈希已提取到 `build/recomp/window-close-check/original-crash-evidence.json`。

固定运行时的 `thread_cleaner_func` 在 `exited` 置位后结束循环；退出流程只等待入口、事件、清理、存档和已适配的计时器线程，没有完整停止、唤醒并回收所有 `osCreateThread` 创建的游戏线程。修复必须先完成游戏线程的协作退出和 join，再释放 RDRAM，不能靠延迟释放或隐藏崩溃。

## 复现

完整命名与真实关窗，分别运行以下两条命令：

```sh
SRW64_SHUTDOWN_TRACE=1 SRW64_NAME_ENTRY_CONTROL=1 SRW64_WINDOW_CONTROL=1 \
.venv/bin/python tools/recomp/run/run_host_probe.py --graphics \
  --profile config/recomp/profiles/play-profile.json --input config/recomp/inputs/native-name-entry.json \
  --output build/recomp/window-close-check/new-window-run --vis 9000
.venv/bin/python tools/recomp/verify/verify_native_name_entry.py \
  --run build/recomp/window-close-check/new-window-run --exit-mode window
```

将 `--exit-mode` 改为 `control` 可对照原脚本退出。验证原版输入 UI 时，在启动器增加 `--original-name-entry`，用 `verify_window_close.py --run RUN --at-vi 1350` 触发指定 VI 的真实关窗。关闭动作只在 `SRW64_WINDOW_CONTROL` 启用时可用。

本轮只新增验证入口、结果记录和默认关闭的线程诊断，未改变退出算法。`make check` 的 60 项测试通过。

## 代码整理后的回归

2026-09-11：通用关窗、缩放和图片模式测试控制已拆至 `src/host/macos/window_test_control_macos.mm`，由图形宿主窗口更新调用，不再依赖姓名页面。线程诊断源码独立存放于 `src/host/runtime-support/shutdown_trace.hpp`，仍由生成器记录摘要并注入本地 runtime 源码。退出算法未改变。

- `build/recomp/cleanup-check/name-window/`：完整现代姓名流程、原校验、八字段读回、1200×800 剧情显示与真实关窗通过，宿主退出码 0。
- `build/recomp/cleanup-check/original-window/`：禁用原生姓名页，在原选字界面于 VI 1350 调用实际关窗；独立 SDL 缩放及 Original/HD 请求/应用也通过，宿主退出码 0。这只验证通用控制路径，未重复完整美术 ROI 对比。
- 两轮在 RDRAM 释放后均仍观测到 4 个游戏线程。本次新增报告明确使用“process exit code 0”，并记录 `shutdown_lifecycle_verified: false`，不将正常进程退出等同于安全回收。

三个图形宿主编译、60 项 Python 检查及 `make recomp-native-check` 的 8 个组件程序通过，测试全程静音。最新整理结果位于 `build/recomp/cleanup-check/verification.json`；开发和运维入口见[原生开发指南](native-development.md)。
