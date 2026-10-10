---
title: Android phones tested
date: 2026-10-08
summary: The Android phones we tested, player reports and where each GPU stands, plus the known issues and what we have not tested. Snapdragon 865/870/888, 8 Gen 2/Gen 3 and Mali phones with older drivers are fixed; the Redmi K60's black screen and a flicker with the settings open on a Snapdragon 8 Elite (Android 17) are still being looked into.
---

*Updated 2026-10-10: the latest version is now 0.4.4; added MediaTek phones played into the first stage, new player reports, the known issues and what we have not tested.*

Since 0.4.0 went public we have had a number of reports of Android phones that would not open, crashed or showed a black screen. This post lists, by GPU, the phones we tested ourselves, what players reported and where things stand, so you can check yours. The latest version, 0.4.4, has every fix below. ⚠️ marks what is not confirmed yet.

Your phone's processor and GPU are under Settings → About phone (on Xiaomi phones, Settings → My device → All specs). If something goes wrong, export a problem report from Options → Feedback in the game and file it on [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues) with your phone's model, processor and game version. Since 0.4.4 the report is saved to Internal storage/Download/Marchwind64/reports/ with no permission needed. It carries the logs of the last 3 runs, so after a crash or a freeze, open the game again and export it then.

## Adreno 740 (Snapdragon 8 Gen 2: Xiaomi 13, Redmi K60 Pro, vivo Neo9, Redmi K70)

- Status: crashed in 0.4.0 → fixed since 0.4.1
- Tested: Xiaomi 13 (Android 13) and Galaxy S23 (Android 14) crashed before the fix and run after it
- Player reports: Redmi K70, vivo Neo9 (Android 16, crash)
  - ⚠️ Exact models not confirmed; a MediaTek version would not belong here. No reply yet

## Adreno 750 (Snapdragon 8 Gen 3: iQOO 12, Redmi K70 Pro)

- Status: crashed in 0.4.0 → fixed since 0.4.1
- Tested: Galaxy S24 Ultra (Android 14) crashed before the fix, the same way the player described, and runs after it
- Player reports: iQOO 12 (Android 16, crash once the touch buttons appear)
  - ⚠️ No reply yet; we have not tested a 750 on Android 16

## Adreno 830 (Snapdragon 8 Elite)

- Status: starts; ⚠️ a flicker with the settings open is reported on Android 17 and being looked into
- Tested: Galaxy S25 (Android 16) runs, tested as far as the title screen
- Player reports: a Snapdragon 8 Elite phone (Android 17): with the settings open on the map for a while, the screen flickers and the game has to be closed
  - ⚠️ Cause unknown, waiting for a problem report; we have not tested a Snapdragon phone on Android 17

## Adreno 730 (Snapdragon 8 Gen 1 / 8+ Gen 1: Redmi K60, K50 Ultra)

- Status: the Redmi K60's black screen is not solved
- Tested: Xiaomi 12 (Android 12, MIUI) and Galaxy S22 (Android 13) both run, with the same driver version as the player's; we could not reproduce it
- Player reports: Redmi K60 (8+ Gen 1, Android 13, MIUI 14.0.3), black screen
  - Two test builds did not fix it; still investigating

## Adreno 650 (Snapdragon 865/870: POCO F3, Redmi K40, K40S)

- Status: crashed → fixed since 0.4.3 (for the 650 only)
- Tested: Galaxy Tab S7 (Android 11) crashed before the fix, runs after it and passed a 3.5-minute run
- Player reports:
  - POCO F3: crash → plays with a test build
  - Redmi K40 (HyperOS, Android 13): ⚠️ model to be confirmed
- ⚠️ The fix makes a few effects that read the picture back inexact; we have not played far enough to see where

## Adreno 660 (Snapdragon 888: Redmi K40 Pro, Xiaomi 11)

- Status: crashed at start → fixed since 0.4.3 (for the 660 only)
- Tested: Galaxy S21 Ultra (Android 12) crashed at start before the fix; after it, a 3.5-minute run with the attract battles drawn correctly
- Player reports: none
- ⚠️ One phone only, no player has tried it, and it has not been into a real game

## Mali, newer drivers (Pixel 7/8: G710/G715; Solana Seeker: G615)

- Status: works
- Tested: Pixel 7, Pixel 8 (Android 14) and Seeker (Android 16) run; the Seeker is our main test phone and has been through every kind of scene at 30 FPS
- Player reports: none

## Mali, older drivers (Dimensity 8100 / 8200 / 7200 / 1080 / 900 / 6100+, Exynos 1380 and others: G57/G68/G610)

- Status: quit at start in 0.4.2 and earlier → fixed since 0.4.3
- Tested:
  - Galaxy A15 (G57) and A35 (G68), Android 14: quit at start on 0.4.2 and run after the fix; the A15 is an entry-level chip and runs at 14 FPS
  - Redmi Note 13 Pro+ (Dimensity 7200-Ultra, G610, HyperOS 2, Android 15) and Galaxy A34 (Dimensity 1080, G68, Android 13): on 0.4.3, played from a new game into the first stage at 30 FPS
  - Redmi Note 10 5G (Dimensity 700, G57, Android 11): runs, at about 10 FPS
- Player reports:
  - A Dimensity 8100 phone (G610, crashes on entering): ⚠️ version and where it crashes not confirmed; the same GPU reaches the first stage on 0.4.3
  - A Dimensity 900 phone (G68, Android 12, crashes on entering): ⚠️ the same; we have not tested a MediaTek phone on Android 12
  - A Dimensity 8200 phone (G610, would not start): ⚠️ we have not tested the 8200 chip itself; no reply yet
  - An Android 14 phone (would not start): ⚠️ model unknown

## Other known issues

- ⚠️ With HD on, a crash in 流星が落ちた日 when an enemy attacks a third-party unit: a player hit it on an Android phone, and it does not happen with HD off. Dozens of runs of the same battles on a computer did not reproduce it; we are waiting for a problem report, and it may not be Android-only
- On a phone without the system file picker, the first start crashes when choosing the ROM: seen only on one phone of a test service; ordinary phones have a file picker
- Entry-level chips are too slow: about 14 FPS on a Galaxy A15 and 10 FPS on a Redmi Note 10 5G

## Not tested yet

- GPUs: Immortalis on the Dimensity 9200/9300 (Redmi K60 Ultra, K70 Ultra, vivo Neo9 Pro), Mali-G77 on the Dimensity 1200 (Redmi K40 Gaming), Snapdragon 8 Elite Gen 5, Samsung Exynos's Xclipse, the Pixel 10's PowerVR, and the Adreno of mid-range Snapdragon 6/7 chips
- Systems: Snapdragon phones on Android 17, the Snapdragon 8 Gen 2/Gen 3 on Android 16, MediaTek phones on Android 12; ColorOS, OriginOS, MagicOS and the like
- Depth: most phones were tested as far as the title screen or the attract battles, and only the two MediaTek phones above went into the first stage; intermission screens, battle animations, the HD pack and long sessions have only run on the Seeker

If you play on one of these phones, whether it has problems or works fine, we would like to hear.
