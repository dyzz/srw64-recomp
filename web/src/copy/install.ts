import type { Lang } from '../i18n';

// Steps may contain inline HTML (<code>, <kbd>, <strong>); file names come in through {file}.
export type Section = { id: string; name: string; summary: string; requires: string; experimental?: boolean; steps: string[]; notes?: string[] };
type Install = {
  title: string;
  lead: string;
  rom: string;
  viewSteps: string;
  experimental: string;
  requires: string;
  sections: Section[];
  downloadsTitle: string;
  downloadsLead: string;
  file: string;
  size: string;
  links: string;
  quark: string;
  quarkPending: string;
  verify: string;
  noFile: string;
};

const ROM_SHA = 'ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e';

export const INSTALL: Record<Lang, Install> = {
  zh: {
    title: '安装',
    lead: '电脑和 Steam Deck 都是下载、解压、放好 ROM 三步；Android 装好 APK 后在应用里选 ROM。HD 美术包是单独的可选下载，Windows、macOS、Linux 通用。',
    rom: `所有平台都需要《超级机器人大战64》日版 Rev 0 的 ROM，<code>.z64</code> 格式（SHA-256 <code class="hash">${ROM_SHA}</code>）。`,
    viewSteps: '查看步骤',
    experimental: '实验性',
    requires: '要求：',
    sections: [
      {
        id: 'windows', name: 'Windows', experimental: true,
        summary: '解压后把 ROM 放进数据文件夹，双击 srw64.cmd。',
        requires: '64 位 Windows',
        steps: [
          '下载 <code>{windows}</code>，解压到任意文件夹。',
          '把 ROM 复制为 <code>%LOCALAPPDATA%\\SRW64Recomp\\rom.z64</code>，或放在 <code>srw64.cmd</code> 旁边并命名为 <code>rom.z64</code>。',
          '双击 <code>srw64.cmd</code> 启动。第一次启动是简体中文，按 <kbd>F7</kbd> 或在设置窗口里换语言。',
        ],
        notes: ['Windows 如果弹出安全提示，选择「更多信息」→「仍要运行」。'],
      },
      {
        id: 'macos', name: 'macOS',
        summary: '把应用拖进「应用程序」，第一次打开时选择 ROM。',
        requires: 'Apple 芯片（M1 及之后），macOS 14 起',
        steps: [
          '下载 <code>{macos}</code>，解压后把 <code>SRW64 Recompiled.app</code> 拖进「应用程序」。',
          '第一次打开会被系统拦下（应用只有本地签名，没有经过 Apple 公证）：到「系统设置 → 隐私与安全性」，点「仍要打开」。',
          '在弹出的窗口里选择 ROM 文件。之后会记住，不用再选。',
        ],
      },
      {
        id: 'linux', name: 'Linux', experimental: false,
        summary: '解压后把 ROM 放进数据文件夹，运行 srw64.sh。',
        requires: 'x86-64，glibc 2.35 起，需要 Vulkan 驱动',
        steps: [
          '下载 <code>{linux}</code>，解压到任意位置，例如 <code>~/Games/Marchwind64</code>。',
          '把 ROM 复制为 <code>~/.local/share/srw64-recomp/rom.z64</code>，或放在 <code>srw64.sh</code> 旁边并命名为 <code>rom.z64</code>。',
          '运行 <code>srw64.sh</code>。',
        ],
      },
      {
        id: 'steam-deck', name: 'Steam Deck', experimental: true,
        summary: '与 Linux 同一个包；在桌面模式里加进 Steam 库，之后在游戏模式启动。',
        requires: '在桌面模式里做一次',
        steps: [
          '在桌面模式下载 <code>{linux}</code>，解压到例如 <code>~/Games/Marchwind64</code>，把 ROM 复制为 <code>~/.local/share/srw64-recomp/rom.z64</code>。',
          '打开 Steam，双击文件夹里的 <code>add-to-steam.sh</code>（询问时选「执行」）。它会把游戏加进 Steam 库，并配好封面和图标。',
          '回到游戏模式，从库里启动。不需要改 Steam 输入设置，游戏会认出 Deck 并显示 Deck 的按键。',
        ],
        notes: ['视图键 ⧉ 打开设置；按下左摇杆切换语言，按下右摇杆切换原版／HD。从 Steam 菜单选「退出游戏」会正常保存退出。'],
      },
      {
        id: 'android', name: 'Android', experimental: true,
        summary: '安装 APK，第一次打开时选择 ROM。触屏按画面显示按键。',
        requires: 'arm64 手机或掌机，Android 9 起，支持 Vulkan 1.1',
        steps: [
          '在手机上下载 Android 安装包（APK）。',
          '打开它安装。系统会提示这是来自浏览器或文件管理器的应用：按提示允许「安装未知应用」，再继续安装。',
          '打开 SRW64，在弹出的文件选择器里选 ROM。ROM 会复制进应用自己的目录，之后不用再选。',
        ],
        notes: ['手指碰到屏幕时出现触屏按键，只显示当前画面用得上的几个；接上手柄或键盘时自动隐藏。顶边的「设置」按钮打开设置窗口。', 'HD 美术包暂时还不能在应用里导入。'],
      },
      {
        id: 'hd', name: 'HD 美术包',
        summary: '可选。解压成 hd 文件夹，放进用户目录。',
        requires: '与应用同一版本',
        steps: [
          '下载 <code>{hd}</code>，解压得到 <code>hd</code> 文件夹（里面直接是 <code>hd.json</code> 和 <code>art/</code>）。',
          '放进用户目录：macOS 是 <code>~/Library/Application Support/SRW64Recomp/hd</code>，Linux／Steam Deck 是 <code>~/.local/share/srw64-recomp/hd</code>，Windows 是 <code>%LOCALAPPDATA%\\SRW64Recomp\\hd</code>。Linux 和 Windows 也可以放在程序旁边。',
          '打开游戏即为 HD。<kbd>F6</kbd> 或设置窗口里的「画面」可以随时切回原版。',
        ],
        notes: ['HD 包只配同一版本的应用，更新时整个替换。包里的图由 AI 以原版画面为参考生成或放大重绘，详见常见问题。'],
      },
    ],
    downloadsTitle: '全部下载',
    downloadsLead: '下载后可以用 SHA-256 校验文件是否完整。',
    file: '文件',
    size: '大小',
    links: '下载',
    quark: '夸克网盘',
    quarkPending: '国内网盘链接随正式发布提供。',
    verify: '校验',
    noFile: '这个版本还没有 Android 安装包。',
  },
  en: {
    title: 'Install',
    lead: 'On computers and the Steam Deck it is download, unzip and add your ROM; on Android you install the APK and choose the ROM in the app. The HD art pack is a separate, optional download shared by Windows, macOS and Linux.',
    rom: `Every platform needs a Super Robot Wars 64 ROM, Japan Rev 0, in <code>.z64</code> format (SHA-256 <code class="hash">${ROM_SHA}</code>).`,
    viewSteps: 'View steps',
    experimental: 'Experimental',
    requires: 'Requires: ',
    sections: [
      {
        id: 'windows', name: 'Windows', experimental: true,
        summary: 'Unzip, put your ROM in the data folder, double-click srw64.cmd.',
        requires: '64-bit Windows',
        steps: [
          'Download <code>{windows}</code> and unzip it anywhere.',
          'Copy your ROM to <code>%LOCALAPPDATA%\\SRW64Recomp\\rom.z64</code>, or next to <code>srw64.cmd</code> as <code>rom.z64</code>.',
          'Double-click <code>srw64.cmd</code>. The first launch is in Simplified Chinese: press <kbd>F7</kbd> or use the settings window to change the language.',
        ],
        notes: ['If Windows shows a security prompt, choose “More info” → “Run anyway”.'],
      },
      {
        id: 'macos', name: 'macOS',
        summary: 'Drag the app to Applications, choose your ROM on first launch.',
        requires: 'Apple silicon (M1 or later), macOS 14 or later',
        steps: [
          'Download <code>{macos}</code>, unzip it and drag <code>SRW64 Recompiled.app</code> to Applications.',
          'The first launch is blocked because the app is not notarized: open System Settings → Privacy &amp; Security and click “Open Anyway”.',
          'Choose your ROM file when asked. It is remembered from then on.',
        ],
      },
      {
        id: 'linux', name: 'Linux',
        summary: 'Unzip, put your ROM in the data folder, run srw64.sh.',
        requires: 'x86-64, glibc 2.35 or later, a Vulkan driver',
        steps: [
          'Download <code>{linux}</code> and extract it anywhere, for example <code>~/Games/Marchwind64</code>.',
          'Copy your ROM to <code>~/.local/share/srw64-recomp/rom.z64</code>, or next to <code>srw64.sh</code> as <code>rom.z64</code>.',
          'Run <code>srw64.sh</code>.',
        ],
      },
      {
        id: 'steam-deck', name: 'Steam Deck', experimental: true,
        summary: 'The Linux package: add it to Steam once in Desktop Mode, then play from Game Mode.',
        requires: 'Done once, in Desktop Mode',
        steps: [
          'In Desktop Mode, download <code>{linux}</code>, extract it to for example <code>~/Games/Marchwind64</code>, and copy your ROM to <code>~/.local/share/srw64-recomp/rom.z64</code>.',
          'With Steam open, double-click <code>add-to-steam.sh</code> in the folder (choose “Execute”). It adds the game to your library with its artwork.',
          'Back in Game Mode, start it from the library. No Steam Input changes are needed: the game recognises the Deck and shows its buttons.',
        ],
        notes: ['The View button ⧉ opens the settings; press the left stick to change language and the right stick for Original/HD. Quitting from the Steam menu saves normally.'],
      },
      {
        id: 'android', name: 'Android', experimental: true,
        summary: 'Install the APK and choose your ROM on first launch. Touch controls follow the screen.',
        requires: 'An arm64 phone or handheld, Android 9 or later, Vulkan 1.1',
        steps: [
          'Download the Android package (APK) on the device.',
          'Open it to install. Android warns that the app comes from your browser or file manager: allow “Install unknown apps” for it when asked, then continue.',
          'Open SRW64 and pick your ROM in the file chooser. It is copied into the app’s own folder, so you only do this once.',
        ],
        notes: ['Touch controls appear when a finger touches the screen, showing only the few buttons the current screen needs; they hide when a controller or keyboard is used. The Settings button along the top opens the settings window.', 'The HD art pack cannot be imported in the app yet.'],
      },
      {
        id: 'hd', name: 'HD art pack',
        summary: 'Optional. Unzip it as an hd folder in the user directory.',
        requires: 'The same version as the app',
        steps: [
          'Download <code>{hd}</code> and unzip it to get an <code>hd</code> folder (with <code>hd.json</code> and <code>art/</code> directly inside).',
          'Put it in the user directory: <code>~/Library/Application Support/SRW64Recomp/hd</code> on macOS, <code>~/.local/share/srw64-recomp/hd</code> on Linux and Steam Deck, <code>%LOCALAPPDATA%\\SRW64Recomp\\hd</code> on Windows. On Linux and Windows it can also sit next to the program.',
          'The game now starts in HD. <kbd>F6</kbd>, or “Images” in the settings window, switches back to the original at any time.',
        ],
        notes: ['The HD pack only works with the app of the same version; replace it whole when you update. Its images are AI-generated or upscaled from the original graphics; see the FAQ.'],
      },
    ],
    downloadsTitle: 'All downloads',
    downloadsLead: 'Check a download with its SHA-256 if you want to be sure it is complete.',
    file: 'File',
    size: 'Size',
    links: 'Download',
    quark: 'Quark Drive',
    quarkPending: 'A mirror for mainland China comes with the public release.',
    verify: 'Checksum',
    noFile: 'This version has no Android package yet.',
  },
  ja: {
    title: 'インストール',
    lead: 'パソコンと Steam Deck は、ダウンロード・展開・ROM の配置の 3 ステップ。Android は APK をインストールしてアプリ内で ROM を選びます。HD アートパックは別配布の任意ダウンロードで、Windows・macOS・Linux 共通です。',
    rom: `すべてのプラットフォームで『スーパーロボット大戦64』日本版 Rev 0 の ROM（<code>.z64</code> 形式、SHA-256 <code class="hash">${ROM_SHA}</code>）が必要です。`,
    viewSteps: '手順を見る',
    experimental: '試験的',
    requires: '動作環境：',
    sections: [
      {
        id: 'windows', name: 'Windows', experimental: true,
        summary: '展開して ROM をデータフォルダーに置き、srw64.cmd をダブルクリック。',
        requires: '64 ビット版 Windows',
        steps: [
          '<code>{windows}</code> をダウンロードし、任意のフォルダーに展開します。',
          'ROM を <code>%LOCALAPPDATA%\\SRW64Recomp\\rom.z64</code> としてコピーするか、<code>srw64.cmd</code> と同じフォルダーに <code>rom.z64</code> という名前で置きます。',
          '<code>srw64.cmd</code> をダブルクリックして起動します。初回は簡体字中国語で起動するので、<kbd>F7</kbd> か設定ウィンドウで日本語に切り替えてください。',
        ],
        notes: ['セキュリティの警告が出た場合は「詳細情報」→「実行」を選んでください。'],
      },
      {
        id: 'macos', name: 'macOS',
        summary: 'アプリを「アプリケーション」に入れ、初回起動時に ROM を選択。',
        requires: 'Apple シリコン（M1 以降）、macOS 14 以降',
        steps: [
          '<code>{macos}</code> をダウンロードして展開し、<code>SRW64 Recompiled.app</code> を「アプリケーション」へドラッグします。',
          'アプリは公証を受けていないため、初回はブロックされます。「システム設定 → プライバシーとセキュリティ」で「このまま開く」をクリックしてください。',
          '表示されるウィンドウで ROM ファイルを選びます。次回からは記憶されます。',
        ],
      },
      {
        id: 'linux', name: 'Linux',
        summary: '展開して ROM をデータフォルダーに置き、srw64.sh を実行。',
        requires: 'x86-64、glibc 2.35 以降、Vulkan ドライバー',
        steps: [
          '<code>{linux}</code> をダウンロードし、<code>~/Games/Marchwind64</code> などに展開します。',
          'ROM を <code>~/.local/share/srw64-recomp/rom.z64</code> としてコピーするか、<code>srw64.sh</code> と同じフォルダーに <code>rom.z64</code> という名前で置きます。',
          '<code>srw64.sh</code> を実行します。',
        ],
      },
      {
        id: 'steam-deck', name: 'Steam Deck', experimental: true,
        summary: 'Linux 版と同じパッケージ。デスクトップモードで一度 Steam に追加すれば、ゲームモードから起動できます。',
        requires: 'デスクトップモードで一度だけ',
        steps: [
          'デスクトップモードで <code>{linux}</code> をダウンロードして <code>~/Games/Marchwind64</code> などに展開し、ROM を <code>~/.local/share/srw64-recomp/rom.z64</code> としてコピーします。',
          'Steam を開いた状態で、フォルダー内の <code>add-to-steam.sh</code> をダブルクリック（確認では「実行」）。カバー画像やアイコン付きでライブラリに追加されます。',
          'ゲームモードに戻り、ライブラリから起動します。Steam 入力の設定変更は不要で、ゲームが Deck を認識して Deck のボタン表示になります。',
        ],
        notes: ['ビューボタン ⧉ で設定を開きます。左スティック押し込みで言語、右スティック押し込みでオリジナル／HD を切り替え。Steam メニューの「ゲームを終了」で正常にセーブして終了します。'],
      },
      {
        id: 'android', name: 'Android', experimental: true,
        summary: 'APK をインストールし、初回起動時に ROM を選択。タッチ操作は画面に合わせて変わります。',
        requires: 'arm64 のスマホまたは携帯機、Android 9 以降、Vulkan 1.1',
        steps: [
          '端末で Android 版のインストールパッケージ（APK）をダウンロードします。',
          '開いてインストールします。ブラウザやファイルマネージャーからのアプリだと警告されたら、案内に従って「不明なアプリのインストール」を許可して続けます。',
          'SRW64 を開き、表示されるファイル選択画面で ROM を選びます。ROM はアプリ専用のフォルダーにコピーされるので、選ぶのは一度だけです。',
        ],
        notes: ['画面に触れるとタッチボタンが表示され、いまの画面で使うものだけが並びます。コントローラーやキーボードを使うと自動で隠れます。上端の「設定」ボタンで設定ウィンドウを開きます。', 'HD アートパックはまだアプリ内で取り込めません。'],
      },
      {
        id: 'hd', name: 'HD アートパック',
        summary: '任意。hd フォルダーとしてユーザーディレクトリに置きます。',
        requires: 'アプリと同じバージョン',
        steps: [
          '<code>{hd}</code> をダウンロードして展開し、<code>hd</code> フォルダー（直下に <code>hd.json</code> と <code>art/</code>）を用意します。',
          'ユーザーディレクトリに置きます。macOS は <code>~/Library/Application Support/SRW64Recomp/hd</code>、Linux／Steam Deck は <code>~/.local/share/srw64-recomp/hd</code>、Windows は <code>%LOCALAPPDATA%\\SRW64Recomp\\hd</code>。Linux と Windows はプログラムと同じフォルダーにも置けます。',
          '起動すると HD になります。<kbd>F6</kbd> か設定ウィンドウの「画面」でいつでもオリジナルに戻せます。',
        ],
        notes: ['HD パックは同じバージョンのアプリ専用です。更新時は丸ごと入れ替えてください。画像は AI がオリジナル画面を参考に生成・拡大したものです（詳しくはよくある質問を参照）。'],
      },
    ],
    downloadsTitle: 'ダウンロード一覧',
    downloadsLead: 'ダウンロードしたファイルは SHA-256 で確認できます。',
    file: 'ファイル',
    size: 'サイズ',
    links: 'ダウンロード',
    quark: 'Quark ドライブ',
    quarkPending: '中国本土向けのミラーは正式公開時に用意します。',
    verify: 'チェックサム',
    noFile: 'このバージョンには Android 版のパッケージがまだありません。',
  },
};
