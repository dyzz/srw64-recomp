# 跨平台文字组件（P2c1）

2026-09-20。接续 [通用 Plume 像素合成](plume-pixel-compositor.md)。本批提供独立 CPU
排版和文字像素输出，尚未把完整游戏对白切换到新后端。CoreText、`rasterize_frame`、
游戏启动与默认构建均保持原状。

## 接口与依赖

`src/native/text/portable_text.*` 提供 `FontSet`、不可变 `TextLayout`、UTF-16 字素边界、
换行、分页和预乘 BGRA8 文字栅格化。组件不链接 SDL、RmlUi、RT64、Plume、CoreText、
JSON、游戏状态或 ROM。构建入口为 `cmake/PortableText.cmake`。

ICU 负责扩展字素、显式 BCP-47 locale 的严格换行及段落双向分析；HarfBuzz 按文字系统、
方向和字体整形；FreeType 读取明确提供的轮廓字体，以灰度覆盖率绘制，不作 LCD 子像素处理。
依赖下限为 FreeType 2.10、HarfBuzz 2.8、ICU 70。目前使用构建环境的库，不等于已锁定
发布包依赖；不同字体／库版本之间不承诺逐像素一致。

API 依据：[ICU 分段](https://unicode-org.github.io/icu/userguide/boundaryanalysis/)、
[ICU 双向文本](https://unicode-org.github.io/icu-docs/apidoc/dev/icu4c/ubidi_8h.html)、
[HarfBuzz buffer](https://harfbuzz.github.io/harfbuzz-hb-buffer.html)、
[HarfBuzz/FreeType](https://harfbuzz.github.io/harfbuzz-hb-ft.html)、
[FreeType glyph](https://freetype.org/freetype2/docs/reference/ft2-glyph_retrieval.html)。

## 不变条件

所有偏移是 UTF-16 code unit，字素边界为 exclusive end；不进行 Unicode 正规化。
CRLF 整体消费，显式换行与空行不丢字。常规换行采用 ICU 合法断点，没有可用断点的
长词才应急断行；单个字素仍比行宽大时保证进度并明确标记 `overflow`，不拆代理对或
组合序列，也不谎称该行已经放入宽度。

字素和字形不是一一对应。布局保留整形后的字形、源范围和位置；逐字显示不重新整形前缀。
非字素边界的 reveal 向下取整；连字覆盖多个字素时，等全部源范围可见后再绘制，不能提前
显示后面的字符。排版／绘制明确拒绝不支持的情况，而非静默生成缺字方框。

字体按调用者提供的顺序、以整个字素为单位回退。布局持有字体文件的自有字节；销毁 FontSet、
删除原字体文件、创建其他语言或字号的布局，不改变旧布局。同一集合内可变 FreeType／
HarfBuzz 状态由互斥锁保护。没有隐式系统字体搜索、当前目录推断或运行时网络下载。

像素为紧密顶向下 BGRA8，覆盖率和颜色 alpha 预乘后作 source-over 合成，与 P2b 输入类型
相同。绘制按所选行从给定原点开始，按 surface 裁剪，支持小数原点与缩放，不增加 sRGB
转换。UTF-16 长度、字体大小、尺寸、页高、坐标、字体文件和总字体内存均有边界检查。

## Reader 适配

`src/host/dialogue_layout_adapter.hpp` 的 `reader_layout()` 只把行／页范围复制到原有
`dialogue::Layout`。测试把真实排版交给原 Reader，检查分页确认、按住按键、字素显示、
历史、语言切换和字号变化；没有复制或改写 Reader 状态机。

适配返回值不包含可绘制字形。正式接线时，Frame 还必须持有对应的 TextLayout／字体快照，
不可拿旧 Reader 范围配新字体重排。这属于下一批场景适配工作。

## 独立验证

安装上述开发库后运行；Python 和 Git 仅用于开发／CI 准备测试字体：

```sh
python tests/portable_text/prepare_fonts.py build/text-fonts
cmake -S tests/portable_text -B build/portable-text -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DSRW64_TEST_CJK_FONT="$PWD/build/text-fonts/NotoSansCJKsc-Regular.otf" \
  -DSRW64_TEST_ARABIC_FONT="$PWD/build/text-fonts/NotoSansArabic-Black.ttf"
cmake --build build/portable-text --config RelWithDebInfo --parallel 2
ctest --test-dir build/portable-text -C RelWithDebInfo --output-on-failure --verbose
```

字体来自固定提交的 Noto 上游 Git 对象：检查提交、树中的 blob 身份、文件大小及取出的
字节，不使用浮动分支或信任原始文件 CDN。字体只放在构建目录，不提交仓库，不作为
Actions artifact 上传，也不作为玩家发行字体。可显式提供自己的本地字体，但测试需要
中日英、组合字符、阿拉伯字母与必需连字覆盖。

`.github/workflows/portable-text.yml` 在 Linux、macOS、Windows UCRT64/GCC 上构建执行；
Linux 另有 AddressSanitizer／UndefinedBehaviorSanitizer。Windows 此项不是 MSVC 发布验证，
其他工作流的 MSVC 结果不能代替本组件的 MSVC 验证。结果以对应提交的 CI 为准。

覆盖扩展字素（含 emoji ZWJ／旗帜／肤色的分段，但不宣称绘制彩色 emoji）、混合中日英、
阿拉伯方向与字体回退、连字逐字显示、窄行与长文本、空帧、透明度／通道顺序、裁剪缩放、
删除字体文件后的旧布局、并发重排／绘制、真实 Reader 适配和异常输入。

## 尚未完成

完整对白场景（双框、人名、阅读指示器、回看）、`rasterize_frame` 替换、语言目录的
字体配置、发行字体与依赖打包、CoreText 场景对照、真实 ROM 验收均在本批之外。
竖排、彩色／位图字体、制表符及任意脚本的全面排版一致性不在支持范围。
分段函数保留 NUL；文字绘制拒绝 NUL 和未实现控制字符，不静默截断。

未开放 Windows/Linux 完整游戏 gate，未默认开启 `SRW64_PLUME_DIALOGUE`。
