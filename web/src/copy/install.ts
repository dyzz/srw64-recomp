import type { Platform } from '../data/release';
import type { TextLang } from '../i18n';

// `body`, tips and the ROM rows may contain inline HTML (<code>, <strong>, <a>).
// A step with `file` shows that download, `secondary` for an optional one.
export type Step = { title: string; body?: string | string[]; file?: Platform; secondary?: boolean };
export type Tip = { title: string; body: string; warn?: boolean };
// What the platform needs, shown beside its steps: rows of [label, value], notes may hold links.
export type Requirements = { rows: [string, string][]; notes?: string[] };
export type Section = { id: string; name: string; summary: string; need?: string; experimental?: boolean; req?: Requirements; steps: Step[]; tips?: Tip[] };
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
  verify: string;
  noFile: string;
  notReleased: string;
};

const ROM_SHA = 'ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e';
// The GPU floor is Zelda64Recomp's, the same renderer, and untested below that; said beside it.
const MINIMUM = {
  zh: '以上是可以运行的最低配置。显卡门槛与其他使用 RT64 渲染的重编译项目相同；更旧的硬件没有测试过。',
  en: 'This is the minimum the game runs on. The graphics requirements are the same as other recompilations drawn with RT64; older hardware has not been tested.',
  ja: 'ゲームが動作する最低限の構成です。グラフィックスの条件は RT64 で描画するほかのリコンパイル作品と同じで、これより古いハードウェアは未確認です。',
  ko: '게임이 실행되는 최소 사양입니다. 그래픽 요구 사항은 RT64로 렌더링하는 다른 리컴파일 작품과 같으며, 이보다 오래된 하드웨어는 테스트하지 않았습니다.',
};
const DRIVERS = {
  zh: '启动闪退、卡住或画面异常时，请先更新显卡驱动，见<a href="/zh/faq/#gpu">常见问题</a>。',
  en: 'If the game crashes at start, freezes or draws wrongly, update your graphics drivers first; see the <a href="/en/faq/#gpu">FAQ</a>.',
  ja: '起動直後に落ちる、止まる、表示がおかしいときは、まずグラフィックスドライバーを更新してください。詳しくは<a href="/ja/faq/#gpu">よくある質問</a>をご覧ください。',
  ko: '실행하자마자 꺼지거나 멈추거나 화면이 이상하게 나오면 먼저 그래픽 드라이버를 업데이트하세요. <a href="/ko/faq/#gpu">자주 묻는 질문</a>을 참고하세요.',
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
        summary: '解压，放好 ROM，双击启动。',
        req: {
          rows: [
            ['系统', 'Windows 10 / 11，64 位'],
            ['图形接口', 'Direct3D 12（Shader Model 6.0）或 Vulkan 1.2'],
            ['NVIDIA', 'GeForce GT 630（Kepler）及以上'],
            ['AMD', 'Radeon HD 7750 及以上'],
            ['Intel', 'Intel HD Graphics 510（第 6 代酷睿）及以上'],
            ['CPU', '支持 x86-64-v2 的处理器（Intel 酷睿 i 系列第 1 代、AMD FX 及以后）'],
            ['驱动', '最新的显卡驱动'],
            ['空间', '约 165 MB，HD 美术包另需约 700 MB'],
          ],
          notes: [MINIMUM.zh, DRIVERS.zh],
        },
        steps: [
          { title: '下载并解压', file: 'windows', body: '右键选「全部解压缩」，得到 <code>Marchwind64-windows-x64</code> 文件夹。不要在压缩包里直接双击运行。' },
          { title: '放好 ROM', body: '<strong>把 ROM 改名为 <code>rom.z64</code>，放进这个文件夹</strong>（里面有 <code>Marchwind64.cmd</code> 和 <code>Marchwind64.exe</code>）。ROM 是压缩包的话先解压。' },
          { title: '启动游戏', body: '<strong>双击 <code>Marchwind64.exe</code></strong>（双击 <code>Marchwind64.cmd</code> 也一样）。首次启动跟随系统语言（中文系统用简体中文，日文系统用日语，其他用英语），可在窗口的「语言」菜单或设置窗口中切换。' },
        ],
        tips: [
          { title: '注意扩展名', warn: true, body: 'Windows 默认隐藏扩展名，容易改成 <code>rom.z64.z64</code>。先在资源管理器的「查看」里勾选「文件扩展名」再改。找不到 ROM 时，启动会弹窗说明该放在哪里。' },
          { title: '出现安全提示', warn: true, body: '选择「更多信息」→「仍要运行」。' },
          { title: 'ROM 的另一个位置', body: '也可以放在 <code>%LOCALAPPDATA%\\SRW64Recomp\\rom.z64</code>（在资源管理器地址栏输入 <code>%LOCALAPPDATA%</code> 回车，新建 <code>SRW64Recomp</code> 文件夹）。' },
          { title: '便携模式', body: '在 <code>Marchwind64.exe</code> 旁边新建一个空文件夹 <code>user-data</code>，存档和设置就放在里面（第一次会从 <code>%LOCALAPPDATA%\\SRW64Recomp</code> 补入已有的设置和存档，正常退出后同步回去）。整个游戏文件夹连同 ROM 可以放在 U 盘或移动硬盘上，换盘符、换电脑都能接着玩。' },
        ],
      },
      {
        id: 'macos', name: 'macOS',
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
        summary: '解压，放好 ROM，运行脚本。',
        req: {
          rows: [
            ['系统', '64 位，glibc 2.35 及以上（如 Ubuntu 22.04、Debian 12）'],
            ['图形接口', 'Vulkan 1.2'],
            ['显卡', 'GeForce GT 630、Radeon HD 7750、Intel HD Graphics 510 及以上'],
            ['CPU', '支持 x86-64-v2 的处理器（Intel 酷睿 i 系列第 1 代、AMD FX 及以后）'],
            ['驱动', '最新的 Mesa 或 NVIDIA 驱动'],
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
          { title: '添加到 Steam', body: '打开 Steam，双击文件夹中的 <code>add-to-steam.sh</code>，出现提示时选择「执行」。脚本会将游戏加入 Steam 库，并设置封面和图标。' },
          { title: '回到游戏模式启动', body: '从 Steam 库启动。使用默认 Steam 输入设置即可，游戏会自动识别 Deck 并显示对应按键。' },
        ],
        tips: [
          { title: '键位', body: '游戏会识别 Deck 并使用适合 Deck 的默认键位，设置里可以改键。' },
          { title: '退出游戏', body: '从 Steam 菜单选择「退出游戏」可保存并退出。' },
        ],
      },
      {
        id: 'android', name: 'Android', experimental: true,
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
        tips: [
          { title: '触屏按键', body: '触摸屏幕时显示当前画面需要的按键；切换到手柄或键盘操作时自动隐藏。点击顶边的「设置」按钮可打开设置窗口。' },
          { title: '导入 HD 美术包', body: '长按应用图标选「导入 HD 包」，或在文件管理器里用 Marchwind64 打开下载的 HD 包 zip，应用会自动解压安装。首次选择 ROM 后也会询问一次。' },
        ],
      },
      {
        id: 'switch', name: 'Nintendo Switch', experimental: true,
        summary: '解压到 SD 卡，从 hbmenu 打开。',
        req: {
          rows: [
            ['主机', '能运行 Atmosphère 自制系统的 Switch（初代机型上测试）'],
            ['系统', 'Atmosphère 与 hbmenu，应用模式（按住 R 启动游戏）'],
            ['空间', 'SD 卡约 110 MB，Switch 版 HD 包另需约 1.6 GB'],
          ],
        },
        steps: [
          { title: '解压到 SD 卡', file: 'switch', body: '解压到 SD 卡根目录，得到 <code>/switch/Marchwind64/Marchwind64.nro</code> 和旁边的几个文件夹。用读卡器或 ftpd 等工具拷进去都可以。' },
          { title: '放好 ROM', body: '<strong>把 ROM 改名为 <code>rom.z64</code>，放进 <code>/switch/Marchwind64/</code></strong>。' },
          { title: '可选：安装 Switch 版 HD 包', file: 'switch-hd', secondary: true, body: '同样解压到 SD 卡根目录，得到 <code>/switch/Marchwind64/hd/</code>。电脑版的 HD 包在 Switch 上不能用。' },
          { title: '按住 R 启动', body: '<strong>按住 R 键启动任意一个游戏</strong>（插一张实体卡带最省事），进入 hbmenu 后选 Marchwind64。第一次启动会先在主机上从 ROM 导入游戏数据，约半分钟，屏幕上有进度；之后每次启动只要几秒。' },
        ],
        tips: [
          { title: '性能还在优化', warn: true, body: 'Switch 版目前性能还有待优化，运行时会有一定卡顿。' },
          { title: '只支持应用模式', warn: true, body: '从相簿打开的 hbmenu 内存不够，游戏会提示改用按住 R 的方式。' },
          { title: '更新到新版', body: '删掉旧的 <code>fonts</code>、<code>dialogue</code>、<code>stages</code>、<code>splash</code> 四个文件夹，再解压新版；存档、设置和 <code>game</code> 文件夹保留，新版第一次启动会自动重新导入一次。HD 包出新版时整体替换 <code>hd</code> 文件夹。' },
          { title: '没有更新提示', body: 'Switch 版没有游戏内的更新提示，新版本请留意本页或 GitHub。' },
          { title: '安装形式', body: '本项目只提供 NRO 自制程序，不提供 NSP，也不需要签名补丁。' },
        ],
      },
      {
        id: 'hd', name: 'HD 美术包',
        summary: '可选下载。电脑和 Android 通用，Switch 另有一份。',
        need: '可选下载，有独立的版本号，有新版时游戏会提示。',
        steps: [
          { title: '下载并解压', file: 'hd', body: '解压得到 <code>hd</code> 文件夹，其根目录应包含 <code>hd.json</code> 和 <code>art/</code>。' },
          { title: '放入用户数据目录', body: [
            'macOS：<code>~/Library/Application Support/SRW64Recomp/hd</code><br>Linux／Steam Deck：<code>~/.local/share/srw64-recomp/hd</code><br>Windows：<code>%LOCALAPPDATA%\\SRW64Recomp\\hd</code>',
            '也可以放在游戏所在的文件夹里，与 <code>Marchwind64.app</code>、<code>marchwind64.sh</code> 或 <code>Marchwind64.exe</code> 放在一起；两处都有时用用户数据目录里的。',
          ] },
          { title: '启动并确认', body: '装好 HD 包后会自动使用 HD 画面。要确认是否装好，可以看设置窗口「关于」页的「HD 包」一行，那里显示已安装的版本。' },
        ],
        tips: [
          { title: 'Android 与 Switch', body: 'Android 不用手动解压，按 <a href="#android">Android</a> 一节的说明在应用里导入。Switch 用单独的 Switch 版 HD 包，下载和安装见 <a href="#switch">Nintendo Switch</a> 一节。' },
          { title: 'macOS 放在应用旁边时', body: '应用要先在访达里移动过一次（例如拖进「应用程序」）。' },
          { title: '切换原版与 HD', body: '随时切换：设置窗口「通用」页的「画面」，或按快捷键（默认键盘 4 或 F6、手柄按下右摇杆）；Windows 和 macOS 的菜单栏里也有「HD 画面」。切换立即生效，不影响存档。这个选择不会保存，每次启动都从 HD 开始。' },
          { title: '地图单位图标', body: 'HD 画面下，地图上的单位图标可以单独换回原版：设置「通用」页的「地图单位图标」，这一项会记住。' },
          { title: '版本与更新', body: 'HD 包有自己的版本号，内容变了才发新版，不必每次更新应用都重新下载。有新版时游戏的更新检查会提示，更新时整体替换旧的 <code>hd</code> 文件夹。' },
          { title: '图像来源', body: '包内图像主要由 AI 参考原版画面生成或放大重绘，详见常见问题。' },
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
        summary: 'Unzip, add your ROM, then double-click to start.',
        req: {
          rows: [
            ['OS', 'Windows 10 / 11, 64-bit'],
            ['Graphics', 'Direct3D 12 (Shader Model 6.0) or Vulkan 1.2'],
            ['NVIDIA', 'GeForce GT 630 (Kepler) or newer'],
            ['AMD', 'Radeon HD 7750 or newer'],
            ['Intel', 'Intel HD Graphics 510 (6th-gen Core) or newer'],
            ['CPU', 'An x86-64-v2 processor (1st-gen Intel Core i, AMD FX or newer)'],
            ['Drivers', 'Up-to-date graphics drivers'],
            ['Space', 'About 165 MB, plus about 700 MB for the HD art pack'],
          ],
          notes: [MINIMUM.en, DRIVERS.en],
        },
        steps: [
          { title: 'Download and extract', file: 'windows', body: 'Right-click the file and choose “Extract All”. You get a <code>Marchwind64-windows-x64</code> folder; do not run the game from inside the zip.' },
          { title: 'Add your ROM', body: '<strong>Rename your ROM to <code>rom.z64</code> and put it in that folder</strong> (the one with <code>Marchwind64.cmd</code> and <code>Marchwind64.exe</code>). Unzip the ROM first if it is in an archive.' },
          { title: 'Start the game', body: '<strong>Double-click <code>Marchwind64.exe</code></strong> (<code>Marchwind64.cmd</code> does the same). The first launch follows the system language (Simplified Chinese on a Chinese system, Japanese on a Japanese one, English otherwise); change it in the window’s Language menu or the settings window.' },
        ],
        tips: [
          { title: 'Watch the extension', warn: true, body: 'Windows hides extensions by default, so the file can end up as <code>rom.z64.z64</code>. Turn on “File name extensions” under View in File Explorer first. If no ROM is found, the game says where to put it.' },
          { title: 'If a security prompt appears', warn: true, body: 'Choose “More info” → “Run anyway”.' },
          { title: 'Another place for the ROM', body: 'It can also go in <code>%LOCALAPPDATA%\\SRW64Recomp\\rom.z64</code> (type <code>%LOCALAPPDATA%</code> in File Explorer’s address bar and create a <code>SRW64Recomp</code> folder).' },
          { title: 'Portable mode', body: 'Create an empty folder named <code>user-data</code> next to <code>Marchwind64.exe</code>, and saves and settings are kept in it (the first time, settings and saves already in <code>%LOCALAPPDATA%\\SRW64Recomp</code> are copied in, and a normal exit copies them back). The whole game folder, ROM included, can live on a USB stick or external drive and keeps working under another drive letter or on another computer.' },
        ],
      },
      {
        id: 'macos', name: 'macOS',
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
        summary: 'Extract, add your ROM, then run the script.',
        req: {
          rows: [
            ['OS', '64-bit, glibc 2.35 or later (Ubuntu 22.04, Debian 12 or newer)'],
            ['Graphics', 'Vulkan 1.2'],
            ['GPU', 'GeForce GT 630, Radeon HD 7750, Intel HD Graphics 510 or newer'],
            ['CPU', 'An x86-64-v2 processor (1st-gen Intel Core i, AMD FX or newer)'],
            ['Drivers', 'Up-to-date Mesa or NVIDIA drivers'],
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
          { title: 'Add it to Steam', body: 'Open Steam, then double-click <code>add-to-steam.sh</code> in the folder, choosing “Execute” if prompted. The script adds the game to your library with its cover art and icon.' },
          { title: 'Start it from Game Mode', body: 'Launch from your library with the default Steam Input settings. The game detects the Deck and displays matching button prompts.' },
        ],
        tips: [
          { title: 'Controls', body: 'The game recognises the Deck and uses Deck-friendly default controls, which you can remap in the settings.' },
          { title: 'Quitting', body: 'Quitting through the Steam menu saves the game before exiting.' },
        ],
      },
      {
        id: 'android', name: 'Android', experimental: true,
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
        tips: [
          { title: 'Touch buttons', body: 'Touching the screen brings up the buttons needed for the current screen. They hide when you switch to a controller or keyboard. Tap Settings along the top to open the settings window.' },
          { title: 'Importing the HD art pack', body: 'Long-press the app icon and choose “Import HD pack”, or open the downloaded HD zip with Marchwind64 from a file manager; the app unpacks and installs it. You are also asked once after choosing the ROM.' },
        ],
      },
      {
        id: 'switch', name: 'Nintendo Switch', experimental: true,
        summary: 'Extract to the SD card and open it from hbmenu.',
        req: {
          rows: [
            ['Console', 'A Switch that runs Atmosphère custom firmware (tested on the original model)'],
            ['System', 'Atmosphère and hbmenu, in application mode (hold R while starting a game)'],
            ['Space', 'About 110 MB on the SD card, plus about 1.6 GB for the Switch HD pack'],
          ],
        },
        steps: [
          { title: 'Extract to the SD card', file: 'switch', body: 'Extract it to the root of the SD card, giving <code>/switch/Marchwind64/Marchwind64.nro</code> and the folders beside it. A card reader or ftpd both work.' },
          { title: 'Add your ROM', body: '<strong>Rename the ROM to <code>rom.z64</code> and put it in <code>/switch/Marchwind64/</code></strong>.' },
          { title: 'Optional: install the Switch HD pack', file: 'switch-hd', secondary: true, body: 'Extract it to the SD card’s root too, giving <code>/switch/Marchwind64/hd/</code>. The computers’ HD pack does not work on the Switch.' },
          { title: 'Hold R to start', body: '<strong>Hold R while starting any game</strong> (a game card is easiest) to open hbmenu, and pick Marchwind64. The first launch imports the game data from the ROM on the console in about half a minute, with its progress on screen; later launches take a few seconds.' },
        ],
        tips: [
          { title: 'Performance is still being tuned', warn: true, body: 'The Switch version still needs optimizing, and you will see some stutter while playing.' },
          { title: 'Application mode only', warn: true, body: 'hbmenu opened from the Album has too little memory, and the game tells you to hold R instead.' },
          { title: 'Updating', body: 'Delete the old <code>fonts</code>, <code>dialogue</code>, <code>stages</code> and <code>splash</code> folders and extract the new version; saves, settings and the <code>game</code> folder stay, and the new version imports once more on its first launch. A new HD pack replaces the whole <code>hd</code> folder.' },
          { title: 'No update notice', body: 'The Switch version has no in-game update notice: watch this page or GitHub for new versions.' },
          { title: 'What you get', body: 'This project releases an NRO homebrew program only: no NSP, and no signature patches needed.' },
        ],
      },
      {
        id: 'hd', name: 'HD art pack',
        summary: 'Optional download, for computers and Android; the Switch has one of its own.',
        need: 'Optional. Versioned separately; the game says when there is a new one.',
        steps: [
          { title: 'Download and extract', file: 'hd', body: 'Extract the <code>hd</code> folder. It should contain <code>hd.json</code> and <code>art/</code> directly inside.' },
          { title: 'Place it in the user data directory', body: [
            'macOS: <code>~/Library/Application Support/SRW64Recomp/hd</code><br>Linux and Steam Deck: <code>~/.local/share/srw64-recomp/hd</code><br>Windows: <code>%LOCALAPPDATA%\\SRW64Recomp\\hd</code>',
            'It can also go in the game’s own folder, next to <code>Marchwind64.app</code>, <code>marchwind64.sh</code> or <code>Marchwind64.exe</code>; when both have one, the user data directory’s is used.',
          ] },
          { title: 'Start the game and check', body: 'With the HD pack installed the game uses the HD art by itself. To check that it is installed, look at the “HD Pack” line on the About page of the settings window, which shows the installed version.' },
        ],
        tips: [
          { title: 'Android and the Switch', body: 'On Android there is nothing to unzip: import it in the app as described under <a href="#android">Android</a>. The Switch takes a Switch HD pack of its own: see <a href="#switch">Nintendo Switch</a> for the download and how to install it.' },
          { title: 'Next to the app on macOS', body: 'This works once the app has been moved in Finder (into Applications, say).' },
          { title: 'Switching between original and HD', body: 'At any time, under “Images” on the General page of the settings window, or with the shortcut (by default 4 or F6 on the keyboard, or pressing the right stick on a controller); the Windows and macOS menu bars also have “HD Pictures”. It takes effect at once and does not touch your saves. The choice is not saved: the game always starts in HD.' },
          { title: 'Map unit icons', body: 'In HD you can keep the original unit icons on the map: “Map unit icons” on the General page. This one is remembered.' },
          { title: 'Versions and updates', body: 'The HD pack has versions of its own and only gets a new one when its contents change, so you do not need to download it again with every app update. The game’s update check says when there is a new one; replace the whole <code>hd</code> folder when you update.' },
          { title: 'Where the images come from', body: 'Most images are generated or upscaled with AI using the original graphics as a reference; see the FAQ.' },
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
        summary: '展開して ROM を置き、ダブルクリックで起動します。',
        req: {
          rows: [
            ['OS', 'Windows 10 / 11（64 ビット）'],
            ['グラフィックス', 'Direct3D 12（Shader Model 6.0）または Vulkan 1.2'],
            ['NVIDIA', 'GeForce GT 630（Kepler）以降'],
            ['AMD', 'Radeon HD 7750 以降'],
            ['Intel', 'Intel HD Graphics 510（第 6 世代 Core）以降'],
            ['CPU', 'x86-64-v2 対応のプロセッサー（第 1 世代 Intel Core i、AMD FX 以降）'],
            ['ドライバー', '最新のグラフィックスドライバー'],
            ['容量', '約 165 MB、HD アートパックは別に約 700 MB'],
          ],
          notes: [MINIMUM.ja, DRIVERS.ja],
        },
        steps: [
          { title: 'ダウンロードして展開', file: 'windows', body: '右クリックして「すべて展開」を選びます。<code>Marchwind64-windows-x64</code> フォルダーができます。zip の中から直接起動しないでください。' },
          { title: 'ROM を置く', body: '<strong>ROM の名前を <code>rom.z64</code> に変更し、このフォルダーに置きます</strong>（<code>Marchwind64.cmd</code> と <code>Marchwind64.exe</code> があるフォルダーです）。ROM が圧縮ファイルなら先に展開してください。' },
          { title: 'ゲームを起動', body: '<strong><code>Marchwind64.exe</code> をダブルクリックします</strong>（<code>Marchwind64.cmd</code> でも同じです）。初回はシステムの言語で起動します（中国語のシステムでは簡体字中国語、日本語では日本語、それ以外は英語）。ウィンドウの「言語」メニューか設定ウィンドウで変更できます。' },
        ],
        tips: [
          { title: '拡張子に注意', warn: true, body: 'Windows は既定で拡張子を隠すため、<code>rom.z64.z64</code> になりがちです。エクスプローラーの「表示」で「ファイル名拡張子」をオンにしてから変更してください。ROM が見つからない場合は、起動時に置き場所が表示されます。' },
          { title: 'セキュリティ警告が出たら', warn: true, body: '「詳細情報」→「実行」を選んでください。' },
          { title: 'ROM のもう一つの置き場所', body: '<code>%LOCALAPPDATA%\\SRW64Recomp\\rom.z64</code> に置くこともできます（エクスプローラーのアドレスバーに <code>%LOCALAPPDATA%</code> と入力し、<code>SRW64Recomp</code> フォルダーを作成）。' },
          { title: 'ポータブルモード', body: '<code>Marchwind64.exe</code> と同じ場所に空の <code>user-data</code> フォルダーを作ると、セーブと設定はそこに保存されます（初回は <code>%LOCALAPPDATA%\\SRW64Recomp</code> にある設定とセーブを取り込み、通常終了時に書き戻します）。ROM を含むゲームフォルダーごと USB メモリや外付けドライブに置けば、ドライブ文字が変わっても、別の PC でも続きから遊べます。' },
        ],
      },
      {
        id: 'macos', name: 'macOS',
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
        summary: '展開して ROM を置き、スクリプトを実行します。',
        req: {
          rows: [
            ['OS', '64 ビット、glibc 2.35 以降（Ubuntu 22.04、Debian 12 など）'],
            ['グラフィックス', 'Vulkan 1.2'],
            ['GPU', 'GeForce GT 630、Radeon HD 7750、Intel HD Graphics 510 以降'],
            ['CPU', 'x86-64-v2 対応のプロセッサー（第 1 世代 Intel Core i、AMD FX 以降）'],
            ['ドライバー', '最新の Mesa または NVIDIA ドライバー'],
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
          { title: 'Steam に追加', body: 'Steam を開いた状態で、フォルダー内の <code>add-to-steam.sh</code> をダブルクリックし、確認画面では「実行」を選びます。カバー画像とアイコン付きでゲームがライブラリに追加されます。' },
          { title: 'ゲームモードから起動', body: 'Steam 入力は初期設定のままライブラリから起動します。ゲームが Deck を自動認識し、対応するボタンを表示します。' },
        ],
        tips: [
          { title: '操作', body: 'Deck を認識し、Deck 向けの初期操作で起動します。操作は設定で割り当てを変更できます。' },
          { title: '終了', body: 'Steam メニューの「ゲームを終了」でセーブして終了できます。' },
        ],
      },
      {
        id: 'android', name: 'Android', experimental: true,
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
        tips: [
          { title: 'タッチボタン', body: '画面に触れると、現在の画面で使うタッチボタンが表示されます。コントローラーやキーボードの操作に切り替えると自動で隠れます。上端の「設定」ボタンで設定ウィンドウを開きます。' },
          { title: 'HD アートパックの導入', body: 'アプリアイコンを長押しして「HD パックを導入」を選ぶか、ダウンロードした HD パックの zip をファイルマネージャーから Marchwind64 で開くと、自動で展開して導入します。初回の ROM 選択後にも一度確認されます。' },
        ],
      },
      {
        id: 'switch', name: 'Nintendo Switch', experimental: true,
        summary: 'SD カードに展開し、hbmenu から起動します。',
        req: {
          rows: [
            ['本体', 'Atmosphère（カスタムファームウェア）が動く Switch（初代モデルで確認）'],
            ['システム', 'Atmosphère と hbmenu、アプリケーションモード（R を押しながらゲームを起動）'],
            ['容量', 'SD カードに約 110 MB、Switch 版 HD パックは別に約 1.6 GB'],
          ],
        },
        steps: [
          { title: 'SD カードに展開', file: 'switch', body: 'SD カードのルートに展開します。<code>/switch/Marchwind64/Marchwind64.nro</code> と横のフォルダーができます。カードリーダーでも ftpd などでも構いません。' },
          { title: 'ROM を置く', body: '<strong>ROM の名前を <code>rom.z64</code> に変えて <code>/switch/Marchwind64/</code> に置きます</strong>。' },
          { title: '任意：Switch 版 HD パックを導入', file: 'switch-hd', secondary: true, body: '同じく SD カードのルートに展開します（<code>/switch/Marchwind64/hd/</code> になります）。パソコン版の HD パックは Switch では使えません。' },
          { title: 'R を押しながら起動', body: '<strong>R ボタンを押しながら任意のゲームを起動</strong>し（ゲームカードを挿すのがいちばん簡単です）、開いた hbmenu で Marchwind64 を選びます。初回は本体で ROM からゲームデータを読み込むため 30 秒ほどかかり、画面に進み具合が出ます。2 回目以降は数秒で起動します。' },
        ],
        tips: [
          { title: 'パフォーマンスは改善中', warn: true, body: 'Switch 版のパフォーマンスはまだ最適化の途中で、プレイ中に多少カクつくことがあります。' },
          { title: 'アプリケーションモードのみ対応', warn: true, body: 'アルバムから開いた hbmenu ではメモリが足りず、R を押しながら起動するよう案内が出ます。' },
          { title: '更新するとき', body: '古い <code>fonts</code>・<code>dialogue</code>・<code>stages</code>・<code>splash</code> フォルダーを削除してから新しい版を展開します。セーブ・設定・<code>game</code> フォルダーはそのまま残り、新しい版の初回起動時に自動でもう一度読み込みます。HD パックの新版は <code>hd</code> フォルダーごと入れ替えます。' },
          { title: '更新通知はありません', body: 'Switch 版にはゲーム内の更新通知がありません。新しい版はこのページか GitHub でご確認ください。' },
          { title: '配布形式', body: '配布するのは NRO 形式の自作プログラムだけで、NSP は提供しません。署名パッチも不要です。' },
        ],
      },
      {
        id: 'hd', name: 'HD アートパック',
        summary: '任意で導入できます。パソコンと Android 共通で、Switch には専用のものがあります。',
        need: '任意。アプリとは別のバージョンで、新版はゲームが通知します。',
        steps: [
          { title: 'ダウンロードして展開', file: 'hd', body: '<code>hd</code> フォルダーの直下に <code>hd.json</code> と <code>art/</code> があることを確認します。' },
          { title: 'ユーザーデータフォルダーに配置', body: [
            'macOS：<code>~/Library/Application Support/SRW64Recomp/hd</code><br>Linux／Steam Deck：<code>~/.local/share/srw64-recomp/hd</code><br>Windows：<code>%LOCALAPPDATA%\\SRW64Recomp\\hd</code>',
            'ゲーム本体のフォルダー（<code>Marchwind64.app</code>、<code>marchwind64.sh</code>、<code>Marchwind64.exe</code> と同じ場所）にも置けます。両方にある場合はユーザーデータフォルダーのものを使います。',
          ] },
          { title: '起動して確認', body: 'HD パックが入っていれば自動で HD 画面になります。導入できたかどうかは、設定ウィンドウ「情報」ページの「HD パック」の行で、導入済みのバージョンを確認できます。' },
        ],
        tips: [
          { title: 'Android と Switch', body: 'Android では展開不要で、<a href="#android">Android</a> の項目のとおりアプリ内で導入します。Switch では専用の Switch 版 HD パックを使います。ダウンロードと導入方法は <a href="#switch">Nintendo Switch</a> の項目をご覧ください。' },
          { title: 'macOS でアプリの隣に置く場合', body: '先にアプリを Finder で一度移動してください（「アプリケーション」へドラッグするなど）。' },
          { title: 'オリジナルと HD の切り替え', body: 'いつでも切り替えられます。設定ウィンドウ「一般」ページの「画面」か、ショートカット（初期設定ではキーボードの 4 または F6、コントローラーは右スティック押し込み）を使います。Windows と macOS のメニューバーにも「HD 画面」があります。切り替えはすぐ反映され、セーブデータには影響しません。この選択は保存されず、起動時は常に HD です。' },
          { title: 'マップのユニットアイコン', body: 'HD 画面でも、マップ上のユニットアイコンだけをオリジナルに戻せます（「一般」ページの「マップユニット」）。こちらは設定が保存されます。' },
          { title: 'バージョンと更新', body: 'HD パックにはアプリとは別のバージョン番号があり、内容が変わったときだけ新版を出します。アプリを更新するたびにダウンロードし直す必要はありません。新版はゲームの更新確認が知らせます。更新時は <code>hd</code> フォルダーを丸ごと入れ替えてください。' },
          { title: '画像について', body: '収録画像は主に、オリジナルを参考に AI で生成、または拡大・描き直したものです。詳しくはよくある質問をご覧ください。' },
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
        summary: '압축을 풀고 ROM을 넣은 뒤 더블클릭합니다.',
        req: {
          rows: [
            ['OS', 'Windows 10 / 11, 64비트'],
            ['그래픽', 'Direct3D 12(Shader Model 6.0) 또는 Vulkan 1.2'],
            ['NVIDIA', 'GeForce GT 630(Kepler) 이상'],
            ['AMD', 'Radeon HD 7750 이상'],
            ['Intel', 'Intel HD Graphics 510(6세대 Core) 이상'],
            ['CPU', 'x86-64-v2 프로세서(1세대 Intel Core i, AMD FX 이상)'],
            ['드라이버', '최신 그래픽 드라이버'],
            ['용량', '약 165 MB, HD 팩은 약 700 MB 추가'],
          ],
          notes: [MINIMUM.ko, DRIVERS.ko],
        },
        steps: [
          { title: '내려받아 압축 풀기', file: 'windows', body: '마우스 오른쪽 버튼으로 클릭하고 “압축 풀기”를 선택합니다. <code>Marchwind64-windows-x64</code> 폴더가 생깁니다. zip 파일 안에서 바로 실행하지 마세요.' },
          { title: 'ROM 넣기', body: '<strong>ROM 이름을 <code>rom.z64</code>로 바꿔 이 폴더에 넣습니다</strong>(<code>Marchwind64.cmd</code>와 <code>Marchwind64.exe</code>가 있는 폴더). ROM이 압축 파일 안에 있다면 먼저 압축을 푸세요.' },
          { title: '게임 실행', body: '<strong><code>Marchwind64.exe</code>를 더블클릭합니다</strong>(<code>Marchwind64.cmd</code>도 같습니다). 처음 실행할 때는 시스템 언어를 따릅니다(한국어 시스템에서는 한국어, 중국어 시스템에서는 간체 중국어, 일본어 시스템에서는 일본어, 그 밖에는 영어). 창의 “언어” 메뉴나 설정 창에서 바꿀 수 있습니다.' },
        ],
        tips: [
          { title: '확장자 주의', warn: true, body: 'Windows는 기본적으로 확장자를 숨기므로 파일 이름이 <code>rom.z64.z64</code>가 되기 쉽습니다. 먼저 파일 탐색기의 “보기”에서 “파일 확장명”을 켜세요. ROM을 찾지 못하면 게임이 어디에 두어야 하는지 알려 줍니다.' },
          { title: '보안 경고가 나타나면', warn: true, body: '“추가 정보” → “실행”을 선택하세요.' },
          { title: 'ROM을 둘 수 있는 다른 위치', body: '<code>%LOCALAPPDATA%\\SRW64Recomp\\rom.z64</code>에 두어도 됩니다(파일 탐색기 주소 표시줄에 <code>%LOCALAPPDATA%</code>를 입력하고 <code>SRW64Recomp</code> 폴더를 만듭니다).' },
          { title: '포터블 모드', body: '<code>Marchwind64.exe</code> 옆에 <code>user-data</code>라는 빈 폴더를 만들면 세이브와 설정이 그 안에 저장됩니다(처음에는 <code>%LOCALAPPDATA%\\SRW64Recomp</code>에 있던 설정과 세이브를 복사해 오고, 정상적으로 종료하면 다시 복사해 둡니다). ROM을 포함한 게임 폴더 전체를 USB 메모리나 외장 드라이브에 두면 드라이브 문자가 바뀌어도, 다른 컴퓨터에서도 그대로 플레이할 수 있습니다.' },
        ],
      },
      {
        id: 'macos', name: 'macOS',
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
        summary: '압축을 풀고 ROM을 넣은 뒤 스크립트를 실행합니다.',
        req: {
          rows: [
            ['OS', '64비트, glibc 2.35 이상(Ubuntu 22.04, Debian 12 이상)'],
            ['그래픽', 'Vulkan 1.2'],
            ['GPU', 'GeForce GT 630, Radeon HD 7750, Intel HD Graphics 510 이상'],
            ['CPU', 'x86-64-v2 프로세서(1세대 Intel Core i, AMD FX 이상)'],
            ['드라이버', '최신 Mesa 또는 NVIDIA 드라이버'],
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
          { title: 'Steam에 추가', body: 'Steam을 연 상태에서 폴더 안의 <code>add-to-steam.sh</code>를 더블클릭하고, 확인 창이 뜨면 “실행”을 선택합니다. 스크립트가 커버 이미지와 아이콘과 함께 게임을 라이브러리에 추가합니다.' },
          { title: '게임 모드에서 실행', body: 'Steam 입력 기본 설정 그대로 라이브러리에서 실행합니다. 게임이 Deck을 인식해 그에 맞는 버튼 안내를 표시합니다.' },
        ],
        tips: [
          { title: '조작', body: '게임이 Deck을 인식해 Deck에 맞는 기본 조작을 사용하며, 설정에서 다시 배정할 수 있습니다.' },
          { title: '종료', body: 'Steam 메뉴로 종료하면 게임을 저장한 뒤 종료합니다.' },
        ],
      },
      {
        id: 'android', name: 'Android', experimental: true,
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
        tips: [
          { title: '터치 버튼', body: '화면을 터치하면 현재 화면에 필요한 버튼이 나타나고, 게임패드나 키보드로 조작하면 숨겨집니다. 위쪽의 “설정”을 누르면 설정 창이 열립니다.' },
          { title: 'HD 팩 가져오기', body: '앱 아이콘을 길게 눌러 “HD 팩 가져오기”를 선택하거나, 내려받은 HD zip을 파일 관리자에서 Marchwind64로 열면 앱이 압축을 풀어 설치합니다. ROM을 고른 뒤에도 한 번 물어봅니다.' },
        ],
      },
      {
        id: 'switch', name: 'Nintendo Switch', experimental: true,
        summary: 'SD 카드에 압축을 풀고 hbmenu에서 실행합니다.',
        req: {
          rows: [
            ['본체', 'Atmosphère 커스텀 펌웨어를 실행할 수 있는 Switch(초기 모델에서 테스트)'],
            ['시스템', 'Atmosphère와 hbmenu, 애플리케이션 모드(R을 누른 채 게임 실행)'],
            ['용량', 'SD 카드에 약 110 MB, Switch용 HD 팩은 약 1.6 GB 추가'],
          ],
        },
        steps: [
          { title: 'SD 카드에 압축 풀기', file: 'switch', body: 'SD 카드 루트에 압축을 풀면 <code>/switch/Marchwind64/Marchwind64.nro</code>와 옆의 폴더들이 생깁니다. 카드 리더기를 써도, ftpd를 써도 됩니다.' },
          { title: 'ROM 넣기', body: '<strong>ROM 이름을 <code>rom.z64</code>로 바꿔 <code>/switch/Marchwind64/</code>에 넣습니다</strong>.' },
          { title: '선택 사항: Switch용 HD 팩 설치', file: 'switch-hd', secondary: true, body: '마찬가지로 SD 카드 루트에 압축을 풀면 <code>/switch/Marchwind64/hd/</code>가 생깁니다. 컴퓨터용 HD 팩은 Switch에서 쓸 수 없습니다.' },
          { title: 'R을 누른 채 실행', body: '<strong>R을 누른 채 아무 게임이나 실행해</strong>(게임 카드가 가장 간단합니다) hbmenu를 열고 Marchwind64를 고릅니다. 처음 실행할 때는 본체에서 ROM의 게임 데이터를 가져오느라 30초 정도 걸리며, 진행 상황이 화면에 표시됩니다. 그다음부터는 몇 초 만에 실행됩니다.' },
        ],
        tips: [
          { title: '성능 개선 중', warn: true, body: 'Switch 버전은 아직 성능 최적화가 더 필요하며, 플레이 중에 다소 끊김이 있을 수 있습니다.' },
          { title: '애플리케이션 모드 전용', warn: true, body: '앨범에서 연 hbmenu는 메모리가 부족해서, 게임이 R을 누른 채 실행하라고 안내합니다.' },
          { title: '업데이트', body: '기존 <code>fonts</code>, <code>dialogue</code>, <code>stages</code>, <code>splash</code> 폴더를 지우고 새 버전의 압축을 풉니다. 세이브, 설정, <code>game</code> 폴더는 그대로 남으며, 새 버전을 처음 실행할 때 데이터를 한 번 더 가져옵니다. 새 HD 팩은 <code>hd</code> 폴더 전체를 바꿉니다.' },
          { title: '업데이트 알림 없음', body: 'Switch 버전에는 게임 내 업데이트 알림이 없습니다. 새 버전은 이 페이지나 GitHub에서 확인하세요.' },
          { title: '배포 형식', body: '이 프로젝트는 NRO 홈브루 프로그램만 배포합니다. NSP는 없으며, 서명 패치도 필요 없습니다.' },
        ],
      },
      {
        id: 'hd', name: 'HD 팩',
        summary: '선택 사항입니다. 컴퓨터와 Android용 하나와 Switch 전용 팩이 있습니다.',
        need: '선택 사항. 앱과 별도로 버전을 관리하며, 새 버전이 나오면 게임이 알려 줍니다.',
        steps: [
          { title: '내려받아 압축 풀기', file: 'hd', body: '<code>hd</code> 폴더를 꺼냅니다. 폴더 바로 안에 <code>hd.json</code>과 <code>art/</code>가 있어야 합니다.' },
          { title: '사용자 데이터 폴더에 넣기', body: [
            'macOS: <code>~/Library/Application Support/SRW64Recomp/hd</code><br>Linux와 Steam Deck: <code>~/.local/share/srw64-recomp/hd</code><br>Windows: <code>%LOCALAPPDATA%\\SRW64Recomp\\hd</code>',
            '게임 폴더(<code>Marchwind64.app</code>, <code>marchwind64.sh</code>, <code>Marchwind64.exe</code> 옆)에 두어도 되며, 두 곳 모두에 있으면 사용자 데이터 폴더의 것을 씁니다.',
          ] },
          { title: '실행해서 확인', body: 'HD 팩이 설치되어 있으면 게임이 알아서 HD 그래픽을 씁니다. 설치되었는지는 설정 창 “정보” 페이지의 “HD 리소스 팩” 줄에서 설치된 버전으로 확인할 수 있습니다.' },
        ],
        tips: [
          { title: 'Android와 Switch', body: 'Android에서는 압축을 풀 필요 없이, <a href="#android">Android</a> 항목에 설명한 대로 앱에서 가져옵니다. Switch는 전용 Switch용 HD 팩을 씁니다. 다운로드와 설치 방법은 <a href="#switch">Nintendo Switch</a> 항목을 보세요.' },
          { title: 'macOS에서 앱 옆에 둘 때', body: '먼저 Finder에서 앱을 한 번 옮겨야 합니다(응용 프로그램 폴더로 옮기는 등).' },
          { title: '원본과 HD 전환', body: '설정 창 “일반” 페이지의 “이미지”에서, 또는 단축키(기본값은 키보드 4나 F6, 게임패드는 오른쪽 스틱 누르기)로 언제든 전환할 수 있습니다. Windows와 macOS의 메뉴 막대에도 “HD 이미지”가 있습니다. 전환은 바로 적용되며 세이브에는 영향을 주지 않습니다. 이 선택은 저장되지 않으며, 게임은 항상 HD로 시작합니다.' },
          { title: '맵 기체 아이콘', body: 'HD에서도 맵의 기체 아이콘만 원본으로 둘 수 있습니다. “일반” 페이지의 “전술맵 기체 아이콘”이며, 이 설정은 저장됩니다.' },
          { title: '버전과 업데이트', body: 'HD 팩은 따로 버전을 관리하며 내용이 바뀔 때만 새 버전이 나오므로, 앱을 업데이트할 때마다 다시 내려받을 필요는 없습니다. 새 버전이 나오면 게임의 업데이트 확인이 알려 줍니다. 업데이트할 때는 <code>hd</code> 폴더 전체를 바꾸세요.' },
          { title: '이미지에 대하여', body: '대부분의 이미지는 원본 그래픽을 참고해 AI로 생성하거나 업스케일했습니다. 자세한 내용은 자주 묻는 질문을 보세요.' },
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
    verify: '체크섬',
    noFile: '이 버전에는 아직 이 플랫폼용 패키지가 없습니다.',
    notReleased: '0.4.0은 준비 중이라 아직 내려받을 수 없습니다. 설치 단계는 아래에서 미리 볼 수 있습니다.',
  },
};
