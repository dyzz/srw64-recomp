import type { Lang } from '../i18n';

type Show = { key: string; tag: string; img: string; title: string; body: string; points: string[] };
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
    tagline: '1999 年 N64 经典的原生重编译。',
    intro: 'Marchwind 64（“三月风”）是《超级机器人大战64》的非官方原生重编译项目。',
    lead: '当年的 N64 战役，原生运行在你的电脑、掌机和手机上：全文中文与英文翻译，画面铺满宽屏，另有可选的 HD 美术包。',
    rom: '需要自备日版原版 ROM',
    romMore: '为什么？',
    allPlatforms: '其他平台与安装说明',
    optional: '每一项修正和新界面都能单独关掉，随时退回 1999 年的原样',
    strip: [
      { title: '原生运行', body: '不是模拟器，解压即玩', accent: 'cyan' },
      { title: '中 · 英 · 日', body: '全文翻译，F7 随时切换', accent: 'gold' },
      { title: 'HD 与宽屏', body: 'F6 随时回到原版画面', accent: 'pink' },
      { title: '原版 bug 修正', body: '默认开启，逐条可关', accent: 'green' },
    ],
    compareTitle: '同一句台词，两个时代',
    compareBody: '左边是原版：4:3 画面，N64 分辨率的头像与地图。右边是本移植加 HD 包：画面铺满 16:9，头像与世界地图全部重绘。按 F6 随时切回原版，存档完全通用。',
    original: '原版 · 4:3',
    ours: 'MARCHWIND 64 · HD 宽屏',
    showTitle: '原汁原味的战役，顺手的操作',
    showBody: '剧情、数值、敌人行动和战斗结算都照原版运行；我们改的是界面、阅读和那些本该有的便利。以下全是游戏内实拍。',
    shows: [
      { key: 'prebattle', tag: 'BATTLE', img: '/media/prebattle-{lang}.webp', title: '开打之前，胜负一目了然', body: '双方的伤害、命中、暴击，盾防与护罩的效果并排摆好；精神指令、换武器、开关战斗动画，都在下方一排。', points: ['喜欢原版的样子，设置里一键换回', '战斗演出随时中止，结算一点不变'] },
      { key: 'history', tag: 'DIALOGUE', img: '/media/history-{lang}.webp', title: '台词，按你的节奏读', body: '一不留神按过去的台词，随时翻回来看。自动阅读、调速、快进、跳过、字号，各有一个键；底栏提示跟着你手里的手柄或键盘变。', points: ['F7 一键切换语言，对白、菜单、数据一起换', '想对照日文原文，随时切过去'] },
      { key: 'map', tag: 'MAP', img: '/media/move-jump.webp', title: '更宽的战场，更少的按键', body: '地图向两侧铺开，一屏看得更远。挑移动位置时按住 R，最远的一圈格子亮起，一步就到。', points: ['L／R 轮流选我方机体，L2／R2 轮流看敌人', '战术地图本身也有 HD 重绘'] },
      { key: 'settings', tag: 'OPTIONS', img: '/media/settings-{lang}.webp', title: '设置就在手边', body: '游戏进行中随时呼出：语言、原版或 HD、画面比例、全屏、界面大小；规则修正逐条开关，键盘和手柄都能改键。', points: ['Steam Deck 按视图键，标题画面右下角也有入口', '界面大小分标准、大、特大三档'] },
      { key: 'library', tag: 'LIBRARY', img: '/media/library-{lang}.webp', title: '图鉴：353 台机体，293 名人物', body: '按作品分好类：能力值、地形适应、武器与满改火力、改造上限，驾驶员的成长、精神指令和特殊技能，数据都直接读自游戏。', points: ['标题画面和游戏中都能打开', '网站上也有同一份图鉴'] },
      { key: 'viewer', tag: 'VIEWER', img: '/media/viewer-{lang}.webp', title: '战斗鉴赏：想看哪招看哪招', body: '自选攻守双方的机体和驾驶员，再定武器、防御反应、伤害和场景，直接播放整段战斗演出。', points: ['任何机体的必杀技都能重温', 'BGM 照原版，响起攻方的主题曲'] },
    ],
    rulesTitle: '原版的 bug 修好了，用不用由你',
    rulesBody: '每一项改动都在设置里有自己的开关，立即生效，不改存档格式。想要 1999 年的原样，「全部关闭」一键就回去。',
    rules: [
      { title: '原版 bug 修正', badge: '默认开启', note: '原版算错或漏算的数值，按设计本意修好', accent: 'green', items: ['超能力：命中、回避按实际等级算（原版固定当 64 级）', '圣战士：回避按实际等级算（原版固定当 32 级）', '限界：命中、回避加运动性后不再超出限界', '底力：HP 档位对齐，满血时不再加成', '底力：命中、回避加成减半，暴击不变', '换机：表里漏登记的 3 件武器也继承改造', '圣战士：超级奥拉斩威力随等级提升'] },
      { title: '便利与难度', badge: '默认关闭', note: '想轻松一点就打开，不开就是原版难度', accent: 'orange', items: ['改造上限突破：所有机体都能改到 15 段', '离队退款：剧情带走机体时，退回改造资金', '部件随行：换机时强化部件跟着走', '头目假身：次数减半，或者干脆取消'] },
      { title: '画面与界面', badge: '新旧任选', note: '新界面看着顺手，原版界面一样保留', accent: 'cyan', items: ['战前确认：新版、高清原版、原版三选一', '场间、主角选择、标题菜单：新版或原版', '美术：HD 或原版，F6 随时切', '画面比例：随屏幕铺满，或保持 4:3', '战斗动画、自动存档：各有开关'] },
    ],
    galleryTitle: '熟悉的演出，更清晰的画面',
    galleryBody: '机体、头像和背景全部高清重绘，演出的节奏与镜头一帧不改。',
    moreTitle: '还有这些',
    more: [
      { title: '原生运行，不是模拟', body: 'N64 程序被重编译成本机代码，图形交给 RT64。没有插件、没有模拟器设置，解压就能玩。', accent: 'cyan' },
      { title: '全文三语', body: '全游戏中文、英文翻译，保留日文原文。人名、机体和武器名统一用各语言最通行的叫法。', accent: 'gold' },
      { title: '多存档与自动存档', body: '存档栏比原版多，关键节点自动保存；卡带存档可导出给 ares、Project64、RetroArch 等模拟器。', accent: 'pink' },
      { title: '宽屏铺满，不是拉伸', body: '画面随屏幕在 4:3 到 16:9 之间铺满：原版画面居中，两侧补上延伸的场景，不留黑边，也不变形。', accent: 'orange' },
      { title: '掌机、手机与手柄', body: '自动识别 Steam Deck，键位与大号界面已调好。安卓手机用触屏，只显示当前画面用得上的按钮，写着「确定」「快进」「下个单位」。', accent: 'cyan' },
      { title: 'HD 包，装不装随你', body: 'HD 美术包单独下载。不装就是原版像素；装上之后，F6 随时切换。', accent: 'gold' },
    ],
    platformsTitle: '支持平台',
    platforms: [
      { name: 'Windows', note: '64 位 Windows', experimental: true },
      { name: 'macOS', note: 'Apple 芯片，macOS 14 起' },
      { name: 'Linux', note: 'x86-64，glibc 2.35 起，需要 Vulkan' },
      { name: 'Steam Deck', note: '一键加入 Steam 库，游戏模式直接玩', experimental: true },
      { name: 'Android', note: 'arm64，Android 9 起，触屏操作', experimental: true },
    ],
    experimental: '实验性',
    communityTitle: '一起把译文打磨好',
    communityBody: '全部剧情按关卡排好，日文原文和译文逐句对照。哪句读着别扭、哪个名字不统一，点一下就能提意见，我们会逐条处理并标明结果。',
    communityStory: '读剧情、提意见',
    communityLibrary: '逛图鉴',
    communityGuide: '看攻略',
    newsTitle: '最新动态',
  },
  en: {
    kicker: 'Unofficial · native recompilation',
    titleParts: ['Super Robot', 'Wars 64'],
    title: 'Super Robot Wars 64',
    tagline: 'A native recompilation of the 1999 N64 classic.',
    intro: 'Marchwind 64 is an unofficial native recompilation project for Super Robot Wars 64.',
    lead: 'The N64 campaign runs natively on your computer, handheld or phone: fully translated into English and Chinese, filling a widescreen display, with an optional HD art pack.',
    rom: 'Bring your own original Japanese ROM',
    romMore: 'Why?',
    allPlatforms: 'Other platforms and install steps',
    optional: 'Every fix and every new screen can be turned off on its own, back to the 1999 original at any time',
    strip: [
      { title: 'Native', body: 'Not an emulator: unzip and play', accent: 'cyan' },
      { title: 'EN · ZH · JA', body: 'Full translations, F7 to switch', accent: 'gold' },
      { title: 'HD and widescreen', body: 'F6 for the original graphics', accent: 'pink' },
      { title: 'Original bugs fixed', body: 'On by default, each one optional', accent: 'green' },
    ],
    compareTitle: 'The same line, then and now',
    compareBody: 'On the left, the original: a 4:3 picture with N64-resolution portraits and maps. On the right, this port with the HD pack: the picture fills 16:9, and the portraits and world map are redrawn. F6 switches back to the original at any time, and saves work either way.',
    original: 'Original · 4:3',
    ours: 'MARCHWIND 64 · HD widescreen',
    showTitle: 'The original campaign, comfortable controls',
    showBody: 'The story, numbers, enemy behaviour and battle results run exactly as on the N64. What we changed is the interface, reading and the conveniences the game should always have had. Everything below is an in-game capture.',
    shows: [
      { key: 'prebattle', tag: 'BATTLE', img: '/media/prebattle-{lang}.webp', title: 'Know the odds before the first shot', body: 'Damage, hit and critical chances, shield and barrier for both sides, side by side. Spirits, weapon choice and the battle animation switch sit in one row below.', points: ['Prefer the original screen? One setting brings it back', 'End a battle animation at any time; the result does not change'] },
      { key: 'history', tag: 'DIALOGUE', img: '/media/history-{lang}.webp', title: 'Read at your own pace', body: 'Page back to any line you skipped by accident. Auto-reading, speed, fast-forward, skip and text size each have a button, and the hints follow whichever controller or keyboard you hold.', points: ['F7 switches dialogue, menus and data to another language together', 'Check the original Japanese whenever you like'] },
      { key: 'map', tag: 'MAP', img: '/media/move-jump.webp', title: 'A wider battlefield, fewer button presses', body: 'The map extends to both sides, so you see further on one screen. Hold R while choosing where to move: the farthest ring of squares lights up and you are there in one press.', points: ['L/R step through your units, L2/R2 through the enemy’s', 'The tactical maps themselves are redrawn in HD too'] },
      { key: 'settings', tag: 'OPTIONS', img: '/media/settings-{lang}.webp', title: 'Settings right at hand', body: 'Open them any time during play: language, original or HD art, aspect ratio, full screen, interface size. Every rule fix has its own switch, and keys and buttons can be rebound.', points: ['The View button on a Steam Deck, or the corner of the title screen', 'Three interface sizes: standard, large and largest'] },
      { key: 'library', tag: 'LIBRARY', img: '/media/library-{lang}.webp', title: 'Library: 353 units, 293 characters', body: 'Sorted by series: stats, terrain ratings, weapons with their fully upgraded power and upgrade caps, and every pilot’s growth, spirits and skills, all read from the game itself.', points: ['Open it from the title screen or in game', 'The same Library is on this website'] },
      { key: 'viewer', tag: 'VIEWER', img: '/media/viewer-{lang}.webp', title: 'Battle Viewer: any attack, any time', body: 'Choose the attacking and defending units and pilots, the weapon, the defensive reaction, the damage and the scene, and watch the whole battle animation.', points: ['Replay any unit’s finishing move', 'As in the original, the attacker’s theme plays'] },
    ],
    rulesTitle: 'The original’s bugs are fixed. Whether you use the fixes is up to you',
    rulesBody: 'Every change has its own switch in the settings. It applies at once and never changes the save format. Want 1999 exactly? “Turn all off” takes you back in one press.',
    rules: [
      { title: 'Original bug fixes', badge: 'On by default', note: 'Numbers the original got wrong or left out, fixed as designed', accent: 'green', items: ['ESP: hit and evade follow the actual skill level (the original always used 64)', 'Holy Warrior: evade follows the actual level (the original always used 32)', 'Limit: hit and evade plus mobility no longer exceed the unit’s limit', 'Potential: HP bands line up, no bonus at full HP', 'Potential: hit and evade bonuses halved, criticals unchanged', 'Machine swap: the 3 weapons the table misses keep their upgrades too', 'Holy Warrior: Hyper Aura Slash grows with the level'] },
      { title: 'Comfort and difficulty', badge: 'Off by default', note: 'Turn them on for an easier run; leave them off for the original difficulty', accent: 'orange', items: ['Upgrade cap raised: every unit can reach 15 levels', 'Departure refund: get upgrade funds back when the story takes a unit away', 'Parts carry over: enhancement parts move to the new machine', 'Boss dummies: halved, or removed altogether'] },
      { title: 'Graphics and screens', badge: 'New or original', note: 'The new screens are easier to use; the original ones stay available', accent: 'cyan', items: ['Pre-battle screen: new, HD original or original', 'Intermission, hero select and title menu: new or original', 'Art: HD or original, F6 at any time', 'Aspect: fill the screen or keep 4:3', 'Battle animations and autosaves: each has a switch'] },
    ],
    galleryTitle: 'The battles you remember, sharper',
    galleryBody: 'Units, portraits and backgrounds are redrawn in high resolution; the timing and camera of every animation stay frame for frame.',
    moreTitle: 'And also',
    more: [
      { title: 'Native, not emulated', body: 'The N64 program is recompiled to native code and drawn by RT64. No plugins, no emulator settings: unzip and play.', accent: 'cyan' },
      { title: 'Fully translated', body: 'The whole game in English and Chinese, with the Japanese kept. Names of characters, units and weapons use each language’s most familiar forms.', accent: 'gold' },
      { title: 'More saves, autosaves', body: 'More save slots than the original and autosaves at key points. The cartridge save exports to ares, Project64, RetroArch and others.', accent: 'pink' },
      { title: 'Widescreen without stretching', body: 'The picture fills any screen from 4:3 to 16:9: the original view stays centred and the scene extends to both sides. No black bars, no distortion.', accent: 'orange' },
      { title: 'Handhelds, phones, controllers', body: 'A Steam Deck is recognised, with controls and a large interface ready. On Android phones the touch controls show only the buttons the current screen needs, labelled “OK”, “Fast” or “Next unit”.', accent: 'cyan' },
      { title: 'HD pack, your call', body: 'The HD art pack is a separate download. Without it you get the original pixels; with it, F6 switches at any time.', accent: 'gold' },
    ],
    platformsTitle: 'Platforms',
    platforms: [
      { name: 'Windows', note: '64-bit Windows', experimental: true },
      { name: 'macOS', note: 'Apple silicon, macOS 14 or later' },
      { name: 'Linux', note: 'x86-64, glibc 2.35+, Vulkan' },
      { name: 'Steam Deck', note: 'One script adds it to Steam; plays from Game Mode', experimental: true },
      { name: 'Android', note: 'arm64, Android 9+, touch controls', experimental: true },
    ],
    experimental: 'Experimental',
    communityTitle: 'Help us polish the translation',
    communityBody: 'The whole script is here in stage order, Japanese and translation line by line. If a line reads oddly or a name is inconsistent, one click sends a suggestion, and we answer every one.',
    communityStory: 'Read the story, suggest fixes',
    communityLibrary: 'Browse the Library',
    communityGuide: 'Open the guide',
    newsTitle: 'Latest news',
  },
  ja: {
    kicker: '非公式 · ネイティブ再コンパイル',
    titleParts: ['スーパーロボット', '大戦64'],
    title: 'スーパーロボット大戦64',
    tagline: '1999 年の N64 の名作を、ネイティブに再コンパイル。',
    intro: 'マーチウィンド64 は『スーパーロボット大戦64』の非公式ネイティブ再コンパイル・プロジェクトです。',
    lead: 'あの N64 の戦いが、あなたのパソコン・携帯機・スマホでネイティブに動きます。英語と中国語の全文翻訳、ワイド画面いっぱいの表示、そして別配布の HD アートパック。',
    rom: '日本版のオリジナル ROM が必要です',
    romMore: 'なぜ？',
    allPlatforms: 'ほかのプラットフォームとインストール方法',
    optional: '修正も新しい画面も一つずつオフにでき、いつでも 1999 年のままに戻せます',
    strip: [
      { title: 'ネイティブ動作', body: 'エミュレーターではなく、展開してすぐ遊べる', accent: 'cyan' },
      { title: '日・英・中', body: '全文翻訳、F7 でいつでも切り替え', accent: 'gold' },
      { title: 'HD とワイド', body: 'F6 でいつでもオリジナル画面に', accent: 'pink' },
      { title: '原作のバグ修正', body: '初期設定でオン、一つずつオフにできる', accent: 'green' },
    ],
    compareTitle: '同じセリフを、オリジナルと今で',
    compareBody: '左がオリジナル。4:3 の画面に、N64 解像度の顔グラフィックとマップ。右がこの移植版と HD パック。画面は 16:9 いっぱいに広がり、顔グラフィックとワールドマップは描き直しています。F6 でいつでもオリジナルに戻せ、セーブもそのまま使えます。',
    original: 'オリジナル · 4:3',
    ours: 'MARCHWIND 64 · HD ワイド',
    showTitle: '原作そのままの戦いを、快適な操作で',
    showBody: 'シナリオ・数値・敵の行動・戦闘結果は原作どおり。手を入れたのはインターフェースと会話、そして本来あってほしかった便利さです。以下はすべてゲーム画面そのままです。',
    shows: [
      { key: 'prebattle', tag: 'BATTLE', img: '/media/prebattle-{lang}.webp', title: '戦う前に、勝敗がひと目でわかる', body: '双方のダメージ・命中率・クリティカル率、シールド防御やバリアの効果を並べて表示。精神コマンド、武器の変更、戦闘アニメの切り替えも下に一列で。', points: ['オリジナルの画面が好みなら、設定ひとつで戻せます', '戦闘アニメはいつでも途中終了でき、結果は変わりません'] },
      { key: 'history', tag: 'DIALOGUE', img: '/media/history-{lang}.webp', title: 'セリフは自分のペースで', body: 'うっかり送ったセリフも、いつでも履歴で読み返せます。オート・速度・早送り・スキップ・文字サイズにそれぞれボタンがあり、操作ガイドは手元のコントローラーやキーボードに合わせて変わります。', points: ['F7 で会話・メニュー・データの言語をまとめて切り替え', '日本語の原文もすぐに確認できます'] },
      { key: 'map', tag: 'MAP', img: '/media/move-jump.webp', title: '広い戦場を、少ない操作で', body: 'マップが左右に広がり、一画面でより遠くまで。移動先を選ぶとき R を押し続けると、いちばん遠いマスが光り、一気にそこへ。', points: ['L／R で味方を、L2／R2 で敵を順に選択', '戦術マップそのものも HD で描き直し'] },
      { key: 'settings', tag: 'OPTIONS', img: '/media/settings-{lang}.webp', title: '設定はいつでも手元に', body: 'プレイ中いつでも開けます。言語、オリジナルか HD か、画面比率、フルスクリーン、UI サイズ。ルール修正は一つずつ切り替えでき、キーボードもコントローラーも割り当てを変えられます。', points: ['Steam Deck はビューボタン、タイトル画面の右下からも', 'UI サイズは標準・大・特大の 3 段階'] },
      { key: 'library', tag: 'LIBRARY', img: '/media/library-{lang}.webp', title: '図鑑：ユニット 353 体、キャラクター 293 人', body: '作品ごとに整理。能力値、地形適応、武器とフル改造時の攻撃力、改造上限、パイロットの成長・精神コマンド・特殊技能まで、すべてゲームから直接読み込みます。', points: ['タイトル画面からもゲーム中からも開けます', '同じ図鑑をこのサイトでも公開'] },
      { key: 'viewer', tag: 'VIEWER', img: '/media/viewer-{lang}.webp', title: 'バトルビューアー：見たい技を、見たいときに', body: '攻撃側と防御側のユニットとパイロット、武器、防御の反応、ダメージ、背景を選んで、戦闘アニメを丸ごと再生。', points: ['どのユニットの必殺技でも見返せます', 'BGM は原作どおり攻撃側のテーマ'] },
    ],
    rulesTitle: '原作のバグは直しました。使うかどうかはあなた次第',
    rulesBody: 'どの変更も設定にそれぞれスイッチがあり、すぐに反映され、セーブ形式は変わりません。1999 年のままで遊びたければ「すべてオフ」ひとつで戻ります。',
    rules: [
      { title: '原作のバグ修正', badge: '初期設定でオン', note: '原作で計算を誤っていた・漏れていた数値を、本来の意図どおりに', accent: 'green', items: ['超能力：命中・回避を実際のレベルで計算（原作は常に 64 扱い）', '聖戦士：回避を実際のレベルで計算（原作は常に 32 扱い）', '限界：命中・回避と運動性の合計が限界を超えない', '底力：HP の段階をそろえ、HP 満タンでは補正なし', '底力：命中・回避の補正を半分に（クリティカルはそのまま）', '乗り換え：テーブルから漏れていた 3 つの武器も改造を引き継ぐ', '聖戦士：ハイパーオーラ斬りの威力がレベルで上がる'] },
      { title: '快適さと難易度', badge: '初期設定でオフ', note: '楽に遊びたいときだけオンに。オフなら原作の難易度', accent: 'orange', items: ['改造上限突破：全ユニット 15 段階まで改造可能', '離脱返金：シナリオでユニットが抜けると改造資金を返却', 'パーツ引き継ぎ：乗り換え時に強化パーツも新しい機体へ', 'ボスの身代わり：回数を半分に、またはなしに'] },
      { title: '画面とインターフェース', badge: '新旧を選択', note: '新しい画面は見やすく、オリジナルの画面もそのまま残しています', accent: 'cyan', items: ['戦闘前の確認：新版・HD オリジナル・オリジナルから選択', 'インターミッション・主人公選択・タイトルメニュー：新版かオリジナル', 'アート：HD かオリジナル、F6 でいつでも', '画面比率：画面いっぱいか 4:3 のままか', '戦闘アニメ・自動セーブ：それぞれスイッチあり'] },
    ],
    galleryTitle: 'おなじみの演出を、くっきりと',
    galleryBody: 'ユニット・顔グラフィック・背景を高解像度で描き直し。演出のテンポとカメラは 1 フレームも変えていません。',
    moreTitle: 'そのほか',
    more: [
      { title: 'エミュレーターではなくネイティブ', body: 'N64 のプログラムをネイティブコードにリコンパイルし、描画は RT64。プラグインもエミュレーターの設定も不要で、展開してすぐ遊べます。', accent: 'cyan' },
      { title: '全文 3 言語', body: 'ゲーム全体を英語と中国語に翻訳し、日本語の原文もそのまま。キャラクター名・ユニット名・武器名は各言語でいちばんなじみのある表記に統一。', accent: 'gold' },
      { title: 'セーブ枠の追加と自動セーブ', body: '原作より多いセーブ枠と、要所での自動セーブ。カートリッジのセーブは ares・Project64・RetroArch などへ書き出せます。', accent: 'pink' },
      { title: '引き伸ばさないワイド表示', body: '4:3 から 16:9 まで画面いっぱいに。オリジナルの画面は中央のまま、左右に景色を広げるので、黒帯もゆがみもありません。', accent: 'orange' },
      { title: '携帯機・スマホ・コントローラー', body: 'Steam Deck を自動認識し、ボタン配置と大きめの UI も設定済み。Android スマホはタッチ操作で、いまの画面で使うボタンだけを「決定」「早送り」「次の味方」のように表示します。', accent: 'cyan' },
      { title: 'HD パックはお好みで', body: 'HD アートパックは別配布。入れなければオリジナルのドット絵のまま、入れれば F6 でいつでも切り替え。', accent: 'gold' },
    ],
    platformsTitle: '対応プラットフォーム',
    platforms: [
      { name: 'Windows', note: '64 ビット版 Windows', experimental: true },
      { name: 'macOS', note: 'Apple シリコン、macOS 14 以降' },
      { name: 'Linux', note: 'x86-64、glibc 2.35 以降、Vulkan 必須' },
      { name: 'Steam Deck', note: 'スクリプトで Steam に追加、ゲームモードで起動', experimental: true },
      { name: 'Android', note: 'arm64、Android 9 以降、タッチ操作', experimental: true },
    ],
    experimental: '試験的',
    communityTitle: 'いっしょに翻訳を磨きませんか',
    communityBody: '全シナリオを話数順に、日本語原文と翻訳を一行ずつ対照しています。不自然な訳や表記の揺れに気づいたら、ワンクリックで意見を送れます。一件ずつ確認し、対応結果をお知らせします。',
    communityStory: 'シナリオを読んで意見を送る',
    communityLibrary: '図鑑を見る',
    communityGuide: '攻略を見る',
    newsTitle: '最新情報',
  },
};
