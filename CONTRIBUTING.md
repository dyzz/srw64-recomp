# 贡献与工程约定

## 仓库边界

本仓库只提交可审查的源码、配置、原生内容包、测试和文档。以下内容必须只
保留在本地：原始或修改后的 ROM、模拟器内存/存档、Libretro 核心、字体、
构建输出和原始测试截图。用于项目介绍、经维护者确认的运行截图可单独放入
`docs/media/`；须记录来源、摘要和实验范围，不随图分发纹理包或其他游戏资源。
待审图片保留在忽略的 `build/`。原始字形映射作为独立参考数据保存在 `reference/`，其来源和摘要固定在 `config/data/original-glyph-map.json`。

第三方输入的来源、固定版本和哈希记录在 `docs/provenance.md`。不得通过提交
构建产物来替代来源记录。

## 开发流程

```sh
make bootstrap
make check
```

`make check` 不依赖 ROM、字体或模拟器，必须在每次提交前通过。涉及 ROM 的
变更还需要按风险分层验收：

1. `make recomp-data`：核对原 ROM 并提取文本、资源与游戏数据；
2. `tools/content/compile_profile.py`：验证语言目录、原文摘要、控制符和美术清单；
3. `make recomp-native-check`：原生组件验证；
4. `tools/recomp/run/run_host_probe.py` 与对应验证脚本：匹配 ROM、输入、存档与目标画面的原生运行证据；必要时使用同目录的参考模拟器工具对照。

静态检查、模拟器启动和目标游戏画面是不同证据层，文档和提交信息不得混为
一谈。核心存档哈希用于标识一次检查点，不作为跨运行必须相同的确定性产物。

## 变更规范

- Python 需要类型标注，错误应使用项目异常而不是静默回退。
- JSON 输入必须带稳定的 `schema` 字段；结构控制符顺序不得隐式改变。
- 生成文件写入 `build/`，源码目录不提交缓存或 `egg-info`。`pip install -e .`
  产生的本地安装元数据保持忽略；测试后的 `__pycache__` 可以清理。
- 修改翻译时同时保留源文本、目标文本和控制 token，并更新对应测试或运行时
  截图路径。
- 原始 ROM、字形表和工具链各有独立身份锁；来源变化时更新 provenance 并重新验证。

建议提交主题使用 `recomp:`, `content:`, `runtime:`, `test:`, `docs:` 或 `chore:` 前缀，
每个提交只覆盖一个可解释的证据边界。

## Preparing a public source snapshot / 公开源码整理

- Keep source, pinned dependency manifests, tests, and documentation in Git.
  ROMs, saves, memory dumps, generated CPU/RSP code, fonts, dependency checkouts,
  credentials, and local agent settings stay outside the published tree.
- Only maintainer-reviewed runtime screenshots belong in `docs/media/`. Record
  their provenance and whether they show optional experimental HD assets.
- Run the ROM-independent checks from a clean source copy as well as the working
  tree. Native runtime validation requires separately supplied local inputs.
- Public documentation must distinguish implemented features, verified scenarios,
  and future plans. Keep historical local evidence references clearly labelled.
- 项目源码许可证尚未选定；不要替维护者添加许可证，也不要把第三方来源说明当成授权。

## 原生代码与验证

模块责任、构建与证据入口见 [原生开发指南](docs/native-development.md)，
文档导航见 [技术索引](docs/README.md)。原生工具链与生成头文件已准备后，
运行 `make recomp-native-check` 汇集现有 C++ 组件检查；它独立于 `make check`，
也不代表实际游戏流程验收。

测试运行保持静音：`run_host_probe.py` 不传 `--audio`，交互入口使用
`play_native.py --mute`。每次使用新运行目录，保留真实宿主退出码、来源摘要和
目标画面。退出码为 0 与游戏线程完整回收分开记录；已知缺陷不得因一次未崩溃而关闭。

默认关闭的通用窗口 QA 放在 `window_test_control*`；姓名编辑器只承载姓名相关
UI 与字段测试。游戏地址和写回逻辑属于适配层，不能从渲染或 AppKit 回调直接写入。
不要直接修改生成的 CPU C 文件或通过删锁文件、宽泛杀进程来解决测试残留。
