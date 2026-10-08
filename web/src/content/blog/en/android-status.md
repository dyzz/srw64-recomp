---
title: Android phones tested
date: 2026-10-08
summary: The Android phones we tested, player reports and where each stands, by GPU. Snapdragon 865/870/888, 8 Gen 2/Gen 3 and Mali phones with older drivers are fixed in 0.4.3; the Redmi K60's black screen is still being looked into.
---

Since 0.4.0 went public we have had a number of reports of Android phones that would not open, crashed or showed a black screen. This post lists, by GPU, the phones we tested ourselves, what players reported and where things stand, so you can check yours. The latest version, 0.4.3, has every fix below. ⚠️ marks what is not confirmed yet.

Your phone's processor and GPU are under Settings → About phone (on Xiaomi phones, Settings → My device → All specs). If something goes wrong, export a problem report from Options → Feedback in the game and file it on [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues) with your phone's model and processor.

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

- Status: works
- Tested: Galaxy S25 (Android 16) runs
- Player reports: none

## Adreno 730 (Snapdragon 8 Gen 1 / 8+ Gen 1: Redmi K60, K50 Ultra)

- Status: the Redmi K60's black screen is not solved
- Tested: Xiaomi 12 (Android 12, MIUI) and Galaxy S22 (Android 13) both run, with the same driver version as the player's; we could not reproduce it
- Player reports: Redmi K60 (8+ Gen 1, Android 13, MIUI 14.0.3), black screen
  - Two test builds did not fix it; still investigating

## Adreno 650 (Snapdragon 865/870: POCO F3, Redmi K40, K40S)

- Status: crashed → fixed in 0.4.3 (for the 650 only)
- Tested: Galaxy Tab S7 (Android 11) crashed before the fix, runs after it and passed a 3.5-minute run
- Player reports:
  - POCO F3: crash → plays with a test build
  - Redmi K40 (HyperOS, Android 13): ⚠️ model to be confirmed; we have not tested HyperOS
- ⚠️ The fix makes a few effects that read the picture back inexact; we have not played far enough to see where

## Adreno 660 (Snapdragon 888: Redmi K40 Pro, Xiaomi 11)

- Status: crashed at start → fixed in 0.4.3 (for the 660 only)
- Tested: Galaxy S21 Ultra (Android 12) crashed at start before the fix; after it, a 3.5-minute run with the attract battles drawn correctly
- Player reports: none
- ⚠️ One phone only, no player has tried it, and it has not been into a real game

## Mali, newer drivers (Pixel 7/8: G710/G715; Solana Seeker: G615)

- Status: works
- Tested: Pixel 7, Pixel 8 (Android 14) and Seeker (Android 16) run
- Player reports: none

## Mali, older drivers (Dimensity 8200 / 6100+, Exynos 1380 and others: G57/G68/G610)

- Status: quit at start in 0.4.2 and earlier → fixed in 0.4.3
- Tested: Galaxy A15 (G57) and A35 (G68), Android 14, quit at start on 0.4.2 and run after the fix
  - The A15 is an entry-level chip and runs at 14 FPS
- Player reports:
  - A Dimensity 8200 phone (G610, would not start): ⚠️ we have not tested the G610 itself; no reply yet
  - An Android 14 phone (would not start): ⚠️ model unknown

## Mali-G77 (Dimensity 1200: Redmi K40 Gaming)

- Status: ⚠️ not tested
- Player reports: none

## Immortalis (Dimensity 9200/9300: Redmi K60 Ultra, K70 Ultra, vivo Neo9 Pro)

- Status: ⚠️ not tested
- Player reports: none
