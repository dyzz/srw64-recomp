# 中日英跨平台文字与游戏对白

2026-09-20。实际游戏的正文、人名、分页提示、阅读进度、底栏和回看现在默认使用
FreeType + HarfBuzz + ICU，像素交给 [Plume 合成器](plume-pixel-compositor.md)。
对白不再链接 CoreText/CoreGraphics，也没有旧后端切换选项。支持范围为 `zh-Hans`、`ja`、`en`。
菜单、姓名和设置继续使用 SDL/RmlUi；游戏窗口、截图和 marker 的 Metal 迁移是另一项工作。

## 实际路径

`src/host/dialogue_scene.cpp` 实现 `typeset()` 和 `rasterize_frame()`，使用
`src/native/text/portable_text.*` 排版和绘字；`src/native/text/game_fonts.*` 负责选择字体。
`src/host/dialogue_layout_adapter.hpp` 将行／页范围交给原 Reader，同时在 Layout 中保存
不可变 TextLayout。游戏帧随之持有字形位置和字体字节，逐字显示只改变可见范围，不重新排版。
原 Reader 的翻页、自动阅读、回看、语言切换和 guest 确认逻辑保持不变。

ICU 处理字素边界与中日文禁则，HarfBuzz 整形，FreeType 输出灰度覆盖率。
偏移使用 UTF-16 code unit；保留组合序列、显式换行和空行。布局一次生成，绘制按已选择的行
和字素范围显示；行宽不足时只在字素边界应急换行。正文裁剪到对白框，标签和回看各有裁剪区域。
像素是顶向下 BGRA8、预乘 alpha；布局快照在换语言／字号和异步 GPU 呈现期间保持有效。
不承诺与 CoreText 抗锯齿逐像素相同，不扩展阿拉伯语、彩色 emoji 或其他语言的产品支持。

## 字体与依赖

构建依赖 FreeType >= 2.10、HarfBuzz >= 2.8、ICU >= 70。macOS 可用：

```sh
brew install freetype harfbuzz icu4c
```

Linux 可安装 `libfreetype6-dev libharfbuzz-dev libicu-dev fonts-noto-cjk`；Windows 的本地组件构建
可用提供上述三个库的 CMake 工具链（例如 UCRT64），游戏完整 Windows 构建尚未开放。

2026-09-23 起游戏运行使用打包字体：启动器把 `SRW64_FONT_DIR` 指向 `tools/content/prepare_fonts.py`
准备的目录（开发运行是 `build/fonts/`，应用包是 `Contents/Resources/fonts/`）。字体链：中文、日文为
HarmonyOS Sans SC → 符号字体 `SRW64Symbols.ttf`；英文为 HarmonyOS Sans Condensed → SC → 符号字体。符号字体里还有武器标记图标（U+E000＋原版字形号，见[改造画面](native-upgrade-screens.md)）。
目录里缺文件时明确报错，不退回系统字体。没有 `SRW64_FONT_DIR` 时（单元测试、旧探针）仍查找本机
Noto Sans CJK（Linux）、Arial Unicode（macOS）、微软雅黑（Windows）；Noto 标准 TTC 按中文／日文选择对应 face。
开发测试可用 `SRW64_TEXT_FONT` 指定一个明确的 TTF/OTF/TTC 文件；不搜索当前目录、不联网下载，
字体读入后由布局持有。旧内容包中的 macOS PostScript 字体名称不再决定对白字体，内容包无需重导入。
此处使用本地系统字体，不意味着发行字体与依赖打包已完成。

## 本地验证

GitHub Actions 已关闭，仓库不保留远端工作流。以下测试在本地执行，无 ROM 或 GPU 依赖：

```sh
# 完整游戏对白场景、原 Reader 和 UTF 转换；只使用 RT64 的 JSON 头文件。
cmake -S tests/dialogue_cpu -B build/dialogue-portable -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DSRW64_RT64_HEADERS="$PWD/build/recomp/upstream/RT64"
cmake --build build/dialogue-portable --parallel 6
ctest --test-dir build/dialogue-portable --output-on-failure

# 独立排版组件。也可以给出自己的本地 CJK 字体，跳过字体准备。
python tests/portable_text/prepare_fonts.py build/text-fonts
cmake -S tests/portable_text -B build/text-cjk -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DSRW64_TEST_CJK_FONT="$PWD/build/text-fonts/NotoSansCJKsc-Regular.otf"
cmake --build build/text-cjk --parallel 6
ctest --test-dir build/text-cjk --output-on-failure
```

独立排版测试覆盖中日英混排、禁则、组合字符、长文本、分页、裁剪、缩放、预乘 alpha、
字体文件删除后的旧布局与并发重排；游戏场景测试覆盖双框、人名、逐字、回看、底栏、换语言和字号。
`SRW64_TEST_CJK_FONT` 也可传给对白场景测试的 CMake，显式指定测试字体。
组件测试与真实游戏截图分别记录，不以 CPU 成功声明 Windows/Linux 整个游戏可玩。

## 本次验收

本地 `make check`：260 项，249 通过、11 跳过；独立文字组件及完整对白 CPU 的 4 个 CTest 通过。
真实 macOS 游戏运行 `build/recomp/portable-dialogue-02/` 中，先通过共享 UI／姓名与剧情进入回归，
再用 `tools/recomp/verify/verify_portable_dialogue.py` 覆盖中日英切换、10/13/18 字号、回看、
800×600 和 1100×760 窗口、只推进宿主分页不推进原脚本，共保存 18 张 GPU 截图。
报告为该目录的 `portable-dialogue-verification.json`；测试静音，独立新游戏，正常退出。
截图检查发现并修复了“整段正文放得下，却因 ICU 的换行断点包含换行符而提前折行”的问题，
组件测试保留此回归。未执行 Windows/Linux 的本次新场景或完整游戏，不借用旧组件测试作此声明。
