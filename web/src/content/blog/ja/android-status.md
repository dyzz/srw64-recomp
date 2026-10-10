---
title: Android 端末の動作確認状況
date: 2026-10-08
summary: Android 端末の実機確認、プレイヤーからの報告と現状を GPU 別にまとめ、既知の問題と未確認の端末も載せました。Snapdragon 865/870/888、8 Gen 2/Gen 3 と、ドライバーの古い Mali 端末は修正済みです。Redmi K60 の黒画面と、Snapdragon 8 Elite（Android 17）で設定を開いたときのちらつきは調査中です。
---

*2026-10-10 更新：最新版を 0.4.4 に改め、MediaTek 端末で第 1 話まで進めた確認結果、新しい報告、既知の問題、未確認の端末を追加しました。*

0.4.0 の公開以降、Android 端末で起動しない、落ちる、画面が黒いままといった報告を多くいただきました。ここでは GPU ごとに、こちらで実際に確認した端末、プレイヤーからの報告、現在の状況をまとめます。お使いの端末と照らし合わせてください。最新版の 0.4.4 には以下の修正がすべて含まれています。⚠️ はまだ確認できていない点です。

端末のプロセッサーと GPU は「設定 → 端末情報」（Xiaomi の端末は「設定 → マイデバイス → すべての仕様」）で確認できます。問題があれば、ゲーム内の「オプション → フィードバック」から問題レポートを書き出し、端末名、プロセッサー、ゲームのバージョンを添えて [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues) にお寄せください。0.4.4 からレポートは「内部ストレージ/Download/Marchwind64/reports/」に保存され、権限は要りません。直近 3 回分のログが入るので、落ちたり固まったりしたあとは、ゲームを開き直してから書き出してください。

## Adreno 740（Snapdragon 8 Gen 2：Xiaomi 13、Redmi K60 Pro、vivo Neo9、Redmi K70）

- 状況：0.4.0 で落ちる → 0.4.1 以降で修正済み
- 実機確認：Xiaomi 13（Android 13）、Galaxy S23（Android 14）で修正前の落ちる現象を再現し、修正後は正常に動作
- プレイヤーの報告：Redmi K70、vivo Neo9（Android 16、落ちる）
  - ⚠️ 正確な機種は未確認で、MediaTek 版ならここには当たりません。返信待ち

## Adreno 750（Snapdragon 8 Gen 3：iQOO 12、Redmi K70 Pro）

- 状況：0.4.0 で落ちる → 0.4.1 以降で修正済み
- 実機確認：Galaxy S24 Ultra（Android 14）で修正前に報告どおり落ちることを再現し、修正後は正常に動作
- プレイヤーの報告：iQOO 12（Android 16、タッチボタンが出たあと落ちる）
  - ⚠️ 返信待ち。Android 16 の 750 は未確認

## Adreno 830（Snapdragon 8 Elite）

- 状況：起動は正常。⚠️ Android 17 で設定を開くとちらつくという報告があり、調査中
- 実機確認：Galaxy S25（Android 16）で正常に動作（タイトル画面まで確認）
- プレイヤーの報告：Snapdragon 8 Elite の端末（Android 17）で、マップ上で設定を開いたまましばらくすると画面がちらつき、ゲームを終了するしかなくなる
  - ⚠️ 原因は不明で、問題レポート待ち。Android 17 の Snapdragon 端末は未確認

## Adreno 730（Snapdragon 8 Gen 1 / 8+ Gen 1：Redmi K60、K50 Ultra）

- 状況：Redmi K60 の黒画面は未解決
- 実機確認：Xiaomi 12（Android 12、MIUI）、Galaxy S22（Android 13）はどちらも正常。ドライバーのバージョンはプレイヤーの端末と同じで、再現できていません
- プレイヤーの報告：Redmi K60（8+ Gen 1、Android 13、MIUI 14.0.3）、黒画面
  - テスト版を 2 回試してもらいましたが直らず、調査中です

## Adreno 650（Snapdragon 865/870：POCO F3、Redmi K40、K40S）

- 状況：落ちる → 0.4.3 以降で修正済み（650 のみ）
- 実機確認：Galaxy Tab S7（Android 11）で修正前の落ちる現象を再現し、修正後は正常に動作。3 分半の連続動作も確認
- プレイヤーの報告：
  - POCO F3：落ちる → テスト版で遊べることを確認
  - Redmi K40（HyperOS、Android 13）：⚠️ 機種は確認中
- ⚠️ この修正により、画面を読み戻す一部の演出が正確でなくなります。どこに影響するかはまだ遊んで確かめていません

## Adreno 660（Snapdragon 888：Redmi K40 Pro、Xiaomi 11）

- 状況：起動時に落ちる → 0.4.3 以降で修正済み（660 のみ）
- 実機確認：Galaxy S21 Ultra（Android 12）で修正前に起動時に落ちることを再現。修正後は 3 分半の連続動作で、デモ戦闘の画面も正常
- プレイヤーの報告：なし
- ⚠️ 確認したのは 1 台だけで、プレイヤーによる確認も、本編での確認もまだです

## Mali（新しいドライバー：Pixel 7/8 の G710/G715、Solana Seeker の G615）

- 状況：正常
- 実機確認：Pixel 7、Pixel 8（Android 14）、Seeker（Android 16）で正常に動作。Seeker は主力のテスト端末で、すべての種類の場面を 30 FPS で確認済み
- プレイヤーの報告：なし

## Mali（古いドライバー：Dimensity 8100 / 8200 / 7200 / 1080 / 900 / 6100+、Exynos 1380 など：G57/G68/G610）

- 状況：0.4.2 以前は起動直後に終了 → 0.4.3 以降で修正済み
- 実機確認：
  - Galaxy A15（G57）、A35（G68）、Android 14：0.4.2 の起動直後の終了を再現し、修正後は正常に動作。A15 はエントリー向けのチップで、14 FPS 程度
  - Redmi Note 13 Pro+（Dimensity 7200-Ultra、G610、HyperOS 2、Android 15）、Galaxy A34（Dimensity 1080、G68、Android 13）：0.4.3 でニューゲームから第 1 話のマップまで進み、30 FPS
  - Redmi Note 10 5G（Dimensity 700、G57、Android 11）：動作するものの 10 FPS 程度
- プレイヤーの報告：
  - Dimensity 8100 の端末（G610、画面に入ると落ちる）：⚠️ バージョンと落ちる場面は未確認。同じ GPU は 0.4.3 で第 1 話まで進めています
  - Dimensity 900 の端末（G68、Android 12、画面に入ると落ちる）：⚠️ 同上。Android 12 の MediaTek 端末は未確認
  - Dimensity 8200 の端末（G610、起動しない）：⚠️ 8200 そのものは未確認、返信待ち
  - Android 14 の端末（起動しない）：⚠️ 機種不明

## そのほかの既知の問題

- ⚠️ HD を有効にしていると、「流星が落ちた日」で敵が第三勢力を攻撃したときに落ちる：Android 端末のプレイヤーからの報告で、HD を切ると落ちません。パソコンで同じ戦闘を何十回も試しても再現せず、問題レポート待ちです。Android に限らない可能性があります
- システムのファイル選択画面がない端末では、初回起動で ROM を選ぶときに落ちる：テストサービスの 1 台でだけ起きたもので、通常の端末にはファイル選択画面があります
- エントリー向けのチップでは速度が足りない：Galaxy A15 で 14 FPS、Redmi Note 10 5G で 10 FPS 程度

## まだ確認していないもの

- GPU：Dimensity 9200/9300 の Immortalis（Redmi K60 Ultra、K70 Ultra、vivo Neo9 Pro）、Dimensity 1200 の Mali-G77（Redmi K40 Gaming）、Snapdragon 8 Elite Gen 5、Samsung Exynos の Xclipse、Pixel 10 の PowerVR、Snapdragon 6/7 系の中級 Adreno
- システム：Android 17 の Snapdragon 端末、Android 16 の Snapdragon 8 Gen 2/Gen 3、Android 12 の MediaTek 端末。ColorOS、OriginOS、MagicOS など
- 確認の深さ：ほとんどの端末はタイトル画面かデモ戦闘まで。第 1 話まで進めたのは上の MediaTek 端末 2 台だけで、インターミッション、戦闘アニメ、HD パック、長時間のプレイは Seeker でしか確認していません

これらの端末で遊んでいる方は、問題があってもなくても、ぜひ様子をお知らせください。
