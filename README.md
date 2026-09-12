# srw64-recomp

**Experimental native recompilation of Super Robot Wars 64.**

**《超级机器人大战64》原生重编译实验工程。**

[English](#english) · [简体中文](#简体中文) · [Technical docs / 技术文档](docs/README.md)

> Work in progress. This is a source and research repository, not a finished game
> release or a complete Chinese translation. The current native host targets
> macOS / Apple Silicon. Full-game compatibility has not been verified.
>
> 项目处于实验阶段。这是源码与研究仓库，尚非完整游戏发布版，也未完成全量汉化。
> 当前原生宿主面向 macOS / Apple Silicon，完整游戏兼容性仍待验证。

<!-- Reviewed runtime screenshots will be added here after maintainer approval. -->

## English

This project uses **N64Recomp**, **N64ModernRuntime**, and **RT64 / Metal** to run
recompiled code from the Japanese Rev 0 release of Super Robot Wars 64. The
original game scripts drive progression; native modules provide text rendering,
reading controls, presentation options, and development diagnostics.

Features are organized as **internal modules**. External MOD loading, a public
plugin API, and a general-purpose editor are outside the current scope.

### What works today

| Area | Current implementation |
| --- | --- |
| Languages | **F7** switches Chinese / Japanese immediately, without a dialog or restart, and remembers the selection. Standard dialogue and the new native UI are integrated; missing translations fall back to Japanese. |
| Presentation | **F6** switches Original / HD independently of language and text size. HD uses optional local experimental artwork and a native replacement for the world-map marker. |
| Dialogue | Unicode text, pagination, adjustable text size, four auto-reading speeds, fast-forward, and dialogue history. Switching language restarts the current text fragment without advancing the script. |
| Name entry | An in-window native editor with original character and length validation. Language changes preserve edited fields and respect active IME composition. |
| Saves | Isolated SRAM session history, integrity checks, and explicit recovery. A first-stage clear save has been cold-loaded into intermission. Full-state safe-node autosave remains a prototype. |
| Developer tools | ROM identity checks, resource and script extraction, data/story/model viewers, native probes, and state comparisons. |

**Chinese coverage:** 153 draft records out of 51,174 extracted text records;
none is marked reviewed yet. This count is an extraction denominator, not a
claim that every menu or text renderer supports language switching. Original
menus, battle labels, and baked-in text still have integration work remaining.

### Getting started

The repository does **not** include a ROM, game saves, extracted game assets,
fonts, HD texture packs, generated game code, or a prebuilt application. Supply
your own matching original Japanese Rev 0 ROM as `rom.z64` in the repository
root. Its identity and pinned toolchain sources are in [provenance](docs/provenance.md).

Python **3.11+** is required. The native host also needs macOS / Apple Silicon,
a C/C++ toolchain, CMake, Ninja, and SDL2. Dependencies are pinned in
`config/recomp/toolchain.json`; setup downloads and builds them locally.

```sh
# ROM-independent Python checks.
make bootstrap
make check

# Prepare the pinned native toolchain and generate game code.
# These steps require the local ROM and native build prerequisites.
make recomp-bootstrap
make recomp-layout
make recomp-scan
make recomp-cpu
.venv/bin/python tools/recomp/prepare_rt64.py

# Start a new game using original artwork; no local HD pack or saved game needed.
.venv/bin/python tools/recomp/play_native.py \
  --profile config/recomp/play-profile.json \
  --language ja --images original --new-game --mute
```

Use `--language zh-Hans` for the Chinese draft, or press **F7** in game. On
keyboards with media keys, the function-key modifier may be needed. **F6** becomes
available for HD when the matching local assets are installed. The checked-in
profile also describes those experimental assets; `--images original` is the
startup override for a checkout without them. The `.command` launchers are local
development conveniences; use the explicit command above for a first launch.

After the native build has been configured, `make recomp-native-check` runs the
C++ component checks. These are separate from Python CI and from actual gameplay
validation. The [development guide](docs/native-development.md) has detailed
build steps and module boundaries; most technical notes are currently Chinese.

### Experimental limits

- Runtime evidence covers the female super-robot route through stage 1 and a
  cold reload into intermission, plus targeted dialogue, name-entry, and graphics
  checks. Later stages and other routes are not fully validated.
- The original per-frame random timing is preserved. Dialogue skip comparisons
  reproduce timing differences; battle-animation skip equivalence is unverified.
- Original SRAM recovery does not restore the entire random state. Full-state
  autosave, reliable turn rollback, and arbitrary save states are not available.
- HD artwork is experimental and remains local. Screenshots showing it do not
  imply the assets are bundled or that all scenes have HD replacements.
- Windows / Linux native backends, complete translation, and the broader QoL
  roadmap remain future work. See the [internal MOD roadmap](docs/mod-roadmap.md)
  and [foundation verification](docs/native-foundations-verification.md).

## 简体中文

本项目基于 **N64Recomp、N64ModernRuntime 和 RT64 / Metal**，重编译并运行
《超级机器人大战64》日版 Rev 0 的原始代码。原游戏脚本负责剧情和进度推进，
原生模块提供文字显示、阅读控制、画面选项与诊断工具。

功能按**项目内部模块**组织；目前不接入外部 MOD，不提供公开插件 API 或完整编辑器。

### 当前功能

| 模块 | 已实现范围 |
| --- | --- |
| 语言 | **F7** 直接中／日热切换，无弹窗、不重启，并记住选择。标准对白和新增原生 UI 已接入，缺译回退日文。 |
| 画面 | **F6** 独立切换 Original／HD，语言和字号不随之改变。HD 使用本地实验素材，并替换世界地图标记模型。 |
| 阅读 | Unicode 文字、分页、字号调节、四档自动阅读、快进和对话回看。切换语言从当前文字片段开头重新显示，不推进脚本。 |
| 姓名输入 | 游戏窗口内的原生编辑页面，遵守原字库和字数限制；语言切换保留已编辑字段，不打断输入法组字。 |
| 存档 | 隔离的 SRAM 会话历史、完整性检查和显式恢复；第一话通关档已冷启动恢复到整备。完整状态的安全节点自动保存仍是原型。 |
| 开发工具 | ROM 身份校验、资源与脚本提取、数据／剧情／模型查看器、原生运行探针和状态比较。 |

**中文覆盖：** 已提取的 51,174 条文本记录中有 153 条中文草稿，已审校数量为 0。
该数字是提取记录的覆盖统计，不代表全游戏所有显示位置已经支持语言切换；原版菜单、
战斗标签和图片内嵌文字仍有待接入。全量翻译另行排期。

### 启动与检查

仓库不包含 ROM、存档、提取后的游戏素材、字体、HD 纹理包、生成的游戏代码或预编译程序。
请自行准备匹配的日版 Rev 0 原始 ROM，放在仓库根目录并命名为 `rom.z64`。
ROM 身份和工具链固定版本见[来源记录](docs/provenance.md)。

需要 **Python 3.11+**；原生宿主还需要 macOS / Apple Silicon、C/C++ 工具链、
CMake、Ninja 和 SDL2。按上方英文部分的命令依次准备 Python 环境、原生依赖和生成代码，
再以 `--images original --new-game --mute` 首次启动，可避免依赖本地 HD 素材和历史存档。
原生依赖会下载并构建到本地 `build/`。

将启动参数改为 `--language zh-Hans` 使用中文草稿，或在游戏中按 **F7** 切换。
媒体键键盘可能需要同时按功能键修饰键。配齐对应的本地 HD 素材后才可用 **F6** 切到高清。
已提交的 profile 同时描述这些实验素材；没有素材时用 `--images original` 覆盖初始模式。
`.command` 文件是本地开发快捷入口，首次启动请使用上面的完整命令。

`make check` 不需要 ROM；完成原生构建配置后，`make recomp-native-check` 运行 C++ 组件检查。
Python CI、原生组件测试与真实游戏流程验证分别记录。构建细节、静音测试和模块责任见
[原生开发指南](docs/native-development.md)。

### 实验阶段的边界

- 原生运行证据覆盖女性超级系第一话通关、通关档冷启动到整备，以及针对对白、姓名页和
  图形切换的检查；尚未完成其他路线、后续关卡和全游戏验收。
- 保留原版每帧随机时序。对白跳过对照已复现时序差异，战斗动画跳过的等价性仍未验证。
- 原版 SRAM 不完整恢复随机状态。完整状态自动保存、可靠的回合回退和任意时刻即时存档尚不可用。
- HD 美术属于本地实验，展示截图不意味着仓库附带素材，也不意味着全部场景已高清化。
- Windows／Linux 原生后端、完整翻译和更多 QoL 功能仍在规划中。见
  [内置 MOD 路线图](docs/mod-roadmap.md)和[底座验证记录](docs/native-foundations-verification.md)。

## Source layout / 源码目录

| Path | Responsibility / 职责 |
| --- | --- |
| `src/srw64_rom/` | ROM identity, formats, codecs / ROM 身份、格式与编解码 |
| `src/srw64_native/` | Content/profile compilation and save tooling / 内容编译与存档工具 |
| `src/native/` | Localization, game adapters, presentation / 本地化、游戏适配与呈现 |
| `tools/recomp/`, `config/recomp/` | Code generation, native host, probes, pinned configuration / 代码生成、宿主与探针 |
| `content/` | Language catalogs and art manifests / 语言目录与美术清单 |
| `tools/content/`, `tools/data_viewer/`, `tools/model_viewer/` | Extraction and inspection / 数据提取与查看 |
| `tools/hd_ai/` | Experimental HD-art processing / 实验性 HD 美术处理 |
| `tests/` | Python and native component checks / Python 与原生组件检查 |
| `docs/`, `reference/` | Engineering notes and source references / 工程记录与来源参考 |

## Credits and contributing / 致谢与贡献

Built on [N64Recomp](https://github.com/N64Recomp/N64Recomp),
[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime), and
[RT64](https://github.com/rt64/rt64). Additional sources and pinned revisions are
listed in [provenance](docs/provenance.md). This is an unofficial project; game
content belongs to its respective rights holders. A license for the project's
own source has not yet been selected; third-party components retain their own terms.

感谢上述项目及[来源记录](docs/provenance.md)中的工具与参考资料。本项目为非官方工程，
游戏内容的权利归各自权利人所有。项目自有源码许可证尚未选定，第三方组件遵守各自条款。

See [CONTRIBUTING.md](CONTRIBUTING.md) for repository boundaries and validation
requirements. Historical links into `build/` refer to local evidence, not files
included in this repository.

贡献约定与验证要求见 [CONTRIBUTING.md](CONTRIBUTING.md)。历史文档中的 `build/` 链接指向本地
证据文件，不随源码仓库分发。
