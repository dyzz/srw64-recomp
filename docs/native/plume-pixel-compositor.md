# 通用 Plume 像素合成（P2b）

2026-09-20。接续 [CPU／GPU 后端拆分](shared-game-ui.md)。本批新增的是可独立测试的
像素上传与 GPU 合成器，不是三平台完整游戏，也没有替换 CoreText 排版器。

## 责任边界

`src/native/presentation/pixel_compositor.*` 只使用 Plume 公共接口；输入为
`Bgra8Surface` 的自有像素，输出为录制到调用者 command list 的绘制命令。
它不创建窗口、不提交队列、不等待 GPU，也不读取语言目录、ROM 或游戏状态。
`cmake/PixelCompositor.cmake` 由游戏和独立测试共用；同一对 HLSL 分别编译为
SPIR-V、DXIL 和 Metal。生成工具只用于构建，不放入玩家启动链。

像素约定保持不变：紧密顶向下 BGRA8、预乘 alpha，目标为单采样 BGRA8/RGBA8 UNORM。
上传按照 256 字节行对齐进行复制；着色器按整数 fragment 坐标读取，不增加滤波、缩放或
sRGB 转换。颜色混合使用 ONE / ONE_MINUS_SRC_ALPHA，不能再次乘源 alpha。
未知格式、HDR、尺寸不一致、空图像和其他 compositor 的图像会被拒绝。

每次上传创建独立纹理、staging buffer 和 descriptor，不能更新仍在 GPU 使用的纹理。
`Image` 与 `draw()` 返回的 `Retention` 保持这些资源及其 pipeline/layout/shader 存活。
调用者必须保留到相应 GPU 完成；单独录制上传但没有绘制，也必须保留 Image。
图像只用于同一有序 graphics queue，依赖上传的 command list 必须先提交；若放弃上传列表，
相应图像必须丢弃，不能继续当作可用缓存。RenderDevice 的销毁必须晚于所有在途资源。

## 游戏接线

`src/host/macos/dialogue_plume.cpp` 是当前 Metal 宿主的薄适配层：查找匹配 workload 的
不可变对白帧、使用已有 CPU raster、管理缓存，并把资源引用挂到 command buffer 的
completion handler。纹理上传和绘制本身不再使用 Metal 专属 pipeline。
目标 attachment 查询和 completion 仍有 Metal 类型，属于后续窗口／呈现后端迁移。

新路径由 CMake 选项 `SRW64_PLUME_DIALOGUE` 控制，**默认 OFF**。现有
`macos/dialogue_metal.cpp` 保留为游戏回归基准；两者不会同时链接。
CPU 排版、阅读器、分页、回看、语言快照与存档不在本批改动范围。

在已有本地 ROM／完整开发依赖的环境中，用独立构建和用户目录试验：

```sh
make host
cmake -S src/host -B build/recomp/gfx-plume-build \
  -DSRW64_ENABLE_RT64=ON -DSRW64_PLUME_DIALOGUE=ON \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build/recomp/gfx-plume-build --target srw64-gfx-host --parallel 6
./build/recomp/gfx-plume-build/srw64-gfx-host --play \
  --rom "$PWD/rom.z64" --user-dir "$PWD/build/recomp/plume-play" --new-game
```

不要用新游戏试验目录覆盖玩家数据目录。上述游戏流程需要实机验证，不从组件 CI 推断通过。

## 独立验证

`tests/pixel_compositor/` 不链接 SDL、游戏代码或 CoreText；使用合成的不对称像素图，
比较 GPU 实际绘制、fence 完成和回读后的结果与 CPU 预乘混合公式。覆盖 1×1、3×5、
65×17、321×241、800×600、1100×760，BGRA/RGBA 目标切换，两层混合及透明／半透明／不透明
像素，共 36 次回读；允许每通道 1 个量化单位的舍入误差。

测试还会在提交前释放 compositor 和缓存，只保留 completion 引用；绘制旧图像、新图像和
再次使用旧图像，检查资源提前释放、内容被覆盖以及完成后引用泄漏。无效参数检查继续保留。

Windows 的 `SRW64_PIXEL_TEST_WARP` 仅用于独立测试：校验固定 Plume 源码摘要，在
构建目录生成允许软件适配器枚举、增强失败诊断的副本，不改依赖 checkout 或游戏后端。
WARP 和 Linux Mesa 软件 Vulkan 的结果是实际 API／驱动执行证据，不是物理 GPU 性能或
厂商驱动覆盖。macOS CI 虚拟 GPU 的支持限制必须单独记录，不能把编译或跳过当作像素验收。

```sh
cmake -S tests/pixel_compositor -B build/pixel-gpu \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DSRW64_RT64_HEADERS="$PWD/build/recomp/upstream/RT64"
cmake --build build/pixel-gpu --config RelWithDebInfo --parallel 2
ctest --test-dir build/pixel-gpu -C RelWithDebInfo --output-on-failure --verbose
```

结果以对应提交的 `Pixel compositor GPU` workflow 为准；合成回读不替代真实游戏的 GPU
workload／窗口缩放／语言切换／退出回归。当前仍需跨平台文字排版、窗口 surface、截图回读、
marker、OS 输入法与三平台游戏冷启动验收。没有发布游戏、ROM 或字体附件。
