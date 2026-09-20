# 原生 ROM 首次导入（P1 导入阶段）

2026-09-20。接续 [P0 发布计划](cross-platform-release-plan.md)。本页更新其中“原生首次导入尚未实现”的状态：导入代码、启动接线和无 ROM 对照测试已加入；完整 macOS 应用分发仍未完成。图形宿主依然只支持 macOS，不代表 Win/Linux 已能运行游戏。

## 现在的入口

在此分支按原来的 `make` 构建后，直接运行：

```sh
./build/recomp/gfx-build/srw64-gfx-host --play \
  --rom "$PWD/rom.z64" --language zh-Hans
```

不再要求 `compile_profile.py`、`export_content.py` 或 `--content`。第一次由 C++ 从匹配的日版 Rev 0 ROM 提取内容，之后校验并复用缓存。`--content` 仍保留为开发者显式提供已有本地内容的路径，不移除 P0 的调试方式。

构建仍用 Python；首次导入和之后的运行不启动 Python、Git、CMake、Ninja 或重编译器。游戏 CLI 没有跳过 ROM 摘要、覆盖导入配置或接受任意基线的选项。

## 责任和资源边界

| 文件 | 责任 |
| --- | --- |
| `tools/release/build_import_spec.py` | 构建时读取已入库布局、字形映射和语言目录，校验来源锁并生成内嵌元数据；不读 ROM、不提取游戏资源 |
| `src/native/app/rom_import_codec.hpp` | 有界的文本表、LZ 环形缓冲、资源描述符、96/97 像素头像解码 |
| `src/native/app/portrait_png.hpp` | 小头像 RGBA8 PNG 输出；存储型 DEFLATE，不新增压缩库依赖 |
| `src/native/app/rom_import.cpp` | 同一 ROM 字节缓冲的身份校验、原文与译文绑定、头像生成、缓存校验与发布 |
| `src/native/app/launch.cpp` | 持有用户目录锁后导入，再使用原来的 native bootstrap；失败不更新存档指针 |
| `tests/test_native_import.py` | 与现有 Python 解码器、翻译校验器、Pillow 像素输出对照 |

程序内嵌的是布局、Unicode 字形映射、已入库译文与 UI 文案；原版日文记录、角色头像来自玩家本地 ROM。缓存含 ROM 派生内容，不作为公共发布资源上传。导入探针只用于测试，不随玩家程序分发。

头像输出的 PNG 压缩编码可以与 Pillow 不同；验收比较解码后的 RGBA 像素，不要求 PNG 文件摘要相同。内容清单中的摘要按各自实际输出计算。

## 缓存与失败处理

缓存目录位于用户数据目录的 `content-cache/`。键是 `(ROM SHA-256, importer version, embedded metadata SHA-256)` 的完整 SHA-256 指纹，避免 Windows 路径被两个长摘要挤满。更新字形、译文或导入器会使用新目录，不覆盖之前有效目录。

从读取、导入到游戏结束始终持有同一用户目录的 OS 文件锁。先在同一缓存父目录创建独立暂存目录，所有内容和 manifest 写完并验证后，再 rename 为正式目录。异常会清理本次暂存目录，不清空已有缓存或存档；进程被强杀留下的 `.tmp-` 目录不会被当作有效缓存。没有断电级事务持久性保证。

坏 ROM、越界描述符、LZ 截断、未知控制词、过期翻译、STOP/END 屏障或动态姓名参数被修改，都会停止导入。坏缓存明确报出目录，需在游戏关闭后只删除报错的缓存子目录，再启动重建。不要删除整个用户目录或 `sessions/`。

## 验证

公共三平台 CI 构建的是应用层、导入器和 fake host，不运行原游戏。合成 ROM 不含游戏代码或游戏图像，覆盖文本、参数、20 表元数据、LZ 模式、16 个头像位置、96/97 像素、缓存复用/失效、失败保留、首次启动与 SRAM 恢复。另一个用例移除 PATH 中的开发工具目录，验证 native bootstrap 无需调用外部工具。

真实 ROM 对照是单独的可选测试：

```sh
SRW64_TEST_ROM=/absolute/path/to/rom.z64 \
  ctest --test-dir build/native-app -C Release \
  -R native-import-oracle --output-on-failure
```

这会把全量原文、所有生效译文、字形映射、16 个头像像素与现有 Python 实现对照。公共 CI 没有 ROM，明确跳过这一项；合成用例通过不等于真实 ROM 或游戏流程已验收。

## 仍然缺少

本批只覆盖 Original 呈现，不迁移 HD 包；旧 Python 试玩入口的 HD 功能保留。文件选择器、双击启动界面、动态库收集、`.app` 签名/公证和干净机器验收尚未实现。Metal/CoreText/AppKit 解耦、Win/Linux 图形、IME 和手柄仍是后续阶段。
