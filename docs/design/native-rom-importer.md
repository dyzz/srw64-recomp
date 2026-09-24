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

本批只覆盖 Original 呈现，不迁移 HD 包；旧 Python 试玩入口的 HD 功能保留。桌面文件选择及 `.app` 暂存/依赖收集已加入，见下节；Developer ID 公证、真实游戏和干净机器验收仍未完成。Metal/CoreText/AppKit 解耦、Win/Linux 图形、IME 和手柄仍是后续阶段。

## P1b：macOS 桌面入口与应用打包

P0/P1 已通过 PR #2 合入主分支。桌面入口在现有图形宿主中运行，不再加一层
Python 启动器或子进程。无参数启动使用系统 ROM 选择框；下次读取用户目录里的
`last-rom.txt` 并重新校验 ROM。按住 Option 启动，或以 `--choose-rom` 启动，
可以重新选择 ROM。`--play` 与旧 positional probe 参数不弹窗口，继续用于自动化。

选择框使用 AppKit `NSOpenPanel`；错误使用 `NSAlert`。不接管 SDL 的 application delegate，
不另开 GUI 线程。只有在原生 bootstrap 校验内容及存档、持有 Session 锁之后，才原子保存
选中的 ROM 路径。取消不创建用户目录；坏 ROM 可以重选；缓存、存档或游戏错误只报错退出，
不在同一进程中重启已初始化的游戏，不静默新开存档。应用包内保持只读。

代码位于 `src/native/app/desktop.cpp`、`src/host/macos/desktop_macos.mm`；前者无 GUI 依赖，
后者是独立 Cocoa adapter。Windows/Linux 只测试共用控制逻辑，本批没有那两个系统的 GUI。

### 开发者生成本地应用包

先按原来的 `make` 完成游戏构建，再显式指定最低 macOS 目标并重新构建：

```sh
cmake -S src/host -B build/recomp/gfx-build -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build build/recomp/gfx-build --target srw64-gfx-host --parallel 6
.venv/bin/python tools/release/package_macos.py \
  --binary build/recomp/gfx-build/srw64-gfx-host \
  --output "dist/SRW64 Recompiled.app" --minimum-macos 14.0
```

输出目录必须不存在。`tools/release/package_macos.py` 是本地开发打包工具，不随应用分发；
它显式复制 executable、链接依赖及通过 `--license-file` 指定的纯文本许可，不扫描或复制
整个仓库、ROM、存档、导入缓存或字体。生成 `Info.plist`，调用 CMake BundleUtilities
收集并重定位依赖，去掉构建机 RPATH，再验证依赖与最低系统版本，最后由内向外签名。

最低 macOS 版本是可核验的打包约束，不是仅修改 plist 的兼容性声明。任何 Mach-O slice
或依赖声明的最低版本高于 `--minimum-macos` 都会失败；需要重编译依赖或选择更高且经过
实际验证的最低版本。失败不会留下已发布的半包，也不会覆盖旧 `.app`。

默认使用 ad-hoc 签名，只用于本地测试，不等于 Developer ID，不等于 Gatekeeper 放行。
`--sign-identity` 可以指定开发者自己的签名身份；脚本不获取凭证、不提交公证、不上传 release。
对外发布前仍要核对完整依赖许可证、签名/公证及硬化运行时要求。应用包带上打包字体
HarmonyOS Sans（附许可全文）与符号字体。

### 新增验收与剩余门槛

`tests/native_desktop.cpp` 测试取消、坏/丢失/Unicode ROM 路径、记住选择、失败不重启宿主、
错误存档不重置、真实 Session 锁及无关工作目录。`tests/test_macos_package.py` 测试文件白名单、
不覆盖、异常清理、版本约束、RPATH 和签名调用顺序。

macOS CI 编译真正的 Cocoa adapter，用不含游戏代码的 executable 加一个动态库测试打包。
`tests/check_macos_bundle.py` 隐藏原始二进制/动态库目录，将 `.app` 移到包含中文与空格的路径，
设为只读，在移除开发工具和 DYLD 覆盖变量的环境中运行并复核签名。它验证实际 Mach-O 依赖闭包，
不打开模态选择框，不运行 ROM、SDL 或 GPU，也不代表 Finder→导入→游戏已实机验收。

真实 macOS 验收还需：Finder 双击、取消、坏 ROM、首次导入、第二次启动、Option 重选、
F7、姓名输入、保存/退出/重启、应用搬移、无 Homebrew/Xcode 的干净机器。
现有 fail-fast `abort`/进程崩溃不保证能弹错误框，当前错误对话框覆盖可捕获的启动错误及返回码。
Metal/CoreText/AppKit 的游戏显示迁移仍属于下一阶段，本次没有宣布 Win/Linux 可玩。

参考：CMake 官方 [BundleUtilities](https://cmake.org/cmake/help/latest/module/BundleUtilities.html)，
Apple 官方 [公证常见问题](https://developer.apple.com/documentation/security/resolving-common-notarization-issues)。

### ICU 与动态加载库

打包器可重复传入 `--search-dir`，供 CMake 在构建机解析 ICU 这类库的 `@loader_path` 依赖。
SDL2-compat 通过 `dlopen` 加载 SDL3，不会在 SDL2 的普通链接依赖中出现；使用兼容层时必须
显式传入 `--runtime-library /path/to/libSDL3.dylib`。打包器按指定名字复制 Mach-O dylib，
再一并收集其依赖、核对最低系统版本、修正路径并签名；不会批量复制整个依赖目录。

以下是使用本机 Homebrew 库的历史构建示例。较低系统版本请使用
[macOS 本地兼容构建](../native/macos-release.md)，从固定源码重编宿主与运行时库。

```sh
.venv/bin/python tools/release/package_macos.py \
  --binary build/recomp/macos-app-build/srw64-gfx-host \
  --output "dist/SRW64-237b629-macos-arm64/SRW64 Recompiled.app" \
  --minimum-macos 27.0 \
  --search-dir /opt/homebrew/opt/icu4c/lib \
  --runtime-library /opt/homebrew/opt/sdl3/lib/libSDL3.dylib
```

2026-09-20 本机 SDL3 的 Mach-O 声明最低 macOS 27，即使宿主按 26 构建，整包也必须声明 27。
较旧系统的包需要匹配的依赖构建，不能只改 Info.plist。应用包仅做本地 ad-hoc 签名，未公证。
