# 本地输入与来源记录

本文件记录可复现实验所需、但不能提交进 Git 的输入。哈希是身份门禁，不代表
这些文件可以重新分发。

## 日版 ROM

- 文件位置：本地 `rom.z64`，不提交；
- 大小：33,554,432 字节；
- SHA-256：`ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e`；
- 字节序 magic：`80371240`；
- 游戏代码/修订：`NS4J`, Rev 0；
- 头部 CRC：`1649d810 f73ad6d2`。

## 原始日文字形映射

- 仓库位置：`reference/original-glyph-map.csv`；
- 独立身份锁：`config/data/original-glyph-map.json`；
- SHA-256：`f9e98bfca8ba13e5b37287bad5c795d57dd3f48b9e0d7865b330a5d5c1bd3567`；
- 来源：[Localize](https://github.com/snowyegret23/Localize)，提交 `91e0c15b76b44c302f29ddebc1c45e61f1828cd0`；
- 上游文件：`SRW N64/reference/srw64_glyph_map_seed.csv`；
- 用途：从原始日文 ROM 解码文字，供原生语言目录、数据浏览和字体实验使用。

该映射从旧参考克隆原样迁入，摘要保持一致；构建不再依赖整个参考仓库。
ROM 身份、文本表和资源解析由 `src/srw64_rom/` 独立完成。

原生字体由 `content/locales/` 声明，macOS 当前使用系统字体；字体文件不随仓库分发。

## Libretro 核心

原生 SRAM 对照工具使用核心报告的提交 `98c1b0d` 对应源码审查聚合保存区布局：
[libretro_memory.h](https://github.com/libretro/mupen64plus-libretro-nx/blob/98c1b0d/libretro/libretro_memory.h)
和 [sram.c](https://github.com/libretro/mupen64plus-libretro-nx/blob/98c1b0d/mupen64plus-core/src/device/cart/sram.c)。
本地副本位于 `build/recomp/reference-sram-source/`，两份文件 SHA-256 分别为
`4d89673af5424d31b391e6afcdaeaaca9fcf73d062637d32c9090906a428582e`
和 `609e1b94dd03384128c579abf0a90faeefea8c1f8a095f65a80a7cd68bdbe0fb`。
工具同时锁定下述已验收核心的完整 SHA-256 和运行时聚合保存区大小，导入前后
核查非 SRAM 字节未变。源码审查与参考模拟器实际读档结果分别保留。

- 核心：Mupen64Plus-Next arm64；
- 获取日期：2026-08-02；
- 来源：`https://buildbot.libretro.com/nightly/apple/osx/arm64/latest/mupen64plus_next_libretro.dylib.zip`；
- dylib SHA-256：
  `8cd7541261b06b89c18189d7621b825e4e6f906b64f4449056a40d0647a6f58d`；
- 本地位置：`build/libretro/cores/mupen64plus_next_libretro.dylib`。

`latest` 地址会漂移；任何不同哈希都应当作为新的运行时环境重新验证，不能沿用
当前截图或存档结论。

## Recomp 工具与参考运行时

固定输入见 `config/recomp/toolchain.json` 和 `config/recomp/requirements.lock`，
来源克隆、生成代码和二进制均位于忽略的 `build/recomp/`。

| 输入 | 固定提交/版本 | 当前用途 |
| --- | --- | --- |
| [N64Recomp / RSPRecomp](https://github.com/N64Recomp/N64Recomp) | `ffb39cdad1da5de07eaaa48bd1db4a89a7986771` | MIPS CPU 与 RSP 代码生成；递归依赖按父提交固定 |
| [n64sym](https://github.com/shygoo/n64sym) | `ccf4600f3389f1a84bde23339225cf372fdf7712` | libultra 签名候选；不能直接视为已确认的系统绑定 |
| [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) | `cdf5abbd5026fef5c364c676e4667c45e42b6863` | 已构建完整静态运行库，接入 CPU 诊断宿主及 RSP 音频任务 |
| [spimdisasm](https://github.com/Decompollaborate/spimdisasm) | `1.42.4` | 分段反汇编与函数候选 |
| [splat](https://github.com/ethteck/splat) | `splat64==0.50.0` | 已准备的分段工具，当前扫描未依赖其导出 |
| [RT64](https://github.com/rt64/rt64) | `43373749dac9bbc1b653e6a02aed40a9e1783bed` | 实际 Metal 渲染及 GPU 帧缓冲读回；递归依赖按父提交固定 |
| [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) | `1a9c26613c6e0906140dc8bcca7362cbe00bf1eb` | 阅读宿主窗口与 RT64 接口用法的参考源码 |

N64ModernRuntime 自身的 N64Recomp 子模块为
`81213c1831fab2521a6a5459c67b63437d67e253`，递归依赖已初始化并编译。
宿主构建前逐字节检查该版本与独立生成器的 `recomp.h` 相同，以核对上下文及
helpers 的接口；这不表示两个提交的所有内部接口都相同。
bootstrap 的 `compiled` 状态指分析工具已编译，不指游戏宿主已编译。

图形依赖由 `tools/recomp/prepare_rt64.py` 单独准备。Plume 子模块固定在
`d890ac899e505fb30040e037a4037cdeca68f033`。当前机器只有 Command Line Tools，
没有离线 Metal 编译器；实验采用可选的 MSL 源码嵌入方式，通过 Metal 运行时
源码编译接口加载同一份 SPIRV-Cross 输出。该后端原有的内部着色器也使用该接口。
两个源码适配位于忽略的克隆中，每次准备都会核对原始内容、固定提交和变更范围，
记录 `build/recomp/graphics-source-patches.json`；仓库保存适配脚本。另在本项目
CMake 中为固定 hlsl++ 版本补入 `labs` 的声明头文件。

GPU 图片由宿主使用 RT64 的 draw hook、Metal texture-to-buffer blit 和完成回调
读回并写出，不依赖系统桌面截图权限。输出代表实际 GPU 结果，是否正确仍须检查
对应场景及参考端，不能由图片存在推定整套游戏通过。

参考 ares v148 可执行文件位于 `/Applications/ares.app/Contents/MacOS/ares`，
本次 SHA-256 为
`7a49f00f96a691458461d7c9cf453d95c0f5c054389bbd87c253987b8b6fa345`。
运行时捕获同时记录 ROM、ares、会话和内存身份。结果及其边界见
[recomp-progress.md](recomp-progress.md)。
