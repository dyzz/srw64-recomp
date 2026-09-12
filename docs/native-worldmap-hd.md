# 对话世界地图高清资源

2026-09-09。当前目标为“同样至关重要。别人拿不到的情报，”这段对话背后的世界地图。用户明确以游戏截图纠正了范围：战斗棋盘的森林、道路 tile 实验不属于本次交付，也没有加入当前高清试玩包。

后续更新：高清入口的标准剧情对白已接入[实时苹方 UI](native-dialogue-ui.md)，提供自动换行、分页、回看和速度控制。当前 profile 只读取允许清单中的纯美术纹理；历史合并包中的中文字形不进入原生美术包。

## 资源与渲染路径

原始资源为 **5604**，完整解压数据唯一匹配捕获内存 `0x2CF200`。从原显示列表的顶点和 UV 还原地图，再取欧洲区域，得到 512×512 源图。原地图采用 64×64 CI4 图块；512×512 是旧素材尺寸，不是 native recomp 或 RT64 的上限。

[`build_worldmap_runtime_pack.py`](../tools/hd_ai/build_worldmap_runtime_pack.py) 将高清地图按原坐标切回 **512×512** 图块，即原纹理的 **8 倍线性密度**，对应 **4096×4096** 地图画布。游戏仍提交原来的地图 mesh、相机和标记，RT64 用已验证的纹理哈希选择高清资源。地图没有被重新压回 CI4，也没有被制作成包含人物和文字的整屏覆盖图。

[`graphics.cpp`](../tools/recomp/native-host/graphics.cpp) 读取包内 `srw64-worldmap-hd.json`，在纹理缓存的锁内检查实际替换尺寸和 UV 倍率，并将结果写到运行目录的 `worldmap-texture-runtime.json`。这是 GPU tile 构建器读取的同一组数据；检查不会修改纹理的访问记录或游戏 RDRAM。已在同任务回放中确认 57 / 57 个图块的原始尺寸为 64×64、替换尺寸为 512×512、坐标倍率为 8×8。

RT64 的 `TextureSampler.hlsli` 对替换纹理使用实际 `tcScale`；其低精度原机 UV 量化不作用于高清替换。常规纹理过滤仍存在，内部渲染倍率与素材像素密度是两个独立设置。提高内部渲染倍率本身不能补出旧素材没有的细节。

## AI 素材与原始几何

使用内置 image_gen 编辑实际资源图，参考用户提供的背景画风。没有把人物、窗口或黄色标记画进地图资产。

- 整图候选：`build/hd-ai/worldmap-runtime/ai-v1/`，实际模型输出 **1254×1254**。
- 欧洲细节候选：`ai-detail-v1/`，源区域是原图 `(0,64)-(320,256)`，实际输出 **1619×971**。单独细化镜头区域，以获得更高的有效细节密度和较小的山峰尺度。
- 定位校正：`ai-detail-registered-v2/`，用原地图的陆海及地形色区约束平滑坐标场，重采样生成图原始像素。记录原始输出、校正脚本、坐标场、库版本和前后陆地掩码重合指标。校正后的原始图并不等于精确的原作几何，最终仍通过源图保护层限制编辑区域。
- 合成时保留原海面、海岸窄边及裁块边界；禁止生成的海面像素在原陆地上新增水域。Alpha 从原 RGBA16 调色板取得，先在整图上放大，再切片。

4096×4096 是**最终运行纹理尺寸**，并不表示模型直接输出了原生 4K 图。现有细节来自上述冻结的 AI 输出。原图保护层及游戏的投影也会影响局部边缘观感。

57 个已核验地图哈希被替换；有邻接歧义的共享哈希继续使用原包。欧洲裁块之外保留既有资源。本次没有宣称全部世界地图或战斗地图均已完成高清化。

## 重建与验证

最终合并素材包为 `build/hd-ai/worldmap-runtime/pack-v6/pack`。重建到新目录：

```sh
.venv/bin/python -m tools.hd_ai.build_worldmap_runtime_pack \
  --ai build/hd-ai/worldmap-runtime/ai-v1 \
  --detail build/hd-ai/worldmap-runtime/ai-detail-registered-v2 \
  --fonts build/hd-ai/worldmap-runtime/pingfang-fonts-v7 \
  --scale 8 --output build/hd-ai/worldmap-runtime/rebuild
```

合并包共有 5,018 个纹理条目：57 个地图条目及 4,842 个字体条目更新，119 个其他条目的资源逐字节相同。银蓝色窗口、人物头像与蓝色人名继续工作。

当前原 ROM 入口：

```sh
.venv/bin/python tools/recomp/play_native.py \
  --profile config/recomp/play-profile.json --new-game --mute
```

以下保留历史渲染验证记录，不能视为清理后的重新验收。

该命令从实际 ROM 启动并运行 CPU／游戏逻辑，使用独立输出与存档目录；GPU 完成后回读截图。它与同任务 replay 分开记录。验收结果以 `live-v6/report.json`、`worldmap-texture-runtime.json`、实际截图及 `acceptance.json` 为准。

最终 `live-v6` 完成 **7,200 VI，exit 0**。实际纹理缓存验证 **57 / 57**，均为 512×512、UV 倍率 8。已查看 [目标对话的实际 GPU 截图](../build/hd-ai/worldmap-runtime/live-v6/present-3540.png)，两个姓名区域分别有 896 和 755 个精确蓝色实心像素。2,023 个不同字符的检查未发现缺字或上下裁切；43 项 Python 测试、编译与依赖检查通过。

当前 `Play SRW64 Native.command` 通过纯美术允许清单读取该包。当前美术为接近参考方向的首版，仍保留海岸保护区的柔化，山体与树冠细节也不等同于参考图；运行管线已确认支持更高密度素材。详见 [验收记录](../build/hd-ai/worldmap-runtime/acceptance.json)。
