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
  baidu: string;
  passcode: string;
  verify: string;
  noFile: string;
  notReleased: string;
};

const ROM_SHA = 'ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e';

export const INSTALL: Record<Lang, Install> = {
  zh: {
    title: '安装',
    lead: '电脑和 Steam Deck 安装分为下载、解压和添加 ROM 三步；Android 则安装 APK 后在应用中选择 ROM。HD 美术包为独立的可选下载，所有平台通用。',
    rom: `所有平台都需要《超级机器人大战64》日版 Rev 0 的 ROM。<code>.z64</code>、<code>.v64</code>、<code>.n64</code> 三种字节序都可以，游戏会自动识别（按 <code>.z64</code> 计算的 SHA-256 为 <code class="hash">${ROM_SHA}</code>）。下面的文件名写作 <code>rom.z64</code>，<code>.n64</code>、<code>.v64</code> 的文件也可以命名为 <code>rom.n64</code>、<code>rom.v64</code>。`,
    viewSteps: '查看步骤',
    experimental: '实验性',
    requires: '要求：',
    sections: [
      {
        id: 'windows', name: 'Windows', experimental: true,
        summary: '解压安装包，放好 ROM 后双击 Marchwind64.cmd。',
        requires: '64 位 Windows',
        steps: [
          '下载 <code>{windows}</code>，解压到任意文件夹。',
          '将 ROM 复制为 <code>%LOCALAPPDATA%\\SRW64Recomp\\rom.z64</code>，或放在 <code>Marchwind64.cmd</code> 所在文件夹并命名为 <code>rom.z64</code>。',
          '双击 <code>Marchwind64.cmd</code> 启动。首次启动使用简体中文，可在设置窗口中切换语言。',
        ],
        notes: ['如果 Windows 显示安全提示，选择「更多信息」→「仍要运行」。'],
      },
      {
        id: 'macos', name: 'macOS',
        summary: '将应用拖入「应用程序」，首次启动时选择 ROM。',
        requires: 'Apple 芯片（M1 及之后），macOS 14 及以上',
        steps: [
          '下载 <code>{macos}</code>，解压后将 <code>Marchwind64.app</code> 拖入「应用程序」。',
          '应用使用本地签名，未经 Apple 公证。首次打开被拦截时，请前往「系统设置 → 隐私与安全性」，点击「仍要打开」。',
          '在文件选择窗口中选择 ROM。应用会记住该选择，后续启动无需重复选择。',
        ],
      },
      {
        id: 'linux', name: 'Linux', experimental: false,
        summary: '解压安装包，放好 ROM 后运行 marchwind64.sh。',
        requires: 'x86-64，glibc 2.35 及以上，需要 Vulkan 驱动',
        steps: [
          '下载 <code>{linux}</code>，解压到任意位置，例如 <code>~/Games/Marchwind64</code>。',
          '将 ROM 复制为 <code>~/.local/share/srw64-recomp/rom.z64</code>，或放在 <code>marchwind64.sh</code> 所在文件夹并命名为 <code>rom.z64</code>。',
          '运行 <code>marchwind64.sh</code>。',
        ],
      },
      {
        id: 'steam-deck', name: 'Steam Deck', experimental: true,
        summary: '使用 Linux 安装包。首次在桌面模式添加到 Steam 库，之后从游戏模式启动。',
        requires: '首次安装在桌面模式完成',
        steps: [
          '在桌面模式下载 <code>{linux}</code>，解压到例如 <code>~/Games/Marchwind64</code> 的位置，将 ROM 复制为 <code>~/.local/share/srw64-recomp/rom.z64</code>。',
          '打开 Steam，双击文件夹中的 <code>add-to-steam.sh</code>，出现提示时选择「执行」。脚本会将游戏加入 Steam 库，并设置封面和图标。',
          '回到游戏模式，从 Steam 库启动。使用默认 Steam 输入设置即可，游戏会自动识别 Deck 并显示对应按键。',
        ],
        notes: ['游戏会识别 Deck 并使用适合 Deck 的默认键位，设置里可以改键。从 Steam 菜单选择「退出游戏」可保存并退出。'],
      },
      {
        id: 'android', name: 'Android', experimental: true,
        summary: '安装 APK，首次启动时选择 ROM。触屏按键随当前画面变化。',
        requires: 'arm64 手机或掌机，Android 9 及以上，支持 Vulkan 1.1',
        steps: [
          '在设备上下载 Android 安装包（APK）。',
          '打开 APK，按系统提示允许浏览器或文件管理器「安装未知应用」，然后完成安装。',
          '打开 Marchwind64，在文件选择窗口中选择 ROM。ROM 会复制到应用目录，后续启动无需重复选择。',
        ],
        notes: ['触摸屏幕时显示当前画面需要的按键；切换到手柄或键盘操作时自动隐藏。点击顶边的「设置」按钮可打开设置窗口。', 'HD 美术包：长按应用图标选「导入 HD 包」，或在文件管理器里用 Marchwind64 打开下载的 HD 包 zip，应用会自动解压安装。首次选择 ROM 后也会询问一次。'],
      },
      {
        id: 'hd', name: 'HD 美术包',
        summary: '可选下载。电脑上解压后放入用户数据目录或游戏所在的文件夹，Android 在应用里导入。',
        requires: '有独立的版本号，有新版时游戏会提示',
        steps: [
          '下载 <code>{hd}</code>，解压得到 <code>hd</code> 文件夹，其根目录应包含 <code>hd.json</code> 和 <code>art/</code>。',
          '将文件夹放入应用的用户数据目录：macOS 为 <code>~/Library/Application Support/SRW64Recomp/hd</code>，Linux／Steam Deck 为 <code>~/.local/share/srw64-recomp/hd</code>，Windows 为 <code>%LOCALAPPDATA%\\SRW64Recomp\\hd</code>。也可以放在游戏所在的文件夹里，与 <code>Marchwind64.app</code>、<code>marchwind64.sh</code> 或 <code>Marchwind64.exe</code> 放在一起；两处都有时用用户数据目录里的。macOS 上放在应用旁边时，应用要先在访达里移动过一次（例如拖进「应用程序」）。Android 不用手动解压，按上面 Android 一节的说明在应用里导入即可。',
          '启动游戏后默认使用 HD，可在设置窗口的「画面」中切换原版与 HD。',
        ],
        notes: ['HD 包有自己的版本号，内容变了才发新版，不必每次更新应用都重新下载。有新版时游戏的更新检查会提示，更新时整体替换旧的 <code>hd</code> 文件夹。包内图像主要由 AI 参考原版画面生成或放大重绘，详见常见问题。'],
      },
    ],
    downloadsTitle: '全部下载',
    downloadsLead: '可通过 SHA-256 校验下载文件的完整性。',
    file: '文件',
    size: '大小',
    links: '下载',
    quark: '夸克网盘',
    baidu: '百度网盘',
    passcode: '（提取码 {p}）',
    verify: '校验',
    noFile: '此版本暂未提供 Android 安装包。',
    notReleased: '0.4.0 正在准备中，暂未开放下载。安装步骤可以先看。',
  },
  en: {
    title: 'Install',
    lead: 'On computers and Steam Deck, download the package, unzip it and add your ROM. On Android, install the APK and select your ROM in the app. The optional HD art pack is a separate download that works on every platform.',
    rom: `Every platform needs a Super Robot Wars 64 ROM, Japan Rev 0. Any byte order works (<code>.z64</code>, <code>.v64</code> or <code>.n64</code>); the game recognises it (SHA-256 of the <code>.z64</code> form: <code class="hash">${ROM_SHA}</code>). The steps below name the file <code>rom.z64</code>; a <code>.n64</code> or <code>.v64</code> file can be named <code>rom.n64</code> or <code>rom.v64</code>.`,
    viewSteps: 'View steps',
    experimental: 'Experimental',
    requires: 'Requires: ',
    sections: [
      {
        id: 'windows', name: 'Windows', experimental: true,
        summary: 'Unzip the package, add your ROM, then double-click Marchwind64.cmd.',
        requires: '64-bit Windows',
        steps: [
          'Download <code>{windows}</code> and unzip it to a folder of your choice.',
          'Save a copy of your ROM as <code>%LOCALAPPDATA%\\SRW64Recomp\\rom.z64</code>, or place it in the same folder as <code>Marchwind64.cmd</code>, named <code>rom.z64</code>.',
          'Double-click <code>Marchwind64.cmd</code> to launch. The first launch uses Simplified Chinese; change the language in the settings window.',
        ],
        notes: ['If Windows shows a security prompt, choose “More info” → “Run anyway”.'],
      },
      {
        id: 'macos', name: 'macOS',
        summary: 'Drag the app to Applications and select your ROM on first launch.',
        requires: 'Apple silicon (M1 or later), macOS 14 or later',
        steps: [
          'Download <code>{macos}</code>, unzip it and drag <code>Marchwind64.app</code> to Applications.',
          'The app is locally signed and has not been notarized by Apple. If the first launch is blocked, open System Settings → Privacy &amp; Security and click “Open Anyway”.',
          'Select your ROM in the file picker. The app remembers your selection for future launches.',
        ],
      },
      {
        id: 'linux', name: 'Linux',
        summary: 'Unzip the package, add your ROM, then run marchwind64.sh.',
        requires: 'x86-64, glibc 2.35 or later, a Vulkan driver',
        steps: [
          'Download <code>{linux}</code> and extract it to a folder of your choice, such as <code>~/Games/Marchwind64</code>.',
          'Save a copy of your ROM as <code>~/.local/share/srw64-recomp/rom.z64</code>, or place it in the same folder as <code>marchwind64.sh</code>, named <code>rom.z64</code>.',
          'Run <code>marchwind64.sh</code>.',
        ],
      },
      {
        id: 'steam-deck', name: 'Steam Deck', experimental: true,
        summary: 'Use the Linux package. Add it to Steam once in Desktop Mode, then launch from Game Mode.',
        requires: 'Set up once in Desktop Mode',
        steps: [
          'In Desktop Mode, download <code>{linux}</code>, extract it to a folder such as <code>~/Games/Marchwind64</code>, and save a copy of your ROM as <code>~/.local/share/srw64-recomp/rom.z64</code>.',
          'Open Steam, then double-click <code>add-to-steam.sh</code> in the folder, choosing “Execute” if prompted. The script adds the game to your library with its cover art and icon.',
          'Return to Game Mode and launch from your library with the default Steam Input settings. The game detects the Deck and displays matching button prompts.',
        ],
        notes: ['The game recognises the Deck and uses Deck-friendly default controls, which you can remap in the settings. Quitting through the Steam menu saves the game before exiting.'],
      },
      {
        id: 'android', name: 'Android', experimental: true,
        summary: 'Install the APK and select your ROM on first launch. Touch buttons adapt to the current screen.',
        requires: 'An arm64 phone or handheld, Android 9 or later, Vulkan 1.1',
        steps: [
          'Download the Android package (APK) on your device.',
          'Open the APK. If prompted, allow “Install unknown apps” for your browser or file manager, then complete the installation.',
          'Open Marchwind64 and select your ROM in the file picker. The ROM is copied into the app’s own folder for future launches.',
        ],
        notes: ['Touching the screen brings up the buttons needed for the current screen. They hide when you switch to a controller or keyboard. Tap Settings along the top to open the settings window.', 'HD art pack: long-press the app icon and choose “Import HD pack”, or open the downloaded HD zip with Marchwind64 from a file manager; the app unpacks and installs it. You are also asked once after choosing the ROM.'],
      },
      {
        id: 'hd', name: 'HD art pack',
        summary: 'Optional download. On computers, unzip it into the user data directory or the game’s own folder; on Android, import it in the app.',
        requires: 'Versioned separately; the game says when there is a new one',
        steps: [
          'Download <code>{hd}</code> and extract the <code>hd</code> folder. It should contain <code>hd.json</code> and <code>art/</code> directly inside.',
          'Place the folder in the app’s user data directory: <code>~/Library/Application Support/SRW64Recomp/hd</code> on macOS, <code>~/.local/share/srw64-recomp/hd</code> on Linux and Steam Deck, or <code>%LOCALAPPDATA%\\SRW64Recomp\\hd</code> on Windows. It can also go in the game’s own folder, next to <code>Marchwind64.app</code>, <code>marchwind64.sh</code> or <code>Marchwind64.exe</code>; when both have one, the user data directory’s is used. On macOS, next to the app works once the app has been moved in Finder (into Applications, say). On Android there is nothing to unzip: import it in the app as described in the Android section.',
          'The game starts in HD. Switch between original and HD art under “Images” in the settings window.',
        ],
        notes: ['The HD pack has versions of its own and only gets a new one when its contents change, so you do not need to download it again with every app update. The game’s update check says when there is a new one; replace the whole <code>hd</code> folder when you update. Most images are generated or upscaled with AI using the original graphics as a reference; see the FAQ.'],
      },
    ],
    downloadsTitle: 'All downloads',
    downloadsLead: 'Use the SHA-256 checksum to verify the downloaded file.',
    file: 'File',
    size: 'Size',
    links: 'Download',
    quark: 'Quark Drive',
    baidu: 'Baidu Netdisk',
    passcode: ' (passcode {p})',
    verify: 'Checksum',
    noFile: 'An Android package is not yet available for this version.',
    notReleased: '0.4.0 is being prepared and cannot be downloaded yet. The installation steps are below.',
  },
  ja: {
    title: 'インストール',
    lead: 'パソコンと Steam Deck は、ダウンロード・展開・ROM の追加の 3 ステップで準備できます。Android は APK をインストールし、アプリ内で ROM を選択します。HD アートパックは別配布のオプションで、すべてのプラットフォーム共通です。',
    rom: `すべてのプラットフォームで『スーパーロボット大戦64』日本版 Rev 0 の ROM が必要です。<code>.z64</code>・<code>.v64</code>・<code>.n64</code> のどのバイトオーダーでもゲームが自動で判別します（<code>.z64</code> 形式での SHA-256 は <code class="hash">${ROM_SHA}</code>）。以下の手順ではファイル名を <code>rom.z64</code> としていますが、<code>.n64</code>・<code>.v64</code> は <code>rom.n64</code>・<code>rom.v64</code> でもかまいません。`,
    viewSteps: '手順を見る',
    experimental: '試験的',
    requires: '動作環境：',
    sections: [
      {
        id: 'windows', name: 'Windows', experimental: true,
        summary: 'パッケージを展開し、ROM を配置して Marchwind64.cmd をダブルクリックします。',
        requires: '64 ビット版 Windows',
        steps: [
          '<code>{windows}</code> をダウンロードし、任意のフォルダーに展開します。',
          'ROM を <code>%LOCALAPPDATA%\\SRW64Recomp\\rom.z64</code> としてコピーするか、<code>Marchwind64.cmd</code> と同じフォルダーに <code>rom.z64</code> という名前で置きます。',
          '<code>Marchwind64.cmd</code> をダブルクリックして起動します。初回は簡体字中国語で起動するので、設定ウィンドウで日本語に切り替えてください。',
        ],
        notes: ['Windows のセキュリティ警告が表示されたら、「詳細情報」→「実行」を選んでください。'],
      },
      {
        id: 'macos', name: 'macOS',
        summary: 'アプリを「アプリケーション」に入れ、初回起動時に ROM を選択します。',
        requires: 'Apple シリコン（M1 以降）、macOS 14 以降',
        steps: [
          '<code>{macos}</code> をダウンロードして展開し、<code>Marchwind64.app</code> を「アプリケーション」へドラッグします。',
          'アプリはローカル署名のみで、Apple の公証を受けていません。初回起動がブロックされた場合は、「システム設定 → プライバシーとセキュリティ」で「このまま開く」をクリックしてください。',
          'ファイル選択画面で ROM を選択します。選択内容は保存され、次回から選び直す必要はありません。',
        ],
      },
      {
        id: 'linux', name: 'Linux',
        summary: 'パッケージを展開し、ROM を配置して marchwind64.sh を実行します。',
        requires: 'x86-64、glibc 2.35 以降、Vulkan ドライバー',
        steps: [
          '<code>{linux}</code> をダウンロードし、<code>~/Games/Marchwind64</code> などに展開します。',
          'ROM を <code>~/.local/share/srw64-recomp/rom.z64</code> としてコピーするか、<code>marchwind64.sh</code> と同じフォルダーに <code>rom.z64</code> という名前で置きます。',
          '<code>marchwind64.sh</code> を実行します。',
        ],
      },
      {
        id: 'steam-deck', name: 'Steam Deck', experimental: true,
        summary: 'Linux 版と同じパッケージを使います。初回にデスクトップモードで Steam に追加すると、以後はゲームモードから起動できます。',
        requires: '初回の設定はデスクトップモードで',
        steps: [
          'デスクトップモードで <code>{linux}</code> をダウンロードして <code>~/Games/Marchwind64</code> などに展開し、ROM を <code>~/.local/share/srw64-recomp/rom.z64</code> としてコピーします。',
          'Steam を開いた状態で、フォルダー内の <code>add-to-steam.sh</code> をダブルクリックし、確認画面では「実行」を選びます。カバー画像とアイコン付きでゲームがライブラリに追加されます。',
          'ゲームモードに戻り、Steam 入力は初期設定のままライブラリから起動します。ゲームが Deck を自動認識し、対応するボタンを表示します。',
        ],
        notes: ['Deck を認識し、Deck 向けの初期操作で起動します。操作は設定で割り当てを変更できます。Steam メニューの「ゲームを終了」でセーブして終了できます。'],
      },
      {
        id: 'android', name: 'Android', experimental: true,
        summary: 'APK をインストールし、初回起動時に ROM を選択します。タッチボタンは現在の画面に合わせて変わります。',
        requires: 'arm64 のスマホまたは携帯機、Android 9 以降、Vulkan 1.1',
        steps: [
          '端末で Android 版のインストールパッケージ（APK）をダウンロードします。',
          'APK を開き、案内に従ってブラウザまたはファイルマネージャーに「不明なアプリのインストール」を許可し、インストールを進めます。',
          'Marchwind64 を開き、ファイル選択画面で ROM を選びます。ROM はアプリ専用のフォルダーにコピーされ、次回から選び直す必要はありません。',
        ],
        notes: ['画面に触れると、現在の画面で使うタッチボタンが表示されます。コントローラーやキーボードの操作に切り替えると自動で隠れます。上端の「設定」ボタンで設定ウィンドウを開きます。', 'HD アートパック：アプリアイコンを長押しして「HD パックを導入」を選ぶか、ダウンロードした HD パックの zip をファイルマネージャーから Marchwind64 で開くと、自動で展開して導入します。初回の ROM 選択後にも一度確認されます。'],
      },
      {
        id: 'hd', name: 'HD アートパック',
        summary: '任意で導入できます。パソコンでは展開してユーザーデータフォルダーかゲーム本体のフォルダーに配置し、Android ではアプリ内で導入します。',
        requires: 'アプリとは別のバージョン。新版はゲームが通知',
        steps: [
          '<code>{hd}</code> をダウンロードして展開し、<code>hd</code> フォルダーの直下に <code>hd.json</code> と <code>art/</code> があることを確認します。',
          'アプリのユーザーデータフォルダーに配置します。macOS は <code>~/Library/Application Support/SRW64Recomp/hd</code>、Linux／Steam Deck は <code>~/.local/share/srw64-recomp/hd</code>、Windows は <code>%LOCALAPPDATA%\\SRW64Recomp\\hd</code>。ゲーム本体のフォルダー（<code>Marchwind64.app</code>、<code>marchwind64.sh</code>、<code>Marchwind64.exe</code> と同じ場所）にも置けます。両方にある場合はユーザーデータフォルダーのものを使います。macOS でアプリの隣に置く場合は、先にアプリを Finder で一度移動してください（「アプリケーション」へドラッグするなど）。Android では展開不要で、上の Android の項目のとおりアプリ内で導入します。',
          'ゲームは HD で起動します。設定ウィンドウの「画面」で、オリジナルと HD を切り替えられます。',
        ],
        notes: ['HD パックにはアプリとは別のバージョン番号があり、内容が変わったときだけ新版を出します。アプリを更新するたびにダウンロードし直す必要はありません。新版はゲームの更新確認が知らせます。更新時は <code>hd</code> フォルダーを丸ごと入れ替えてください。収録画像は主に、オリジナルを参考に AI で生成、または拡大・描き直したものです。詳しくはよくある質問をご覧ください。'],
      },
    ],
    downloadsTitle: 'ダウンロード一覧',
    downloadsLead: 'SHA-256 チェックサムでダウンロードファイルの完全性を確認できます。',
    file: 'ファイル',
    size: 'サイズ',
    links: 'ダウンロード',
    quark: 'Quark ドライブ',
    baidu: 'Baidu ネットディスク',
    passcode: '（パスコード {p}）',
    verify: 'チェックサム',
    noFile: 'このバージョンの Android パッケージは現在提供していません。',
    notReleased: '0.4.0 は公開準備中のため、まだダウンロードできません。インストール手順は先にご覧いただけます。',
  },
};
