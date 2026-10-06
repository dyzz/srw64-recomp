import type { Lang } from '../i18n';

// credit: a line under the pictures (where an image comes from, what was left out).
// wide: the text first, then the picture across the whole column (a very wide shot).
type Show = { key: string; tag: string; img: string; img2?: string; wide?: boolean; credit?: string; title: string; body: string; points: string[] };
type Card = { title: string; body: string; accent: 'cyan' | 'gold' | 'pink' | 'orange' | 'green' };
type RuleGroup = { title: string; badge: string; note: string; accent: 'green' | 'orange' | 'cyan'; items: string[] };
type Home = {
  kicker: string;
  titleParts: string[];
  title: string;
  tagline: string;
  intro: string;
  lead: string;
  rom: string;
  romMore: string;
  allPlatforms: string;
  optional: string;
  strip: { title: string; body: string; accent: 'cyan' | 'gold' | 'pink' | 'green' }[];
  compareTitle: string;
  compareBody: string;
  original: string;
  ours: string;
  showTitle: string;
  showBody: string;
  shows: Show[];
  rulesTitle: string;
  rulesBody: string;
  rules: RuleGroup[];
  galleryTitle: string;
  galleryBody: string;
  moreTitle: string;
  more: Card[];
  platformsTitle: string;
  platforms: { name: string; note: string; experimental?: boolean }[];
  experimental: string;
  communityTitle: string;
  communityBody: string;
  communityStory: string;
  communityLibrary: string;
  communityGuide: string;
  newsTitle: string;
};

export const HOME: Record<Lang, Home> = {
  zh: {
    kicker: '非官方 · 原生重编译',
    titleParts: ['超级机器人', '大战64'],
    title: '超级机器人大战64',
    tagline: '1999 年 N64 游戏的原生重编译版。',
    intro: 'Marchwind 64（“三月风”）是《超级机器人大战64》的非官方原生重编译项目。',
    lead: '支持电脑、Steam Deck 和安卓手机，提供中英文全文翻译和宽屏显示，另有可选的 HD 美术包。',
    rom: '需要自备日版 ROM',
    romMore: 'ROM 说明',
    allPlatforms: '其他平台与安装说明',
    optional: '在设置中逐项选择修正和界面模式',
    strip: [
      { title: '原生运行', body: '安装后添加 ROM 即可开始', accent: 'cyan' },
      { title: '中 · 英 · 日', body: '全文翻译，游戏中随时切换', accent: 'gold' },
      { title: 'HD 与宽屏', body: '原版与 HD 美术随时切换', accent: 'pink' },
      { title: '原版 bug 修正', body: '默认开启，支持逐项设置', accent: 'green' },
    ],
    compareTitle: '原版与 HD 画面对比',
    compareBody: '左侧为原版 4:3 画面，使用 N64 分辨率的头像和地图。右侧为安装 HD 包后的 Marchwind 64，以 16:9 显示重绘的头像和世界地图。游戏中可随时切换原版与 HD 美术，两种模式共用存档。',
    original: '原版 · 4:3',
    ours: 'MARCHWIND 64 · HD 宽屏',
    showTitle: '界面与操作改进',
    showBody: '在 N64 原作的基础上，加入新的界面、全文翻译和操作改进，并提供独立的 bug 修正选项。以下均为游戏内截图。',
    shows: [
      { key: 'prebattle', tag: 'BATTLE', img: '/media/prebattle-{lang}.webp', img2: '/media/move-jump.webp', title: '现代机战式的界面与操作', body: '战前确认画面按现代机战的样式重新设计：攻守双方的伤害、命中率、暴击率，以及盾防和护罩的效果并排显示，精神指令、更换武器和战斗动画开关集中在下方。战术地图上也补上了现代机战常见的便捷操作。', points: ['选择移动位置时，可以一步跳到移动范围最外圈的格子', 'L1／R1 依次切换我方机体，L2／R2 依次查看敌方机体', '战斗演出可随时中止，战斗结果照常结算', '设置中也提供原版界面选项'] },
      { key: 'history', tag: 'DIALOGUE', img: '/media/auto-{lang}.webp', img2: '/media/history-{lang}.webp', title: '对白阅读', body: '全部对白用高清字体重新排版：整条台词连续排列，按字号自动分行分页，尽量不在半句话处翻页。读过的对白可以随时回看；自动播放、快进和跳过都有专门的操作，屏幕底栏显示当前的阅读模式和对应按键。', points: ['回看最近 256 条对白，按说话人区分颜色', '自动播放有 4 档速度，字号可在 10–18 之间调整', '快进和跳过照现代机战的做法：按住快进，松手即停；跳过会一次走完整段剧情，停在下一个选择或战斗前，结果和逐句读完相同', '随时切换语言或对照日文原文，当前这句立即重新排版', '台词保存为纯文本文件，可以自己修改'] },
      { key: 'settings', tag: 'OPTIONS', img: '/media/settings-{lang}.webp', title: '游戏内设置', body: '游戏中可调整语言、原版或 HD 美术、画面比例、全屏和界面大小，也可逐项设置规则修正、自定义键盘与手柄按键。', points: ['游戏中随时可以打开，标题画面右下角也有入口', '界面大小有标准、大、特大三档'] },
      { key: 'filters', tag: 'FILTERS', img: '/media/filters-tv-crt.webp', wide: true, title: 'RetroArch 滤镜与框体', body: '可以直接使用 RetroArch 的 slang 着色器预设（.slangp），给画面加上 CRT 扫描线、NTSC 色彩等效果；画面比例为 4:3 时还能套上 RetroArch 的框体，透明窗口自动对准游戏画面。在「选项 → 通用」里设置。', points: ['自带 crt-lottes、crt-geom、zfast-crt（适合掌机）、ntsc-adaptive、xbrz-freescale 等 9 个常用预设', '自动找到已安装的 RetroArch 的全部 slang 着色器；自己的预设和框体放进数据文件夹即可', '可按原版 240 行（最接近 CRT）、480、960 行或窗口分辨率处理', '只作用于游戏画面和对白，选项窗口、提示和框体保持清晰', '滤镜支持 macOS 和 Linux／Steam Deck，Windows 版暂不支持'], credit: '框体为 RetroArch 自带的 tv-integer（libretro/common-overlays，CC BY 4.0），滤镜为 crt-lottes（480 行）。安装包不内置任何框体。' },
      { key: 'library', tag: 'LIBRARY', img: '/media/library-{lang}.webp', title: '图鉴：353 台机体、293 名人物', body: '按作品分类，收录机体能力值、地形适应、武器及满改攻击力、改造上限，以及驾驶员成长、精神指令和特殊技能。数据直接取自游戏。', points: ['标题画面和游戏中都能打开', '网站提供同一份图鉴'] },
      { key: 'viewer', tag: 'VIEWER', img: '/media/viewer-{lang}.webp', title: '战斗鉴赏', body: '选择攻守双方的机体和驾驶员，设置武器、防御方式、伤害和场景，即可播放完整战斗演出。', points: ['支持查看所有机体的武器演出', 'BGM 沿用原版规则，播放攻击方的主题曲'] },
    ],
    rulesTitle: '原版 bug 修正与可选规则',
    rulesBody: '每项规则都有独立开关，调整后立即生效，不改变存档格式。「全部关闭」可统一使用原版规则。',
    rules: [
      { title: '原版 bug 修正', badge: '默认开启', note: '修正原版中的数值计算错误和遗漏', accent: 'green', items: ['超能力：命中、回避按实际等级计算（原版固定按 64 级计算）', '圣战士：回避按实际等级计算（原版固定按 32 级计算）', '限界：命中、回避分别加上运动性后，以限界为上限', '底力：修正 HP 档位，满血时不触发加成', '底力：命中、回避加成减半，暴击不变', '换乘：补上表中遗漏的 3 件武器，使其可以继承改造', '圣战士：超级奥拉斩的威力随等级提升'] },
      { title: '便利与难度选项', badge: '默认关闭', note: '可按需要开启，调整改造、资金和战斗难度', accent: 'orange', items: ['改造上限突破：所有机体都能改到 15 段', '离队退款：机体因剧情离队时，退还其改造花费', '部件随行：换乘时，强化部件随驾驶员转移到新机体', '头目假身：可将次数减半，或完全取消'] },
      { title: '画面与界面', badge: '新旧可选', note: '可按画面分别选择新版或原版界面', accent: 'cyan', items: ['战前确认：新版、高清原版、原版三选一', '场间画面、主角选择、标题菜单：新版或原版', '美术：HD 或原版，随时切换', '画面比例：铺满屏幕，或保持 4:3', '战斗动画、自动存档：分别设置开关'] },
    ],
    galleryTitle: 'HD 战斗画面',
    galleryBody: '机体、cut-in、头像和背景采用高清素材，沿用原版的演出时序和镜头。',
    moreTitle: '更多功能',
    more: [
      { title: '原生运行', body: 'N64 程序重编译为本机代码，图形由 RT64 渲染。安装后按说明添加 ROM，即可启动游戏。', accent: 'cyan' },
      { title: '中英日三语', body: '游戏全文提供中文、英文翻译及日文原文。人名、机体名和武器名采用各语言的常用译名。', accent: 'gold' },
      { title: '多存档栏与自动存档', body: '增加存档栏，并在关键节点自动存档。卡带存档可导出，供 ares、Project64、RetroArch 等模拟器使用。', accent: 'pink' },
      { title: '宽屏不拉伸', body: '支持 4:3 到 16:9 的屏幕比例。原版画面居中，两侧扩展场景，以保持比例的方式铺满屏幕。', accent: 'orange' },
      { title: '掌机、手机与手柄', body: '自动识别 Steam Deck，并默认使用对应键位和大号界面。Android 提供触屏操作，仅显示当前画面需要的按键，并标注「确定」「快进」「下个单位」等功能。', accent: 'cyan' },
      { title: '可选 HD 美术包', body: 'HD 美术包单独下载，所有平台通用；未安装时使用原版像素画面，安装后可在游戏中随时切换。', accent: 'gold' },
    ],
    platformsTitle: '支持平台',
    platforms: [
      { name: 'Windows', note: '64 位 Windows', experimental: true },
      { name: 'macOS', note: 'Apple 芯片，macOS 14 及以上' },
      { name: 'Linux', note: 'x86-64，glibc 2.35 及以上，需要 Vulkan' },
      { name: 'Steam Deck', note: '运行脚本加入 Steam 库，在游戏模式启动', experimental: true },
      { name: 'Android', note: 'arm64，Android 9 及以上，触屏操作', experimental: true },
    ],
    experimental: '实验性',
    communityTitle: '一起改进译文',
    communityBody: '全部剧情按关卡整理，日文原文与译文逐句对照。发现语句不顺或译名不一致，可在对应台词下提交修改建议；每条建议的处理结果公开可查。',
    communityStory: '读剧情、提建议',
    communityLibrary: '查图鉴',
    communityGuide: '看攻略',
    newsTitle: '最新动态',
  },
  en: {
    kicker: 'Unofficial · native recompilation',
    titleParts: ['Super Robot', 'Wars 64'],
    title: 'Super Robot Wars 64',
    tagline: 'A native recompilation of the 1999 N64 game.',
    intro: 'Marchwind 64 is an unofficial native recompilation project for Super Robot Wars 64.',
    lead: 'Runs natively on Windows, macOS, Linux, Steam Deck and Android, with full English and Chinese translations and widescreen support, plus an optional HD art pack.',
    rom: 'Requires your own Japanese ROM',
    romMore: 'About ROMs',
    allPlatforms: 'Other platforms and installation',
    optional: 'Choose fixes and screen layouts individually in settings',
    strip: [
      { title: 'Native', body: 'Install, add your ROM and play', accent: 'cyan' },
      { title: 'EN · ZH · JA', body: 'Full translations, switchable in game', accent: 'gold' },
      { title: 'HD and widescreen', body: 'Switch between original and HD art at any time', accent: 'pink' },
      { title: 'Original bug fixes', body: 'Enabled by default, with individual toggles', accent: 'green' },
    ],
    compareTitle: 'Original and HD graphics',
    compareBody: 'Left: the original 4:3 view, with N64-resolution portraits and maps. Right: Marchwind 64 with the HD pack, showing redrawn portraits and world map in 16:9. Switch between original and HD art at any time in game; both use the same saves.',
    original: 'Original · 4:3',
    ours: 'MARCHWIND 64 · HD widescreen',
    showTitle: 'Interface and control updates',
    showBody: 'Built on the N64 original, with updated screens, full translations and control improvements, plus individually selectable bug fixes. All images below are in-game screenshots.',
    shows: [
      { key: 'prebattle', tag: 'BATTLE', img: '/media/prebattle-{lang}.webp', img2: '/media/move-jump.webp', title: 'Modern SRW screens and controls', body: 'The pre-battle screen is redesigned in the style of modern Super Robot Wars games: damage, hit and critical rates for both sides appear side by side with shield and barrier effects, and spirits, weapon selection and the battle animation toggle are grouped below. The tactical map gains the shortcuts modern entries have too.', points: ['While choosing a destination, jump straight to the farthest reachable squares', 'L1/R1 step through your units, L2/R2 through the enemy’s', 'Stop a battle animation at any time; the result is calculated normally', 'The original screen is also available in settings'] },
      { key: 'history', tag: 'DIALOGUE', img: '/media/auto-{lang}.webp', img2: '/media/history-{lang}.webp', title: 'Dialogue reader', body: 'All dialogue is re-typeset in a high-resolution font. Each line runs continuously and is paged to fit the text size, avoiding page breaks mid-sentence. Earlier lines can be reviewed at any time, and auto-play, fast-forward and skip each have their own control, with the current reading mode and controls shown along the bottom of the screen.', points: ['Review the last 256 lines, coloured by speaker', 'Four auto-play speeds; text size from 10 to 18', 'Fast-forward and skip work as in modern Super Robot Wars games: hold to fast-forward and let go to stop; skip runs through the whole scene to the next choice or battle, with the same result as reading every line', 'Switch language or check the original Japanese at any time; the current line is re-typeset at once', 'Dialogue is stored as plain text files you can edit yourself'] },
      { key: 'settings', tag: 'OPTIONS', img: '/media/settings-{lang}.webp', title: 'In-game settings', body: 'Adjust language, original or HD graphics, aspect ratio, full screen and interface size during play. You can also toggle individual rule fixes and remap keyboard and controller inputs.', points: ['Open them at any time in game, or from the button at the bottom right of the title screen', 'Three interface sizes: standard, large and largest'] },
      { key: 'filters', tag: 'FILTERS', img: '/media/filters-tv-crt.webp', wide: true, title: 'RetroArch filters and bezels', body: 'Use RetroArch slang shader presets (.slangp) directly for CRT scanlines, NTSC colour and more. At 4:3 you can also add a RetroArch bezel; its transparent window lines up with the picture by itself. Both are in Options → General.', points: ['Nine common presets included, such as crt-lottes, crt-geom, zfast-crt (for handhelds), ntsc-adaptive and xbrz-freescale', 'Finds every slang shader of an installed RetroArch; drop your own presets and bezels in the data folder', 'Works on the original 240 lines (closest to a CRT), 480, 960, or the window’s resolution', 'Only the game picture and dialogue are filtered; the options window, notices and the bezel stay sharp', 'Filters run on macOS and Linux/Steam Deck; not on Windows yet'], credit: 'Bezel: tv-integer from RetroArch’s own overlays (libretro/common-overlays, CC BY 4.0). Filter: crt-lottes at 480 lines. The game ships with no bezels.' },
      { key: 'library', tag: 'LIBRARY', img: '/media/library-{lang}.webp', title: 'Library: 353 units, 293 characters', body: 'Organised by series, with unit stats, terrain ratings, weapons and their fully upgraded power, upgrade caps, and each pilot’s growth, spirits and skills. All entries come directly from the game data.', points: ['Open it from the title screen or during play', 'The same Library is available on this website'] },
      { key: 'viewer', tag: 'VIEWER', img: '/media/viewer-{lang}.webp', title: 'Battle Viewer', body: 'Choose the attacking and defending units and pilots, set the weapon, the defender’s reaction, the damage and the scene, then play the full battle animation.', points: ['View weapon animations for every unit', 'Plays the attacker’s theme, as in the original'] },
    ],
    rulesTitle: 'Bug fixes and optional rules',
    rulesBody: 'Each rule has its own toggle and takes effect immediately, using the same save format. Select “Turn all off” to use the original rules.',
    rules: [
      { title: 'Original bug fixes', badge: 'On by default', note: 'Corrections to calculation errors and omissions in the original', accent: 'green', items: ['ESP: hit and evade use the actual skill level (the original always used 64)', 'Holy Warrior: evade uses the actual level (the original always used 32)', 'Limit: hit and evade, each combined with mobility, are capped by the unit’s limit', 'Potential: corrected HP bands, with no bonus at full HP', 'Potential: hit and evade bonuses halved, criticals unchanged', 'Machine swap: the 3 weapons omitted from the table also retain their upgrades', 'Holy Warrior: Hyper Aura Slash grows stronger with the level'] },
      { title: 'Convenience and difficulty', badge: 'Off by default', note: 'Optional adjustments to upgrades, funds and battle difficulty', accent: 'orange', items: ['Raised upgrade cap: every unit can reach 15 levels', 'Departure refund: returns upgrade funds when a unit leaves as part of the story', 'Parts carry over: enhancement parts move with the pilot to the new machine', 'Boss dummies: halve their uses or disable them'] },
      { title: 'Graphics and interface', badge: 'New or original', note: 'Choose the new or original layout for each screen', accent: 'cyan', items: ['Pre-battle screen: new, HD original or original', 'Intermission, hero selection and title menu: new or original', 'Art: HD or original, switchable at any time', 'Aspect ratio: fill the screen or keep 4:3', 'Battle animations and autosaves: individual toggles'] },
    ],
    galleryTitle: 'Battles in HD',
    galleryBody: 'High-resolution art for units, cut-ins, portraits and backgrounds, with the original animation timing and camera work.',
    moreTitle: 'More features',
    more: [
      { title: 'Runs natively', body: 'The N64 program is recompiled into native code, with graphics rendered by RT64. After installation, add your ROM following the setup instructions to start playing.', accent: 'cyan' },
      { title: 'Full text in three languages', body: 'Full English and Chinese translations alongside the original Japanese. Character, unit and weapon names follow common usage in each language.', accent: 'gold' },
      { title: 'Extra save slots and autosaves', body: 'More save slots than the original, with autosaves at key points. Export cartridge saves for use with ares, Project64, RetroArch and other emulators.', accent: 'pink' },
      { title: 'Widescreen without stretching', body: 'Supports screen ratios from 4:3 to 16:9. The original view stays centred while the scenery extends to the sides, filling the screen without stretching the image.', accent: 'orange' },
      { title: 'Handhelds, phones, controllers', body: 'Automatically detects Steam Deck and uses its button layout with a large interface by default. On Android, touch controls show the buttons needed for the current screen, labelled with their functions, such as “OK”, “Fast” and “Next unit”.', accent: 'cyan' },
      { title: 'Optional HD art pack', body: 'The HD art pack is a separate download that works on every platform. The game uses the original pixel art without it; once installed, you can switch between the two at any time in game.', accent: 'gold' },
    ],
    platformsTitle: 'Platforms',
    platforms: [
      { name: 'Windows', note: '64-bit Windows', experimental: true },
      { name: 'macOS', note: 'Apple silicon, macOS 14 or later' },
      { name: 'Linux', note: 'x86-64, glibc 2.35 or later, Vulkan' },
      { name: 'Steam Deck', note: 'Add it to Steam with the script, then launch from Game Mode', experimental: true },
      { name: 'Android', note: 'arm64, Android 9 or later, touch controls', experimental: true },
    ],
    experimental: 'Experimental',
    communityTitle: 'Help improve the translation',
    communityBody: 'Browse the full script by stage, with the Japanese original and translation side by side. Suggest changes on individual lines to help resolve awkward wording or inconsistent names. The outcome of each suggestion is public.',
    communityStory: 'Read the story and suggest changes',
    communityLibrary: 'Browse the Library',
    communityGuide: 'Browse the guide',
    newsTitle: 'Latest news',
  },
  ja: {
    kicker: '非公式 · ネイティブ再コンパイル',
    titleParts: ['スーパーロボット', '大戦64'],
    title: 'スーパーロボット大戦64',
    tagline: '1999 年に発売された N64 用ゲームのネイティブ再コンパイル版。',
    intro: 'マーチウィンド64 は『スーパーロボット大戦64』の非公式ネイティブ再コンパイルプロジェクトです。',
    lead: 'Windows・macOS・Linux・Steam Deck・Android でネイティブに動作し、英語と中国語の全文翻訳とワイド画面に対応し、別配布の HD アートパックも利用できます。',
    rom: '日本版の ROM をご用意ください',
    romMore: 'ROM について',
    allPlatforms: 'ほかのプラットフォームとインストール方法',
    optional: '修正や画面の種類は、設定で個別に選べます',
    strip: [
      { title: 'ネイティブ動作', body: 'インストールして ROM を追加すればプレイ可能', accent: 'cyan' },
      { title: '日・英・中', body: '全文翻訳に対応、ゲーム中に切り替え可能', accent: 'gold' },
      { title: 'HD とワイド', body: 'オリジナルと HD のグラフィックをいつでも切り替え', accent: 'pink' },
      { title: '原作のバグ修正', body: '初期設定はオン、項目ごとに切り替え可能', accent: 'green' },
    ],
    compareTitle: 'オリジナルと HD の比較',
    compareBody: '左はオリジナル版の 4:3 画面で、顔グラフィックとマップは N64 の解像度です。右は HD パックを導入したマーチウィンド64。描き直した顔グラフィックとワールドマップを 16:9 で表示しています。ゲーム中いつでもオリジナルと HD のグラフィックを切り替えられ、セーブデータは共通です。',
    original: 'オリジナル · 4:3',
    ours: 'MARCHWIND 64 · HD ワイド',
    showTitle: '画面と操作の改良',
    showBody: 'N64 の原作をもとに、画面の刷新、全文翻訳、操作の改良を加え、バグ修正を項目ごとに選べるようにしました。以下はすべてゲーム内のスクリーンショットです。',
    shows: [
      { key: 'prebattle', tag: 'BATTLE', img: '/media/prebattle-{lang}.webp', img2: '/media/move-jump.webp', title: '最近のスパロボ風の画面と操作', body: '戦闘前の確認画面を最近のスパロボのスタイルで作り直しました。双方のダメージ・命中率・クリティカル率とシールド防御やバリアの効果を並べて表示し、精神コマンド、武器の変更、戦闘アニメの切り替えは下にまとめています。戦術マップにも、最近の作品でおなじみの便利な操作を加えました。', points: ['移動先を選ぶとき、移動範囲のいちばん外側のマスへ一気に移れます', 'L1／R1 で味方、L2／R2 で敵のユニットを順に切り替えて確認できます', '戦闘アニメは途中で終了でき、戦闘結果は通常どおり反映されます', '設定ではオリジナルの画面も選べます'] },
      { key: 'history', tag: 'DIALOGUE', img: '/media/auto-{lang}.webp', img2: '/media/history-{lang}.webp', title: '会話の表示と読み返し', body: 'すべてのセリフを高解像度フォントで組み直しました。セリフは一続きに組み、文字サイズに合わせて改行・改ページするので、文の途中でページが切れにくくなっています。読んだセリフはいつでも読み返せ、オート再生・早送り・スキップにはそれぞれ専用の操作があり、画面下に現在のモードと操作が表示されます。', points: ['直近 256 件のセリフを、話者ごとに色分けして読み返せます', 'オート再生は 4 段階、文字サイズは 10〜18', '早送りとスキップは最近のスパロボと同じ感覚で使えます。押している間だけ早送りし、離せば止まります。スキップは場面をまとめて飛ばし、次の選択や戦闘の手前で止まります。結果は一行ずつ読んだ場合と同じです', '言語の切り替えや日本語原文との読み比べはいつでも。表示中のセリフもすぐに組み直されます', 'セリフはテキストファイルで保存されており、自分で書き換えられます'] },
      { key: 'settings', tag: 'OPTIONS', img: '/media/settings-{lang}.webp', title: 'ゲーム内の設定', body: 'プレイ中に言語、オリジナル／HD のグラフィック、画面比率、フルスクリーン、UI サイズを変更できます。ルール修正の個別設定や、キーボードとコントローラーの割り当て変更にも対応しています。', points: ['ゲーム中いつでも開け、タイトル画面右下からも開けます', 'UI サイズは標準・大・特大の 3 段階'] },
      { key: 'filters', tag: 'FILTERS', img: '/media/filters-tv-crt.webp', wide: true, title: 'RetroArch のフィルターとベゼル', body: 'RetroArch の slang シェーダープリセット（.slangp）をそのまま使い、CRT の走査線や NTSC の色合いなどを加えられます。4:3 なら RetroArch のベゼルも重ねられ、透明部分は自動でゲーム画面に合わせます。設定は「オプション → 一般」から。', points: ['crt-lottes、crt-geom、zfast-crt（携帯機向け）、ntsc-adaptive、xbrz-freescale など 9 種のプリセットを同梱', 'インストール済みの RetroArch の slang シェーダーを自動で検出。自分のプリセットやベゼルはデータフォルダに置くだけ', 'オリジナルの 240 ライン（CRT にいちばん近い）、480、960 ライン、またはウィンドウの解像度で処理', 'フィルターはゲーム画面と会話だけにかかり、オプション画面・通知・ベゼルはくっきりしたまま', 'フィルターは macOS と Linux／Steam Deck に対応。Windows 版はまだ非対応'], credit: 'ベゼルは RetroArch 付属の tv-integer（libretro/common-overlays、CC BY 4.0）、フィルターは crt-lottes（480 ライン）。ゲームにベゼルは同梱していません。' },
      { key: 'library', tag: 'LIBRARY', img: '/media/library-{lang}.webp', title: '図鑑：ユニット 353 体、キャラクター 293 人', body: '作品ごとに、ユニットの能力値、地形適応、武器とフル改造時の攻撃力、改造上限、パイロットの成長・精神コマンド・特殊技能を収録しています。データはすべてゲームから直接読み込んでいます。', points: ['タイトル画面からもゲーム中からも開けます', '同じ図鑑をこのサイトでも公開しています'] },
      { key: 'viewer', tag: 'VIEWER', img: '/media/viewer-{lang}.webp', title: 'バトルビューアー', body: '攻撃側と防御側のユニット・パイロットを選び、武器、防御行動、ダメージ、背景を設定して、戦闘アニメを最後まで再生できます。', points: ['全ユニットの武器演出を鑑賞できます', 'BGM は原作と同じく、攻撃側のテーマを再生します'] },
    ],
    rulesTitle: '原作のバグ修正と選べるルール',
    rulesBody: '各項目を個別に切り替えられ、変更はすぐに反映されます。セーブ形式は共通です。「すべてオフ」で原作のルールを使用できます。',
    rules: [
      { title: '原作のバグ修正', badge: '初期設定でオン', note: '原作の数値計算の誤りや計算漏れを修正', accent: 'green', items: ['超能力：命中・回避に実際のレベルを反映（原作では常に 64 として計算）', '聖戦士：回避に実際のレベルを反映（原作では常に 32 として計算）', '限界：命中・回避に運動性を加えた値を、それぞれ限界で制限', '底力：HP の段階を修正し、HP 満タンでは補正なし', '底力：命中・回避の補正を半分に（クリティカルはそのまま）', '乗り換え：テーブルに未登録だった 3 つの武器も改造を引き継ぐ', '聖戦士：ハイパーオーラ斬りの威力がレベルで上がる'] },
      { title: '快適性と難易度', badge: '初期設定でオフ', note: '改造・資金・戦闘の難易度を、必要に応じて調整できます', accent: 'orange', items: ['改造上限突破：全ユニット 15 段階まで改造可能', '離脱返金：シナリオでユニットが離脱すると改造資金を返却', 'パーツ引き継ぎ：乗り換え時に強化パーツも新しい機体へ', 'ボスの身代わり：回数を半分にするか、無効化'] },
      { title: '画面とインターフェース', badge: '新旧を選択', note: '画面ごとに新版とオリジナルを選択できます', accent: 'cyan', items: ['戦闘前の確認：新版・HD オリジナル・オリジナルから選択', 'インターミッション・主人公選択・タイトルメニュー：新版またはオリジナル', 'アート：HD またはオリジナル、いつでも切り替え', '画面比率：画面全体に表示、または 4:3 を維持', '戦闘アニメ・自動セーブ：それぞれオン／オフを設定'] },
    ],
    galleryTitle: 'HD の戦闘画面',
    galleryBody: 'ユニット・カットイン・顔グラフィック・背景に高解像度の素材を使い、演出のタイミングとカメラワークは原作を引き継いでいます。',
    moreTitle: 'そのほかの機能',
    more: [
      { title: 'ネイティブ動作', body: 'N64 のプログラムをネイティブコードに再コンパイルし、RT64 で描画します。インストール後、案内に沿って ROM を追加するとプレイできます。', accent: 'cyan' },
      { title: '全文 3 言語対応', body: '日本語原文に加え、ゲーム全文の英語・中国語訳を収録しています。キャラクター名・ユニット名・武器名には、各言語で一般的な表記を採用しています。', accent: 'gold' },
      { title: 'セーブ枠の追加と自動セーブ', body: '原作よりセーブ枠を増やし、要所で自動セーブを行います。カートリッジのセーブデータは、ares・Project64・RetroArch などで使える形式で書き出せます。', accent: 'pink' },
      { title: '引き伸ばさないワイド表示', body: '4:3 から 16:9 までの画面比率に対応。オリジナルの表示範囲を中央に置き、左右に背景を広げることで、比率を保ったまま黒帯のない表示にします。', accent: 'orange' },
      { title: '携帯機・スマホ・コントローラー', body: 'Steam Deck を自動認識し、対応するボタン配置と大きめの UI を初期設定にします。Android では、現在の画面で使うタッチボタンだけを「決定」「早送り」「次の味方」などの機能名付きで表示します。', accent: 'cyan' },
      { title: '追加の HD アートパック', body: 'HD アートパックは別配布で、すべてのプラットフォームで使えます。未導入時はオリジナルのドット絵で表示され、導入後はゲーム中いつでも切り替えられます。', accent: 'gold' },
    ],
    platformsTitle: '対応プラットフォーム',
    platforms: [
      { name: 'Windows', note: '64 ビット版 Windows', experimental: true },
      { name: 'macOS', note: 'Apple シリコン、macOS 14 以降' },
      { name: 'Linux', note: 'x86-64、glibc 2.35 以降、Vulkan が必要' },
      { name: 'Steam Deck', note: 'スクリプトで Steam に追加し、ゲームモードで起動', experimental: true },
      { name: 'Android', note: 'arm64、Android 9 以降、タッチ操作', experimental: true },
    ],
    experimental: '試験的',
    communityTitle: 'シナリオ・図鑑・攻略',
    communityBody: '全シナリオを話数順に収録し、日本語原文と英語・中国語訳を一行ずつ読み比べられます。図鑑ではキャラクターやユニットのデータ、攻略では分岐や隠し要素を確認できます。',
    communityStory: 'シナリオを読む',
    communityLibrary: '図鑑を見る',
    communityGuide: '攻略を見る',
    newsTitle: '最新情報',
  },
};
