# RecompFrontend 共享姓名页原型

2026-09-20。接续[跨平台发布计划](../design/cross-platform-release-plan.md)的 P2/P3。

这是一项独立窗口实验：复用 RecompFrontend 的 RT64/Plume 渲染器与其固定版本
RmlUi，显示主角选择与确认页（2026-09-27 起不再有姓名输入，见[默认姓名三语显示](default-names.md)）。正式游戏现已默认使用同一共享页面，见[游戏集成](shared-game-ui.md)。
原型中的请求由合成适配器提供，不执行原游戏、不写 SRAM，也不证明
Windows/Linux 游戏可玩。

## 依赖与代码边界

- `config/recomp/frontend.json` 固定 RecompFrontend 与 RmlUi 的提交。
- `tools/recomp/toolchain/prepare_frontend.py --fetch` 下载到忽略的依赖目录；拒绝
  错误版本或脏上游，不自动更新已有 checkout。默认不访问网络。
- 原型使用上游 `RmlRenderInterface_RT64` 的完整渲染实现。准备工具仅将其头文件的
  launcher umbrella include 改为 RmlUi/Plume 的最小依赖；生成副本及摘要留在本地。
  不接入完整 launcher、MOD 菜单、配置、recompinput 或上游的 macOS 全局 swizzle。
- `src/native/ui/name_page.*` 使用现有 `names::Request` 和带 serial 的语义回调。
  页面只处理展示与输入，不接触游戏内存；真正游戏侧的验证、写回与状态推进仍由
  `src/host/native_name_entry.cpp` 负责。独立可执行文件仍使用合成适配器，正式宿主已接到该运行适配器。
- `src/native/ui/text_input.*` 补充固定版本 SDL backend 缺少的组字事件（临时文字、
  提交、取消、失焦恢复、确认键隔离），现在只有游戏宿主里的资金输入框在用，姓名页和探针都不再用。
- `src/native/ui/probe_surface_macos.cpp` 单独承载 Metal 窗口与 GPU 截图回读。
  页面与组字代码不引用 Cocoa、CoreText、Metal；当前探针构建入口仍限 macOS。

## 构建与交互

先按[开发指南](../guide/native-development.md)准备原生图形工具链，再执行：

```sh
make recomp-ui-probe
./build/recomp/gfx-build/ui-probe/srw64-ui-probe \
  --catalog-dir content/locales \
  --font '/absolute/path/to/local-cjk-font.ttf' \
  --output build/recomp/ui-manual-01
```

字体必须由本地显式提供；加载到 FreeType 并统一注册为 `srw64-ui`，不使用语言目录
里的 macOS PostScript 名称，不复制或提交系统字体。示例需要一份覆盖中日英的字体。
发行用字体的选择与授权仍需另行落实。

四张卡片是合成数据。左右键选择，Enter/Z 进入确认页；确认页 Enter 开始、Esc 返回选角；
F7 切换日／中／英。合成流程的最终确认只记录 `starts`，返回记录 `backs`，不会启动游戏。
名字按请求原样显示：换成阅读语言是游戏宿主前端的事，探针不做。

可选 `--dialogue /absolute/path/to/prepared/dialogue.json` 使用本地 prepared content 的
前八张头像。头像仅作为视觉素材示例，不代表与合成人名的角色绑定。
这是私有 ROM 派生内容，不随原型代码分发。

## 可重跑的语义控制

```sh
./build/recomp/gfx-build/ui-probe/srw64-ui-probe \
  --catalog-dir content/locales \
  --font '/absolute/path/to/local-cjk-font.ttf' \
  --output build/recomp/ui-script-01 \
  --script config/recomp/ui-probe/name-entry.json
```

输出目录必须不存在。脚本采用 `srw64.ui-probe-script.v1`，支持 `click`（控件 ID）、
`key`、`language`、`resize`、`capture`、`expect` 和 `quit`。按键走交互所用的 SDL/RmlUi
处理路径；`click` 直接调用页面的语义动作，不是鼠标命中测试。断言失败返回非零。每步状态写入 `events.jsonl`；
截图在 GPU fence 完成后读回，并附同帧状态 JSON。`result.json` 标明合成流程的范围。

脚本验证：选人、选角页 Esc 无效、确认、三语切换、缩放、确认页返回选角、换人确认、开始。
2026-09-27 改写后尚未重新运行。

### 本地验证记录（2026-09-20，姓名编辑页时期）

macOS arm64 使用现有固定 RT64/Plume、AppleClang、FreeType 与本地 CJK 字体完成
编译及实际 Metal 窗口运行。66 步脚本、17 个状态断言通过，7 张 GPU 回读截图
检查了中英文选择页、组字中的姓名页、日文错误提示、英文姓名页、搭档页与确认页。
同时检查了中文组字提交、日文退格和语言切换后保留输入焦点。

本地记录目录为 `build/recomp/ui-probe-check-07`，`verification.json` 记录脚本、源码、
二进制与截图摘要；合成姓名配本地头像用于视觉验证，
不是原游戏角色绑定或游戏运行证据。`make check` 通过 256 项测试（11 项跳过），
记录在 `build/recomp/ui-make-check.log`。字体、头像、截图与依赖生成物均不入库。
另外已重新编译正式 `srw64-gfx-host`，确认可选原型没有阻断原宿主构建；未以该构建
代替真实游戏运行验收。

## 原型之后的验收

1. ~~在真实 OS 输入法下核验中文／日文候选~~：姓名页已不再输入文字。
2. 真实姓名请求、workload 遮挡与窗口／渲染线程串行化已接入默认宿主，见游戏集成文档。
3. 默认宿主复用输入释放门控；后续平台继续核验关闭按键不穿透。
4. 迁移手柄导航／recompinput，再补 Windows/Linux surface 与构建验证。
5. 共享页面已走通真实开场和命名；三平台第一话和存档冷启动仍属后续验收。
