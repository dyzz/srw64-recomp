# 流程与隐藏要素攻略

`srw64-flow-guide.html` 是面向玩家的离线单文件攻略，一个文件内含中日英三语：页签栏右侧切换语言，
切换时停在原处；默认语言按上次选择或浏览器语言，也可用 `?lang=zh-Hans|ja|en` 指定。

内容：流程图、隐藏要素，以及精神指令、驾驶员技能、机体能力、强化部件、恋爱补正、改造与继承、
追加武器、合体攻击、GB 联动等资料页。页面已内嵌样式与脚本，不依赖网络资源，直接用浏览器打开即可。
写法沿用姊妹项目 srwz-zh 的攻略页。

## 依据

所有内容来自本项目对日版 ROM 关卡脚本与代码的解析；社区攻略 Akurasu Wiki 只用于对比，不作依据。
正文用三种徽标标出对比结果：「与 Akurasu 不同」「Akurasu 未载」「未实机验证」（数据里写作
`{≠}`、`{+}`、`{?}`）。技术依据见：

- [隐藏要素、说服与路线分歧](../docs/gameplay/hidden-elements.md)
- [战斗计算](../docs/gameplay/battle-formulas.md)、[改造继承](../docs/gameplay/upgrade-inheritance.md)、[改造上限](../docs/gameplay/upgrade-limits.md)、[GB 联动](../docs/gameplay/link-battler.md)
- [关卡脚本指令](../docs/script/stage-script-exploration.md)

## 数据

`data/<语言>/` 下三个文件，各语言结构与 id 完全一致（中文为基准，`tests/test_guide.py` 检查）：

- `progression.json`：流程图关卡卡片（话数按脚本的关卡衔接推算）
- `hidden-elements.json`：隐藏要素、路线分歧，以及「说服与判定规则」
- `reference.json`：资料页签

人名、机体、武器、部件、精神指令、关卡名取自语言目录（`content/locales/`）；改了词条表后应复查这里的写法。

改数据后重新生成页面：

```bash
python3 tools/content/build_guide.py
```
