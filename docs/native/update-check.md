# 更新检查

2026-10-06。桌面三平台（macOS、Windows、Linux／Steam Deck）的游戏内更新检查：游戏只读官网的 `/latest.json`，不带标识、不下载；首次联网前先问玩家，之后可在设置里关掉。安卓不做（`update::supported()` 为假，「关于」页只有链接行）。

## 做什么、不做什么

- 读官网的 `https://srw64.dreamquest.club/latest.json`（网站 `web/src/pages/latest.json.ts` 生成，`schema` 为 `srw64.latest.v1`），把其中的 `version` 与本程序的 `SRW64_VERSION` 比较。
- 请求不带任何标识（没有 token、没有设备信息），只有一次 GET。
- 不下载、不安装：有新版时只给出版本和日期，按钮在系统浏览器里打开对应语言的下载页（`download.<zh|en|ja>`）和更新说明（`notes.<语言>`）。macOS 签名、Deck、Windows 的安装方式各不相同，先只提示。

## 入口

| 入口 | 行为 |
| --- | --- |
| 设置「关于」页 | 链接行：官网（`/<语言>/`）、源代码、问题反馈（GitHub issues）。更新行：「检查更新」按钮与结果（尚未检查／正在检查／已是最新／有新版本 X（日期）／检查失败：原因），有新版时多「下载页」「更新说明」。「启动时检查更新」开／关。 |
| macOS 应用菜单 | 「关于 Marchwind64」改为打开设置的「关于」页（替换 SDL 的系统关于面板）；其后新增「检查更新…」：打开「关于」页并立即检查。Windows／Linux 没有菜单栏，用设置里的「关于」页。 |
| 标题画面 | 第一次停在标题（`intro::title_waiting()`）时，在设置面板的框里问一次「要在启动时检查是否有新版本吗？」：「检查」「不检查」，关闭（B／Esc）算「不检查」。之后在「关于」页改。调试会话（`SRW64_DEBUG`）、隐藏窗口运行（`SRW64_BACKGROUND`）和没有用户目录的运行不问。 |
| 标题左下角 | 某次检查发现新版本后，版本号上方出现金色「新版本 X」，点它打开「关于」页并把焦点放在「下载页」。本程序升级到该版本或更新后自动消失。 |

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

所有请求 15 秒超时、响应上限 1 MB。界面文字键在 `src/srw64_native/profile.py` 的 UI_KEYS（`update_*`、`settings_update*`、`about_link_*`、`menu_about`、`menu_check_updates`）。

## 验证

- `tests/native_update.cpp`（根 CMake 的 `native-update`）：版本比较与网站语言。
- 测试时用 `SRW64_UPDATE_URL` 指向别处（本地 `file://` 或本地服务器）；启动器会保留这个变量。2026-10-06 用独立小程序（只链接 `update_check.cpp`、`update_http_macos.mm`）在 macOS 上验证：本地 0.3.6 对 0.3.5 报新版、对 0.3.6 报已是最新、链接按语言取、`update.json` 写入；官网尚未部署时报「HTTP 404」；非 `latest.json` 的 200 响应报「not a release description」。
- 「关于」页排版：`tools/recomp/ui_audit/run_audit.py --only about`，三语四种尺寸无溢出。
- Windows、Linux 的 HTTP 只做了编译层面的检查（Linux 分支在 macOS 上 `-fsyntax-only`），要在 CI 产物或真机上确认。
