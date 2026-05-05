# 다음 세션 구현 과제

> 갱신: 2026-05-06
> 이전 세션 완료: 슬램 기믹 제거, 순수 원형 포위(HoldSlot + greedy nearest-slot), Squad당 20명, 문서 정비

---

## 과제 1 — 원형 포위 슬롯 할당 개선

### 현재 구현

`TacticalSquad::pushCommandsToMembers()` — `Encircle` case:

1. `calcEncircleSlots()` 로 Squad의 섹터 안에 슬롯 N개 생성
2. **greedy nearest-slot**: 멤버 배열 순서(i=0, 1, 2…)로 순회하면서
   각 NPC가 남은 슬롯 중 자신과 가장 가까운 것을 선점

```
멤버 순회:  A0 → A1 → A2 → …
각자:       미사용 슬롯 중 최단 거리 슬롯 선택 후 사용 처리
```

### 문제

- 처리 순서가 멤버 배열 인덱스에 따라 고정되어 있어, 앞 번호 NPC가 좋은 슬롯을 독점한다.
- 개별 최적(각자 가장 가까운 슬롯)이 전체 최적(총 이동거리 최소)을 보장하지 않는다.
- 같은 Squad 내에서도 경로가 교차하는 경우가 여전히 발생한다.

### 개선 방향 (검토 필요)

| 방법 | 설명 | 비용 |
|---|---|---|
| **랜덤 순서 greedy** | 멤버 순회 순서를 섞은 뒤 동일 greedy 적용 | 간단, 완전 최적 아님 |
| **Hungarian algorithm** | 이분 그래프 최적 매칭, 총 이동거리 전역 최소화 | O(N³), 구현 복잡 |
| **반복 교환(2-opt)** | greedy 초기 할당 후 쌍 교환 반복 개선 | O(N²), 준최적 |
| **각도 기반 정렬** | NPC의 현재 위치 각도와 슬롯 각도를 맞춰 배정 | O(N log N), 직관적 |

**각도 기반 정렬** 아이디어:
```
NPC 위치를 타겟 중심 기준 각도로 변환
슬롯도 각도 순으로 정렬
각도가 가장 가까운 NPC-슬롯 쌍으로 1:1 배정
```
원형 대형이므로 각도가 유사한 NPC와 슬롯을 매칭하면 경로 교차가 자연히 줄어든다.

### 관련 파일

| 파일 | 위치 |
|---|---|
| `sim/TacticalSquad.cpp` | `pushCommandsToMembers()` — `Encircle` case |
| `sim/TacticalSquad.cpp` | `calcEncircleSlots()` — 슬롯 생성 |

---

## 과제 2 — 중간보스(PlatoonLeader) 움직임 구현

### 현재 구현

`PlatoonLeader::update()` 에서 자체 전투 FSM을 실행한다.

```cpp
pendingCmd_.type = TacticalCommandType::None;  // Squad 명령 차단
TacticalNpc::update(dt, room);                 // 일반 TacticalNpc FSM 실행
```

즉, 보스는 일반 NPC와 동일하게 **Chase → AttackWindup → AttackRecover** 사이클만 반복한다.
이동 패턴 없이 플레이어 방향으로 직선 추격만 한다.

### 구현 방향 (미확정 — 설계 필요)

아래는 가능한 보스 이동 패턴 후보다. 어떤 방식을 원하는지 확정이 필요하다.

#### 후보 A — TacticalPhase 연동 이동

보스가 Squad의 전술 단계에 따라 다른 위치를 취한다.

| TacticalPhase | 보스 행동 |
|---|---|
| `Encircle` | Squad가 포위 완성할 때까지 플레이어 전방에서 견제 (접근하되 공격은 Squad 주도) |
| `Vigilance` | 플레이어와 일정 거리 유지하며 후퇴 (Squad가 재정비하는 동안 시간 벌기) |
| `DivideAndConquer` | 쐐기 돌격 Squad 뒤에서 함께 돌격 |

#### 후보 B — 거리 기반 포지셔닝

플레이어와의 거리에 따라 전진/후퇴를 자율 판단한다.

```
dist < MIN_RANGE → 후퇴 (플레이어가 너무 가까움)
MIN_RANGE ≤ dist ≤ MAX_RANGE → 유지 (최적 전투 거리)
dist > MAX_RANGE → 전진 (너무 멀어짐)
```

#### 후보 C — 페이즈별 공격 패턴

HP 구간에 따라 보스 이동 속도·공격 패턴이 달라진다.

```
HP 100~70%:  기본 Chase + 일반 공격
HP 70~40%:   이동 속도 상승, 공격 빈도 증가
HP 40~0%:    특수 이동 패턴 (돌진, 회전 등)
```

### 구현 시 고려 사항

- `PlatoonLeader`는 `TacticalNpc`를 상속하므로 `state_`, `transitionTo()`, `updateChase()` 등을 그대로 활용할 수 있다.
- 이동 패턴 전용 상태가 필요하면 `PlatoonLeader`에서 `update()` override 안에 별도 처리하거나, 새 상태(enum) 추가를 검토한다.
- 보스 이동이 Squad 전술 평가(`evaluateTactics()`)와 같은 주기(1초)로 결정되어야 하는지, 매 틱 결정되어야 하는지 확정 필요.

### 관련 파일

| 파일 | 위치 |
|---|---|
| `sim/PlatoonLeader.hpp` | 보스 전용 상태/상수 추가 위치 |
| `sim/PlatoonLeader.cpp` | `update()` — 자체 FSM 실행 부분 |
| `sim/PlatoonLeader.cpp` | `evaluateTactics()` — 전술 단계와 연동할 경우 |

---

## 빌드 명령

```
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" ^
    "D:\source\repos\NPC-AI-Lab\NPCAI\NPCAI.sln" ^
    /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal
```
