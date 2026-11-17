# MOD 上架：Thunderstore、GameBanana 与 RecompLauncher

2026-11-16 整理。方向见 `mod-packages.md` §13.0（11-11 定：Thunderstore 为主，不考虑国内玩家）。本文记录两个 MOD 站各自要求什么、我们的包能不能用、怎么开通和上传、RecompLauncher 目前支持到哪里。网站规则会变，标了日期的数字以当天核对为准。

## 1. 结论

- 两个站都能上，但都要先有我们自己的游戏页：Thunderstore 叫「社区」，要申请；GameBanana 叫「游戏页」，会员登录后提交 Add Game。目前两边都还没有。
- 同一个 zip 两个站都能传：`tools/mods/thunderstore.py` 生成的那个。
- 现在的 zip 是「`.srw64mod` 原样放在 zip 里」，已发布的 0.5.3 装不了，要等带 `bundled_packages` 的下个版本（§5）。
- RecompLauncher 的 MOD 页只认 Thunderstore 和 GameBanana，但它的装法会把 `.srw64mod` 丢掉，还要请维护者改（§4）。

## 2. Thunderstore

### 2.1 开通（只做一次）

1. **申请社区**：在 Thunderstore 的 Discord 频道 #game-requests 发帖（草稿见 §8）。wiki「Adding a new game」的要求是有开发者兴趣、最好已有可上传的 MOD，没有写数量门槛和审批时长。通过后由他们给 ecosystem-schema 仓库提 PR，合并后约 1 小时生效。背景图等由他们手动加。
2. **建 Team**：用 GitHub、Discord 或 Overwolf 登录 thunderstore.io 后创建。Team 名建好就不能改，会出现在包页面和别人的依赖串里（`Team-包名-版本`）。打包工具默认按 `Marchwind64` 起文件名。
3. **社区管理员**：申请时一并请求，这样能审核、下架本社区的包，也能写社区规则。
4. **AI Generated 分类**：其他新的 recomp 社区（Smash 64、Def Jam、Nuts & Bolts）都开了，申请时可以一起提（§7）。

r2modman、Gale、Thunderstore Mod Manager 都不支持 recomp 社区（ecosystem schema 里这些社区的 `r2modman` 是 `null`，Gale 的游戏表里也没有）。玩家用 Manual Download，和 Zelda64Recomp 的做法一样。

### 2.2 包格式（wiki「Creating a package」）

- zip 根目录直接放 `manifest.json`、`icon.png`、`README.md`，文件名区分大小写；压整个文件夹会多一层目录，包无效。`CHANGELOG.md` 可选。其余文件不限。
- `manifest.json` 五个键都必填：
  - `name`：只用字母、数字、下划线，最长 128。
  - `version_number`：三段数字，不带后缀。
  - `website_url`：没有就写空字符串。
  - `description`：最长 250 字符。
  - `dependencies`：`Team-包名-版本` 的列表，没有就写空列表。
- `icon.png`：PNG，正好 256×256。
- `README.md`：UTF-8，Thunderstore 的 Markdown 和 GitHub 的略有不同。
- 单包上限 10 GB（软上限，可申请提高）。

### 2.3 上传后改不了的

- 已发布的版本只能改 README 和 CHANGELOG，文件要改就发更高的版本号。站上总把版本号最大的当最新，不看上传时间。
- 改 `name` 等于新建一个包。
- 「Contains NSFW content」只有发新版本才能改，而且不会延续到下一版，每次上传都要重新选。
- 分类和社区随时可以在 Manage Package 里改。
- wiki 没写能不能删包；「弃用（deprecated）」状态是有的（RecompLauncher 列表会跳过弃用的包）。

### 2.4 内容规则（wiki「Global Rules」）

- 不能分发游戏文件，除非游戏开发商明确允许；转载别人的包或素材要有许可；带许可证的话用通用许可证。
- 禁止混淆代码、运行从外部下载的代码、自己更新自己、恶意软件、带开发或测试文件。
- 垃圾包会被删：没功能的包、和已有包完全相同的重传、主要为了打广告的包、把一个功能拆成很多份的包（钱 ×2、×3、×4 各发一个）、没测过的 AI 生成 MOD。
- 标题、描述、图标不能误导；README 要和当前功能一致，包要先测过再传。
- 有 AI 生成素材或代码的包，社区有「AI Generated」分类时要打上（README 和包图标一般不查）。
- 社区可以在全站规则之外加自己的规则。
- 上传后出现「Invalid submission」表示要人工审核，去 Discord 找管理员。

我们的包只放 JSON 数值、剧情脚本、名字表和自制图片，不含 ROM 内容，许可证 CC0-1.0，以上都符合。唯一要处理的是 AI 标记（§7）。

## 3. GameBanana

### 3.1 现状（11-16 用 apiv11 搜索）

没有机战 64 的游戏页，也没有 Marchwind64 的。recomp 都是单独开页，不挂在原版游戏下面：

| 游戏页 | 编号 | 建立时间 |
|---|---|---|
| MM Zelda 64: Recompiled | 21763 | 2025-01 |
| Unleashed Recompiled | 21975 | 2025-03 |
| Marathon Recompiled | 23176 | 2025-08 |
| Banjo: Recompiled | 24169 | 2026-01 |
| Zelda64: Recompiled Majora's Mask | 24851 | 2026-05 |
| Baku Bomberman (Bomberman 64): Recompiled | 24910 | 2026-05 |

Bomberman 64 那页开出来时只有 2 个 MOD，门槛看来不高。

### 3.2 开通

- 页面 `gamebanana.com/games/add`（「Add Game」），要注册会员登录后才能填。页面要 JavaScript 才能显示，没读到要填什么。
- 游戏页数据里有「已批准」标记（`_bIsApproved`），提交后应该要等管理员审核；审核标准和时长没找到官方说明。
- MOD 分类（如「Gameplay Modification」「Other/Misc」）是挂在游戏页下的，开页后再看谁能建。

### 3.3 上传限制

- 文件要是 zip、7z 或 rar，并且要通过它的病毒扫描（站规有「File Analysis Failure」一条）。`.srw64mod` 内部是 zip 但扩展名不是，直接传大概率被拒（未实测），所以传 Thunderstore 那个 zip。
- 站规有版权条：衍生自别人有版权的作品、又没有许可的内容不允许。站上大量粉丝 MOD 都属于这种情况，实际执行不严，但规则确实写着。
- 另有 AI Generated Content、Paywalled Content、Manual Review Required 等条目。

玩家下载 zip 拖进游戏即可；先解压了的话，拿到的 `.srw64mod` 也能拖进去装。

## 4. RecompLauncher 目前的支持

11-16 核对 0.90 beta 55（`elripalda/RecompLauncher` 86ab494）：管 MOD 的 `mods.rs`、`gamebanana.rs` 从 4d429c66 到这一版没有改动。

### 4.1 支持的游戏

写死在启动器里的 9 个；其他游戏要在服务端目录 `pc-catalog.json` 的条目里写 `mods`，而 11-16 拉下的 1802 个条目一个都没写。

| 游戏 | Thunderstore 社区 | GameBanana | 装法 |
|---|---|---|---|
| Zelda 64: Recompiled | zelda-64-recompiled | 21763 | nrm |
| Banjo: Recompiled | banjo-recompiled | 24169 | nrm |
| Unleashed Recompiled | 无 | 21975 | hmm |
| Marathon Recompiled | 无 | 23176 | hmm |
| Ship of Harkinian | 无 | 16121 | otr |
| Starship | 无 | 21612 | otr |
| SpaghettiKart | 无 | 22970 | otr |
| Ghostship | 无 | 24131 | otr |

### 4.2 三种装法只认这些文件

- **nrm**（N64Recomp 系）：`.nrm`、`.rtz` 和本平台的动态库（`.dll`/`.dylib`/`.so`），拆出来平铺进 `<数据目录>/mods`。Thunderstore 和 GameBanana 都走它。
- **otr**（Harbour Masters 系）：`.otr`、`.o2r`，放进程序旁边的 `mods`。只走 GameBanana。
- **hmm**（Unleashed/Marathon）：带 `mod.ini` 的文件夹，并登记进游戏读的 `ModsDB.ini`。只走 GameBanana。

来源方面：Thunderstore 跳过已弃用和 NSFW 的包、按下载量排序、递归装依赖；GameBanana 只装过了病毒扫描的 zip/7z/rar，装不了的给页面链接。深链只有 `recomplauncher://game/<id>`，只打开游戏页，没有 MOD 一键安装。

### 4.3 要请维护者做的

两个站开通后一起提：

1. 在服务端目录的 `srw64-recomp` 条目写上 `mods`：Thunderstore 社区名和 GameBanana 编号。只写在我们包里的 `recomplauncher.json` 没用，源码只读服务端目录。
2. 加一种装法，把 `.srw64mod`（或我们的 zip）原样放进 mods 目录。现有三种都会按扩展名把它丢掉，报「This mod has no files the game reads by itself」。
3. 数据目录按我们的规则算：Windows `%LOCALAPPDATA%\SRW64Recomp\mods` 对得上；Linux 它用 `~/.config/<data>/mods`，我们是 `$XDG_DATA_HOME/srw64-recomp/mods`（默认 `~/.local/share/srw64-recomp/mods`）；便携它看 `portable.txt`，我们是程序旁的 `user-data/mods`；macOS 我们是 `~/Library/Application Support/SRW64Recomp/mods`。

## 5. 我们的包

### 5.1 生成

先用 `tools/mods/pack.py` 打出 `.srw64mod`，再包一层：

```bash
.venv/bin/python tools/mods/thunderstore.py build/mods/<id>-<版本>.srw64mod --icon <方形图> --team Marchwind64 --website https://srw64.dreamquest.club/en/mods/
```

- 包依赖别的 MOD 时，每项加 `--dep 包id=Team-名字-版本`；`srw64.hd`（HD 包）由游戏处理，不进依赖。
- `optional`、`conflicts` 在 Thunderstore 没有对应字段，只写进 README。
- 图标是任意方形图，缩到 256×256；四个官方包现在都用 M64 徽标（`local/store-pictures`，不进仓库）。
- 输出 `build/thunderstore/<Team>-<名字>-<版本>.zip`，同一个包每次生成的字节相同。
- 先跑 `tools/mods/validate.py` 的检查，要求有许可证。

11-11 已生成四个官方包：`Marchwind64-final_dancouga-0.1.0.zip`、`Marchwind64-funnels_p-1.0.0.zip`、`Marchwind64-master_asia-0.3.3.zip`、`Marchwind64-space_a-1.0.2.zip`。

### 5.2 两种布局与 0.5.3（待定）

0.5.3 收 `.zip` 和 `.srw64mod`，要求 `mod.json` 在 zip 根目录，或在唯一的一层文件夹里（`v0.5.3:src/host/mod_library.cpp` `unpack`）。

| | 嵌套（现在） | 平铺 |
|---|---|---|
| zip 里 | Thunderstore 三个文件＋原样的 `.srw64mod` | Thunderstore 三个文件＋`mod.json`、`data/`、`story/` 等直接在根目录 |
| 0.5.3 | 报「没有 mod.json」；README 里写了让玩家先取出 `.srw64mod` | 能装，但会把 `manifest.json`、`icon.png`、`README.md` 一起拷进 MOD 文件夹（按加载代码应该不读它们，未实测） |
| 下个版本 | `bundled_packages` 逐个装里面的包（已写、未提交） | 能装；可以让它跳过那三个文件 |
| SHA-256 | 里面的 `.srw64mod` 和官网记录一致 | zip 里没有原样的 `.srw64mod`，要另记一份 |

## 6. 每次上传

1. 打开 thunderstore.io/package/create/，选 zip 和 Team。
2. Communities 选 Marchwind64；Categories 按内容选（有 AI 素材的选 AI Generated）；NSFW 不勾。
3. 提交，看包页面：图标、README 显示、列表里的描述、分类、依赖。
4. GameBanana 开通后，同一个 zip 在游戏页下再传一份。

## 7. 待用户定

- **AI 标记**：终极断空我的地图图标 `art/icons/314.png` 是 Codex image_gen 画的，按两站规则要标 AI；这和 `mod-packages.md` 13.3「不标注 AI 生成内容」冲突。可选：申请社区时请开 AI Generated 分类、上传时勾上；或这个包先不传。其余三个包是纯数值和剧情，不受影响。
- **布局**：嵌套还是平铺（§5.2）。
- `mod-packages.md` §13.0 里的另外几项：Issue 表单与 `intake.py` 留不留、投稿前校验变成事后检查、官网 MOD 页列不列 Thunderstore。

## 8. 申请帖草稿（#game-requests，由用户发）

> Hi! I'd like to request a community for **Marchwind64**, a native PC/Android/Switch recompilation of Super Robot Wars 64 (N64), built on N64Recomp / N64ModernRuntime like Zelda 64: Recompiled and Banjo: Recompiled. Players supply their own ROM.
> - Source: https://github.com/dyzz/srw64-recomp
> - Site and mod docs: https://srw64.dreamquest.club/en/mods/
> - Mods ready to upload now: 4 data-only packages (new unit Final Dancouga, terrain and weapon-attribute changes). The game installs them by drag-and-drop, so like the other recomp communities it doesn't need r2modman support.
>
> Could I also be a moderator of the community, so I can review packages for it? Thanks!

## 来源

- Thunderstore wiki：[Creating a package](https://wiki.thunderstore.io/mods/creating-a-package)、[Your first upload](https://wiki.thunderstore.io/mods/your-first-upload)、[Updating a package](https://wiki.thunderstore.io/mods/updating-a-package)、[Global Rules](https://wiki.thunderstore.io/moderation/global-rules)、[Adding a new game](https://wiki.thunderstore.io/ecosystem/adding-a-new-game)、[Community Moderators](https://wiki.thunderstore.io/moderation/community-moderators)
- [Thunderstore ecosystem schema](https://thunderstore.io/api/experimental/schema/dev/latest/)
- GameBanana：[Add Game](https://gamebanana.com/games/add)、[站规列表](https://gamebanana.com/apiv11/Rule/Index)、[Bomberman 64 Recompiled 游戏页](https://gamebanana.com/games/24910)
- RecompLauncher 86ab494：[mods.rs](https://github.com/elripalda/RecompLauncher/blob/86ab4943/src-tauri/src/mods.rs)、[gamebanana.rs](https://github.com/elripalda/RecompLauncher/blob/86ab4943/src-tauri/src/gamebanana.rs)、[DEVELOPERS.md](https://github.com/elripalda/RecompLauncher/blob/86ab4943/docs/DEVELOPERS.md)、[pc-catalog.json](https://recomplauncher.com/pc-catalog.json)
