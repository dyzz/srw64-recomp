# 文本单独更新

2026-10-30 规划，未实现。目标：从下一版起，台词、数据文本（机体／驾驶员／武器名、系统讯息）和界面文字的修正不必等发新版程序，放在官网上，游戏自己下载、套用。

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
- 打包前跑完整条文本流水线：`renames.json`、`make_zh_hant.py`、`apply_terms`，再跑 `check_dialogue.py` 与 `run_audit.py`（排版溢出），任一失败不出包。用 Python 版加载器（`dialogue_text` 的 Py 实现）完整读一遍，问题数必须为 0。
- 包内 `text.json`：`schema: srw64.text-pack.v1`、`base`（程序版本，如 `0.5.3`）、`revision`（整数，每个 base 从 1 递增）、`date`、各语言一句更新说明、`content_sha256`。zip 本身可复现（固定时间戳、排序）。
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

  数据由 `build_text_pack.py` 写进 `web/src/data/release.json` 的 `text`，`latest.json.ts` 原样输出。`latest.json` 的 CDN 缓存刷新沿用部署脚本现有的刷新。
- 不必每次写博文；更新说明就是 `notes` 里那一句，「关于」页显示。

## 游戏端

- **检查**：沿用现有更新检查（同一次 GET、同一 User-Agent、同一「启动时检查更新」开关、一天一次）。`latest.json` 里有本程序版本的 `text`，且 `revision` 比已装的新，就下载。
- **自动套用**：开着「启动时检查更新」的玩家，后台下载（`http_download`）→ 校验 `bytes`／`sha256` → 解到临时目录 → 用 C++ 加载器读一遍，有问题就丢弃 → 原子改名为 `text/<base>-r<revision>/`，保留上一版。下载完在标题画面时立即热重载（台词走 `request_reload()`，目录层见下），否则下次启动生效；右下角一条通知「文本已更新（r4）」。手动「检查更新」也会顺带拿文本。
- **叠加顺序**：随包文本 → 文本更新 → 名字 MOD → 玩家自己的覆盖目录（F5 那层）。玩家改的永远在最上面。
  - 台词：`roots` 从两层变三层。
  - `entries`／`ui`／图鉴名：新增一层，在 `localization::initialize` 之后用 `with_translations()` 叠上；`ui` 需要 `Catalog` 增加一个叠 `labels` 的方法。键不在原始记录里的条目忽略并计入问题。
- **版本不符**：目录名带 `base`，程序升级后旧版本的文本包自动失效（不读，下次检查时删掉）。
- **「关于」页**：显示「文本 r4（2026-11-12）」和更新说明；一个「使用随包文本」开关（关掉文本更新层，排查问题用）。
- **问题报告 zip** 和 `dialogue-report.txt` 写上文本 revision。
- **隐私**：不新增请求内容，只多一次文本包下载；`update_ask_note` 的说明补半句「也会下载文本修正」。

## 平台

| 平台 | 第一阶段 | 之后 |
| --- | --- | --- |
| Windows／macOS／Linux／Deck | 自动更新 | — |
| RecompLauncher 渠道 | 同桌面（就是桌面程序） | `--prepare` 可顺带装最新文本包 |
| 安卓 | 自动更新（见下） | — |
| Switch | 自动更新（更新检查 10-30 已接上）；注意 Switch 的台词在 Mac 上预先并进准备数据（`console_data.cpp` 的 `merge_dialogue_text`），文本更新层要在运行时叠加，不能只换 `dialogue/` 目录 | — |
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

1. `build_text_pack.py`＋测试（增量规则、可复现、`ui` 键过滤）；`release.json`／`latest.json.ts` 的 `text` 段。
2. 游戏端：下载／校验／安装目录、三层台词、目录叠加层（`entries`／`ui`／图鉴名）、关于页与通知、问题报告。随下一版（0.5.3）发出——**这一版本身必须带上客户端，之后的文本修正才能单独发**。
3. 发版流程：`docs/guide/release.md` 加「只发文本」一节（流水线 → 出包 → 部署 → 核对 `latest.json`）。
4. ~~安卓：联网权限、JNI HTTP、放开更新检查~~（10-30 已做，Switch 一并接上）。
5. `--prepare` 顺带装文本（之后）。

## 待定

- 自动套用是否跟「启动时检查更新」同一个开关（本文假定是），还是单独一个「自动更新文本」。
