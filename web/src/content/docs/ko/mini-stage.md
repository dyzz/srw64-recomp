---
title: 미니 스테이지
summary: 맵, 그 위의 기체, 이벤트 스크립트를 JSON 파일 하나에 적고, 시나리오 대신 곧바로 진입해 전투, 화면, 스크립트 명령을 재현합니다. MCP로만 불러올 수 있습니다.
section: tools
order: 2
updated: 2026-10-31
---

미니 스테이지는 직접 쓰는 작은 시나리오입니다. 원작 맵 하나를 고르고, 필요한 아군과 적 기체를 배치하고, 이벤트 스크립트 몇 개 (오프닝 대사, 턴 시작, 증원, 승리 조건 등)를 추가합니다. 불러오면 게임이 시나리오 하나의 슬롯을 빌려 새 게임, 프롤로그, 주인공 선택을 건너뛰고 곧바로 여러분의 스테이지로 들어가므로, 몇 초 만에 맵 위에 서게 됩니다.

저는 개발 중에 특정 전투와 화면을 재현하는 데 미니 스테이지를 씁니다. 전투 전 화면의 명중률을 확인하려고 특정 기체 둘을 나란히 놓거나, L2/R2 순환을 테스트하려고 적을 화면 밖에 두거나, 스토리 명령 하나만 따로 실행해 정확히 무엇을 하는지 봅니다. 저장소에는 참고할 수 있는 완성된 스테이지가 60개 넘게 있습니다.

미니 스테이지는 디버깅 도구이며 **게임에는 이를 위한 메뉴가 없습니다.** 디버그 인터페이스를 켠 상태에서 MCP를 통해서만 불러올 수 있습니다. 연결 방법은 [AI 에이전트로 게임 조작하기 (MCP)](../mcp/)를 참고하세요.

스테이지를 이어 플레이어가 설치해 즐길 수 있는 캠페인으로 만들려면 [캠페인 MOD](../mod-campaign/)를 참고하세요.

## AI 에이전트용: llms.txt

[`/docs/mini-stage/llms.txt`](/docs/mini-stage/llms.txt)는 미니 스테이지를 쓰는 데 필요한 모든 것을 AI 에이전트용 일반 텍스트 파일 하나에 담았습니다: 작업 흐름, 컴파일러가 검사하는 모든 제한, 캐릭터와 기체 번호를 찾는 곳, 그리고 명령, 조건, 마커, 이벤트 유형, 출격 레코드 필드의 전체 레퍼런스 (영어 이름과 중국어로 된 구현 메모 및 실측 결과). Claude Code처럼 페이지를 가져올 때 Markdown을 요청하는 도구는 이 페이지 주소에서 이 파일을 자동으로 받습니다. 다른 도구에는 이 주소를 알려 주세요. 게임이 연결되어 있으면 이렇게 말하면 됩니다.

```text
https://srw64.dreamquest.club/docs/mini-stage/llms.txt를 읽은 다음, 아군 기체 하나와 그 옆에 적 둘이 있는 미니 스테이지를 써서 컴파일하고, srw64_mini_stage_load로 불러온 뒤 안에 들어가면 스크린샷을 찍어 줘.
```

## 필요한 것

- [MCP 안내](../mcp/)대로 연결된 게임 (`srw64_attach`가 동작해야 합니다)
- 스테이지 정의를 게임이 읽는 이미지로 컴파일하기 위한 [소스 저장소](https://github.com/dyzz/srw64-recomp)와 Python 3
- 스테이지가 원작의 출격 레코드나 이벤트를 참조한다면 (아래의 `template`, `deployments_from`, `copy_from`), 자신의 ROM을 저장소에 `rom.z64`로 넣고 `make recomp-data`를 한 번 실행해 원작 데이터를 추출해야 합니다. 직접 쓴 필드만 사용하는 스테이지라면 필요 없습니다.

## 1. 스테이지 정의 쓰기

스테이지 정의는 `schema`가 `srw64.mini-stage.v1`인 JSON 파일입니다. 아래는 저장소의 `move-jump.json`을 줄인 것으로, 맵 20에 아군 하나와 적 하나가 있고, 오프닝에서 카메라를 아군에게 맞춘 뒤 아군 페이즈로 넘깁니다.

```json
{
  "schema": "srw64.mini-stage.v1",
  "name": "my-test",
  "note": "Free text: what this stage tests",
  "map": 20,
  "deployments": [
    {"template": "base:stage_deployments:001f0f1c", "group": 0, "x": 8, "y": 8, "faction": 0},
    {"template": "base:stage_deployments:001f2720", "group": 1, "x": 12, "y": 8, "faction": 1}
  ],
  "events": [
    {"name": "opening", "type": 12, "header": [0, 0, 0, 0], "commands": [
      {"op": "3DD0"},
      {"op": "3D32", "args": [4]},
      {"op": "3D4D"},
      {"op": "3D65", "args": [17, 31]},
      {"op": "3D3B", "args": [1]},
      {"op": "3D45", "args": [0]},
      {"op": "3D45", "args": [1]},
      {"op": "3D35", "args": [16384, 0]},
      {"op": "3D48"}
    ]},
    {"name": "ending", "type": 14, "header": [0, 0, 0, 0], "commands": [
      {"op": "3DD0"},
      {"op": "3D4B", "args": [4]}
    ]}
  ]
}
```

필드:

| 필드 | 의미 |
|---|---|
| `map` | 원작 맵 번호 (0–255). |
| `slot` | 선택. 빌려 쓸 시나리오 슬롯 (장면 번호). 없으면 불러온 뒤 게임이 등록하는 시나리오를 씁니다. |
| `deployments_from` | 선택. 시나리오의 원작 출격 블록 전체를 복사합니다. 예: 시나리오 1은 `base:stage_auxiliary:001e4268`. |
| `deployments` | 추가 출격 레코드. `group`은 등장 그룹 (`3D45 <group>`이 불러들이는 대상), `x`/`y`는 칸, `faction`은 아군 0, 적 1입니다. `template`는 원작 레코드에서 시작하며, `unit`, `actor`, `upgrade`, `behavior` 같은 필드로 필요한 부분을 바꿉니다. |
| `initial_resources` | 선택. 맵에 도착했을 때 일부 기체의 HP와 EN을 최대치의 백분율로 설정합니다: `{"side": 1, "slot": 0, "hp_percent": 30, "en_percent": 100}`. |
| `events` | 이벤트 최대 63개. 각 이벤트에는 `type` (0–14. 예: 12 오프닝, 13 초기 배치, 0 턴 시작, 7 적 수, 14 종료), `header`의 트리거 파라미터 4개, `{"op": "3D45", "args": [1]}` 형식으로 쓰는 `commands` 목록이 있습니다. `copy_from`은 원작 이벤트를 그대로 복사합니다. |

모든 명령, 조건, 이벤트 유형, 출격 레코드 필드는 [스테이지 스크립트 레퍼런스](../script-commands/)에 있습니다. 그 바탕이 된 연구는 저장소에 (중국어로) 있습니다: [스테이지 스크립트와 이벤트 유형](https://github.com/dyzz/srw64-recomp/blob/main/docs/script/stage-script-exploration.md), [명령 작성법](https://github.com/dyzz/srw64-recomp/blob/main/docs/script/script-debug-injection.md), [미니 스테이지 연구 노트](https://github.com/dyzz/srw64-recomp/blob/main/docs/script/mini-stage.md). 가장 쉬운 시작은 [`config/recomp/mini-stages/`](https://github.com/dyzz/srw64-recomp/tree/main/config/recomp/mini-stages)에서 가장 비슷한 스테이지를 복사하는 것입니다. 각 파일의 `note`에 무엇을 테스트하는지 적혀 있습니다.

## 2. 이미지로 컴파일하기

저장소 폴더에서 다음을 실행합니다.

```bash
python3 tools/recomp/script_lab/mini_stage.py compile my-test.json --out my-test.image.json
```

이미지 (`srw64.mini-stage-image.v1`)는 게임이 읽는 형식입니다. 이벤트가 63개를 넘거나, 이벤트나 출격 데이터가 게임의 버퍼보다 크면 컴파일러가 이유와 함께 중단합니다.

이미지에는 여러분의 ROM에서 복사한 원작 데이터가 들어 있습니다. 자신의 컴퓨터에서만 사용하고 공유하지 마세요.

## 3. 불러와서 진입하기

게임을 타이틀의 메인 메뉴 (새 게임 / 이어하기 단계)에 둔 상태에서 에이전트에게 이미지를 불러오게 합니다.

```text
타이틀 메인 메뉴가 나올 때까지 기다렸다가 srw64_mini_stage_load로 /your/path/my-test.image.json을 불러오고, 스테이지가 준비되면 스크린샷을 찍어 줘.
```

- `srw64_mini_stage_load`에 주는 `path`는 **게임이 실행되는 기기의** 경로입니다. Steam Deck이라면 먼저 이미지를 Deck에 복사하세요.
- 게임은 즉시 스테이지에 진입합니다. 오프닝 이벤트가 재생된 뒤 맵에서 여러분을 기다립니다. 조작할 수 있게 되면 `srw64_status`의 `mini_stage.ready`가 `true`가 됩니다.
- 타이틀 메인 메뉴가 아니거나, 파일을 읽을 수 없거나 올바르지 않으면 이유와 함께 불러오기가 거부됩니다.
- 설치된 게임은 컴파일된 이미지만 불러옵니다. `srw64_launch`로 소스에서 시작한 개발 세션은 스테이지 정의를 직접 불러올 수 있으며 (게임이 컴파일러를 직접 호출합니다), `srw64_launch`의 `mini_stage` 인수로 시작 시 스테이지를 넣어 두고 메인 메뉴에서 F8로 진입합니다.

그다음에는 [도구 목록](../mcp/#도구)의 도구를 사용합니다: 기체 선택, 공격, 전투 전 화면 확인, 스크린샷, 녹화.

## 알아 둘 점

- **자신의 세이브 폴더를 사용합니다.** 스테이지 안에서 저장하거나, 자동 저장이 켜져 있으면 평소 세이브 슬롯에 기록됩니다. 먼저 사용자 폴더의 `saves/`를 백업하거나, MCP 안내에 나온 대로 `--user-dir`로 별도 폴더를 사용하세요.
- **주인공 선택이 없습니다.** 주인공과 파트너의 이름과 루트가 비어 있습니다. 이벤트에 필요하면 스테이지에서 설정하거나, 먼저 일반 새 게임을 시작하세요.
- **난수가 일반 게임과 다릅니다.** 직접 진입은 일반 새 게임보다 수백 프레임 일찍 이루어지므로 적의 행동과 명중 판정이 달라집니다. 결과를 재현하려면 같은 이미지에서 같은 입력으로 다시 시작하세요.
- **한 번에 스테이지 하나.** 다른 스테이지를 불러오면 앞의 것을 대체합니다. 다 끝나면 `srw64_quit`로 종료하거나, 타이틀로 돌아가 다음 스테이지를 불러오세요.
