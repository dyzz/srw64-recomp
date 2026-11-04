import type { TextLang } from '../i18n';

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

export const HOME: Record<TextLang, Home> = {
  zh: {
    kicker: '非官方 · 原生重编译',
    titleParts: ['超级机器人', '大战64'],
    title: '超级机器人大战64',
    tagline: '1999 年 N64 游戏的原生重编译版。',
    intro: 'Marchwind 64（“三月风”）是《超级机器人大战64》的非官方原生重编译项目。',
    lead: '支持电脑、Steam Deck、安卓手机和 Nintendo Switch，提供简繁中文、英文和韩文全文翻译与宽屏显示，另有可选的 HD 美术包。',
    rom: '需要自备日版 ROM',
    romMore: 'ROM 说明',
    allPlatforms: '其他平台与安装说明',
    optional: '在设置中逐项选择修正和界面模式',
    strip: [
      { title: '原生运行', body: '安装后添加 ROM 即可开始', accent: 'cyan' },
      { title: '简 · 繁 · 英 · 韩 · 日', body: '全文翻译，游戏中随时切换', accent: 'gold' },
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
      { key: 'filters', tag: 'FILTERS', img: '/media/filters-tv-crt.webp', wide: true, title: 'RetroArch 滤镜与框体', body: '可以直接使用 RetroArch 的 slang 着色器预设（.slangp），给画面加上 CRT 扫描线、NTSC 色彩等效果；画面比例为 4:3 时还能套上 RetroArch 的框体，透明窗口自动对准游戏画面。在「选项 → 通用」里设置。', points: ['自带 crt-lottes、crt-geom、zfast-crt（适合掌机）、ntsc-adaptive、xbrz-freescale 等 9 个常用预设', '自动找到已安装的 RetroArch 的全部 slang 着色器；自己的预设和框体放进数据文件夹即可', '可按原版 240 行（最接近 CRT）、480、960 行或窗口分辨率处理', '只作用于游戏画面和对白，选项窗口、提示和框体保持清晰', '滤镜支持 Windows、macOS、Linux／Steam Deck 和安卓，Switch 版暂不支持'], credit: '框体为 RetroArch 自带的 tv-integer（libretro/common-overlays，CC BY 4.0），滤镜为 crt-lottes（480 行）。安装包不内置任何框体。' },
      { key: 'library', tag: 'LIBRARY', img: '/media/library-{lang}.webp', title: '图鉴：353 台机体、293 名人物', body: '按作品分类，收录机体能力值、地形适应、武器及满改攻击力、改造上限，以及驾驶员成长、精神指令和特殊技能。数据直接取自游戏。', points: ['标题画面和游戏中都能打开', '网站提供同一份图鉴'] },
      { key: 'viewer', tag: 'VIEWER', img: '/media/viewer-{lang}.webp', title: '战斗鉴赏', body: '选择攻守双方的机体和驾驶员，设置武器、防御方式、伤害和场景，即可播放完整战斗演出。', points: ['支持查看所有机体的武器演出', 'BGM 沿用原版规则，播放攻击方的主题曲'] },
      { key: 'link', tag: 'LINK BATTLER', img: '/media/link-{lang}.webp', title: 'Link Battler 联动：不用 GB 卡带', body: '在场间菜单选「联动」，勾选作品，F91、豪将军和赞波特3 就会照原版的联动关加入。不需要 Transfer Pak 和 GB 卡带。', points: ['已在游戏里完整走通', '三部作品可以任选组合'] },
      { key: 'mods', tag: 'MODS', img: '/media/mods-{lang}.webp', title: 'MOD：音乐、数值、名字和图标', body: '把 .srw64mod 文件拖进游戏窗口就能安装；安卓版放进「文件」里 Marchwind64 文件夹的 mods/。包里只有数据、不带程序，存档里保存的仍是游戏原来的内容，随时可以停用或删除。', points: ['音乐：用 Ogg 文件替换 49 首曲子中的任意几首，还能指定某个人物或机体战斗时放哪首', '数值：机体、驾驶员和武器的能力、地形适应、技能与精神', '名字与头像：主角、搭档和部队的名字，任意人物、机体、武器的名字和人物头像', '地图图标与战斗语音：一张图就能换掉机体的地图图标；战斗台词出现时播放语音', '官网有五个制作工具，在浏览器里完成，文件不会上传'] },
      { key: 'speed', tag: 'SPEED', img: '/media/speed-{lang}.webp', img2: '/media/speedset-{lang}.webp', title: '最高 4 倍速', body: '倍速键在原速、2 倍、3 倍、4 倍速之间依次切换：键盘 Tab、Steam Deck 背键 R4，手机上有倍速按钮，也可以在「选项 → 通用」里直接选。加快的是地图上的剧情、敌方回合、单位移动、战斗动画和世界地图；我方菜单、光标和场间画面保持原速，音乐也不变调。', points: ['3 倍速和 4 倍速要每秒画 90、120 帧；跟不上的设备隔帧绘制，游戏逻辑照常每帧运行', '卡拉OK、标题演示和战斗鉴赏始终原速'] },
    ],
    rulesTitle: '原版 bug 修正与可选规则',
    rulesBody: '每项规则都有独立开关，调整后立即生效，不改变存档格式。「全部关闭」可统一使用原版规则。',
    rules: [
      { title: '原版 bug 修正', badge: '默认开启', note: '修正原版中的数值计算错误和遗漏', accent: 'green', items: ['超能力：命中、回避按实际等级计算（原版固定按 64 级计算）', '圣战士：回避按实际等级计算（原版固定按 32 级计算）', '限界：命中、回避分别加上运动性后，以限界为上限', '底力：修正 HP 档位，满血时不触发加成', '底力：命中、回避加成减半，暴击不变', '换乘：补上表中遗漏的 3 件武器，使其可以继承改造', '圣战士：超级奥拉斩的威力随等级提升'] },
      { title: '便利与难度选项', badge: '默认关闭', note: '可按需要开启，调整改造、资金和战斗难度', accent: 'orange', items: ['改造上限突破：所有机体都能改到 15 段', '离队退款：机体因剧情离队时，退还其改造花费', '部件随行：换乘时，强化部件随驾驶员转移到新机体', '头目假身：可将次数减半，或完全取消', '周目继承：上周目完成过的隐藏要素，在原本入队的那一关之后直接加入'] },
      { title: '画面与界面', badge: '新旧可选', note: '可按画面分别选择新版或原版界面', accent: 'cyan', items: ['战前确认：新版、高清原版、原版三选一', '场间画面、主角选择、标题菜单：新版或原版', '美术：HD 或原版，随时切换', '画面比例：铺满屏幕，或保持 4:3', '武器改造：统一（一台机体的武器一起改到同一段数，费用不变）或逐件（原版）', '战斗动画、自动存档：分别设置开关'] },
    ],
    galleryTitle: 'HD 战斗画面',
    galleryBody: '机体、cut-in、头像和背景采用高清素材，沿用原版的演出时序和镜头。',
    moreTitle: '更多功能',
    more: [
      { title: '原生运行', body: 'N64 程序重编译为本机代码，图形由 RT64 渲染。安装后按说明添加 ROM，即可启动游戏。', accent: 'cyan' },
      { title: '五种语言', body: '游戏全文提供简体中文、繁体中文、英文、韩文翻译及日文原文。韩文翻译由 테오（jung5441）完成，以 CC BY 4.0 授权。人名、机体名和武器名采用各语言的常用译名，繁体中文用台湾译名。', accent: 'gold' },
      { title: '多存档栏与自动存档', body: '增加存档栏，并在关键节点自动存档。卡带存档可导出，供 ares、Project64、RetroArch 等模拟器使用。', accent: 'pink' },
      { title: '宽屏不拉伸', body: '支持 4:3 到 16:9 的屏幕比例。原版画面居中，两侧扩展场景，以保持比例的方式铺满屏幕。', accent: 'orange' },
      { title: '掌机、手机与手柄', body: '自动识别 Steam Deck，并默认使用对应键位和大号界面。Android 提供触屏操作，仅显示当前画面需要的按键，并标注「确定」「快进」「下个单位」等功能。', accent: 'cyan' },
      { title: '可选 HD 美术包', body: 'HD 美术包单独下载，电脑与 Android 通用，Switch 另有一份；未安装时使用原版像素画面，安装后可在游戏中随时切换。', accent: 'gold' },
    ],
    platformsTitle: '支持平台',
    platforms: [
      { name: 'Windows', note: '64 位 Windows', experimental: true },
      { name: 'macOS', note: 'Apple 芯片，macOS 14 及以上' },
      { name: 'Linux', note: 'x86-64，glibc 2.35 及以上，需要 Vulkan' },
      { name: 'Steam Deck', note: '运行脚本加入 Steam 库，在游戏模式启动', experimental: true },
      { name: 'Android', note: 'arm64，Android 9 及以上，触屏操作', experimental: true },
      { name: 'Nintendo Switch', note: 'Atmosphère 自制系统，从 hbmenu 启动', experimental: true },
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
    lead: 'Runs natively on Windows, macOS, Linux, Steam Deck, Android and Nintendo Switch, with full English, Chinese (Simplified and Traditional) and Korean translations and widescreen support, plus an optional HD art pack.',
    rom: 'Requires your own Japanese ROM',
    romMore: 'About ROMs',
    allPlatforms: 'Other platforms and installation',
    optional: 'Choose fixes and screen layouts individually in settings',
    strip: [
      { title: 'Native', body: 'Install, add your ROM and play', accent: 'cyan' },
      { title: 'EN · 简 · 繁 · KO · JA', body: 'Full translations, switchable in game', accent: 'gold' },
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
      { key: 'filters', tag: 'FILTERS', img: '/media/filters-tv-crt.webp', wide: true, title: 'RetroArch filters and bezels', body: 'Use RetroArch slang shader presets (.slangp) directly for CRT scanlines, NTSC colour and more. At 4:3 you can also add a RetroArch bezel; its transparent window lines up with the picture by itself. Both are in Options → General.', points: ['Nine common presets included, such as crt-lottes, crt-geom, zfast-crt (for handhelds), ntsc-adaptive and xbrz-freescale', 'Finds every slang shader of an installed RetroArch; drop your own presets and bezels in the data folder', 'Works on the original 240 lines (closest to a CRT), 480, 960, or the window’s resolution', 'Only the game picture and dialogue are filtered; the options window, notices and the bezel stay sharp', 'Filters run on Windows, macOS, Linux/Steam Deck and Android; not on Switch yet'], credit: 'Bezel: tv-integer from RetroArch’s own overlays (libretro/common-overlays, CC BY 4.0). Filter: crt-lottes at 480 lines. The game ships with no bezels.' },
      { key: 'library', tag: 'LIBRARY', img: '/media/library-{lang}.webp', title: 'Library: 353 units, 293 characters', body: 'Organised by series, with unit stats, terrain ratings, weapons and their fully upgraded power, upgrade caps, and each pilot’s growth, spirits and skills. All entries come directly from the game data.', points: ['Open it from the title screen or during play', 'The same Library is available on this website'] },
      { key: 'viewer', tag: 'VIEWER', img: '/media/viewer-{lang}.webp', title: 'Battle Viewer', body: 'Choose the attacking and defending units and pilots, set the weapon, the defender’s reaction, the damage and the scene, then play the full battle animation.', points: ['View weapon animations for every unit', 'Plays the attacker’s theme, as in the original'] },
      { key: 'link', tag: 'LINK BATTLER', img: '/media/link-{lang}.webp', title: 'Link Battler without a Game Boy cartridge', body: 'Choose Link in the intermission menu and tick the series: F91, GoShogun and Zambot 3 join through the original link stages. No Transfer Pak or Game Boy cartridge needed.', points: ['Confirmed working from start to finish', 'Any combination of the three series'] },
      { key: 'mods', tag: 'MODS', img: '/media/mods-{lang}.webp', title: 'Mods: music, numbers, names and icons', body: 'Drop a .srw64mod file on the game window to install it; on Android, put it in mods/ in the Marchwind64 folder under Files. A package holds data only, no code, and your saves keep the game’s own values, so you can turn a mod off or remove it at any time.', points: ['Music: replace any of the 49 tracks with Ogg files, and pick the song a character or unit fights to', 'Numbers: unit, pilot and weapon stats, terrain ratings, skills and spirit commands', 'Names and portraits: the heroes’, partners’ and unit’s names, any character, unit or weapon name, and character portraits', 'Map icons and battle voices: one picture replaces a unit’s map icon; a voice plays when a battle line comes up', 'Five makers on the website do it all in the browser; nothing is uploaded'] },
      { key: 'speed', tag: 'SPEED', img: '/media/speed-{lang}.webp', img2: '/media/speedset-{lang}.webp', title: 'Up to 4× speed', body: 'The speed key steps through normal, 2×, 3× and 4×: Tab on the keyboard, R4 on the back of the Steam Deck, a speed button on phones, or pick one in Options → General. It speeds up story scenes on the map, the enemy phase, units moving, battle animations and the world map; your menus, the cursor and the intermission keep the original speed, and the music keeps its pitch.', points: ['3× and 4× need 90 and 120 frames a second; a device that can’t keep up draws every other frame while the game logic still runs every frame', 'Karaoke, the title demo and the battle viewer always run at normal speed'] },
    ],
    rulesTitle: 'Bug fixes and optional rules',
    rulesBody: 'Each rule has its own toggle and takes effect immediately, using the same save format. Select “Turn all off” to use the original rules.',
    rules: [
      { title: 'Original bug fixes', badge: 'On by default', note: 'Corrections to calculation errors and omissions in the original', accent: 'green', items: ['ESP: hit and evade use the actual skill level (the original always used 64)', 'Holy Warrior: evade uses the actual level (the original always used 32)', 'Limit: hit and evade, each combined with mobility, are capped by the unit’s limit', 'Potential: corrected HP bands, with no bonus at full HP', 'Potential: hit and evade bonuses halved, criticals unchanged', 'Machine swap: the 3 weapons omitted from the table also retain their upgrades', 'Holy Warrior: Hyper Aura Slash grows stronger with the level'] },
      { title: 'Convenience and difficulty', badge: 'Off by default', note: 'Optional adjustments to upgrades, funds and battle difficulty', accent: 'orange', items: ['Raised upgrade cap: every unit can reach 15 levels', 'Departure refund: returns upgrade funds when a unit leaves as part of the story', 'Parts carry over: enhancement parts move with the pilot to the new machine', 'Boss dummies: halve their uses or disable them', 'New Game+: hidden extras you completed in an earlier playthrough join right after the stage they first join in'] },
      { title: 'Graphics and interface', badge: 'New or original', note: 'Choose the new or original layout for each screen', accent: 'cyan', items: ['Pre-battle screen: new, HD original or original', 'Intermission, hero selection and title menu: new or original', 'Art: HD or original, switchable at any time', 'Aspect ratio: fill the screen or keep 4:3', 'Weapon upgrades: unified (all of a unit’s weapons go up together, at the same total cost) or one by one (original)', 'Battle animations and autosaves: individual toggles'] },
    ],
    galleryTitle: 'Battles in HD',
    galleryBody: 'High-resolution art for units, cut-ins, portraits and backgrounds, with the original animation timing and camera work.',
    moreTitle: 'More features',
    more: [
      { title: 'Runs natively', body: 'The N64 program is recompiled into native code, with graphics rendered by RT64. After installation, add your ROM following the setup instructions to start playing.', accent: 'cyan' },
      { title: 'Full text in five languages', body: 'Full English, Simplified Chinese, Traditional Chinese and Korean translations alongside the original Japanese. The Korean translation is by 테오 (jung5441), under CC BY 4.0. Character, unit and weapon names follow common usage in each language; Traditional Chinese uses the names used in Taiwan.', accent: 'gold' },
      { title: 'Extra save slots and autosaves', body: 'More save slots than the original, with autosaves at key points. Export cartridge saves for use with ares, Project64, RetroArch and other emulators.', accent: 'pink' },
      { title: 'Widescreen without stretching', body: 'Supports screen ratios from 4:3 to 16:9. The original view stays centred while the scenery extends to the sides, filling the screen without stretching the image.', accent: 'orange' },
      { title: 'Handhelds, phones, controllers', body: 'Automatically detects Steam Deck and uses its button layout with a large interface by default. On Android, touch controls show the buttons needed for the current screen, labelled with their functions, such as “OK”, “Fast” and “Next unit”.', accent: 'cyan' },
      { title: 'Optional HD art pack', body: 'The HD art pack is a separate download for computers and Android, with one of its own for the Switch. The game uses the original pixel art without it; once installed, you can switch between the two at any time in game.', accent: 'gold' },
    ],
    platformsTitle: 'Platforms',
    platforms: [
      { name: 'Windows', note: '64-bit Windows', experimental: true },
      { name: 'macOS', note: 'Apple silicon, macOS 14 or later' },
      { name: 'Linux', note: 'x86-64, glibc 2.35 or later, Vulkan' },
      { name: 'Steam Deck', note: 'Add it to Steam with the script, then launch from Game Mode', experimental: true },
      { name: 'Android', note: 'arm64, Android 9 or later, touch controls', experimental: true },
      { name: 'Nintendo Switch', note: 'Atmosphère custom firmware, launched from hbmenu', experimental: true },
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
    lead: 'Windows・macOS・Linux・Steam Deck・Android・Nintendo Switch でネイティブに動作し、英語・中国語（簡体字・繁体字）・韓国語の全文翻訳とワイド画面に対応し、別配布の HD アートパックも利用できます。',
    rom: '日本版の ROM をご用意ください',
    romMore: 'ROM について',
    allPlatforms: 'ほかのプラットフォームとインストール方法',
    optional: '修正や画面の種類は、設定で個別に選べます',
    strip: [
      { title: 'ネイティブ動作', body: 'インストールして ROM を追加すればプレイ可能', accent: 'cyan' },
      { title: '日・英・簡・繁・韓', body: '全文翻訳に対応、ゲーム中に切り替え可能', accent: 'gold' },
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
      { key: 'filters', tag: 'FILTERS', img: '/media/filters-tv-crt.webp', wide: true, title: 'RetroArch のフィルターとベゼル', body: 'RetroArch の slang シェーダープリセット（.slangp）をそのまま使い、CRT の走査線や NTSC の色合いなどを加えられます。4:3 なら RetroArch のベゼルも重ねられ、透明部分は自動でゲーム画面に合わせます。設定は「オプション → 一般」から。', points: ['crt-lottes、crt-geom、zfast-crt（携帯機向け）、ntsc-adaptive、xbrz-freescale など 9 種のプリセットを同梱', 'インストール済みの RetroArch の slang シェーダーを自動で検出。自分のプリセットやベゼルはデータフォルダに置くだけ', 'オリジナルの 240 ライン（CRT にいちばん近い）、480、960 ライン、またはウィンドウの解像度で処理', 'フィルターはゲーム画面と会話だけにかかり、オプション画面・通知・ベゼルはくっきりしたまま', 'フィルターは Windows・macOS・Linux／Steam Deck・Android に対応。Switch 版はまだ非対応'], credit: 'ベゼルは RetroArch 付属の tv-integer（libretro/common-overlays、CC BY 4.0）、フィルターは crt-lottes（480 ライン）。ゲームにベゼルは同梱していません。' },
      { key: 'library', tag: 'LIBRARY', img: '/media/library-{lang}.webp', title: '図鑑：ユニット 353 体、キャラクター 293 人', body: '作品ごとに、ユニットの能力値、地形適応、武器とフル改造時の攻撃力、改造上限、パイロットの成長・精神コマンド・特殊技能を収録しています。データはすべてゲームから直接読み込んでいます。', points: ['タイトル画面からもゲーム中からも開けます', '同じ図鑑をこのサイトでも公開しています'] },
      { key: 'viewer', tag: 'VIEWER', img: '/media/viewer-{lang}.webp', title: 'バトルビューアー', body: '攻撃側と防御側のユニット・パイロットを選び、武器、防御行動、ダメージ、背景を設定して、戦闘アニメを最後まで再生できます。', points: ['全ユニットの武器演出を鑑賞できます', 'BGM は原作と同じく、攻撃側のテーマを再生します'] },
      { key: 'link', tag: 'LINK BATTLER', img: '/media/link-{lang}.webp', title: 'リンクバトラー連動：GB カートリッジ不要', body: 'インターミッションの「リンク」で作品にチェックを入れると、F91・ゴーショーグン・ザンボット3 が元のリンクステージで参戦します。64GB パックも GB カートリッジも不要です。', points: ['最初から最後まで動作確認済み', '3 作品は自由に組み合わせ可能'] },
      { key: 'mods', tag: 'MODS', img: '/media/mods-{lang}.webp', title: 'MOD：音楽・数値・名前・アイコン', body: '.srw64mod ファイルをゲームウィンドウにドロップするとインストールされます。Android では「ファイル」の Marchwind64 フォルダーにある mods/ に置きます。パッケージに入るのはデータだけでプログラムは含まず、セーブにはゲーム本来の内容が残るので、いつでも無効化・削除できます。', points: ['音楽：49 曲のうち好きな曲を Ogg ファイルで差し替え、キャラクターやユニットごとに戦闘曲も指定可能', '数値：ユニット・パイロット・武器の能力、地形適応、技能、精神コマンド', '名前と顔グラフィック：主人公・パートナー・部隊の名前、任意のキャラクター・ユニット・武器の名前と顔グラフィック', 'マップアイコンと戦闘ボイス：画像 1 枚でユニットのマップアイコンを差し替え、戦闘のセリフに合わせてボイスを再生', '公式サイトに 5 つの制作ツールがあり、ブラウザーだけで完結。ファイルはアップロードされません'] },
      { key: 'speed', tag: 'SPEED', img: '/media/speed-{lang}.webp', img2: '/media/speedset-{lang}.webp', title: '最大 4 倍速', body: '倍速キーで等速・2 倍・3 倍・4 倍速を順に切り替えます。キーボードは Tab、Steam Deck は背面の R4、スマートフォンには倍速ボタンがあり、「オプション → 一般」でも直接選べます。速くなるのはマップ上のイベント、敵フェイズ、ユニットの移動、戦闘アニメ、ワールドマップで、自軍のメニュー・カーソル・インターミッションは元の速さのまま、音楽の音程も変わりません。', points: ['3 倍速と 4 倍速には毎秒 90・120 フレームの描画が必要です。追いつかない端末では描画を間引き、ゲームの処理は毎フレーム行います', 'カラオケ、タイトルデモ、バトルビューアーは常に等速'] },
    ],
    rulesTitle: '原作のバグ修正と選べるルール',
    rulesBody: '各項目を個別に切り替えられ、変更はすぐに反映されます。セーブ形式は共通です。「すべてオフ」で原作のルールを使用できます。',
    rules: [
      { title: '原作のバグ修正', badge: '初期設定でオン', note: '原作の数値計算の誤りや計算漏れを修正', accent: 'green', items: ['超能力：命中・回避に実際のレベルを反映（原作では常に 64 として計算）', '聖戦士：回避に実際のレベルを反映（原作では常に 32 として計算）', '限界：命中・回避に運動性を加えた値を、それぞれ限界で制限', '底力：HP の段階を修正し、HP 満タンでは補正なし', '底力：命中・回避の補正を半分に（クリティカルはそのまま）', '乗り換え：テーブルに未登録だった 3 つの武器も改造を引き継ぐ', '聖戦士：ハイパーオーラ斬りの威力がレベルで上がる'] },
      { title: '快適性と難易度', badge: '初期設定でオフ', note: '改造・資金・戦闘の難易度を、必要に応じて調整できます', accent: 'orange', items: ['改造上限突破：全ユニット 15 段階まで改造可能', '離脱返金：シナリオでユニットが離脱すると改造資金を返却', 'パーツ引き継ぎ：乗り換え時に強化パーツも新しい機体へ', 'ボスの身代わり：回数を半分にするか、無効化', '周回引き継ぎ：前の周回で達成した隠し要素は、本来加入するステージの後にそのまま加入'] },
      { title: '画面とインターフェース', badge: '新旧を選択', note: '画面ごとに新版とオリジナルを選択できます', accent: 'cyan', items: ['戦闘前の確認：新版・HD オリジナル・オリジナルから選択', 'インターミッション・主人公選択・タイトルメニュー：新版またはオリジナル', 'アート：HD またはオリジナル、いつでも切り替え', '画面比率：画面全体に表示、または 4:3 を維持', '武器改造：一括（ユニットの武器をまとめて同じ段階に、費用は同じ）または個別（オリジナル）', '戦闘アニメ・自動セーブ：それぞれオン／オフを設定'] },
    ],
    galleryTitle: 'HD の戦闘画面',
    galleryBody: 'ユニット・カットイン・顔グラフィック・背景に高解像度の素材を使い、演出のタイミングとカメラワークは原作を引き継いでいます。',
    moreTitle: 'そのほかの機能',
    more: [
      { title: 'ネイティブ動作', body: 'N64 のプログラムをネイティブコードに再コンパイルし、RT64 で描画します。インストール後、案内に沿って ROM を追加するとプレイできます。', accent: 'cyan' },
      { title: '全文 5 言語対応', body: '日本語原文に加え、ゲーム全文の英語・簡体字中国語・繁体字中国語・韓国語訳を収録しています。韓国語訳は 테오（jung5441）によるもので、CC BY 4.0 で提供されています。キャラクター名・ユニット名・武器名には各言語で一般的な表記を採用し、繁体字中国語は台湾での表記に合わせています。', accent: 'gold' },
      { title: 'セーブ枠の追加と自動セーブ', body: '原作よりセーブ枠を増やし、要所で自動セーブを行います。カートリッジのセーブデータは、ares・Project64・RetroArch などで使える形式で書き出せます。', accent: 'pink' },
      { title: '引き伸ばさないワイド表示', body: '4:3 から 16:9 までの画面比率に対応。オリジナルの表示範囲を中央に置き、左右に背景を広げることで、比率を保ったまま黒帯のない表示にします。', accent: 'orange' },
      { title: '携帯機・スマホ・コントローラー', body: 'Steam Deck を自動認識し、対応するボタン配置と大きめの UI を初期設定にします。Android では、現在の画面で使うタッチボタンだけを「決定」「早送り」「次の味方」などの機能名付きで表示します。', accent: 'cyan' },
      { title: '追加の HD アートパック', body: 'HD アートパックは別配布で、パソコンと Android で共通、Switch には専用のものがあります。未導入時はオリジナルのドット絵で表示され、導入後はゲーム中いつでも切り替えられます。', accent: 'gold' },
    ],
    platformsTitle: '対応プラットフォーム',
    platforms: [
      { name: 'Windows', note: '64 ビット版 Windows', experimental: true },
      { name: 'macOS', note: 'Apple シリコン、macOS 14 以降' },
      { name: 'Linux', note: 'x86-64、glibc 2.35 以降、Vulkan が必要' },
      { name: 'Steam Deck', note: 'スクリプトで Steam に追加し、ゲームモードで起動', experimental: true },
      { name: 'Android', note: 'arm64、Android 9 以降、タッチ操作', experimental: true },
      { name: 'Nintendo Switch', note: 'Atmosphère（カスタムファームウェア）、hbmenu から起動', experimental: true },
    ],
    experimental: '試験的',
    communityTitle: 'シナリオ・図鑑・攻略',
    communityBody: '全シナリオを話数順に収録し、日本語原文と英語・中国語訳を一行ずつ読み比べられます。図鑑ではキャラクターやユニットのデータ、攻略では分岐や隠し要素を確認できます。',
    communityStory: 'シナリオを読む',
    communityLibrary: '図鑑を見る',
    communityGuide: '攻略を見る',
    newsTitle: '最新情報',
  },
  ko: {
    kicker: '비공식 · 네이티브 리컴파일',
    titleParts: ['슈퍼로봇', '대전64'],
    title: '슈퍼로봇대전64',
    tagline: '1999년 N64 게임의 네이티브 리컴파일 버전.',
    intro: 'Marchwind 64는 『슈퍼로봇대전64』의 비공식 네이티브 리컴파일 프로젝트입니다.',
    lead: 'Windows, macOS, Linux, Steam Deck, Android, Nintendo Switch에서 네이티브로 실행되며, 한국어, 영어, 중국어(간체, 번체) 전문 번역과 와이드 화면을 지원합니다. 별도의 HD 팩도 선택해서 쓸 수 있습니다.',
    rom: '일본판 ROM을 직접 준비해야 합니다',
    romMore: 'ROM 안내',
    allPlatforms: '다른 플랫폼과 설치 방법',
    optional: '수정 사항과 화면 방식은 설정에서 항목별로 고를 수 있습니다',
    strip: [
      { title: '네이티브 실행', body: '설치하고 ROM을 추가하면 바로 플레이', accent: 'cyan' },
      { title: '한 · 영 · 중 · 일', body: '전문 번역 지원, 게임 중 전환 가능', accent: 'gold' },
      { title: 'HD와 와이드 화면', body: '원본과 HD 그래픽을 언제든 전환', accent: 'pink' },
      { title: '원작 버그 수정', body: '기본으로 켜져 있으며 항목별로 전환 가능', accent: 'green' },
    ],
    compareTitle: '원본과 HD 그래픽',
    compareBody: '왼쪽은 원본 4:3 화면으로, 얼굴 그래픽과 맵이 N64 해상도입니다. 오른쪽은 HD 팩을 적용한 Marchwind 64로, 새로 그린 얼굴 그래픽과 월드맵을 16:9로 보여 줍니다. 게임 중 언제든 원본과 HD 그래픽을 전환할 수 있으며, 세이브는 공통입니다.',
    original: '원본 · 4:3',
    ours: 'MARCHWIND 64 · HD 와이드',
    showTitle: '화면과 조작 개선',
    showBody: 'N64 원작을 바탕으로 화면을 새로 만들고 전문 번역과 조작 개선을 더했으며, 버그 수정은 항목별로 고를 수 있습니다. 아래 이미지는 모두 게임 내 스크린샷입니다.',
    shows: [
      { key: 'prebattle', tag: 'BATTLE', img: '/media/prebattle-{lang}.webp', img2: '/media/move-jump.webp', title: '최근 슈퍼로봇대전 스타일의 화면과 조작', body: '전투 준비 화면을 최근 슈퍼로봇대전 시리즈 스타일로 새로 만들었습니다. 양측의 피해량, 명중률, 크리티컬 확률을 실드 방어와 배리어 효과와 함께 나란히 보여 주고, 정신기, 무기 선택, 전투 애니메이션 전환은 아래에 모았습니다. 전술맵에도 최근 작품에 있는 편리한 조작을 더했습니다.', points: ['이동할 곳을 고를 때 이동 범위의 가장 먼 칸으로 바로 옮길 수 있습니다', 'L1/R1로 아군, L2/R2로 적 기체를 차례로 확인할 수 있습니다', '전투 애니메이션은 언제든 중단할 수 있으며, 전투 결과는 정상적으로 계산됩니다', '설정에서 원본 화면도 고를 수 있습니다'] },
      { key: 'history', tag: 'DIALOGUE', img: '/media/auto-{lang}.webp', img2: '/media/history-{lang}.webp', title: '대사 읽기', body: '모든 대사를 고해상도 글꼴로 다시 조판했습니다. 대사는 끊김 없이 이어서 배치하고 글자 크기에 맞춰 페이지를 나누므로, 문장 중간에서 페이지가 끊기지 않습니다. 지난 대사는 언제든 다시 볼 수 있고, 자동 진행, 빨리 넘기기, 건너뛰기에는 각각 전용 조작이 있으며, 현재 읽기 모드와 조작 방법이 화면 아래에 표시됩니다.', points: ['최근 256줄의 대사를 화자별 색으로 구분해 다시 볼 수 있습니다', '자동 진행 속도는 4단계, 글자 크기는 10~18입니다', '빨리 넘기기와 건너뛰기는 최근 슈퍼로봇대전과 같은 방식입니다. 누르고 있는 동안 빨리 넘기고 떼면 멈춥니다. 건너뛰기는 다음 선택지나 전투까지 장면 전체를 넘기며, 결과는 모든 대사를 읽었을 때와 같습니다', '언어 전환과 일본어 원문 확인은 언제든 할 수 있으며, 표시 중인 대사도 바로 다시 조판됩니다', '대사는 직접 고칠 수 있는 일반 텍스트 파일로 저장되어 있습니다'] },
      { key: 'settings', tag: 'OPTIONS', img: '/media/settings-{lang}.webp', title: '게임 내 설정', body: '플레이 중에 언어, 원본/HD 그래픽, 화면 비율, 전체 화면, 인터페이스 크기를 바꿀 수 있습니다. 규칙 수정을 항목별로 켜고 끄거나 키보드와 게임패드 조작을 다시 배정할 수도 있습니다.', points: ['게임 중 언제든, 또는 타이틀 화면 오른쪽 아래 버튼으로 열 수 있습니다', '인터페이스 크기는 보통, 크게, 가장 크게의 3단계입니다'] },
      { key: 'filters', tag: 'FILTERS', img: '/media/filters-tv-crt.webp', wide: true, title: 'RetroArch 필터와 테두리', body: 'RetroArch의 slang 셰이더 프리셋(.slangp)을 그대로 사용해 CRT 주사선, NTSC 색감 등을 더할 수 있습니다. 4:3 화면에서는 RetroArch 테두리(베젤)도 씌울 수 있으며, 테두리의 투명한 창이 알아서 게임 화면에 맞춰집니다. 두 기능 모두 옵션 → 일반에 있습니다.', points: ['crt-lottes, crt-geom, zfast-crt(휴대용 기기용), ntsc-adaptive, xbrz-freescale 등 자주 쓰는 프리셋 9종을 기본 제공합니다', '설치된 RetroArch의 slang 셰이더를 모두 찾아 주며, 직접 준비한 프리셋과 테두리는 데이터 폴더에 넣으면 됩니다', '원본 240라인(CRT에 가장 가까움), 480라인, 960라인 또는 창 해상도에서 처리합니다', '필터는 게임 화면과 대사에만 적용되고, 설정 창, 알림, 테두리는 선명하게 유지됩니다', '필터는 Windows, macOS, Linux/Steam Deck, Android에서 동작하며, Switch는 아직 지원하지 않습니다'], credit: '테두리: RetroArch 자체 오버레이의 tv-integer(libretro/common-overlays, CC BY 4.0). 필터: crt-lottes, 480라인. 게임에는 테두리가 포함되어 있지 않습니다.' },
      { key: 'library', tag: 'LIBRARY', img: '/media/library-{lang}.webp', title: '도감: 기체 353종, 인물 293명', body: '작품별로 기체의 능력치, 지형 적응, 무기와 풀개조 시 위력, 개조 상한, 파일럿의 성장, 정신기, 특수 기능을 정리했습니다. 모든 항목은 게임 데이터에서 직접 가져왔습니다.', points: ['타이틀 화면에서도, 플레이 중에도 열 수 있습니다', '같은 도감을 이 웹사이트에서도 볼 수 있습니다'] },
      { key: 'viewer', tag: 'VIEWER', img: '/media/viewer-{lang}.webp', title: '전투 뷰어', body: '공격 측과 방어 측의 기체와 파일럿을 고르고, 무기, 방어 측의 대응, 피해량, 배경을 정한 뒤 전투 애니메이션을 처음부터 끝까지 재생합니다.', points: ['모든 기체의 무기 연출을 볼 수 있습니다', '원작처럼 공격 측의 테마곡이 재생됩니다'] },
      { key: 'link', tag: 'LINK BATTLER', img: '/media/link-{lang}.webp', title: '게임보이 카트리지 없는 링크 배틀러 연동', body: '인터미션 메뉴에서 링크를 고르고 작품을 체크하면 F91, 고쇼군, 점보트 3가 원작의 링크 스테이지를 통해 합류합니다. 64GB 팩이나 게임보이 카트리지는 필요 없습니다.', points: ['처음부터 끝까지 동작을 확인했습니다', '세 작품을 자유롭게 조합할 수 있습니다'] },
      { key: 'mods', tag: 'MODS', img: '/media/mods-{lang}.webp', title: '모드: 음악, 수치, 이름, 아이콘', body: '.srw64mod 파일을 게임 창에 끌어다 놓으면 설치됩니다. Android에서는 “파일” 앱의 Marchwind64 폴더 안 mods/에 넣습니다. 패키지에는 코드 없이 데이터만 들어 있고 세이브에는 게임 본래의 값이 남으므로, 언제든 모드를 끄거나 지울 수 있습니다.', points: ['음악: 49곡 중 원하는 곡을 Ogg 파일로 바꾸고, 캐릭터나 기체별 전투 음악도 지정할 수 있습니다', '수치: 기체, 파일럿, 무기의 능력치, 지형 적응, 특수 기능, 정신기', '이름과 얼굴 그래픽: 주인공, 파트너, 부대의 이름, 모든 캐릭터, 기체, 무기의 이름과 캐릭터 얼굴 그래픽', '맵 아이콘과 전투 음성: 그림 한 장으로 기체의 맵 아이콘을 바꾸고, 전투 대사가 나올 때 음성을 재생합니다', '웹사이트의 제작 도구 5종으로 모든 작업을 브라우저 안에서 끝낼 수 있으며, 아무것도 업로드되지 않습니다'] },
      { key: 'speed', tag: 'SPEED', img: '/media/speed-{lang}.webp', img2: '/media/speedset-{lang}.webp', title: '최대 4배속', body: '속도 키로 보통, 2배속, 3배속, 4배속을 차례로 바꿉니다. 키보드는 Tab, Steam Deck은 뒷면의 R4, 스마트폰은 배속 버튼을 쓰며, 옵션 → 일반에서 직접 고를 수도 있습니다. 맵 위의 스토리 장면, 적 페이즈, 기체 이동, 전투 애니메이션, 월드맵이 빨라지고, 아군 메뉴, 커서, 인터미션은 원래 속도를 유지하며 음악의 음높이도 바뀌지 않습니다.', points: ['3배속과 4배속에는 초당 90, 120프레임이 필요합니다. 따라가지 못하는 기기에서는 한 프레임씩 건너뛰어 그리지만, 게임 처리는 매 프레임 진행됩니다', '노래방 모드, 타이틀 데모, 전투 뷰어는 항상 보통 속도로 실행됩니다'] },
    ],
    rulesTitle: '버그 수정과 선택 규칙',
    rulesBody: '각 규칙은 따로 켜고 끌 수 있고 바로 적용되며, 세이브 형식은 같습니다. “전부 끄기”를 고르면 원작 규칙으로 플레이할 수 있습니다.',
    rules: [
      { title: '원작 버그 수정', badge: '기본 켜짐', note: '원작의 계산 오류와 누락을 바로잡습니다', accent: 'green', items: ['초능력: 명중과 회피에 실제 기능 레벨을 반영합니다(원작은 항상 64로 계산)', '성전사: 회피에 실제 레벨을 반영합니다(원작은 항상 32로 계산)', '한계 반응: 명중과 회피에 각각 운동성을 더한 값을 기체의 한계 수치로 제한합니다', '저력: HP 구간을 바로잡았으며, HP가 가득 차 있으면 보정이 없습니다', '저력: 명중과 회피 보정을 절반으로 줄입니다(크리티컬은 그대로)', '기체 교체: 표에서 빠져 있던 무기 3종도 개조 단계를 계승합니다', '성전사: 하이퍼 오라 베기의 위력이 레벨에 따라 올라갑니다'] },
      { title: '편의성과 난이도', badge: '기본 꺼짐', note: '개조, 자금, 전투 난이도를 원하는 대로 조정합니다', accent: 'orange', items: ['개조 제한 완화: 모든 기체를 15단계까지 개조할 수 있습니다', '이탈 환급: 스토리에서 기체가 이탈하면 개조 자금을 돌려받습니다', '파츠 계승: 강화파츠가 파일럿과 함께 새 기체로 옮겨집니다', '보스 더미: 횟수를 절반으로 줄이거나 없앱니다', '회차 계승: 이전 회차에서 달성한 숨겨진 요소는 원래 합류하는 스테이지 직후에 바로 합류합니다'] },
      { title: '화면과 인터페이스', badge: '새 UI / 원본', note: '화면마다 새 UI와 원본 중에서 고를 수 있습니다', accent: 'cyan', items: ['전투 UI: 새 UI, HD 원본, 원본 중에서 선택', '인터미션, 주인공 선택, 타이틀 메뉴: 새 UI 또는 원본', '그래픽: HD 또는 원본, 언제든 전환', '화면 비율: 화면에 맞춤 또는 4:3 유지', '무기 개조: 일괄(기체의 모든 무기가 함께 오르며 총비용은 같음) 또는 개별(원본)', '전투 애니메이션과 자동 저장: 각각 켜고 끌 수 있음'] },
    ],
    galleryTitle: 'HD 전투 화면',
    galleryBody: '기체, 컷인, 얼굴 그래픽, 배경에 고해상도 그래픽을 쓰면서, 연출 타이밍과 카메라 워크는 원작 그대로입니다.',
    moreTitle: '그 밖의 기능',
    more: [
      { title: '네이티브 실행', body: 'N64 프로그램을 네이티브 코드로 리컴파일하고, 그래픽은 RT64로 렌더링합니다. 설치한 뒤 안내에 따라 ROM을 추가하면 바로 플레이할 수 있습니다.', accent: 'cyan' },
      { title: '5개 언어 전문 지원', body: '일본어 원문과 함께 한국어, 영어, 간체 중국어, 번체 중국어 전문 번역을 수록했습니다. 한국어 번역은 커뮤니티 번역가 테오(jung5441)의 작업이며, CC BY 4.0으로 제공됩니다. 캐릭터, 기체, 무기 이름은 각 언어에서 널리 쓰이는 표기를 따르며, 번체 중국어는 대만 표기를 사용합니다.', accent: 'gold' },
      { title: '세이브 슬롯 추가와 자동 저장', body: '원작보다 많은 세이브 슬롯을 제공하고, 주요 시점마다 자동 저장합니다. 카트리지 세이브는 ares, Project64, RetroArch 등 에뮬레이터에서 쓸 수 있도록 내보낼 수 있습니다.', accent: 'pink' },
      { title: '늘이지 않는 와이드 화면', body: '4:3부터 16:9까지의 화면 비율을 지원합니다. 원래 화면은 가운데에 두고 좌우로 배경을 넓혀, 이미지를 늘이지 않고 화면을 채웁니다.', accent: 'orange' },
      { title: '휴대용 기기, 스마트폰, 게임패드', body: 'Steam Deck을 자동으로 인식해 Deck에 맞는 버튼 배치와 큰 인터페이스를 기본으로 씁니다. Android에서는 현재 화면에 필요한 터치 버튼만 “확인”, “빨리 넘기기”, “다음 아군”처럼 기능 이름을 붙여 표시합니다.', accent: 'cyan' },
      { title: 'HD 팩(선택)', body: 'HD 팩은 따로 내려받으며, 컴퓨터와 Android용 하나와 Switch 전용 팩이 있습니다. 설치하지 않으면 원본 도트 그래픽으로 표시되고, 설치한 뒤에는 게임 중 언제든 둘을 전환할 수 있습니다.', accent: 'gold' },
    ],
    platformsTitle: '지원 플랫폼',
    platforms: [
      { name: 'Windows', note: '64비트 Windows', experimental: true },
      { name: 'macOS', note: 'Apple 실리콘, macOS 14 이상' },
      { name: 'Linux', note: 'x86-64, glibc 2.35 이상, Vulkan' },
      { name: 'Steam Deck', note: '스크립트로 Steam에 추가한 뒤 게임 모드에서 실행', experimental: true },
      { name: 'Android', note: 'arm64, Android 9 이상, 터치 조작', experimental: true },
      { name: 'Nintendo Switch', note: 'Atmosphère 커스텀 펌웨어, hbmenu에서 실행', experimental: true },
    ],
    experimental: '실험적',
    communityTitle: '시나리오, 도감, 공략',
    communityBody: '전체 시나리오를 화수 순으로 수록해, 일본어 원문과 한국어, 영어 번역을 한 줄씩 비교하며 읽을 수 있습니다. 도감에서는 캐릭터와 기체 데이터를, 공략에서는 분기와 숨겨진 요소를 확인할 수 있습니다.',
    communityStory: '시나리오 읽기',
    communityLibrary: '도감 보기',
    communityGuide: '공략 보기',
    newsTitle: '최신 소식',
  },
};
