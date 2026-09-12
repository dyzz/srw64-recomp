# Original 模式的 HD 资源回退

日期：2026-09-12。属于本项目内置呈现模块；没有新增外部 MOD 接口。

## 当前行为

| 配置与资源 | 启动行为 |
| --- | --- |
| Original，HD 图片与姓名头像资源齐全 | 校验并准备 HD 资源，F6 可切换图片及配置中的 5600 模型 |
| Original，HD 清单、图片或姓名头像文件缺失 | 使用 ROM 图片和八张原始开场头像；保留所选语言、字体和 UI；本次运行禁用 HD 切换，不准备水滴模型 |
| HD，必要的 HD 文件缺失 | 启动报错，需补齐资源或明确改用 Original |
| 图片/头像摘要不符、清单非法、路径越界 | 报错，不以“缺少资源”为由忽略损坏 |

启动日志说明缺失路径；窗口标题显示 `Original | HD unavailable`，F6 与自动验证的 HD 请求均不能启用未准备的内容。资源恢复后重新启动即可重新取得切换能力；当前不支持运行中安装/重新加载资源。

这个回退仅针对可选 HD 美术文件。原始 ROM、语言目录、字形映射、工具链等基础输入仍然必须有效。水滴模型在完整 HD 美术可用且配置启用时仍按原有流程生成/验证；已有模型损坏不属于本次回退范围。

```sh
.venv/bin/python tools/recomp/play_native.py \
  --profile config/recomp/play-profile.json --language zh-Hans --images original --new-game
```

Original 表示原始美术；中文、原生姓名页及独立字体仍可使用，不意味着恢复全部 N64 UI。语言仍在启动时选择，全游戏多语种覆盖与运行中语言切换尚未完成。

## 实现边界

- `profile.py` 只捕获 HD 准备中的 `FileNotFoundError`；Original 记录 `hd_available: false` 与原因，HD 则继续报错。已编译但随后发现头像缺失的图片输出不会被宿主加载。
- `name_assets.py` 在写出前校验 HD 头像；ROM-only 提取不读取 HD 头像清单。八张原图仍校验 ROM 表与图像形状。
- `run_host_probe.py` 在内容可用性明确后准备模型；无 HD 时不传图片包/模型路径，并显式传递 HD 不可用状态。
- `ImageMode` 明确区分未配置、仅 Original、可切换三种状态。窗口标题与运行记录反映实际能力。

## 验证

`make check`：115 项 Python 测试通过，其中新增 7 项覆盖缺失包、缺失头像、显式 HD 失败、非法清单失败、语言/字体保留、ROM-only 头像提取及头像损坏。

`make recomp-native-check`：9 个原生组件程序通过；图像模式用例确认仅 Original 时请求 HD 和 toggle 都不能改变模式。原生组件通过不等同于全游戏运行验收。

实际运行与原始证据放在 `build/recomp/original-fallback-check/`，使用新配置指向不存在的美术清单；没有移动或删除现有 HD 文件，没有读取或覆盖玩家存档。

- `missing-zh`：中文 Original 冷启动，VI 1270 打开现代姓名页；记录 HD 不可用、原模型、无图片包；VI 2400 正常退出，4 个游戏线程全部回收。VI 780 左右投递 HD 请求后 GPU 帧仍记录 Original。此轮没有捕获 Cocoa 姓名页窗口截图，GPU 截图不包含该页叠层。
- `intact-ja`：首次日文配置切换观测，Original→HD→Original 的模型模式均正确，但首次快照前姓名字段已经出现 U+0010；来源未确定，不能作为默认姓名完整性验收。恢复默认后的补拍遇到 VI 上限退出，保留失败记录。
- `intact-ja-final`：使用相同二进制的独立重测通过。日文姓名页三次切换为 Original→HD→Original，头像视觉切换并恢复，姓名/姓氏/昵称均保持 `マナミ` / `ハミル` / `マナミ`；VI 1404 请求退出，宿主退出码 0，4 个游戏线程全部回收。最终汇总见 `build/recomp/original-fallback-check/verification.json` 与本轮 `roundtrip-verification.json`。

本轮没有验证全游戏 HD 覆盖、所有路线、存档恢复或完整游戏状态/RNG 等价；姓名页模型状态仅证明模式选择，不能替代世界地图模型的视觉验收。既有世界地图切换证据见[内容架构](native-content-foundation.md)。
