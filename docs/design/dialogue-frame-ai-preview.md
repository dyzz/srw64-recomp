# 对话框 AI 外观预览

2026-09-09。按用户要求，用内置 image_gen 编辑修正 gamma 后的同一场景，生成一版银蓝色对话框设计。窄银色金属边框、蓝色内线、切角连接和深蓝灰色底板，沿用上下两组对白的位置。

- [设计预览](../../assets/hd-ai/dialogue-frame-ai/v1/dialogue-frame-preview.png)：1448×1086。
- [生成提示词](../../assets/hd-ai/dialogue-frame-ai/v1/prompt.txt)。
- [来源、哈希与范围记录](../../assets/hd-ai/dialogue-frame-ai/v1/manifest.json)。
- [输入的实际渲染图](../../build/recomp/coretext-probe/run-3/system-39/present-60.png)：960×720，Core Text / PingFang SC Medium 39px。

此图是 AI 外观预览，尚未接入游戏；它不是 GPU 截图，也不是可直接替换的透明边框纹理。生成过程会重新采样场景和字形，不能用此图验证文字后端或颜色一致性。游戏里的文字现在由跨平台文字引擎绘制，见[中日英跨平台文字与游戏对白](../native/portable-text.md)。

用户随后明确要求通过游戏资源和代码实现。后续[地图、边框高清与蓝色人名的实际接入](../native/native-dialogue-runtime-hd.md) 使用原始地图／边框资源和渲染代码，没有使用此整图预览作为运行资产。

实际接入应提取无字边框资产，采用可拉伸的九宫格边框与独立底板，并将 Core Text 文字在最终输出分辨率下单独合成。这样才能继续调整字号、换行和底板透明度。当前未修改普通游戏宿主的对话框资源。
