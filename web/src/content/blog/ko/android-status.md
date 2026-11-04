---
title: 테스트한 Android 휴대폰
date: 2026-10-08
summary: 제가 테스트한 Android 휴대폰과 플레이어 제보, GPU별 현황, 그리고 알려진 문제와 아직 테스트하지 못한 부분을 정리했습니다. Snapdragon 865/870/888, 8 Gen 2/Gen 3, 구형 드라이버의 Mali 휴대폰은 수정되었고, Redmi K60의 검은 화면과 Snapdragon 8 Elite(Android 17)에서 설정을 열었을 때의 화면 깜빡임은 아직 조사 중입니다.
---

*이 글은 0.4.4 시점의 기록이며 이후의 수정은 반영되어 있지 않습니다. 현재 상황은 [Android 호환 목록](/ko/android/)을 참고하세요.*

*2026-10-10 업데이트: 최신 버전이 0.4.4가 되었습니다. 첫 스테이지까지 플레이해 본 MediaTek 휴대폰, 새로운 플레이어 제보, 알려진 문제, 아직 테스트하지 못한 부분을 추가했습니다.*

0.4.0을 공개한 뒤로 Android 휴대폰에서 실행되지 않거나, 강제 종료되거나, 검은 화면이 나온다는 제보가 여러 건 들어왔습니다. 이 글에서는 제가 직접 테스트한 휴대폰, 플레이어 제보, 현재 상황을 GPU별로 정리했으니 자신의 휴대폰을 확인해 보세요. 최신 버전인 0.4.4에는 아래의 수정이 모두 들어 있습니다. ⚠️는 아직 확인되지 않은 부분입니다.

휴대폰의 프로세서와 GPU는 설정 → 휴대전화 정보에서 확인할 수 있습니다(Xiaomi 휴대폰은 설정 → 내 기기 → 전체 사양). 문제가 생기면 게임의 옵션 → 피드백에서 문제 신고 파일을 내보내고, 휴대폰 모델, 프로세서, 게임 버전과 함께 [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues)에 올려 주세요. 0.4.4부터 신고 파일은 권한 없이 내장 저장공간/Download/Marchwind64/reports/에 저장됩니다. 최근 3회 실행의 로그가 들어 있으므로, 강제 종료나 멈춤이 일어난 뒤에는 게임을 다시 실행한 다음 내보내 주세요.

## Adreno 740 (Snapdragon 8 Gen 2: Xiaomi 13, Redmi K60 Pro, vivo Neo9, Redmi K70)

- 현황: 0.4.0에서 강제 종료 → 0.4.1부터 수정
- 테스트: Xiaomi 13(Android 13)과 Galaxy S23(Android 14)은 수정 전에는 강제 종료되었고 수정 후에는 실행됨
- 플레이어 제보: Redmi K70, vivo Neo9(Android 16, 강제 종료)
  - ⚠️ 정확한 모델은 확인되지 않았습니다. MediaTek 버전이라면 이 항목에 해당하지 않습니다. 아직 답변이 없습니다

## Adreno 750 (Snapdragon 8 Gen 3: iQOO 12, Redmi K70 Pro)

- 현황: 0.4.0에서 강제 종료 → 0.4.1부터 수정
- 테스트: Galaxy S24 Ultra(Android 14)는 수정 전에 플레이어가 설명한 것과 같은 방식으로 강제 종료되었고, 수정 후에는 실행됨
- 플레이어 제보: iQOO 12(Android 16, 터치 버튼이 나타나자마자 강제 종료)
  - ⚠️ 아직 답변이 없습니다. Android 16에서 750은 테스트하지 못했습니다

## Adreno 830 (Snapdragon 8 Elite)

- 현황: 실행됨. ⚠️ Android 17에서 설정을 열었을 때 화면이 깜빡인다는 제보가 있어 조사 중
- 테스트: Galaxy S25(Android 16)에서 실행됨, 타이틀 화면까지 테스트
- 플레이어 제보: Snapdragon 8 Elite 휴대폰(Android 17): 맵에서 설정을 한동안 열어 두면 화면이 깜빡이고 게임을 종료해야 함
  - ⚠️ 원인 불명, 문제 신고 파일을 기다리는 중입니다. Android 17에서 Snapdragon 휴대폰은 테스트하지 못했습니다

## Adreno 730 (Snapdragon 8 Gen 1 / 8+ Gen 1: Redmi K60, K50 Ultra)

- 현황: Redmi K60의 검은 화면은 해결되지 않음
- 테스트: Xiaomi 12(Android 12, MIUI)와 Galaxy S22(Android 13) 모두 플레이어와 같은 드라이버 버전으로 실행됨. 재현하지 못했습니다
- 플레이어 제보: Redmi K60(8+ Gen 1, Android 13, MIUI 14.0.3), 검은 화면
  - 테스트 빌드 두 개로도 고쳐지지 않았습니다. 계속 조사 중입니다

## Adreno 650 (Snapdragon 865/870: POCO F3, Redmi K40, K40S)

- 현황: 강제 종료 → 0.4.3부터 수정(650에만 적용)
- 테스트: Galaxy Tab S7(Android 11)은 수정 전에는 강제 종료되었고, 수정 후에는 실행되며 3분 30초 동안의 실행 테스트를 통과함
- 플레이어 제보:
  - POCO F3: 강제 종료 → 테스트 빌드로 플레이 가능
  - Redmi K40(HyperOS, Android 13): ⚠️ 모델 확인 필요
- ⚠️ 이 수정으로 화면을 다시 읽어 들이는 일부 효과가 부정확해집니다. 어디에서 나타나는지 확인할 만큼 플레이하지는 못했습니다

## Adreno 660 (Snapdragon 888: Redmi K40 Pro, Xiaomi 11)

- 현황: 시작 시 강제 종료 → 0.4.3부터 수정(660에만 적용)
- 테스트: Galaxy S21 Ultra(Android 12)는 수정 전에는 시작 시 강제 종료되었고, 수정 후에는 3분 30초 동안 실행하며 데모 전투가 올바르게 그려짐
- 플레이어 제보: 없음
- ⚠️ 휴대폰 한 대뿐이고, 플레이어가 시도한 적이 없으며, 실제 게임 플레이까지는 들어가지 않았습니다

## Mali, 신형 드라이버 (Pixel 7/8: G710/G715, Solana Seeker: G615)

- 현황: 정상 작동
- 테스트: Pixel 7, Pixel 8(Android 14), Seeker(Android 16)에서 실행됨. Seeker는 제 주력 테스트 휴대폰으로, 모든 종류의 장면을 30 FPS로 거쳤습니다
- 플레이어 제보: 없음

## Mali, 구형 드라이버 (Dimensity 8100 / 8200 / 7200 / 1080 / 900 / 6100+, Exynos 1380 등: G57/G68/G610)

- 현황: 0.4.2 이전에는 시작 시 종료 → 0.4.3부터 수정
- 테스트:
  - Galaxy A15(G57)와 A35(G68), Android 14: 0.4.2에서는 시작 시 종료되었고 수정 후에는 실행됨. A15는 보급형 칩이라 14 FPS로 동작
  - Redmi Note 13 Pro+(Dimensity 7200-Ultra, G610, HyperOS 2, Android 15)와 Galaxy A34(Dimensity 1080, G68, Android 13): 0.4.3에서 새 게임부터 첫 스테이지까지 30 FPS로 플레이
  - Redmi Note 10 5G(Dimensity 700, G57, Android 11): 약 10 FPS로 실행됨
- 플레이어 제보:
  - Dimensity 8100 휴대폰(G610, 진입 시 강제 종료): ⚠️ 버전과 강제 종료 위치는 확인되지 않았습니다. 같은 GPU로 0.4.3에서 첫 스테이지까지 갑니다
  - Dimensity 900 휴대폰(G68, Android 12, 진입 시 강제 종료): ⚠️ 위와 같습니다. Android 12에서 MediaTek 휴대폰은 테스트하지 못했습니다
  - Dimensity 8200 휴대폰(G610, 실행되지 않음): ⚠️ 8200 칩 자체는 테스트하지 못했습니다. 아직 답변이 없습니다
  - Android 14 휴대폰(실행되지 않음): ⚠️ 모델 불명

## 그 밖의 알려진 문제

- ⚠️ HD를 켠 상태에서 流星が落ちた日에서 적이 제3세력 유닛을 공격하면 강제 종료됨: 한 플레이어가 Android 휴대폰에서 겪었으며, HD를 끄면 발생하지 않습니다. 컴퓨터에서 같은 전투를 수십 번 실행해도 재현되지 않았습니다. 문제 신고 파일을 기다리고 있으며, Android에서만 생기는 문제가 아닐 수도 있습니다
- 시스템 파일 선택기가 없는 휴대폰에서는 처음 실행할 때 ROM을 고르는 단계에서 강제 종료됨: 테스트 서비스의 휴대폰 한 대에서만 확인되었으며, 일반 휴대폰에는 파일 선택기가 있습니다
- 보급형 칩은 너무 느림: Galaxy A15에서 약 14 FPS, Redmi Note 10 5G에서 약 10 FPS

## 아직 테스트하지 못한 부분

- GPU: Dimensity 9200/9300의 Immortalis(Redmi K60 Ultra, K70 Ultra, vivo Neo9 Pro), Dimensity 1200의 Mali-G77(Redmi K40 Gaming), Snapdragon 8 Elite Gen 5, 삼성 Exynos의 Xclipse, Pixel 10의 PowerVR, 중급 Snapdragon 6/7 칩의 Adreno
- 시스템: Android 17의 Snapdragon 휴대폰, Android 16의 Snapdragon 8 Gen 2/Gen 3, Android 12의 MediaTek 휴대폰, ColorOS, OriginOS, MagicOS 등
- 테스트 범위: 대부분의 휴대폰은 타이틀 화면이나 데모 전투까지만 테스트했고, 첫 스테이지까지 들어간 것은 위의 MediaTek 휴대폰 두 대뿐입니다. 인터미션 화면, 전투 애니메이션, HD 팩, 장시간 플레이는 Seeker에서만 실행해 보았습니다

이 휴대폰들 중 하나로 플레이하신다면, 문제가 있든 잘 작동하든 알려 주시면 좋겠습니다.
