import type { Platform } from '../data/release';
import type { TextLang } from '../i18n';

// `body`, `need` and the ROM rows may contain inline HTML (<code>, <strong>, <a>).
// A step with `file` shows that download, `secondary` for an optional one.
export type Step = { title: string; body?: string | string[]; file?: Platform; secondary?: boolean };
// What the platform needs, shown beside its steps: rows of [label, value], notes may hold links.
export type Requirements = { rows: [string, string][]; notes?: string[] };
export type Section = { id: string; name: string; summary: string; need?: string; experimental?: boolean; hd?: string; req?: Requirements; steps: Step[] };
type Install = {
  title: string;
  lead: string;
  rom: { title: string; rows: [string, string][] };
  viewSteps: string;
  experimental: string;
  reqTitle: string;
  sections: Section[];
  downloadsTitle: string;
  downloadsLead: string;
  file: string;
  size: string;
  quark: string;
  baidu: string;
  passcode: string;
  savesLink: string;
  faqLink: string;
  verify: string;
  noFile: string;
  notReleased: string;
};

const ROM_SHA = 'ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e';
// The GPU floor is Zelda64Recomp's, the same renderer, and untested below that; said beside it.
const MINIMUM = {
  zh: '这是最低配置，显卡门槛与其他 RT64 重编译项目相同；更旧的硬件没有测试过。',
  en: 'This is the minimum, the same graphics floor as other RT64 recompilations; older hardware is untested.',
  ja: '最低構成です。GPU の条件は RT64 を使うほかのリコンパイル作品と同じで、より古い機種は未確認です。',
  ko: '최소 사양입니다. GPU 조건은 RT64를 쓰는 다른 리컴파일 작품과 같으며, 더 오래된 기기는 테스트하지 않았습니다.',
};
const DRIVERS = {
  zh: '闪退、卡住或画面异常时，先更新显卡驱动，见<a href="/zh/faq/#gpu">常见问题</a>。',
  en: 'If it crashes, freezes or draws wrongly, update your graphics drivers first; see the <a href="/en/faq/#gpu">FAQ</a>.',
  ja: '落ちる、止まる、表示がおかしいときは、まずグラフィックスドライバーを更新してください（<a href="/ja/faq/#gpu">よくある質問</a>）。',
  ko: '꺼지거나 멈추거나 화면이 이상하면 먼저 그래픽 드라이버를 업데이트하세요(<a href="/ko/faq/#gpu">자주 묻는 질문</a>).',
};

// Under each platform's steps: the HD pack is optional and has a section of its own (#hd).
// Android imports the same pack inside the app, in the FAQ's words.
const HD_PC = {
  zh: '可选：安装 <a href="#hd">HD 美术包</a>，换上高清的头像、地图和机体画面，游戏中可以随时切回原版。',
  en: 'Optional: install the <a href="#hd">HD art pack</a> for high-resolution portraits, maps and unit art. You can switch back to the original art at any time in the game.',
  ja: '任意：<a href="#hd">HD アートパック</a>を導入すると、顔グラフィック、マップ、ユニットの絵が高解像度になります。ゲーム中いつでも原作の絵に戻せます。',
  ko: '선택: <a href="#hd">HD 아트 팩</a>을 설치하면 초상화, 맵, 기체 그림이 고해상도로 바뀝니다. 게임 중 언제든 원작 그림으로 되돌릴 수 있습니다.',
};
const HD_ANDROID = {
  zh: '可选：下载 <a href="#hd">HD 美术包</a>，然后长按应用图标选「导入 HD 包」，或用 Marchwind64 打开下载的 zip。游戏中可以随时切回原版。',
  en: 'Optional: download the <a href="#hd">HD art pack</a>, then long-press the app icon and choose “Import HD pack”, or open the downloaded zip with Marchwind64. You can switch back to the original art at any time in the game.',
  ja: '任意：<a href="#hd">HD アートパック</a>をダウンロードし、アプリアイコンを長押しして「HD パックを導入」を選ぶか、ダウンロードした zip を Marchwind64 で開きます。ゲーム中いつでも原作の絵に戻せます。',
  ko: '선택: <a href="#hd">HD 아트 팩</a>을 다운로드한 뒤 앱 아이콘을 길게 눌러 “HD 팩 가져오기”를 선택하거나, 다운로드한 zip을 Marchwind64로 여세요. 게임 중 언제든 원작 그림으로 되돌릴 수 있습니다.',
};

export const INSTALL: Record<TextLang, Install> = {
  zh: {
    title: '安装',
    lead: '选择平台，下载对应的版本。需要自备《超级机器人大战64》日版 ROM。<a href="#rom">ROM 要求</a>',
    rom: {
      title: 'ROM 要求',
      rows: [
        ['版本', '《超级机器人大战64》日版 Rev 0，所有平台都需要'],
        ['格式', '<code>.z64</code>、<code>.v64</code>、<code>.n64</code> 三种字节序都可以，游戏会自动识别'],
        ['文件名', '步骤里写作 <code>rom.z64</code>；<code>.n64</code>、<code>.v64</code> 的文件也可以命名为 <code>rom.n64</code>、<code>rom.v64</code>'],
        ['SHA-256', `<code class="hash">${ROM_SHA}</code>（按 <code>.z64</code> 计算）`],
      ],
    },
    viewSteps: '查看步骤',
    experimental: '实验性',
    reqTitle: '系统要求',
    sections: [
      {
        id: 'windows', name: 'Windows', experimental: true,
        hd: HD_PC.zh,
        summary: '解压，放好 ROM，双击启动。',
        req: {
          rows: [
            ['系统', 'Windows 10 / 11，64 位'],
            ['图形接口', 'Direct3D 12（SM 6.0）或 Vulkan 1.2'],
            ['显卡', 'GeForce GT 630、Radeon HD 7750、Intel HD Graphics 510 及以上'],
            ['CPU', 'x86-64-v2（酷睿 i 第 1 代、AMD FX 及以后）'],
            ['空间', '约 165 MB，HD 美术包另需约 700 MB'],
          ],
          notes: [MINIMUM.zh, DRIVERS.zh],
        },
        steps: [
          { title: '下载并解压', file: 'windows', body: '右键选「全部解压缩」，不要在压缩包里直接运行。' },
          { title: '放好 ROM', body: '<strong>把 ROM 改名为 <code>rom.z64</code>，放进解压出的文件夹</strong>，和 <code>Marchwind64.exe</code> 放在一起。' },
          { title: '启动游戏', body: '<strong>双击 <code>Marchwind64.exe</code></strong>。语言跟随系统，可以在设置里切换。' },
        ],
      },
      {
        id: 'macos', name: 'macOS',
        hd: HD_PC.zh,
        summary: '拖入「应用程序」，首次启动时选择 ROM。',
        req: {
          rows: [
            ['系统', 'macOS 14 及以上'],
            ['处理器', 'Apple 芯片（M1 及以后），不支持 Intel Mac'],
            ['图形接口', 'Metal'],
            ['空间', '约 140 MB，HD 美术包另需约 700 MB'],
          ],
        },
        steps: [
          { title: '下载并解压', file: 'macos', body: '解压后将 <code>Marchwind64.app</code> 拖入「应用程序」。' },
          { title: '允许打开', body: '应用使用本地签名，未经 Apple 公证。首次打开被拦截时，请前往「系统设置 → 隐私与安全性」，点击「仍要打开」。' },
          { title: '选择 ROM', body: '在文件选择窗口中选择 ROM。应用会记住该选择，后续启动无需重复选择。' },
        ],
      },
      {
        id: 'linux', name: 'Linux',
        hd: HD_PC.zh,
        summary: '解压，放好 ROM，运行脚本。',
        req: {
          rows: [
            ['系统', '64 位，glibc 2.35 及以上（如 Ubuntu 22.04、Debian 12）'],
            ['图形接口', 'Vulkan 1.2'],
            ['显卡', 'GeForce GT 630、Radeon HD 7750、Intel HD Graphics 510 及以上'],
            ['CPU', 'x86-64-v2（酷睿 i 第 1 代、AMD FX 及以后）'],
            ['空间', '约 150 MB，HD 美术包另需约 700 MB'],
          ],
          notes: [MINIMUM.zh, DRIVERS.zh],
        },
        steps: [
          { title: '下载并解压', file: 'linux', body: '解压到任意位置，例如 <code>~/Games/Marchwind64</code>。' },
          { title: '放好 ROM', body: '将 ROM 复制为 <code>~/.local/share/srw64-recomp/rom.z64</code>，或放在 <code>marchwind64.sh</code> 所在文件夹并命名为 <code>rom.z64</code>。' },
          { title: '启动游戏', body: '运行 <code>marchwind64.sh</code>。' },
        ],
      },
      {
        id: 'steam-deck', name: 'Steam Deck', experimental: true,
        hd: HD_PC.zh,
        summary: '用 Linux 安装包，添加到 Steam 库。',
        req: {
          rows: [
            ['型号', 'LCD 版与 OLED 版'],
            ['系统', '最新的 SteamOS'],
            ['空间', '约 150 MB，HD 美术包另需约 700 MB'],
          ],
          notes: ['首次安装在桌面模式完成。'],
        },
        steps: [
          { title: '在桌面模式下载并解压', file: 'linux', body: '解压到例如 <code>~/Games/Marchwind64</code> 的位置。' },
          { title: '放好 ROM', body: '将 ROM 复制为 <code>~/.local/share/srw64-recomp/rom.z64</code>。' },
          { title: '添加到 Steam', body: '打开 Steam，双击文件夹里的 <code>add-to-steam.sh</code>，提示时选「执行」。' },
          { title: '回到游戏模式启动', body: '从 Steam 库启动，用默认的 Steam 输入设置即可。' },
        ],
      },
      {
        id: 'android', name: 'Android', experimental: true,
        hd: HD_ANDROID.zh,
        summary: '安装 APK，首次启动时选择 ROM。',
        req: {
          rows: [
            ['系统', 'Android 9 及以上'],
            ['处理器', 'arm64（64 位）手机或掌机'],
            ['图形接口', 'Vulkan 1.1'],
            ['推荐', '骁龙 865、天玑 1080、Exynos 1380、Tensor G2 及以上，约 30 FPS'],
            ['入门芯片', '天玑 6100+、天玑 700 等（Mali-G57）可以运行，约 10–14 FPS'],
          ],
          notes: ['各机型的实测情况见<a href="/zh/android/">安卓兼容列表</a>。'],
        },
        steps: [
          { title: '在设备上下载 APK', file: 'android' },
          { title: '安装 APK', body: '打开 APK，按系统提示允许浏览器或文件管理器「安装未知应用」，然后完成安装。' },
          { title: '选择 ROM', body: '打开 Marchwind64，在文件选择窗口中选择 ROM。ROM 会复制到应用目录，后续启动无需重复选择。' },
        ],
      },
      {
        id: 'switch', name: 'Nintendo Switch', experimental: true,
        summary: '解压到 SD 卡，从 hbmenu 打开。',
        need: 'Switch 版性能还有待优化，运行时会有一定卡顿。',
        req: {
          rows: [
            ['主机', '能运行 Atmosphère 自制系统的 Switch（初代机型上测试）'],
            ['系统', 'Atmosphère 与 hbmenu，应用模式（按住 R 启动游戏）'],
            ['空间', 'SD 卡约 110 MB，Switch 版 HD 包另需约 1.6 GB'],
          ],
        },
        steps: [
          { title: '解压到 SD 卡', file: 'switch', body: '解压到 SD 卡根目录，得到 <code>/switch/Marchwind64/</code>。' },
          { title: '放好 ROM', body: '<strong>把 ROM 改名为 <code>rom.z64</code>，放进 <code>/switch/Marchwind64/</code></strong>。' },
          { title: '可选：安装 Switch 版 HD 包', file: 'switch-hd', secondary: true, body: '同样解压到 SD 卡根目录。电脑版 HD 包不能用。' },
          { title: '按住 R 启动', body: '<strong>按住 R 键启动任意一个游戏</strong>，进入 hbmenu 后选 Marchwind64。第一次启动会从 ROM 导入数据，约半分钟。' },
        ],
      },
      {
        id: 'hd', name: 'HD 美术包',
        summary: '可选下载。电脑和 Android 通用，Switch 另有一份。',
        need: '可选下载，有新版时游戏会提示。Android 和 Switch 的装法不同，见<a href="/zh/faq/#hd">常见问题</a>。',
        steps: [
          { title: '下载并解压', file: 'hd', body: '解压得到 <code>hd</code> 文件夹，其根目录应包含 <code>hd.json</code> 和 <code>art/</code>。' },
          { title: '放入用户数据目录', body: [
            'macOS：<code>~/Library/Application Support/SRW64Recomp/hd</code><br>Linux／Steam Deck：<code>~/.local/share/srw64-recomp/hd</code><br>Windows：<code>%LOCALAPPDATA%\\SRW64Recomp\\hd</code>',
            '也可以放在游戏所在的文件夹里。',
          ] },
          { title: '启动并确认', body: '装好 HD 包后会自动使用 HD 画面。要确认是否装好，可以看设置窗口「关于」页的「HD 包」一行，那里显示已安装的版本。' },
        ],
      },
    ],
    downloadsTitle: '全部下载',
    downloadsLead: '可通过 SHA-256 校验下载文件的完整性。',
    file: '文件',
    size: '大小',
    quark: '夸克网盘',
    baidu: '百度网盘',
    passcode: '（提取码 {p}）',
    savesLink: '存档在哪里、怎么备份 →',
    faqLink: '安装遇到问题 →',
    verify: '校验',
    noFile: '此版本暂未提供这个平台的安装包。',
    notReleased: '0.4.0 正在准备中，暂未开放下载。安装步骤可以先看。',
  },
  en: {
    title: 'Install',
    lead: 'Choose your platform and download the matching build. You need your own ROM of the Japanese Super Robot Wars 64. <a href="#rom">ROM requirements</a>',
    rom: {
      title: 'ROM requirements',
      rows: [
        ['Version', 'Super Robot Wars 64, Japan Rev 0, on every platform'],
        ['Format', 'Any byte order works (<code>.z64</code>, <code>.v64</code> or <code>.n64</code>); the game recognises it'],
        ['File name', 'The steps name the file <code>rom.z64</code>; a <code>.n64</code> or <code>.v64</code> file can be named <code>rom.n64</code> or <code>rom.v64</code>'],
        ['SHA-256', `<code class="hash">${ROM_SHA}</code> (of the <code>.z64</code> form)`],
      ],
    },
    viewSteps: 'View steps',
    experimental: 'Experimental',
    reqTitle: 'System requirements',
    sections: [
      {
        id: 'windows', name: 'Windows', experimental: true,
        hd: HD_PC.en,
        summary: 'Unzip, add your ROM, then double-click to start.',
        req: {
          rows: [
            ['OS', 'Windows 10 / 11, 64-bit'],
            ['Graphics', 'Direct3D 12 (SM 6.0) or Vulkan 1.2'],
            ['GPU', 'GeForce GT 630, Radeon HD 7750, Intel HD Graphics 510 or newer'],
            ['CPU', 'x86-64-v2 (1st-gen Intel Core i, AMD FX or newer)'],
            ['Space', 'About 165 MB, plus about 700 MB for the HD art pack'],
          ],
          notes: [MINIMUM.en, DRIVERS.en],
        },
        steps: [
          { title: 'Download and extract', file: 'windows', body: 'Right-click it and choose “Extract All”. Don’t run the game from inside the zip.' },
          { title: 'Add your ROM', body: '<strong>Rename your ROM to <code>rom.z64</code> and put it in the extracted folder</strong>, next to <code>Marchwind64.exe</code>.' },
          { title: 'Start the game', body: '<strong>Double-click <code>Marchwind64.exe</code></strong>. The language follows your system; change it in the settings.' },
        ],
      },
      {
        id: 'macos', name: 'macOS',
        hd: HD_PC.en,
        summary: 'Drag the app to Applications and select your ROM on first launch.',
        req: {
          rows: [
            ['OS', 'macOS 14 or later'],
            ['Processor', 'Apple silicon (M1 or later); Intel Macs are not supported'],
            ['Graphics', 'Metal'],
            ['Space', 'About 140 MB, plus about 700 MB for the HD art pack'],
          ],
        },
        steps: [
          { title: 'Download and extract', file: 'macos', body: 'Unzip it and drag <code>Marchwind64.app</code> to Applications.' },
          { title: 'Allow it to open', body: 'The app is locally signed and has not been notarized by Apple. If the first launch is blocked, open System Settings → Privacy &amp; Security and click “Open Anyway”.' },
          { title: 'Select your ROM', body: 'Select your ROM in the file picker. The app remembers your selection for future launches.' },
        ],
      },
      {
        id: 'linux', name: 'Linux',
        hd: HD_PC.en,
        summary: 'Extract, add your ROM, then run the script.',
        req: {
          rows: [
            ['OS', '64-bit, glibc 2.35 or later (Ubuntu 22.04, Debian 12 or newer)'],
            ['Graphics', 'Vulkan 1.2'],
            ['GPU', 'GeForce GT 630, Radeon HD 7750, Intel HD Graphics 510 or newer'],
            ['CPU', 'x86-64-v2 (1st-gen Intel Core i, AMD FX or newer)'],
            ['Space', 'About 150 MB, plus about 700 MB for the HD art pack'],
          ],
          notes: [MINIMUM.en, DRIVERS.en],
        },
        steps: [
          { title: 'Download and extract', file: 'linux', body: 'Extract it to a folder of your choice, such as <code>~/Games/Marchwind64</code>.' },
          { title: 'Add your ROM', body: 'Save a copy of your ROM as <code>~/.local/share/srw64-recomp/rom.z64</code>, or place it in the same folder as <code>marchwind64.sh</code>, named <code>rom.z64</code>.' },
          { title: 'Start the game', body: 'Run <code>marchwind64.sh</code>.' },
        ],
      },
      {
        id: 'steam-deck', name: 'Steam Deck', experimental: true,
        hd: HD_PC.en,
        summary: 'Use the Linux package and add it to Steam.',
        req: {
          rows: [
            ['Model', 'LCD and OLED'],
            ['OS', 'Up-to-date SteamOS'],
            ['Space', 'About 150 MB, plus about 700 MB for the HD art pack'],
          ],
          notes: ['Set it up once in Desktop Mode.'],
        },
        steps: [
          { title: 'Download and extract in Desktop Mode', file: 'linux', body: 'Extract it to a folder such as <code>~/Games/Marchwind64</code>.' },
          { title: 'Add your ROM', body: 'Save a copy of your ROM as <code>~/.local/share/srw64-recomp/rom.z64</code>.' },
          { title: 'Add it to Steam', body: 'With Steam open, double-click <code>add-to-steam.sh</code> in the folder and choose Execute when asked.' },
          { title: 'Start it from Game Mode', body: 'Start it from your Steam library. The default Steam Input settings are fine.' },
        ],
      },
      {
        id: 'android', name: 'Android', experimental: true,
        hd: HD_ANDROID.en,
        summary: 'Install the APK and select your ROM on first launch.',
        req: {
          rows: [
            ['OS', 'Android 9 or later'],
            ['Processor', 'arm64 (64-bit) phone or handheld'],
            ['Graphics', 'Vulkan 1.1'],
            ['Recommended', 'Snapdragon 865, Dimensity 1080, Exynos 1380, Tensor G2 or newer, about 30 FPS'],
            ['Entry-level', 'Dimensity 6100+, Dimensity 700 and similar (Mali-G57) run at about 10–14 FPS'],
          ],
          notes: ['How each Android phone did in our tests: <a href="/en/android/">Android compatibility</a>.'],
        },
        steps: [
          { title: 'Download the APK on your device', file: 'android' },
          { title: 'Install the APK', body: 'Open the APK. If prompted, allow “Install unknown apps” for your browser or file manager, then complete the installation.' },
          { title: 'Select your ROM', body: 'Open Marchwind64 and select your ROM in the file picker. The ROM is copied into the app’s own folder for future launches.' },
        ],
      },
      {
        id: 'switch', name: 'Nintendo Switch', experimental: true,
        summary: 'Extract to the SD card and open it from hbmenu.',
        need: 'The Switch version still needs optimizing, and you will see some stutter while playing.',
        req: {
          rows: [
            ['Console', 'A Switch that runs Atmosphère custom firmware (tested on the original model)'],
            ['System', 'Atmosphère and hbmenu, in application mode (hold R while starting a game)'],
            ['Space', 'About 110 MB on the SD card, plus about 1.6 GB for the Switch HD pack'],
          ],
        },
        steps: [
          { title: 'Extract to the SD card', file: 'switch', body: 'Extract it to the root of the SD card, which gives <code>/switch/Marchwind64/</code>.' },
          { title: 'Add your ROM', body: '<strong>Rename the ROM to <code>rom.z64</code> and put it in <code>/switch/Marchwind64/</code></strong>.' },
          { title: 'Optional: install the Switch HD pack', file: 'switch-hd', secondary: true, body: 'Extract it to the root of the SD card too. The PC HD pack doesn’t work here.' },
          { title: 'Hold R to start', body: '<strong>Hold R while starting any game</strong>, then pick Marchwind64 in hbmenu. The first launch imports the ROM, which takes about half a minute.' },
        ],
      },
      {
        id: 'hd', name: 'HD art pack',
        summary: 'Optional download, for computers and Android; the Switch has one of its own.',
        need: 'An optional download; the game tells you when a new one is out. Android and the Switch install it differently: see the <a href="/en/faq/#hd">FAQ</a>.',
        steps: [
          { title: 'Download and extract', file: 'hd', body: 'Extract the <code>hd</code> folder. It should contain <code>hd.json</code> and <code>art/</code> directly inside.' },
          { title: 'Place it in the user data directory', body: [
            'macOS: <code>~/Library/Application Support/SRW64Recomp/hd</code><br>Linux and Steam Deck: <code>~/.local/share/srw64-recomp/hd</code><br>Windows: <code>%LOCALAPPDATA%\\SRW64Recomp\\hd</code>',
            'It can also go in the game’s own folder.',
          ] },
          { title: 'Start the game and check', body: 'With the HD pack installed the game uses the HD art by itself. To check that it is installed, look at the “HD Pack” line on the About page of the settings window, which shows the installed version.' },
        ],
      },
    ],
    downloadsTitle: 'All downloads',
    downloadsLead: 'Use the SHA-256 checksum to verify the downloaded file.',
    file: 'File',
    size: 'Size',
    quark: 'Quark Drive',
    baidu: 'Baidu Netdisk',
    passcode: ' (passcode {p})',
    savesLink: 'Where your saves are and how to back them up →',
    faqLink: 'Trouble installing →',
    verify: 'Checksum',
    noFile: 'This version has no package for this platform yet.',
    notReleased: '0.4.0 is being prepared and cannot be downloaded yet. The installation steps are below.',
  },
  ja: {
    title: 'インストール',
    lead: 'プラットフォームを選び、対応するファイルをダウンロードします。『スーパーロボット大戦64』日本版の ROM をご用意ください。<a href="#rom">ROM の条件</a>',
    rom: {
      title: 'ROM の条件',
      rows: [
        ['バージョン', '『スーパーロボット大戦64』日本版 Rev 0（すべてのプラットフォームで必要）'],
        ['形式', '<code>.z64</code>・<code>.v64</code>・<code>.n64</code> のどのバイトオーダーでもゲームが自動で判別します'],
        ['ファイル名', '手順では <code>rom.z64</code> としていますが、<code>.n64</code>・<code>.v64</code> は <code>rom.n64</code>・<code>rom.v64</code> でもかまいません'],
        ['SHA-256', `<code class="hash">${ROM_SHA}</code>（<code>.z64</code> 形式）`],
      ],
    },
    viewSteps: '手順を見る',
    experimental: '試験的',
    reqTitle: '動作環境',
    sections: [
      {
        id: 'windows', name: 'Windows', experimental: true,
        hd: HD_PC.ja,
        summary: '展開して ROM を置き、ダブルクリックで起動します。',
        req: {
          rows: [
            ['OS', 'Windows 10 / 11（64 ビット）'],
            ['グラフィックス', 'Direct3D 12（SM 6.0）または Vulkan 1.2'],
            ['GPU', 'GeForce GT 630、Radeon HD 7750、Intel HD Graphics 510 以降'],
            ['CPU', 'x86-64-v2（第 1 世代 Intel Core i、AMD FX 以降）'],
            ['容量', '約 165 MB、HD アートパックは別に約 700 MB'],
          ],
          notes: [MINIMUM.ja, DRIVERS.ja],
        },
        steps: [
          { title: 'ダウンロードして展開', file: 'windows', body: '右クリックして「すべて展開」を選びます。ZIP の中から直接起動しないでください。' },
          { title: 'ROM を置く', body: '<strong>ROM の名前を <code>rom.z64</code> に変更し、展開したフォルダーに置きます</strong>（<code>Marchwind64.exe</code> と同じ場所）。' },
          { title: 'ゲームを起動', body: '<strong><code>Marchwind64.exe</code> をダブルクリック</strong>します。言語はシステムに合わせて選ばれ、設定で変更できます。' },
        ],
      },
      {
        id: 'macos', name: 'macOS',
        hd: HD_PC.ja,
        summary: '「アプリケーション」に入れ、初回起動時に ROM を選択します。',
        req: {
          rows: [
            ['OS', 'macOS 14 以降'],
            ['プロセッサー', 'Apple シリコン（M1 以降）。Intel Mac には非対応'],
            ['グラフィックス', 'Metal'],
            ['容量', '約 140 MB、HD アートパックは別に約 700 MB'],
          ],
        },
        steps: [
          { title: 'ダウンロードして展開', file: 'macos', body: '展開して <code>Marchwind64.app</code> を「アプリケーション」へドラッグします。' },
          { title: '起動を許可', body: 'アプリはローカル署名のみで、Apple の公証を受けていません。初回起動がブロックされた場合は、「システム設定 → プライバシーとセキュリティ」で「このまま開く」をクリックしてください。' },
          { title: 'ROM を選択', body: 'ファイル選択画面で ROM を選択します。選択内容は保存され、次回から選び直す必要はありません。' },
        ],
      },
      {
        id: 'linux', name: 'Linux',
        hd: HD_PC.ja,
        summary: '展開して ROM を置き、スクリプトを実行します。',
        req: {
          rows: [
            ['OS', '64 ビット、glibc 2.35 以降（Ubuntu 22.04、Debian 12 など）'],
            ['グラフィックス', 'Vulkan 1.2'],
            ['GPU', 'GeForce GT 630、Radeon HD 7750、Intel HD Graphics 510 以降'],
            ['CPU', 'x86-64-v2（第 1 世代 Intel Core i、AMD FX 以降）'],
            ['容量', '約 150 MB、HD アートパックは別に約 700 MB'],
          ],
          notes: [MINIMUM.ja, DRIVERS.ja],
        },
        steps: [
          { title: 'ダウンロードして展開', file: 'linux', body: '<code>~/Games/Marchwind64</code> などに展開します。' },
          { title: 'ROM を置く', body: 'ROM を <code>~/.local/share/srw64-recomp/rom.z64</code> としてコピーするか、<code>marchwind64.sh</code> と同じフォルダーに <code>rom.z64</code> という名前で置きます。' },
          { title: 'ゲームを起動', body: '<code>marchwind64.sh</code> を実行します。' },
        ],
      },
      {
        id: 'steam-deck', name: 'Steam Deck', experimental: true,
        hd: HD_PC.ja,
        summary: 'Linux 版のパッケージを使い、Steam に追加します。',
        req: {
          rows: [
            ['モデル', 'LCD 版・OLED 版'],
            ['OS', '最新の SteamOS'],
            ['容量', '約 150 MB、HD アートパックは別に約 700 MB'],
          ],
          notes: ['初回の設定はデスクトップモードで行います。'],
        },
        steps: [
          { title: 'デスクトップモードでダウンロードして展開', file: 'linux', body: '<code>~/Games/Marchwind64</code> などに展開します。' },
          { title: 'ROM を置く', body: 'ROM を <code>~/.local/share/srw64-recomp/rom.z64</code> としてコピーします。' },
          { title: 'Steam に追加', body: 'Steam を開いた状態で、フォルダー内の <code>add-to-steam.sh</code> をダブルクリックし、確認が出たら「実行」を選びます。' },
          { title: 'ゲームモードから起動', body: 'Steam ライブラリから起動します。Steam 入力は既定のままで構いません。' },
        ],
      },
      {
        id: 'android', name: 'Android', experimental: true,
        hd: HD_ANDROID.ja,
        summary: 'APK をインストールし、初回起動時に ROM を選択します。',
        req: {
          rows: [
            ['OS', 'Android 9 以降'],
            ['プロセッサー', 'arm64（64 ビット）のスマホまたは携帯機'],
            ['グラフィックス', 'Vulkan 1.1'],
            ['推奨', 'Snapdragon 865、Dimensity 1080、Exynos 1380、Tensor G2 以降で約 30 FPS'],
            ['エントリー向け', 'Dimensity 6100+、Dimensity 700 など（Mali-G57）は約 10〜14 FPS で動作'],
          ],
          notes: ['Android 端末ごとの確認状況は<a href="/ja/android/">Android 対応状況</a>をご覧ください。'],
        },
        steps: [
          { title: '端末で APK をダウンロード', file: 'android' },
          { title: 'APK をインストール', body: 'APK を開き、案内に従ってブラウザまたはファイルマネージャーに「不明なアプリのインストール」を許可し、インストールを進めます。' },
          { title: 'ROM を選択', body: 'Marchwind64 を開き、ファイル選択画面で ROM を選びます。ROM はアプリ専用のフォルダーにコピーされ、次回から選び直す必要はありません。' },
        ],
      },
      {
        id: 'switch', name: 'Nintendo Switch', experimental: true,
        summary: 'SD カードに展開し、hbmenu から起動します。',
        need: 'Switch 版のパフォーマンスはまだ最適化の途中で、プレイ中に多少カクつくことがあります。',
        req: {
          rows: [
            ['本体', 'Atmosphère（カスタムファームウェア）が動く Switch（初代モデルで確認）'],
            ['システム', 'Atmosphère と hbmenu、アプリケーションモード（R を押しながらゲームを起動）'],
            ['容量', 'SD カードに約 110 MB、Switch 版 HD パックは別に約 1.6 GB'],
          ],
        },
        steps: [
          { title: 'SD カードに展開', file: 'switch', body: 'SD カードのルートに展開すると <code>/switch/Marchwind64/</code> ができます。' },
          { title: 'ROM を置く', body: '<strong>ROM の名前を <code>rom.z64</code> に変えて <code>/switch/Marchwind64/</code> に置きます</strong>。' },
          { title: '任意：Switch 版 HD パックを導入', file: 'switch-hd', secondary: true, body: '同じく SD カードのルートに展開します。PC 版の HD パックは使えません。' },
          { title: 'R を押しながら起動', body: '<strong>R を押しながら任意のゲームを起動</strong>し、hbmenu で Marchwind64 を選びます。初回は ROM の読み込みに 30 秒ほどかかります。' },
        ],
      },
      {
        id: 'hd', name: 'HD アートパック',
        summary: '任意で導入できます。パソコンと Android 共通で、Switch には専用のものがあります。',
        need: '任意のダウンロードで、新しい版が出るとゲームが知らせます。Android と Switch は導入方法が異なります（<a href="/ja/faq/#hd">よくある質問</a>）。',
        steps: [
          { title: 'ダウンロードして展開', file: 'hd', body: '<code>hd</code> フォルダーの直下に <code>hd.json</code> と <code>art/</code> があることを確認します。' },
          { title: 'ユーザーデータフォルダーに配置', body: [
            'macOS：<code>~/Library/Application Support/SRW64Recomp/hd</code><br>Linux／Steam Deck：<code>~/.local/share/srw64-recomp/hd</code><br>Windows：<code>%LOCALAPPDATA%\\SRW64Recomp\\hd</code>',
            'ゲームのフォルダーに置くこともできます。',
          ] },
          { title: '起動して確認', body: 'HD パックが入っていれば自動で HD 画面になります。導入できたかどうかは、設定ウィンドウ「情報」ページの「HD パック」の行で、導入済みのバージョンを確認できます。' },
        ],
      },
    ],
    downloadsTitle: 'ダウンロード一覧',
    downloadsLead: 'SHA-256 チェックサムでダウンロードファイルの完全性を確認できます。',
    file: 'ファイル',
    size: 'サイズ',
    quark: 'Quark ドライブ',
    baidu: 'Baidu ネットディスク',
    passcode: '（パスコード {p}）',
    savesLink: 'セーブデータの場所とバックアップ方法 →',
    faqLink: 'インストールで困ったら →',
    verify: 'チェックサム',
    noFile: 'このバージョンにはこのプラットフォーム向けのパッケージがまだありません。',
    notReleased: '0.4.0 は公開準備中のため、まだダウンロードできません。インストール手順は先にご覧いただけます。',
  },
  ko: {
    title: '설치',
    lead: '플랫폼을 고르고 맞는 파일을 내려받습니다. 『슈퍼로봇대전64』 일본판 ROM이 필요합니다. <a href="#rom">ROM 요구 사항</a>',
    rom: {
      title: 'ROM 요구 사항',
      rows: [
        ['버전', '『슈퍼로봇대전64』 일본판 Rev 0(모든 플랫폼에 필요)'],
        ['형식', '바이트 순서는 상관없으며(<code>.z64</code>, <code>.v64</code>, <code>.n64</code>), 게임이 알아서 인식합니다'],
        ['파일 이름', '단계에서는 <code>rom.z64</code>로 적었으며, <code>.n64</code>나 <code>.v64</code> 파일은 <code>rom.n64</code>나 <code>rom.v64</code>로 해도 됩니다'],
        ['SHA-256', `<code class="hash">${ROM_SHA}</code>(<code>.z64</code> 형식)`],
      ],
    },
    viewSteps: '설치 단계 보기',
    experimental: '실험적',
    reqTitle: '시스템 요구 사항',
    sections: [
      {
        id: 'windows', name: 'Windows', experimental: true,
        hd: HD_PC.ko,
        summary: '압축을 풀고 ROM을 넣은 뒤 더블클릭합니다.',
        req: {
          rows: [
            ['OS', 'Windows 10 / 11, 64비트'],
            ['그래픽', 'Direct3D 12(SM 6.0) 또는 Vulkan 1.2'],
            ['GPU', 'GeForce GT 630, Radeon HD 7750, Intel HD Graphics 510 이상'],
            ['CPU', 'x86-64-v2(1세대 Intel Core i, AMD FX 이상)'],
            ['용량', '약 165 MB, HD 팩은 약 700 MB 추가'],
          ],
          notes: [MINIMUM.ko, DRIVERS.ko],
        },
        steps: [
          { title: '내려받아 압축 풀기', file: 'windows', body: '마우스 오른쪽 버튼으로 “모두 압축 풀기”를 선택하세요. 압축 파일 안에서 바로 실행하지 마세요.' },
          { title: 'ROM 넣기', body: '<strong>ROM 이름을 <code>rom.z64</code>로 바꿔 압축을 푼 폴더에 넣으세요</strong>. <code>Marchwind64.exe</code>와 같은 위치입니다.' },
          { title: '게임 실행', body: '<strong><code>Marchwind64.exe</code>를 더블클릭하세요</strong>. 언어는 시스템을 따르며 설정에서 바꿀 수 있습니다.' },
        ],
      },
      {
        id: 'macos', name: 'macOS',
        hd: HD_PC.ko,
        summary: '앱을 응용 프로그램 폴더로 옮기고 처음 실행할 때 ROM을 고릅니다.',
        req: {
          rows: [
            ['OS', 'macOS 14 이상'],
            ['프로세서', 'Apple 실리콘(M1 이상). Intel Mac은 지원하지 않습니다'],
            ['그래픽', 'Metal'],
            ['용량', '약 140 MB, HD 팩은 약 700 MB 추가'],
          ],
        },
        steps: [
          { title: '내려받아 압축 풀기', file: 'macos', body: '압축을 풀고 <code>Marchwind64.app</code>을 응용 프로그램 폴더로 끌어다 놓습니다.' },
          { title: '실행 허용', body: '이 앱은 로컬 서명만 되어 있고 Apple의 공증을 받지 않았습니다. 처음 실행이 차단되면 시스템 설정 → 개인정보 보호 및 보안에서 “그래도 열기”를 클릭하세요.' },
          { title: 'ROM 고르기', body: '파일 선택 창에서 ROM을 고릅니다. 앱이 선택을 기억하므로 다음부터는 다시 고를 필요가 없습니다.' },
        ],
      },
      {
        id: 'linux', name: 'Linux',
        hd: HD_PC.ko,
        summary: '압축을 풀고 ROM을 넣은 뒤 스크립트를 실행합니다.',
        req: {
          rows: [
            ['OS', '64비트, glibc 2.35 이상(Ubuntu 22.04, Debian 12 이상)'],
            ['그래픽', 'Vulkan 1.2'],
            ['GPU', 'GeForce GT 630, Radeon HD 7750, Intel HD Graphics 510 이상'],
            ['CPU', 'x86-64-v2(1세대 Intel Core i, AMD FX 이상)'],
            ['용량', '약 150 MB, HD 팩은 약 700 MB 추가'],
          ],
          notes: [MINIMUM.ko, DRIVERS.ko],
        },
        steps: [
          { title: '내려받아 압축 풀기', file: 'linux', body: '<code>~/Games/Marchwind64</code> 등 원하는 폴더에 압축을 풉니다.' },
          { title: 'ROM 넣기', body: 'ROM 사본을 <code>~/.local/share/srw64-recomp/rom.z64</code>로 저장하거나, <code>marchwind64.sh</code>와 같은 폴더에 <code>rom.z64</code>라는 이름으로 둡니다.' },
          { title: '게임 실행', body: '<code>marchwind64.sh</code>를 실행합니다.' },
        ],
      },
      {
        id: 'steam-deck', name: 'Steam Deck', experimental: true,
        hd: HD_PC.ko,
        summary: 'Linux 패키지를 사용해 Steam에 추가합니다.',
        req: {
          rows: [
            ['모델', 'LCD, OLED'],
            ['OS', '최신 SteamOS'],
            ['용량', '약 150 MB, HD 팩은 약 700 MB 추가'],
          ],
          notes: ['처음 한 번은 데스크톱 모드에서 설정합니다.'],
        },
        steps: [
          { title: '데스크톱 모드에서 내려받아 압축 풀기', file: 'linux', body: '<code>~/Games/Marchwind64</code> 등의 폴더에 압축을 풉니다.' },
          { title: 'ROM 넣기', body: 'ROM 사본을 <code>~/.local/share/srw64-recomp/rom.z64</code>로 저장합니다.' },
          { title: 'Steam에 추가', body: 'Steam을 연 상태에서 폴더의 <code>add-to-steam.sh</code>를 더블클릭하고, 물으면 “실행”을 선택하세요.' },
          { title: '게임 모드에서 실행', body: 'Steam 라이브러리에서 실행하세요. Steam 입력 설정은 기본값이면 됩니다.' },
        ],
      },
      {
        id: 'android', name: 'Android', experimental: true,
        hd: HD_ANDROID.ko,
        summary: 'APK를 설치하고 처음 실행할 때 ROM을 고릅니다.',
        req: {
          rows: [
            ['OS', 'Android 9 이상'],
            ['프로세서', 'arm64(64비트) 스마트폰 또는 휴대용 기기'],
            ['그래픽', 'Vulkan 1.1'],
            ['권장', 'Snapdragon 865, Dimensity 1080, Exynos 1380, Tensor G2 이상, 약 30 FPS'],
            ['보급형', 'Dimensity 6100+, Dimensity 700 등(Mali-G57)은 약 10~14 FPS'],
          ],
          notes: ['Android 기기별 테스트 결과: <a href="/ko/android/">Android 호환성</a>.'],
        },
        steps: [
          { title: '기기에서 APK 내려받기', file: 'android' },
          { title: 'APK 설치', body: 'APK를 엽니다. 안내가 나오면 브라우저나 파일 관리자에 “출처를 알 수 없는 앱 설치”를 허용한 뒤 설치를 마칩니다.' },
          { title: 'ROM 고르기', body: 'Marchwind64를 열고 파일 선택 창에서 ROM을 고릅니다. ROM은 앱 전용 폴더에 복사되므로 다음부터는 다시 고를 필요가 없습니다.' },
        ],
      },
      {
        id: 'switch', name: 'Nintendo Switch', experimental: true,
        summary: 'SD 카드에 압축을 풀고 hbmenu에서 실행합니다.',
        need: 'Switch 버전은 아직 성능 최적화가 더 필요하며, 플레이 중에 다소 끊김이 있을 수 있습니다.',
        req: {
          rows: [
            ['본체', 'Atmosphère 커스텀 펌웨어를 실행할 수 있는 Switch(초기 모델에서 테스트)'],
            ['시스템', 'Atmosphère와 hbmenu, 애플리케이션 모드(R을 누른 채 게임 실행)'],
            ['용량', 'SD 카드에 약 110 MB, Switch용 HD 팩은 약 1.6 GB 추가'],
          ],
        },
        steps: [
          { title: 'SD 카드에 압축 풀기', file: 'switch', body: 'SD 카드 최상위에 압축을 풀면 <code>/switch/Marchwind64/</code>가 생깁니다.' },
          { title: 'ROM 넣기', body: '<strong>ROM 이름을 <code>rom.z64</code>로 바꿔 <code>/switch/Marchwind64/</code>에 넣습니다</strong>.' },
          { title: '선택 사항: Switch용 HD 팩 설치', file: 'switch-hd', secondary: true, body: '마찬가지로 SD 카드 최상위에 압축을 푸세요. PC용 HD 팩은 쓸 수 없습니다.' },
          { title: 'R을 누른 채 실행', body: '<strong>R을 누른 채 아무 게임이나 실행</strong>하고 hbmenu에서 Marchwind64를 고르세요. 처음 실행할 때 ROM을 가져오는 데 30초쯤 걸립니다.' },
        ],
      },
      {
        id: 'hd', name: 'HD 팩',
        summary: '선택 사항입니다. 컴퓨터와 Android용 하나와 Switch 전용 팩이 있습니다.',
        need: '선택 다운로드이며, 새 버전이 나오면 게임이 알려 줍니다. Android와 Switch는 설치 방법이 다릅니다(<a href="/ko/faq/#hd">자주 묻는 질문</a>).',
        steps: [
          { title: '내려받아 압축 풀기', file: 'hd', body: '<code>hd</code> 폴더를 꺼냅니다. 폴더 바로 안에 <code>hd.json</code>과 <code>art/</code>가 있어야 합니다.' },
          { title: '사용자 데이터 폴더에 넣기', body: [
            'macOS: <code>~/Library/Application Support/SRW64Recomp/hd</code><br>Linux와 Steam Deck: <code>~/.local/share/srw64-recomp/hd</code><br>Windows: <code>%LOCALAPPDATA%\\SRW64Recomp\\hd</code>',
            '게임 폴더에 두어도 됩니다.',
          ] },
          { title: '실행해서 확인', body: 'HD 팩이 설치되어 있으면 게임이 알아서 HD 그래픽을 씁니다. 설치되었는지는 설정 창 “정보” 페이지의 “HD 리소스 팩” 줄에서 설치된 버전으로 확인할 수 있습니다.' },
        ],
      },
    ],
    downloadsTitle: '전체 다운로드',
    downloadsLead: 'SHA-256 체크섬으로 내려받은 파일을 검증할 수 있습니다.',
    file: '파일',
    size: '크기',
    quark: 'Quark 드라이브',
    baidu: 'Baidu 넷디스크',
    passcode: ' (비밀번호 {p})',
    savesLink: '세이브 위치와 백업 방법 →',
    faqLink: '설치 문제 해결 →',
    verify: '체크섬',
    noFile: '이 버전에는 아직 이 플랫폼용 패키지가 없습니다.',
    notReleased: '0.4.0은 준비 중이라 아직 내려받을 수 없습니다. 설치 단계는 아래에서 미리 볼 수 있습니다.',
  },
};
