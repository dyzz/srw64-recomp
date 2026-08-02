# SRW64 中文化工程

当前阶段已完成 W0 无损基线和 W1 中文纵向样片：为《超级机器人大战64》
日版 Rev 0 建立可验证的 ROM、文本、字库、构建和 Libretro 运行时证据链。
韩版项目保留在 `ref-project/`，只作为格式 oracle，不作为本工程的运行时依赖。

## 已完成的 W0 能力

- 精确 ROM 身份门禁：大小、SHA-256、字节序、游戏编号、版本和头部 CRC
- 20 张文本表的独立结构解析
- 保留原始 8 字节文本头和每个 16 位单元的 JSONL IR
- 严格区分 `FFFF`、`FFFE`、`FFFD`
- IR 哈希清单和逐记录 ROM 对照校验
- 从 IR 生成逐字节相同的 no-op ROM
- 防止输出路径覆盖输入 ROM

设计、信任边界和验收数据见 [docs/w0-baseline.md](docs/w0-baseline.md)。

## 快速验收

```sh
make bootstrap
make check
.venv/bin/srw64-w0 accept \
  --rom rom.z64 \
  --baseline config/srw64-jp-rev0.json \
  --work-dir build/w0
```

`build/`、ROM、RAM 和参考仓库均不进入本工程源码版本控制。
来源与固定哈希见 [docs/provenance.md](docs/provenance.md)，开发与提交规范见
[CONTRIBUTING.md](CONTRIBUTING.md)。

## W1 中文样片

W1 在 W0 无损基线上增加显式 token 翻译层、简体中文字库、受限文本池和
50 条端到端技术样片。设计与构建命令见
[docs/w1-slice.md](docs/w1-slice.md)。

macOS 运行时自动化使用 `tools/libretro_runner.py` 直接驱动 Libretro
RetroPad 回调，不依赖窗口焦点或键盘映射。当前本机核心、脚本化路线和截图
证据见 [docs/w1-slice.md](docs/w1-slice.md#libretro-runtime-automation)。

## 全文本清单

`make inventory` 会对 20 张表的 51,174 条记录生成逐条清单、保留上下文 key 的
36,348 条去重翻译目录，以及机器可读汇总。当前候选文本为 51,006 条、
1,186,583 个非空白解码字符；统计口径、产物字段和覆盖边界见
[docs/text-inventory.md](docs/text-inventory.md)。
