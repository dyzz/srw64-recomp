---
title: Android 端末の動作確認状況
date: 2026-10-08
summary: Android 端末の実機確認、プレイヤーからの報告と現状を GPU 別にまとめました。Snapdragon 865/870/888、8 Gen 2/Gen 3 と、ドライバーの古い Mali 端末は 0.4.3 で修正済みです。Redmi K60 の黒画面は調査中です。
---

0.4.0 の公開以降、Android 端末で起動しない、落ちる、画面が黒いままといった報告を多くいただきました。ここでは GPU ごとに、こちらで実際に確認した端末、プレイヤーからの報告、現在の状況をまとめます。お使いの端末と照らし合わせてください。最新版の 0.4.3 には以下の修正がすべて含まれています。⚠️ はまだ確認できていない点です。

端末のプロセッサーと GPU は「設定 → 端末情報」（Xiaomi の端末は「設定 → マイデバイス → すべての仕様」）で確認できます。問題があれば、ゲーム内の「オプション → フィードバック」から問題レポートを書き出し、端末名とプロセッサーを添えて [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues) にお寄せください。

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

- 状況：正常
- 実機確認：Galaxy S25（Android 16）で正常に動作
- プレイヤーの報告：なし

## Adreno 730（Snapdragon 8 Gen 1 / 8+ Gen 1：Redmi K60、K50 Ultra）

- 状況：Redmi K60 の黒画面は未解決
- 実機確認：Xiaomi 12（Android 12、MIUI）、Galaxy S22（Android 13）はどちらも正常。ドライバーのバージョンはプレイヤーの端末と同じで、再現できていません
- プレイヤーの報告：Redmi K60（8+ Gen 1、Android 13、MIUI 14.0.3）、黒画面
  - テスト版を 2 回試してもらいましたが直らず、調査中です

## Adreno 650（Snapdragon 865/870：POCO F3、Redmi K40、K40S）

- 状況：落ちる → 0.4.3 で修正（650 のみ）
- 実機確認：Galaxy Tab S7（Android 11）で修正前の落ちる現象を再現し、修正後は正常に動作。3 分半の連続動作も確認
- プレイヤーの報告：
  - POCO F3：落ちる → テスト版で遊べることを確認
  - Redmi K40（HyperOS、Android 13）：⚠️ 機種は確認中。HyperOS は未確認
- ⚠️ この修正により、画面を読み戻す一部の演出が正確でなくなります。どこに影響するかはまだ遊んで確かめていません

## Adreno 660（Snapdragon 888：Redmi K40 Pro、Xiaomi 11）

- 状況：起動時に落ちる → 0.4.3 で修正（660 のみ）
- 実機確認：Galaxy S21 Ultra（Android 12）で修正前に起動時に落ちることを再現。修正後は 3 分半の連続動作で、デモ戦闘の画面も正常
- プレイヤーの報告：なし
- ⚠️ 確認したのは 1 台だけで、プレイヤーによる確認も、本編での確認もまだです

## Mali（新しいドライバー：Pixel 7/8 の G710/G715、Solana Seeker の G615）

- 状況：正常
- 実機確認：Pixel 7、Pixel 8（Android 14）、Seeker（Android 16）で正常に動作
- プレイヤーの報告：なし

## Mali（古いドライバー：Dimensity 8200 / 6100+、Exynos 1380 など：G57/G68/G610）

- 状況：0.4.2 以前は起動直後に終了 → 0.4.3 で修正済み
- 実機確認：Galaxy A15（G57）、A35（G68）、Android 14 で、0.4.2 の起動直後の終了を再現し、修正後は正常に動作
  - A15 はエントリー向けのチップで、14 FPS 程度
- プレイヤーの報告：
  - Dimensity 8200 の端末（G610、起動しない）：⚠️ G610 そのものは未確認、返信待ち
  - Android 14 の端末（起動しない）：⚠️ 機種不明

## Mali-G77（Dimensity 1200：Redmi K40 Gaming）

- 状況：⚠️ 未確認
- プレイヤーの報告：なし

## Immortalis（Dimensity 9200/9300：Redmi K60 Ultra、K70 Ultra、vivo Neo9 Pro）

- 状況：⚠️ 未確認
- プレイヤーの報告：なし
