# 更新检查

2026-10-06。桌面三平台（macOS、Windows、Linux／Steam Deck）的游戏内更新检查：游戏只读官网的 `/latest.json`，不带标识、不下载；首次联网前先问玩家，之后可在设置里关掉。安卓不做（`update::supported()` 为假，「关于」页只有链接行）。

## 做什么、不做什么

- 读官网的 `https://srw64.dreamquest.club/latest.json`（网站 `web/src/pages/latest.json.ts` 生成，`schema` 为 `srw64.latest.v1`），把其中的 `version` 与本程序的 `SRW64_VERSION` 比较。
- 请求不带任何标识（没有 token、没有设备信息），只有一次 GET。
- 不下载、不安装：有新版时只给出版本和日期，按钮在系统浏览器里打开对应语言的下载页（`download.<zh|en|ja>`）和更新说明（`notes.<语言>`）。macOS 签名、Deck、Windows 的安装方式各不相同，先只提示。

## HD 包的版本

2026-10-07 起 HD 包有自己的版本号，和应用分开发布：

- 版本号和应用一样是数字（第一个是 `1.0`，之后默认末位加一：`1.1`、`1.2`…；大改时用 `--hd-version 2.0` 指定），写在包里 `hd.json` 的 `version`；`hd.json` 还有 `content_sha256`，是包内全部文件（路径＋内容，除 `hd.json`、`NOTICE.txt`）的摘要（`prepare_hd_bundle.py`）。HD 包可以复现：同一份素材连建两次，6644 个文件逐字节相同。
- `build_release.py` 每次都建 HD 包，把摘要和所建提交里的 `web/src/data/release.json` 的 `hd.content_sha256` 比较：相同就沿用那一版，不打包；不同就取新版本号（默认末位加一，`--hd-version` 可指定），打成 `Marchwind64-HD-<版本>.zip`，`release.json` 里多一条 `publish_hd`（标签 `hd-<版本>`，`--latest=false`，先于应用发布），另写 `hd-release-notes.md`。应用的发布说明只链接当前 HD 包的发布页。
- 发布后 `web/scripts/sync-release.mjs` 把 `hd` 写进官网的 `release.json`；`/latest.json` 的 `hd` 给出版本、各语言下载页（安装页 `#hd`）、文件与校验值。没有版本号的旧包（0.3.5 随应用发布的那个）不出现在 `latest.json`。
- 游戏读已装的包：`SRW64_ART_PACK`（`hd/art`）旁边的 `hd/hd.json`。开发运行的美术目录没有 `hd.json`，算没装。`hd_newer()`（`update_version.hpp`）：装了包、官网的版本比它新，或装的包没有版本号，才提示；没装 HD 的玩家不提示。

## 入口

| 入口 | 行为 |
| --- | --- |
| 设置「关于」页 | 链接行：官网（`/<语言>/`）、源代码、问题反馈（GitHub issues）。更新行：「检查更新」按钮与结果（尚未检查／正在检查／已是最新／有新版本 X（日期）／检查失败：原因），有新版时多「下载页」「更新说明」。「启动时检查更新」开／关。 |
| macOS 应用菜单 | 「关于 Marchwind64」改为打开设置的「关于」页（替换 SDL 的系统关于面板）；其后新增「检查更新…」：打开「关于」页并立即检查。Windows／Linux 没有菜单栏，用设置里的「关于」页。 |
| 标题画面 | 第一次停在标题（`intro::title_waiting()`）时，在设置面板的框里问一次「要在启动时检查是否有新版本吗？」：「检查」「不检查」，关闭（B／Esc）算「不检查」。之后在「关于」页改。调试会话（`SRW64_DEBUG`）、隐藏窗口运行（`SRW64_BACKGROUND`）和没有用户目录的运行不问。 |
| 「关于」页 HD 包行 | 已安装的 HD 版本（没有版本号的旧包、未安装各有一句），官网有更新的版本时多一句「有新版 HD X」，「下载 HD 包」按钮打开安装页的 HD 一节。 |
| 标题左下角 | 某次检查发现新版本后，版本号上方出现金色「新版本 X」，点它打开「关于」页并把焦点放在「下载页」。本程序升级到该版本或更新后自动消失。应用已是最新而 HD 包有新版时显示「HD 包新版本 X」，焦点放在「下载 HD 包」。 |

## 状态与文件

`update.json`（用户目录，启动器经 `SRW64_UPDATE_STATE` 传入，与 `presentation.json` 同目录）：

```json
{"schema": "srw64.update-check.v1", "automatic": true, "checked_at": 1791262005, "latest": { …上次读到的 latest.json… }}
```

- `automatic` 缺省＝还没回答过（会在标题问）。
- 开着时，启动后（界面初始化时）距 `checked_at` 满一天才检查；自动检查失败不显示错误，保留上次结果。手动检查无视间隔，失败显示原因。
- 存下的 `latest` 让标题提示在离线时也能显示。

## 实现

| 文件 | 内容 |
| --- | --- |
| `src/host/update_check.{hpp,cpp}` | 状态、后台线程（分离线程，状态对象不释放）、`update.json` 读写、`latest.json` 解析、`SDL_OpenURL` |
| `src/host/update_version.hpp` | 版本比较 `newer`（按数字段，`0.3.10 > 0.3.9`，后缀忽略）与网站语言；`tests/native_update.cpp` 测它 |
| `src/host/update_http.cpp` | Windows：WinHTTP（链接 `winhttp`）；Linux／Deck：运行时 `dlopen` 系统的 `libcurl.so.4`（不进打包闭包，缺了只是检查失败）；安卓：存根 |
| `src/host/macos/update_http_macos.mm` | macOS：`NSURLSession`（无界面） |
| `src/native/ui/frontend.cpp` | `about_rows`、`update_ask_panel`、`open_about`、标题提示 `home-update`、`update-*`／`about-link:*` 按钮 |
| `src/host/macos/app_menu.mm` | 「关于」改道与「检查更新…」 |

所有请求 15 秒超时、响应上限 1 MB。界面文字键在 `src/srw64_native/profile.py` 的 UI_KEYS（`update_*`、`update_hd_*`、`settings_update*`、`about_link_*`、`menu_about`、`menu_check_updates`）。

## 验证

- `tests/native_update.cpp`（根 CMake 的 `native-update`）：版本比较、网站语言与 `hd_newer`。`tests/test_hd_release.py` 的 `HdVersionTests`：摘要只随内容变、版本号末位加一。
- 2026-10-07 用独立小程序（`update_check.cpp`＋`update_http_macos.mm`，`SRW64_UPDATE_URL` 指向本地 `latest.json`，`SRW64_ART_PACK` 指向假的 `hd/art`）验证四种情况：没有版本号的包、较旧的包提示，同版本、没装 HD 不提示。
- 测试时用 `SRW64_UPDATE_URL` 指向别处（本地 `file://` 或本地服务器）；启动器会保留这个变量。2026-10-06 用独立小程序（只链接 `update_check.cpp`、`update_http_macos.mm`）在 macOS 上验证：本地 0.3.6 对 0.3.5 报新版、对 0.3.6 报已是最新、链接按语言取、`update.json` 写入；官网尚未部署时报「HTTP 404」；非 `latest.json` 的 200 响应报「not a release description」。
- 「关于」页排版：`tools/recomp/ui_audit/run_audit.py --only about`，三语四种尺寸无溢出。
- Windows、Linux 的 HTTP 只做了编译层面的检查（Linux 分支在 macOS 上 `-fsyntax-only`），要在 CI 产物或真机上确认。
