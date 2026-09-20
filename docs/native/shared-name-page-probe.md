# RecompFrontend 共享姓名页原型

2026-09-20。接续[跨平台发布计划](../design/cross-platform-release-plan.md)的 P2/P3。

这是一项独立窗口实验：复用 RecompFrontend 的 RT64/Plume 渲染器与其固定版本
RmlUi，显示主角选择、主人公／搭档姓名输入及确认页。正式游戏现已默认使用同一共享页面，见[游戏集成](shared-game-ui.md)。
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
- `src/native/ui/text_input.*` 补充固定版本 SDL backend 缺少的组字事件：临时文字、
  提交、取消、失焦恢复、确认键隔离。RmlUi 6 只提供输入框边界，候选窗定位使用
  当前输入框矩形，还不是逐字光标位置。
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

四张卡片是合成数据。左右键选择，Enter/Z 进入姓名页；Tab 换字段，Enter 提交，
Esc 返回；F7 切换日／中／英。错误字符经过与游戏相同的 `names::Codec` 校验，
组字尚未提交时 Enter 不推进页面，Esc 先取消组字。切换语言保留已提交的字段值。
合成流程的最终确认只记录 `starts`，不会启动游戏。

可选 `--dialogue /absolute/path/to/prepared/dialogue.json` 使用本地 prepared content 的
真实字形映射和前八张头像。头像仅作为视觉素材示例，不代表与合成人名的角色绑定。
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
`focus`、`select_all`、`key`、`text`、`preedit`、`language`、`resize`、`capture`、
`expect` 和 `quit`。文本及按键进入交互所用的 SDL/RmlUi 处理路径；`click` 直接调用
页面的语义动作，不是鼠标命中测试。断言失败返回非零。每步状态写入 `events.jsonl`；
截图在 GPU fence 完成后读回，并附同帧状态 JSON。`result.json` 标明合成流程的范围。

脚本验证：选人、三语切换保留姓名、组字确认键不提交、提交文字、取消恢复、失焦
恢复、拒绝不受支持的字符、缩放、主人公→搭档→确认→返回修改→结束。

### 本地验证记录（2026-09-20）

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

1. 在真实 OS 输入法下核验中文／日文候选、选词、退格、取消、粘贴与焦点切换。
   注入 SDL 组字事件通过不等于操作系统输入法通过。
2. 真实姓名请求、workload 遮挡与窗口／渲染线程串行化已接入默认宿主，见游戏集成文档。
3. 默认宿主复用输入释放门控；后续平台继续核验关闭按键不穿透。
4. 迁移手柄导航／recompinput，再补 Windows/Linux surface 与构建验证。
5. 共享页面已走通真实开场和命名；三平台第一话和存档冷启动仍属后续验收。
