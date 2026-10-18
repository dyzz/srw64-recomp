// The Android compatibility list (/<lang>/android/): every chip we have run on a real phone
// (mine, AWS Device Farm or a player's), its state in the current release, and what is not
// known yet. Update it with each release that changes anything for Android; `asOf` is the
// release the list describes and `updated` the day it was last checked.
import type { Lang } from '../i18n';

type L = Record<Lang, string>;
export type Status = 'ok' | 'slow' | 'issue';
// How far a phone got: the title screen and the demo battle, into stage 1, or real play
// (all scenes on our own phone, or a player playing through).
export type Depth = 'title' | 'stage' | 'play';

export interface Chip {
  vendor: 'qualcomm' | 'mediatek' | 'samsung' | 'google';
  chip: L;
  gpu: string;
  // Phones the reader may own with this chip, to find their row; empty when we only know ours.
  phones: L;
  tested: L;
  depth: Depth;
  status: Status;
  fps?: number;
  note?: L;
}

export const asOf = '0.4.8';
export const updated = '2026-10-18';

const same = (s: string): L => ({ zh: s, en: s, ja: s });
const snapdragon = (n: string): L => ({ zh: `骁龙 ${n}`, en: `Snapdragon ${n}`, ja: `Snapdragon ${n}` });
const dimensity = (n: string): L => ({ zh: `天玑 ${n}`, en: `Dimensity ${n}`, ja: `Dimensity ${n}` });

export const CHIPS: Chip[] = [
  {
    vendor: 'qualcomm', chip: { zh: '骁龙 8 至尊版', en: 'Snapdragon 8 Elite', ja: 'Snapdragon 8 Elite' }, gpu: 'Adreno 830',
    phones: { zh: '小米 15、一加 13、iQOO 13', en: 'Xiaomi 15, OnePlus 13, iQOO 13', ja: 'Xiaomi 15、OnePlus 13、iQOO 13' },
    tested: { zh: 'Galaxy S25（安卓 16）', en: 'Galaxy S25 (Android 16)', ja: 'Galaxy S25（Android 16）' },
    depth: 'title', status: 'issue', fps: 30,
    note: {
      zh: '安卓 16 能启动。安卓 17 有玩家反馈在地图上打开设置后画面闪烁，原因还在查。',
      en: 'Starts on Android 16. A player on Android 17 reports the screen flickering after opening the settings on the map; still being looked into.',
      ja: 'Android 16 では起動します。Android 17 でマップ上で設定を開くと画面がちらつくという報告があり、調査中です。',
    },
  },
  {
    vendor: 'qualcomm', chip: snapdragon('8 Gen 3'), gpu: 'Adreno 750',
    phones: { zh: '小米 14、iQOO 12、红米 K70 Pro', en: 'Xiaomi 14, iQOO 12, Redmi K70 Pro', ja: 'Xiaomi 14、iQOO 12、Redmi K70 Pro' },
    tested: { zh: 'Galaxy S24 Ultra（安卓 14）', en: 'Galaxy S24 Ultra (Android 14)', ja: 'Galaxy S24 Ultra（Android 14）' },
    depth: 'title', status: 'ok', fps: 30,
    note: { zh: '0.4.0 闪退，0.4.1 起修好。', en: 'Crashed in 0.4.0; fixed since 0.4.1.', ja: '0.4.0 で強制終了、0.4.1 で修正。' },
  },
  {
    vendor: 'qualcomm', chip: snapdragon('8 Gen 2'), gpu: 'Adreno 740',
    phones: { zh: '红米 K60 Pro、红米 K70、iQOO Neo9', en: 'Redmi K60 Pro, Redmi K70, iQOO Neo9', ja: 'Redmi K60 Pro、Redmi K70、iQOO Neo9' },
    tested: { zh: '小米 13（安卓 13）、Galaxy S23（安卓 14）', en: 'Xiaomi 13 (Android 13), Galaxy S23 (Android 14)', ja: 'Xiaomi 13（Android 13）、Galaxy S23（Android 14）' },
    depth: 'title', status: 'ok', fps: 30,
    note: { zh: '0.4.0 闪退，0.4.1 起修好。', en: 'Crashed in 0.4.0; fixed since 0.4.1.', ja: '0.4.0 で強制終了、0.4.1 で修正。' },
  },
  {
    vendor: 'qualcomm', chip: snapdragon('8+ Gen 1'), gpu: 'Adreno 730',
    phones: { zh: '红米 K50 至尊版、小米 12S', en: 'Redmi K50 Ultra, Xiaomi 12S', ja: 'Redmi K50 Ultra、Xiaomi 12S' },
    tested: { zh: '红米 K60（安卓 13，玩家）', en: 'Redmi K60 (Android 13, a player)', ja: 'Redmi K60（Android 13、プレイヤー）' },
    depth: 'play', status: 'ok', fps: 30,
    note: { zh: '0.4.7 及以前黑屏，0.4.8 修好，玩家已确认。', en: 'A black screen up to 0.4.7; fixed in 0.4.8, confirmed by the player.', ja: '0.4.7 までは画面が真っ暗、0.4.8 で修正し、プレイヤーが確認済み。' },
  },
  {
    vendor: 'qualcomm', chip: snapdragon('8 Gen 1'), gpu: 'Adreno 730',
    phones: same(''),
    tested: { zh: '小米 12（安卓 12）、Galaxy S22（安卓 13）', en: 'Xiaomi 12 (Android 12), Galaxy S22 (Android 13)', ja: 'Xiaomi 12（Android 12）、Galaxy S22（Android 13）' },
    depth: 'title', status: 'ok', fps: 30,
  },
  {
    vendor: 'qualcomm', chip: snapdragon('888'), gpu: 'Adreno 660',
    phones: { zh: '小米 11、红米 K40 Pro', en: 'Xiaomi 11, Redmi K40 Pro', ja: 'Xiaomi 11、Redmi K40 Pro' },
    tested: { zh: 'Galaxy S21 Ultra（安卓 12）', en: 'Galaxy S21 Ultra (Android 12)', ja: 'Galaxy S21 Ultra（Android 12）' },
    depth: 'title', status: 'ok', fps: 30,
    note: { zh: '启动闪退，0.4.3 起修好。只测了一台，没进过正式游戏。', en: 'Crashed at start; fixed since 0.4.3. Only one phone tested, not into the game proper.', ja: '起動時に強制終了、0.4.3 で修正。確認は 1 台のみで、本編までは進めていません。' },
  },
  {
    vendor: 'qualcomm', chip: snapdragon('865 / 870'), gpu: 'Adreno 650',
    phones: { zh: '红米 K40、K40S、POCO F3', en: 'Redmi K40, K40S, POCO F3', ja: 'Redmi K40、K40S、POCO F3' },
    tested: { zh: 'Galaxy Tab S7（安卓 11）、POCO F3（玩家）', en: 'Galaxy Tab S7 (Android 11), POCO F3 (a player)', ja: 'Galaxy Tab S7（Android 11）、POCO F3（プレイヤー）' },
    depth: 'play', status: 'ok', fps: 30,
    note: { zh: '闪退，0.4.3 起修好。少数读回画面的特效可能不准。', en: 'Crashed; fixed since 0.4.3. A few effects that read the screen back may be off.', ja: '強制終了、0.4.3 で修正。画面を読み戻す一部の演出は正確でない場合があります。' },
  },
  {
    vendor: 'qualcomm', chip: snapdragon('855 / 860'), gpu: 'Adreno 640',
    phones: { zh: '小米 9、红米 K20 Pro、Poco X3 Pro、一加 7', en: 'Xiaomi Mi 9, Redmi K20 Pro, Poco X3 Pro, OnePlus 7', ja: 'Xiaomi Mi 9、Redmi K20 Pro、Poco X3 Pro、OnePlus 7' },
    tested: { zh: 'Pixel 4（安卓 11）、Poco X3 Pro（安卓 12，玩家）', en: 'Pixel 4 (Android 11), Poco X3 Pro (Android 12, a player)', ja: 'Pixel 4（Android 11）、Poco X3 Pro（Android 12、プレイヤー）' },
    depth: 'title', status: 'issue', fps: 30,
    note: {
      zh: '0.4.8 启动即闪退，下一版修好，玩家已用测试版确认。安卓 9、10 的旧驱动编译画面时会崩溃，下一版起会提示升级到安卓 11 以上。',
      en: 'Crashes at start in 0.4.8; fixed in the next release, confirmed by a player on a test build. On Android 9 and 10 the old driver crashes building the graphics; from the next release the game asks to update to Android 11 or later.',
      ja: '0.4.8 では起動直後に強制終了します。次のバージョンで修正し、プレイヤーがテスト版で確認済みです。Android 9・10 の古いドライバーは描画の準備中に強制終了するため、次のバージョンからは Android 11 以降へのアップデートを案内します。',
    },
  },
  {
    vendor: 'qualcomm', chip: snapdragon('778G'), gpu: 'Adreno 642L',
    phones: { zh: '小米 Civi、荣耀 50、Galaxy A52s', en: 'Xiaomi Civi, Honor 50, Galaxy A52s', ja: 'Xiaomi Civi、Honor 50、Galaxy A52s' },
    tested: { zh: 'Galaxy A73（安卓 12）', en: 'Galaxy A73 (Android 12)', ja: 'Galaxy A73（Android 12）' },
    depth: 'title', status: 'issue', fps: 18,
    note: {
      zh: '0.4.8 启动即闪退，下一版修好，但只有 14–22 帧。',
      en: 'Crashes at start in 0.4.8; fixed in the next release, but runs at only 14–22 FPS.',
      ja: '0.4.8 では起動直後に強制終了します。次のバージョンで修正しますが、14〜22 FPS しか出ません。',
    },
  },
  {
    vendor: 'qualcomm', chip: snapdragon('765G'), gpu: 'Adreno 620',
    phones: { zh: '红米 K30 5G、一加 Nord', en: 'Redmi K30 5G, OnePlus Nord', ja: 'Redmi K30 5G、OnePlus Nord' },
    tested: { zh: 'Pixel 5（安卓 12）', en: 'Pixel 5 (Android 12)', ja: 'Pixel 5（Android 12）' },
    depth: 'title', status: 'issue', fps: 30,
    note: { zh: '0.4.8 启动即闪退，下一版修好。', en: 'Crashes at start in 0.4.8; fixed in the next release.', ja: '0.4.8 では起動直後に強制終了します。次のバージョンで修正。' },
  },
  {
    vendor: 'qualcomm', chip: snapdragon('730 / 732G'), gpu: 'Adreno 618',
    phones: { zh: '红米 K20、Poco X3 NFC、Galaxy A71', en: 'Redmi K20, Poco X3 NFC, Galaxy A71', ja: 'Redmi K20、Poco X3 NFC、Galaxy A71' },
    tested: { zh: 'Pixel 4a（安卓 11）', en: 'Pixel 4a (Android 11)', ja: 'Pixel 4a（Android 11）' },
    depth: 'title', status: 'issue', fps: 30,
    note: { zh: '0.4.8 启动即闪退，下一版修好。', en: 'Crashes at start in 0.4.8; fixed in the next release.', ja: '0.4.8 では起動直後に強制終了します。次のバージョンで修正。' },
  },
  {
    vendor: 'qualcomm', chip: snapdragon('845 / 670'), gpu: 'Adreno 630 / 615',
    phones: { zh: '小米 8、Poco F1、一加 6', en: 'Xiaomi Mi 8, Poco F1, OnePlus 6', ja: 'Xiaomi Mi 8、Poco F1、OnePlus 6' },
    tested: { zh: 'Pixel 3、Pixel 3a（安卓 10）', en: 'Pixel 3, Pixel 3a (Android 10)', ja: 'Pixel 3、Pixel 3a（Android 10）' },
    depth: 'title', status: 'issue',
    note: {
      zh: '安卓 10 的驱动在编译画面时崩溃，没法修；下一版起会提示升级到安卓 11 以上。升级过系统的这代手机没测过。',
      en: 'Android 10’s driver crashes building the graphics, which cannot be worked around; from the next release the game asks to update to Android 11 or later. Phones of this generation on newer Android are untested.',
      ja: 'Android 10 のドライバーは描画の準備中に強制終了し、回避できません。次のバージョンからは Android 11 以降へのアップデートを案内します。アップデート済みの同世代の端末は未確認です。',
    },
  },
  {
    vendor: 'mediatek', chip: dimensity('7300'), gpu: 'Mali-G615',
    phones: same(''),
    tested: { zh: 'Solana Seeker（安卓 16）', en: 'Solana Seeker (Android 16)', ja: 'Solana Seeker（Android 16）' },
    depth: 'play', status: 'ok', fps: 30,
    note: { zh: '我的主力测试机，全部场景都跑过。', en: 'My main test phone; every scene has been run on it.', ja: 'メインのテスト端末で、すべての場面を確認しています。' },
  },
  {
    vendor: 'mediatek', chip: dimensity('7200-Ultra'), gpu: 'Mali-G610',
    phones: same(''),
    tested: { zh: '红米 Note 13 Pro+（安卓 15）', en: 'Redmi Note 13 Pro+ (Android 15)', ja: 'Redmi Note 13 Pro+（Android 15）' },
    depth: 'stage', status: 'ok', fps: 30,
  },
  {
    vendor: 'mediatek', chip: dimensity('1080'), gpu: 'Mali-G68',
    phones: same(''),
    tested: { zh: 'Galaxy A34（安卓 13）', en: 'Galaxy A34 (Android 13)', ja: 'Galaxy A34（Android 13）' },
    depth: 'stage', status: 'ok', fps: 30,
  },
  {
    vendor: 'mediatek', chip: dimensity('6100+'), gpu: 'Mali-G57',
    phones: same(''),
    tested: { zh: 'Galaxy A15（安卓 14）', en: 'Galaxy A15 (Android 14)', ja: 'Galaxy A15（Android 14）' },
    depth: 'title', status: 'slow', fps: 14,
    note: { zh: '入门芯片，帧数不足。', en: 'An entry-level chip; too slow for full speed.', ja: 'エントリー向けのチップで、速度が足りません。' },
  },
  {
    vendor: 'mediatek', chip: dimensity('700'), gpu: 'Mali-G57',
    phones: same(''),
    tested: { zh: '红米 Note 10 5G（安卓 11）', en: 'Redmi Note 10 5G (Android 11)', ja: 'Redmi Note 10 5G（Android 11）' },
    depth: 'title', status: 'slow', fps: 10,
    note: { zh: '能运行，但帧数很低。', en: 'Runs, but very slowly.', ja: '動作はしますが、かなり遅いです。' },
  },
  {
    vendor: 'samsung', chip: same('Exynos 1380'), gpu: 'Mali-G68',
    phones: same(''),
    tested: { zh: 'Galaxy A35（安卓 14）', en: 'Galaxy A35 (Android 14)', ja: 'Galaxy A35（Android 14）' },
    depth: 'title', status: 'ok', fps: 30,
    note: { zh: '0.4.2 及以前启动即退，0.4.3 起修好。', en: 'Quit at start up to 0.4.2; fixed since 0.4.3.', ja: '0.4.2 までは起動直後に終了、0.4.3 で修正。' },
  },
  {
    vendor: 'google', chip: same('Tensor G3'), gpu: 'Mali-G715',
    phones: same(''),
    tested: { zh: 'Pixel 8（安卓 14）', en: 'Pixel 8 (Android 14)', ja: 'Pixel 8（Android 14）' },
    depth: 'title', status: 'ok', fps: 30,
  },
  {
    vendor: 'google', chip: same('Tensor G2'), gpu: 'Mali-G710',
    phones: same(''),
    tested: { zh: 'Pixel 7（安卓 14）', en: 'Pixel 7 (Android 14)', ja: 'Pixel 7（Android 14）' },
    depth: 'title', status: 'ok', fps: 30,
  },
];

// Chips a player reported trouble on that we could not confirm.
export const UNCONFIRMED: L[] = [
  {
    zh: '天玑 8100 / 900 / 8200：有玩家反馈启动或进画面时闪退，但版本和闪退位置都没确认。0.4.3 修好了同一代 Mali 驱动的启动问题，同款 GPU 的天玑 7200 已能进第一关。',
    en: 'Dimensity 8100 / 900 / 8200: players reported crashes at start or on entering the game, without the version or where it crashed. 0.4.3 fixed the start-up problem of that generation of Mali drivers, and the Dimensity 7200, with the same GPU, gets into stage 1.',
    ja: 'Dimensity 8100 / 900 / 8200：起動時や画面に入るときに強制終了するという報告がありますが、バージョンや終了した場所は未確認です。同世代の Mali ドライバーの起動問題は 0.4.3 で修正済みで、同じ GPU の Dimensity 7200 は第 1 話まで進めます。',
  },
];

export const KNOWN_ISSUES: L[] = [
  {
    zh: '开 HD 时，「流星が落ちた日」敌方攻击第三方会闪退，关掉 HD 不会。电脑上同样的战斗测了几十场没有复现，不一定只在安卓上出现，在等问题报告。',
    en: 'With HD on, “流星が落ちた日” crashes when an enemy attacks the third party; with HD off it does not. Dozens of the same battles on a computer did not crash, so it may not be Android-only; waiting for a report.',
    ja: 'HD をオンにすると、「流星が落ちた日」で敵が第三勢力を攻撃したときに強制終了します（HD オフでは発生しません）。パソコンで同じ戦闘を数十回試しても再現せず、Android 限定とは限りません。問題レポートを待っています。',
  },
  {
    zh: '手机里没有系统文件选择器时，第一次启动选 ROM 会闪退。一般手机都有文件选择器。',
    en: 'A phone without the system file picker crashes when choosing the ROM on first start. Ordinary phones have one.',
    ja: 'システムのファイル選択画面がない端末では、初回起動時の ROM 選択で強制終了します。通常のスマホには備わっています。',
  },
];

export const UNTESTED: { label: L; items: L }[] = [
  {
    label: { zh: 'GPU', en: 'GPUs', ja: 'GPU' },
    items: {
      zh: '天玑 9200/9300 的 Immortalis（红米 K60 至尊版、K70 至尊版、iQOO Neo9 Pro）、天玑 1200 的 Mali-G77、骁龙 8 至尊版第五代、三星 Exynos 的 Xclipse、Pixel 10 的 PowerVR、骁龙 7 Gen 系列和 6/4 系的 Adreno',
      en: 'Immortalis on the Dimensity 9200/9300 (Redmi K60 Ultra, K70 Ultra, iQOO Neo9 Pro), Mali-G77 on the Dimensity 1200, Snapdragon 8 Elite Gen 5, Samsung’s Xclipse, PowerVR on the Pixel 10, Adreno on the Snapdragon 7 Gen series and the 6 and 4 series',
      ja: 'Dimensity 9200/9300 の Immortalis（Redmi K60 Ultra、K70 Ultra、iQOO Neo9 Pro）、Dimensity 1200 の Mali-G77、Snapdragon 8 Elite Gen 5、Samsung Exynos の Xclipse、Pixel 10 の PowerVR、Snapdragon 7 Gen シリーズと 6/4 シリーズの Adreno',
    },
  },
  {
    label: { zh: '系统', en: 'Systems', ja: 'OS' },
    items: {
      zh: '安卓 17 的高通机型、安卓 16 的骁龙 8 Gen 2/Gen 3、安卓 12 的联发科机型；ColorOS、OriginOS、MagicOS',
      en: 'Qualcomm phones on Android 17, the Snapdragon 8 Gen 2/Gen 3 on Android 16, MediaTek phones on Android 12; ColorOS, OriginOS, MagicOS',
      ja: 'Android 17 の Qualcomm 端末、Android 16 の Snapdragon 8 Gen 2/Gen 3、Android 12 の MediaTek 端末、ColorOS・OriginOS・MagicOS',
    },
  },
];
