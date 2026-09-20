# macOS 本地兼容构建

发行构建目标为 Apple Silicon、macOS 14.0。这个目标必须用于宿主和所有运行时依赖；
不能靠降低 Info.plist 或改写现成 dylib 的版本字段取得兼容性。
在新系统构建、检查 Mach-O、运行游戏，只能证明构建和当前机器的运行结果；
macOS 14/15 的实际启动与游戏流程仍需在对应系统验收。

## 依赖

`config/recomp/macos-dependencies.json` 固定 SDL3、SDL2-compat、FreeType、HarfBuzz、ICU
的源码 URL 和 SHA-256。`tools/release/build_macos_dependencies.py` 下载校验后从源码构建，
安装到 `build/macos-deps/14.0-arm64/prefix`，不改 Homebrew 或系统目录。
构建工具仍可来自 Homebrew；运行时库不链接 Homebrew 路径。

保留现有 SDL2 API → SDL2-compat → SDL3 路径。文字使用 FreeType + HarfBuzz 的 OpenType
排版和 ICU 分段；关闭 HarfBuzz CoreText、GLib、Graphite2、辅助工具以及 FreeType
PNG/Brotli/BZip2 等当前 TTF/TTC 文字不使用的依赖。字体文件仍由运行时选择，不随包分发。

准备好原项目的固定工具链与生成代码后运行：

```sh
.venv/bin/python tools/release/build_macos_dependencies.py --jobs 8

release_deps="$PWD/build/macos-deps/14.0-arm64/prefix"
cmake -S src/host -B build/recomp/macos14-app-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_PREFIX_PATH="$release_deps" \
  '-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local' \
  -DSDL2_DIR="$release_deps/lib/cmake/SDL2" \
  -DICU_ROOT="$release_deps" \
  -Dharfbuzz_DIR="$release_deps/lib/cmake/harfbuzz" \
  -DSRW64_ENABLE_RT64=ON -DSRW64_METAL_SOURCE_SHADERS=ON \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build/recomp/macos14-app-build --target srw64-gfx-host --parallel 8

.venv/bin/python tools/release/package_macos.py \
  --binary build/recomp/macos14-app-build/srw64-gfx-host \
  --output "dist/SRW64-macos14-arm64/SRW64 Recompiled.app" \
  --minimum-macos 14.0 --search-dir "$release_deps/lib" \
  --runtime-library "$release_deps/lib/libSDL3.dylib"
```

使用新的构建目录，避免复用含 Homebrew 库路径的 CMake 缓存。打包器拒绝覆盖已有输出；
重复打包时换一个输出目录。SDL3 由兼容层动态加载，必须显式传给打包器。
依赖构建报告在 `build/macos-deps/14.0-arm64/dependencies.json`；打包器再次检查所有
Mach-O 的最低系统版本、依赖路径和签名。整个过程仅本地运行，不使用 GitHub Actions。

产物仅本地 ad-hoc 签名，没有 Developer ID 公证。对外分发前应携带各依赖的许可说明。
现有最低 27 的旧包来自本机 Homebrew 二进制的部署目标，不是 SDL 或文字组件的源码要求。

## 本地验证（2026-09-20）

`build/recomp/macos14-game-01/bundle-verification.json` 记录新包的 8 个 Mach-O
（主程序和 7 个 dylib）全部声明最低 14.0。从 `/tmp`、精简 PATH 启动真实游戏，
动态加载日志没有 Homebrew 或依赖构建目录，SDL3 来自应用包，退出码为 0，签名检查通过。
同目录的共享 UI 验证覆盖应用菜单、快捷键、姓名写回和 Retina 窗口缩放；对白验证覆盖
中日英、字号、历史与翻页，共 18 张 GPU 截图。新依赖对应的 4 项文字/对白 C++ 检查通过。

以上运行机器是 macOS 27；不能据此宣称 macOS 14/15 已经完成实机验收。
