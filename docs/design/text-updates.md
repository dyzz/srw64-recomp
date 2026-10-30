# 文本单独更新

2026-10-30 规划，同日实现（工具、官网数据、桌面／安卓／Switch 客户端），Mac 开发会话、安卓 Seeker、Switch、Windows 实测通过（见文末）。目标：从下一版起，台词、数据文本（机体／驾驶员／武器名、系统讯息）和界面文字的修正不必等发新版程序，放在官网上，游戏自己下载、套用。

## 现状（为什么可行）

| 文本 | 在哪 | 运行时怎么用 |
| --- | --- | --- |
| 台词（剧情、战斗、序章、结局、制作人员） | `content/dialogue/<语言>/`，393 个文件／语言，随包放在 `dialogue/` | `native_dialogue.cpp` 按「随包目录 → 用户覆盖目录」两层读，`with_translations()` 叠到目录上；F5 热重载（`request_reload()`）已有 |
| 数据文本词条 `entries`（4674 条／语言） | `content/locales/<语言>.json`，`build_import_spec.py` 编进程序（`srw64_import_spec.hpp`），首次运行由导入器写进准备好的数据 | `localization::Catalog` |
| 界面文字 `ui`（935 键） | 同上 | `catalog.ui(key)`，缺键回落日文 |
| 图鉴名 | `content/locales/library-names.json` | `library.cpp` |

大小（2026-10-30 实测）：台词每种语言约 10 MB，xz 后约 1.6 MB；三个 `locales/*.json` xz 后共 0.28 MB。全量三语约 5 MB；相对发布标签的差异通常只有几十 KB。放官网（CDN）完全没问题，不用走 GitHub／网盘。

文本都绑在固定的日版 ROM 上：台词键、词条键、`source_sha256` 不会因为程序升级而变，所以文本可以脱离程序单独发。

## 文本包

- **按程序版本出增量包**：`tools/release/build_text_pack.py --base v0.5.3` 把当前提交的文本和该发布标签比较，只收改过的文件／条目：
  - 台词：改过的整个文件（文件粒度，格式不用新造）；
  - `entries`：改过或新增的条目（键＋译文）；
  - `ui`：只收**该标签里已有的键**、值有变化的。新功能的新键不进旧版的包（旧程序用不上，也避免文字提到旧版没有的功能）；
  - `library-names.json`：有变化就整份收。
- 一律从提交读（默认 `HEAD`），同一提交出同样的 zip（固定时间戳、排序）。出包前工具自己检查：该提交的全部台词文件用 Python 版加载器读一遍问题数为 0，各语言词条表照导入器的规则编译通过（`source_sha256`、脚本签名），包里每条词条合它的原文记录。流水线（`renames.json`、`make_zh_hant.py`、`apply_terms`、`check_dialogue.py`、`run_audit.py`）照平常在提交前跑，见 `docs/guide/release.md`「只发文本」。
- 包内 `text.json`：`schema: srw64.text-pack.v1`、`base`（程序版本，如 `0.5.3`）、`revision`（整数，每个 base 从 1 递增）、`date`、`commit`、各语言一句更新说明 `notes`、`counts`、`files`（每个文件的 SHA-256）、`content_sha256`。
- 包内路径：`dialogue/<语言>/…txt`、`locales/<语言>.json`（`{"entries": {键: 译文}, "ui": {键: 文字}}`）、`library-names.json`。
- 同时只给**最近两个程序版本**出包；更老的版本只提示升级程序。新程序发版时随包文本就是最新的，它的文本包从空开始。

## 官网

- 文件放 `web/public/text/<base>/Marchwind64-Text-<base>-r<revision>.zip`，随 `deploy.sh` 上线。
- `/latest.json` 加一段：

  ```json
  "text": {
    "0.5.3": {"revision": 4, "date": "2026-11-12", "url": "…", "bytes": 41234, "sha256": "…",
              "notes": {"zh": "…", "tw": "…", "en": "…", "ja": "…"}},
    "0.5.2": {"revision": 9, …}
  }
  ```

  数据由 `build_text_pack.py` 写进 `web/src/data/text-packs.json`（不放 `release.json`：`sync-release.mjs` 会整份重写它），`latest.json.ts` 原样输出为 `text`。`latest.json` 的 CDN 缓存刷新沿用部署脚本现有的刷新。
- 不必每次写博文；更新说明就是 `notes` 里那一句，「关于」页显示。

## 游戏端

- **检查**：沿用现有更新检查（同一次 GET、同一 User-Agent、同一「启动时检查更新」开关、一天一次）。`latest.json` 里有本程序版本的 `text`，且 `revision` 比已装的新，就下载。
- **自动套用**（`src/host/text_update.cpp`）：开着「启动时检查更新」的玩家，后台下载（`http_download`，上限 16 MB）→ 校验 `bytes`／`sha256` → 解到临时目录（只认上面三类路径）→ 核对 `text.json` 与每个文件的摘要 → 对着游戏的目录检查（台词用 C++ 加载器读、词条比脚本签名、界面键必须是这一版有的），有问题就丢弃 → 改名为 `text/<base>-r<revision>/`，保留上一版。装好立即热重载（同 F5，但不弹台词报告），一条通知「文本已更新（r4）」。手动「检查更新」也会顺带拿文本。文件夹由 `SRW64_TEXT_UPDATES` 指定（用户目录下 `text/`）。
- **叠加顺序**：随包文本 → 文本更新 → 名字 MOD → 玩家自己的覆盖目录（F5 那层）。玩家改的永远在最上面。
  - 台词：`roots` 从两层变三层。
  - `entries`／`ui`／图鉴名：新增一层，在 `localization::initialize` 之后用 `with_translations()` 叠上；`ui` 需要 `Catalog` 增加一个叠 `labels` 的方法。键不在原始记录里的条目忽略并计入问题。
- **版本不符**：目录名带 `base`，程序升级后旧版本的文本包自动失效（不读，下次检查时删掉）。
- **「关于」页**：「文本更新」一行显示「正在使用文本更新 r4（2026-11-12）」和更新说明、下载中或失败；装了包时有「使用更新／随包文本」两个按钮（随包文本＝关掉文本更新层，排查问题用，记在 `text/use-bundled`）。
- **问题报告 zip**（`text_update`、`text_update_bypassed`）和 `dialogue-report.txt` 写上文本 revision。
- **隐私**：不新增请求内容，只多一次文本包下载；`update_ask_note` 已改为「读取官网上的版本号，并下载这一版的文本修正」。

## 平台

| 平台 | 第一阶段 | 之后 |
| --- | --- | --- |
| Windows／macOS／Linux／Deck | 自动更新 | — |
| RecompLauncher 渠道 | 同桌面（就是桌面程序） | `--prepare` 可顺带装最新文本包 |
| 安卓 | 自动更新（见下） | — |
| Switch | 自动更新。Switch 的台词在 Mac 上预先并进准备数据（`console_data.cpp` 的 `merge_dialogue_text`），有文本更新时启动后在准备好的目录上再叠一层（`native_dialogue.cpp` 照样走 `load_text`，序章页保留准备数据的） | — |
| Vita | 手动：zip 放到数据目录 | — |

手动安装（各平台都可以）和 MOD 收件箱共用入口，但文本包按 `text.json` 的 schema 识别，不进 MOD 列表。

### 安卓（2026-10-30 定：第一阶段就做）

更新检查本身 10-30 已在安卓和 Switch 接上（`docs/native/update-check.md`「安卓与 Switch」），文本包只需在其上加下载与安装。当时的做法：

- 清单加 `android.permission.INTERNET`（普通权限，安装时授予，不弹框）。这是安卓包第一次有联网权限，发布说明要写清楚：只读官网的 `latest.json` 和文本包，开关在「关于」页。
- `update_http.cpp` 的安卓分支经 JNI 调 Java（`SRW64Activity` 旁新增 `UpdateHttp.java`，`HttpURLConnection`）：`http_get` 与流式 `http_download`，超时、大小上限、User-Agent（平台 `android`）和桌面一致。
- `supported()` 对安卓放开，整套更新检查跟着上：标题问一次、「关于」页检查按钮与开关、新版本提示（「下载页」用 `SDL_OpenURL` 开浏览器）。HD 包提示同样适用。
- 文本包装在内部数据目录（和存档一样以内部为准），不进 `Documents/Marchwind64`。
- 验证：Seeker 实机（Wi‑Fi／无网／中途断网三种），Win 上的安卓模拟器补一台 x86_64。

## 不做的

- 不更新字体、图片、程序逻辑；新界面键、新功能的文字仍随程序发。
- 不做按语言分包（增量包已经很小）；不做差分补丁。
- 不支持更老的程序版本。

## 工作量与顺序

1. ~~`build_text_pack.py`＋测试（`tests/test_text_pack.py`）；`text-packs.json`／`latest.json.ts` 的 `text` 段~~（10-30）。
2. ~~游戏端：下载／校验／安装目录、三层台词、目录叠加层（`entries`／`ui`／图鉴名）、关于页与通知、问题报告~~（10-30，Mac／安卓／Switch／Windows 实测过）。随下一版（0.5.3）发出——**这一版本身必须带上客户端，之后的文本修正才能单独发**。
3. ~~发版流程：`docs/guide/release.md`「只发文本」~~（10-30）。
4. ~~安卓：联网权限、JNI HTTP、放开更新检查~~（10-30 已做，Switch 一并接上）。
5. `--prepare` 顺带装文本（之后）。

## 待定

- 自动套用是否跟「启动时检查更新」同一个开关（本文假定是），还是单独一个「自动更新文本」。

## 实测（2026-10-30，Mac 开发会话）

本地 HTTP 服务器放测试包（v0.5.2 以来的真实改动＋一处故意改的界面文字），`SRW64_UPDATE_URL`／`SRW64_TEXT_UPDATES` 指过去：

- 手动检查后下载、校验、安装 r1，弹「文本已更新（r1）」；台词重读无问题（简中 393→422 个文件），界面文字立即生效。
- 重启后 r1 照用；「随包文本」立即换回，再切回也立即生效（`use-bundled` 标记随之建删）。
- 坏包（界面键这一版没有）被拒，「关于」页写出原因，r1 照用；校验值不符同样被拒；两次失败都不留临时文件。
- 好的 r2 装上后替换 r1，r1 留作上一版。
- 当时发现并已修：装好后设置页没有重画（页面戳只看语言，现加上目录版本，F5 同样受益）；没有包时空按钮框画成一个点；「启动时检查更新」说明还写着「不会自动下载」。

安卓 Seeker（同日）：`attach.py --env SRW64_UPDATE_URL=file://…` 指向应用私有目录里的测试包（`UpdateHttp` 认 `file://`）。手动检查后装上 r1、弹通知、界面文字生效、台词重读无问题（393→422 个文件）；「随包文本」重启后保留，切回「使用更新」在游戏跑起来后生效（刚启动画面停着时要等游戏开始走帧）。测试后删掉 `text/` 与测试文件，存档与设置和备份一致。

Switch（同日，`/switch/Marchwind64-update` 测试文件夹＋HD 1.2）：Switch 版没有改更新地址的办法，测试 NRO 临时把地址写死成局域网里 Mac 上的服务器（只进测试构建，源码已还原）。手动检查后经 switch-curl 下载 r1、装上、通知；准备数据路径下目录版本为 `+text-r1+text-r1+dialogue-text`（词条、界面文字、台词三层都在），界面文字生效；「随包文本」立即回到准备数据本身，切回又生效。

Windows（同日，Win 台式机 `D:\srw64-textupd` 单独目录）：`SRW64_UPDATE_URL` 指向本机 127.0.0.1 上的测试服务器。手动检查后下载 r1、装上、通知，界面文字生效；目录版本同 Mac；「随包文本」与切回都立即生效。
