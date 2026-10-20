# 漩涡天空 6107 与叠加层 6106 的宽图

> **结论（2026-10-20）：外扩宽图未采用。** 实机看两侧仍有拼接感，改为宽屏下把静止天空（kind 8）
> 整张放大到铺满屏幕宽度、上下裁切（16:9 放大约 1.33 倍、上下各裁约 1/6；16:10 约 1.25 倍），
> 两层用同一比例，左右不再拼接。这样只用 HD 包里原有的 4:3 图，HD 包保持 1.1，旧包玩家更新程序即可。
> `content/art/stage1-hd.json` 的背景集已退回 `whole-v4`；`whole-v5`、`whole-v6` 和生成记录只留在本地 assets/ 备查。
> 下文是外扩方案的过程记录，宿主那一节描述的整块宽图绘制已删除（放大方案在宽屏下总是先命中）。

6107/6213 与其叠加层 6106/6212 都是静止的 kind 8 天空，`80085328` 将横竖速度置零。
两层的左右边缘都不连续，横向平铺会在原 4:3 边界产生内容分界。
背景类 `bg-20-065`（sky 8 6107）同时使用这两层；6106 实际包含亮核与旋臂，不能只当作零散星点。
只外扩 6107 的隔离截图已经亮度连续，但完整场景仍受 6106 平铺分界影响，因此两层一起外扩。
6097 横纹与其余图片保持原样。

- `whole-v5` 以 `whole-v4` 为底，只替换 6107/6213。
- `whole-v6` 以 `whole-v5` 为底，再替换 6106/6212，是 `content/art/stage1-hd.json` 引用的最终图集。
- 两个版本中未替换的图片均使用硬链接。素材遵循 `assets/README.md` 的本地资源约定，不随源码提交。

源图是 `assets/hd-ai/battle-skies/v1/` 下的两张 1280×960 RGBA。
每张外扩画布 1708×960，中间原图占 `[214,1494)` 列。
给模型看的输入将透明像素垫黑；最终图中间 1280 列的 RGBA 与各自源图逐字节相同。
6107 新增两侧全不透明；6106 新增两侧保留模型生成的实际 alpha，透出下层漩涡。
两行清单都记录 `size: [1708,960]`、`extent: [-53.333333333333336,0,373.3333333333333,240]`。

## 重跑

使用带 Pillow、numpy 的 ESRGAN Python 环境准备输入：

```sh
build/esrgan-venv/bin/python -m tools.hd_ai.battle_sky_hd wide-prepare \
  --output build/vortex-input
build/esrgan-venv/bin/python -m tools.hd_ai.battle_sky_hd wide-prepare \
  --source assets/hd-ai/battle-skies/v1/sky-6106-6212.png --output build/galaxy-input
```

用内置 `image_gen` 编辑各目录的 `input.png`。
6107 使用 `prompt.txt` 中的提示词；6106 改用生成记录目录内的 `prompt.txt`，请求透明背景，
强调只在两侧延续亮蓝旋臂、亮点与透明空隙，不填不透明深色星云。
生成记录分别保存在 `assets/hd-ai/battle-skies/vortex-wide-v1/` 与 `galaxy-wide-v1/`，
每层只使用一个模型候选。接口未返回具体模型 ID，记录为内置 `image_gen`，不猜测版本。
生成和拼接分开，重跑拼接不会重新调用模型。

```sh
build/esrgan-venv/bin/python -m tools.hd_ai.battle_sky_hd wide-compose \
  --generated assets/hd-ai/battle-skies/vortex-wide-v1/generated.png \
  --base assets/hd-ai/backgrounds/whole-v4 \
  --output assets/hd-ai/backgrounds/whole-v5 --feather 48
build/esrgan-venv/bin/python -m tools.hd_ai.battle_sky_hd wide-compose \
  --source assets/hd-ai/battle-skies/v1/sky-6106-6212.png \
  --generated assets/hd-ai/battle-skies/galaxy-wide-v1/generated.png \
  --base assets/hd-ai/backgrounds/whole-v5 \
  --output assets/hd-ai/backgrounds/whole-v6 --feather 48 --transparent-margins
```

输出目录必须不存在。脚本先对中心做粗到细的缩放、平移配准，再按不透明中心像素拟合每通道亮度。
生成图配准后越界的几像素用边缘延展，避免黑框。
接缝残差只在新增两侧羽化：低频用 48 像素余弦衰减，高频用 3 像素指数衰减，避免把边缘细节拖成长横线。
6106 同样羽化 alpha，并将新增颜色投到原图不透明像素的青蓝至白色渐变，消除模型 alpha 边缘的杂色。
最后原样贴回源图，包括透明像素。
源图、生成图和输出 SHA-256、画布、模型说明、配准结果、亮度参数和羽化参数
写入生成图旁的 `recipe.json`。若生成图旁有 `prompt.txt`，按原文记录该提示词。
更换模型候选后应重新做目视验收。
将 `whole-v6/backgrounds.json` 的 SHA-256 填入活动美术配置的 `backgrounds.manifest_sha256`。

## 宿主与验证

（已改）`native_background.cpp` 现在对 kind 8 在宽屏下一律放大铺满：以画面中心为支点，
放大到画面宽度，只取图的 4:3 部分；其他漂移天空继续用 `wrap_quads`。
HD 首层下面的原版不透明底图用独立的 4:3 UV；宽图透明孔洞不会采错位置。
4:3 模式按整数画布的中央原尺寸区域采样，消除 1708 像素与理想 16:9 宽度取整造成的微小偏移。

```sh
SRW64_DEV_HIDE_BATTLE_SPRITES=1 SRW64_DEV_HIDE_BATTLE_HUD=1 \
  .venv/bin/python tools/recomp/debug/check_battle_backgrounds.py build/vortex-16x9 \
  --window 1280 720 --only bg-20-065
```

再用 `--window 1280 800` 核对 16:10，用 `--aspect 4:3` 核对 4:3。
`--only` 限定一个真实背景类，无需复制其他类的截图来跳过它们。
两张截图都要目视检查；在 2560×1440 截图上比较 319–320、2239–2240 列及邻域亮度。
4:3 的原版遮幅和 HUD 仍可能显示（开发隐藏开关作用于宽屏绘制路径）。
静态核对包括两张中央 RGBA 完全一致、6107 新增两侧全不透明、6106 新增两侧保留透明空隙、
`whole-v4` 至 `whole-v6` 仅两行清单改变、其他文件硬链接保持。
两张都实际用 `compress_backgrounds` 确认输出 JPEG 和逐字节相同的 `.alpha.png`，保留 extent。

## 2026-10-20 本机验证记录

- `srw64-gfx-host` 目标编译通过；HD 发布压缩测试 9 项、文档测试 3 项通过。
- 两层源图中央 RGBA 完全相同；两份图集重跑后图片与清单 SHA-256 相同。
  `whole-v4` 至 `whole-v6` 只有 6106/6212、6107/6213 两行改变。
- 两层各自实际压缩成 JPEG＋alpha PNG，alpha 逐字节相同，尺寸与 extent 保留。
- `bg-20-065` 的 1280×720、1280×800、4:3 各取 a/b 两张实机截图。
  宽屏截图无黑边；4:3 保留原有遮幅与 HUD。
  与 `whole-v4` 比较，4:3 截图的静止背景区域 `(1350,350)–(1700,500)` 像素差为零；
  整幅截图因小行星位置、演出时序不同，不能据此宣称整幅逐像素相同。

下表为 a 截图中相邻两列逐像素亮度绝对差的均值（0–255）。
“仅 6107”来自 `whole-v5`，“两层”来自 `whole-v6`；包含实际场景中的 3D 小行星。

| 窗口 | 截图列 | 仅 6107 | 两层 |
| --- | --- | ---: | ---: |
| 1280×720 | 319–320 | 4.19 | 1.76 |
| 1280×720 | 2239–2240 | 4.54 | 1.73 |
| 1280×800 | 212–213 | 9.75 | 2.73 |
| 1280×800 | 2346–2347 | 10.24 | 2.17 |

本地证据在 `build/sky-wide/`：`both-16x9/`、`both-16x10/`、`both-4x3/` 为最终截图，
`final-column-checks.json` 为 a/b 列统计，`static-checks.json`、`static-6106.json` 为实际素材与压缩校验。
这组数值证明边界亮度突变显著降低；材质与旋臂接续效果以原尺寸截图目视复核。
