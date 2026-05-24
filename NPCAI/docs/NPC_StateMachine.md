# NPC AI 상태 전이 문서

## 목차
1. [NPC 상태 머신 (기존 Npc 클래스)](#1-npc-상태-머신-기존-npc-클래스)
2. [NPC 주요 파라미터](#2-npc-주요-파라미터)
3. [NpcGroup 시야 공유 시스템](#3-npcgroup-시야-공유-시스템)
4. [전술 NPC 시스템 (TacticalNpc / TacticalSquad / PlatoonLeader)](#4-전술-npc-시스템)

> **2026-04-26:** Squad / Platoon 계층 전면 제거. 모든 Npc 클래스 인스턴스는 단독 행동(standalone).
> **2026-05-01:** 전술 NPC 시스템 추가 (섹션 4). 기존 Npc 클래스는 변경 없음.
> **2026-05-04:** 홉 고블린 전술 3종 구현 — HoldSlot(8) 상태 추가, DenseHold/DenseAdvance/WedgeCharge 명령 추가, PlatoonLeader 전술 조건부 발동·3단계 전환 로직 구현.
> **2026-05-05:** 전술 이동 속도 부스트(Flank/HoldSlot ×`TACTICAL_SPEED_MULT`) 구현. 포위 슬롯 고정(플레이어 이동 시 재할당 방지, 쿨타임 후 새 슬롯 발행).
> **2026-05-06:** 슬램/무적/CircleGuard 기믹 전면 제거. Encircle 명령 → HoldSlot(greedy nearest-slot). `calcEncircleSlots()` center-of-subdivision 공식으로 교체. `TACTIC_ACTIVE_DURATION` 제거 — `allMembersArrived()` 즉시 쿨타임. `ENCIRCLE_RADIUS` 20.0f. Squad당 20명(총 60명+Boss).

---

## 1. NPC 상태 머신 (기존 Npc 클래스)

### 상태 목록 (`NpcState`)

| 값 | 상태 | 설명 |
|---|---|---|
| 0 | `Idle` | 대기. `detectionRange` 내 플레이어 자율 감지 후 Chase 진입. 그룹 소속 NPC는 감지 실패 시 Investigate 전이. |
| 1 | `Chase` | 타겟 추격. 분리 힘(separation force)과 추격 방향 블렌드로 이동. |
| 2 | `AttackWindup` | 공격 선딜 (이동 없음). `windupTimer` 완료 시 hit/miss 판정. 타겟 이탈해도 취소 없음. |
| 3 | `AttackRecover` | 공격 후딜. 약한 separation drift 허용. |
| 4 | `Return` | 스폰 위치로 귀환. `returnSpeedMult_` 배율 적용. |
| 5 | `Reposition` | 과밀 탈출 비켜서기. 타겟 방향 + 수직 이탈 블렌드 이동. |
| 6 | `Dead` | 종단 상태. |
| 7 | `Investigate` | 그룹 공유 메모리의 최종 목격 위치로 이동하며 조사. 감지 성공 시 Chase, 도달 후 플레이어 없으면 Return. |

### 핵심 행동 원칙

- 모든 NPC는 **standalone**: 분대/소대 명령 없이 자율 타겟 선택.
- **활동 구역(`activityZone`)**: 스폰 위치 중심의 반경. 이 범위를 벗어나면 어떤 상태에서든 Return 전이.
- **타겟 소실 시**: 항상 Return (스폰 귀환).
- **Windup commit**: NPC는 windupTimer가 완료될 때까지 스윙을 commit. 타겟 이탈 시에도 취소하지 않음.

### 전이 다이어그램

```
                    ┌────────────────────────────────────────────────────────┐
                    │   detectionRange 내 플레이어 감지 (score 기반 선택)      │
             ┌──────▼──────┐
             │    IDLE     │◄──────────────────────────────────────────────────┐
             └──────┬──────┘                                                    │
                    │ 타겟 존재                                           스폰 위치 도달 (dist < 0.3)
                    ▼                                                            │
        ┌──►┌──────────────┐                                                   │
        │   │    CHASE     │                                                    │
        │   └──────┬───────┘                                                   │
        │          │ dist ≤ attackRange                                         │
        │          ▼                                                             │
        │   ┌──────────────┐                                                   │
        │   │ ATTACK       │  windupTimer 완료                                  │
        │   │ WINDUP       │──── hit(범위 내) or miss(범위 밖) ──►             │
        │   └──────────────┘                         ┌──────────────┐         │
        │                                             │ ATTACK       │         │
        │                                             │ RECOVER      │         │
        │                                             └──┬───────────┘         │
        │                  recoverTimer 만료             │                      │
        │   ┌──── isOvercrowded() ──────────────────────┤                      │
        │   ▼                                            │                      │
        │   ┌──────────────┐                            │                      │
        │   │ REPOSITION   │──► Chase / AttackWindup   │                      │
        │   └──────────────┘                            │                      │
        │                         dist ≤ attackRange ───┘                      │
        └────────────────── dist > attackRange ──────────────────────── RETURN ┘

어느 상태에서든:
  타겟 소실/사망         → RETURN
  isOutsideActivityZone  → RETURN
```

### 전이 조건 상세

#### Idle → Chase

| 조건 |
|---|
| `detectionRange` 내 생존 플레이어 존재. `evaluateTargetScore()`로 최고 점수 타겟 선택. |

**그룹 소속 NPC (groupId ≥ 0)의 추가 행동:**

직접 감지 실패 시 `NpcGroup::getBestMemory()`를 조회한다.
- 유효 메모리 존재 && `!isOutsideActivityZone()` → `Investigate` 전이
- 유효 메모리 존재 && `isOutsideActivityZone()` → `Return`
- 유효 메모리 없음 && 스폰에서 1u 이상 이탈 → `Return`

#### Chase → *

| 전이 대상 | 조건 |
|---|---|
| `AttackWindup` | `dist ≤ attackRange_` |
| `Investigate` | 타겟 소실/사망 && 그룹 유효 메모리 존재 && `!isOutsideActivityZone()` |
| `Return` | 타겟 소실/사망 (그룹 메모리 없거나 활동 구역 이탈) |
| `Return` | `isOutsideActivityZone()` |

Chase 중 0.5s(`TARGET_EVAL_INTERVAL`) 주기로 타겟 재평가. 더 높은 점수의 타겟으로 교체 가능.
Chase 중 그룹에 `reportSight()` 호출 → 다른 그룹원이 공유 메모리를 통해 타겟 위치 파악 가능.

#### AttackWindup → *

NPC는 windupTimer 완료까지 스윙을 commit. 타겟이 이탈해도 취소하지 않는다.

| 전이 대상 | 조건 |
|---|---|
| `AttackRecover` | `windupTimer_` 완료 → `dist ≤ attackRange_` 이면 hit(데미지), 초과 시 miss (둘 다 AttackRecover) |
| `Return` | 타겟 소실/사망, 또는 `isOutsideActivityZone()` |

#### AttackRecover → *

| 전이 대상 | 조건 |
|---|---|
| `Reposition` | `recoverTimer_` 만료 && `isOvercrowded()` (주변 NPC ≥ overlapThreshold) |
| `AttackWindup` | `recoverTimer_` 만료 && `dist ≤ attackRange_` |
| `Chase` | `recoverTimer_` 만료 && `dist > attackRange_` |
| `Return` | 타겟 소실/사망, 또는 `isOutsideActivityZone()` |

#### Reposition → *

| 전이 대상 | 조건 |
|---|---|
| `AttackWindup` | `isOvercrowded()` 해소 && `dist ≤ attackRange_` |
| `Chase` | `isOvercrowded()` 해소 && `dist > attackRange_`, 또는 `REPOSITION_TIMEOUT(1.5s)` 초과 |
| `Return` | 타겟 소실/사망, 또는 `isOutsideActivityZone()` |

#### Return → *

| 전이 대상 | 조건 |
|---|---|
| `Chase` | `canReAggroOnReturn_=true` && `!isOutsideActivityZone()` && `detectionRange_` 내 플레이어 감지 |
| `Idle` | `dist to spawnPos_ < 0.3` |

---

## 2. NPC 주요 파라미터

### NPC 파라미터 (`NpcConfig`)

| 파라미터 | 기본값 | 효과 |
|---|---|---|
| `maxHp` | 80.0 | 최대 HP |
| `moveSpeed` | 4.0 | 이동 속도 (units/s) |
| `detectionRange` | 10.0 | Idle 자율 탐지 반경. Return 중 re-aggro 기준. |
| `attackRange` | 2.0 | 근접 사거리 |
| `activityZoneRadius` | 28.0 | 활동 구역 반경. 이 구역 이탈 시 Return 전이. |
| `attackDamage` | 10.0 | 타격 데미지 |
| `attackWindupTime` | 0.4s | 공격 선딜 시간. 플레이어의 회피 가능 창. |
| `attackRecoverTime` | 0.6s | 공격 후딜 시간 |
| `separationRadius` | 4.0 | 충돌 회피 반경 |
| `separationWeight` | 0.6 | 분리 힘 강도 (추격 방향 대비 비율) |
| `canReAggroOnReturn` | true | 귀환 중 재어그로 허용 여부 |
| `overlapThreshold` | 2 | Reposition 트리거 주변 NPC 수 |
| `returnSpeedMult` | 2.5 | Return 상태 이동 속도 배율 |

### NPC 상수

| 상수 | 값 | 효과 |
|---|---|---|
| `TARGET_EVAL_INTERVAL` | 0.5s | Chase 상태 타겟 재평가 주기 |
| `REPOSITION_TIMEOUT` | 1.5s | Reposition 최대 지속 시간 (초과 시 Chase 강제 전환) |

### 활동 구역 (`activityZone`)

- 기본값: 스폰 위치 중심, 반경 `activityZoneRadius_(=cfg.activityZoneRadius)`
- `setActivityZone(center, radius)`로 외부에서 재설정 가능
- `isOutsideActivityZone()` = `dist(position_, activityZoneCenter_) > activityZoneRadius_`

### 타겟 점수 함수 (`evaluateTargetScore`)

```
score = max(0, (1 − dist / (activityZoneRadius × 2))) × 50  // 거리 점수
      + 20                                                    // 현재 타겟 유지 히스테리시스
      + 15                                                    // dist ≤ attackRange 이면 사거리 내 보너스
      − aggro × 8                                             // 해당 플레이어를 이미 추적 중인 NPC 수 × 패널티
```

### NPC 프리셋 (ScenarioSoloNpc 기준)

| 종류 | speed | detectionRange | attackRange | windupTime | recoverTime | sepRadius | canReAggro |
|---|---|---|---|---|---|---|---|
| Goblin | 5.5 | 12 | 1.8 | 0.3s | 0.6s | 3.5 | true |
| Orc | 3.0 | 8 | 3.0 | 0.6s | 1.4s | 5.0 | false |

---

## 전체 상태 전이 요약

```
Idle        → Chase          : detectionRange 내 플레이어 감지 (score 기반)
Idle        → Investigate   : (그룹) 감지 실패 && 유효 메모리 존재 && 활동 구역 내
Idle        → Return        : (그룹) 활동 구역 이탈 / 메모리 만료 후 이탈
Chase       → AttackWindup  : dist ≤ attackRange
Chase       → Investigate   : (그룹) 타겟 소실 && 유효 메모리 존재 && 활동 구역 내
Chase       → Return        : 타겟 소실 / isOutsideActivityZone
Investigate → Chase         : detectionRange 내 플레이어 감지
Investigate → Return        : 활동 구역 이탈 / 메모리 만료 / 조사 위치 도달 후 플레이어 없음
AttackWindup → AttackRecover : windupTimer 완료 → hit(범위 내) or miss(범위 밖)
AttackWindup → Return        : 타겟 소실 / isOutsideActivityZone
AttackRecover → AttackWindup : 경직 완료, in range, 혼잡 없음
AttackRecover → Chase        : 경직 완료, out of range, 혼잡 없음
AttackRecover → Reposition   : 경직 완료, isOvercrowded()
AttackRecover → Return       : 타겟 소실 / isOutsideActivityZone
Reposition → AttackWindup    : isOvercrowded() 해소 && dist ≤ attackRange
Reposition → Chase           : isOvercrowded() 해소 && dist > attackRange, 또는 REPOSITION_TIMEOUT 초과
Reposition → Return          : 타겟 소실 / isOutsideActivityZone
Return → Chase               : detectionRange 내 플레이어 재감지 (canReAggroOnReturn=true)
Return → Idle                : dist to spawnPos < 0.3
Dead   → (none)              : terminal
```

---

---

## 3. NpcGroup 시야 공유 시스템

### 개요

`NpcGroup`은 경량 시야 공유 그룹이다. 지휘 계층(Squad/Platoon)이 없고 NPC에게 명령을 내리지 않는다.
Room이 소유하며, NPC는 `groupId_`를 통해 조회만 한다.

### SharedTargetMemory

```
struct SharedTargetMemory {
    playerId            -- 추적 대상 플레이어 id (0 = 빈 슬롯)
    reporterNpcId       -- 마지막으로 보고한 NPC id
    lastKnownPosition   -- 마지막 목격 위치
    lastSeenTick        -- 보고된 틱
    expireTick          -- 유효 기한 (lastSeenTick + memoryDurationTick)
    valid               -- 슬롯 유효 여부
}
```

플레이어당 슬롯 1개 (`MaxPlayerCount = 4`). 기본 유효 기간 180 틱 (≈ 3초 @ 60fps).

### 메모리 상세 동작

#### 저장 구조

```cpp
// NpcGroup.hpp
static constexpr int MaxPlayerCount = 4;
std::array<SharedTargetMemory, MaxPlayerCount> memories_{};
```

슬롯 4개짜리 고정 배열. 플레이어 1명당 슬롯 1개를 사용하며, `playerId`로 식별한다.

#### 1. 등록/갱신 — `reportSight()`

```cpp
void NpcGroup::reportSight(uint32_t npcId, uint32_t playerId,
                            const Vec3& pos, uint32_t currentTick) {
    // 1단계: 해당 playerId의 기존 슬롯 탐색
    int slot = -1;
    for (int i = 0; i < MaxPlayerCount; ++i) {
        if (memories_[i].valid && memories_[i].playerId == playerId) {
            slot = i; break;
        }
    }
    // 2단계: 없으면 빈 슬롯 확보
    if (slot == -1) {
        for (int i = 0; i < MaxPlayerCount; ++i) {
            if (!memories_[i].valid) { slot = i; break; }
        }
    }
    if (slot == -1) return;  // 슬롯 부족 (플레이어 5명 이상이면 발생)

    auto& m             = memories_[slot];
    m.playerId          = playerId;
    m.reporterNpcId     = npcId;
    m.lastKnownPosition = pos;
    m.lastSeenTick      = currentTick;
    m.expireTick        = currentTick + memoryDurationTick_;  // 기본 180틱 ≈ 3초
    m.valid             = true;
}
```

NPC가 플레이어를 직접 감지했을 때 `Npc.cpp`의 4곳에서 호출된다.

| 호출 위치 | 시점 |
|---|---|
| `updateIdle()` | 직접 감지 → Chase 전환 직전 |
| `updateChase()` | 매 틱 추격 중 |
| `updateReturn()` | re-aggro 직전 (메모리 갱신 목적, Troubleshooting [15] 참고) |
| `updateInvestigate()` | 직접 감지 → Chase 전환 직전 |

같은 `playerId`의 기존 슬롯이 있으면 **덮어쓴다** (신규 슬롯 생성 없음). `expireTick`이 매번 `currentTick + 180`으로 갱신되므로 NPC가 계속 보는 한 만료되지 않는다.

#### 2. 관리(만료 처리) — `update()`

```cpp
void NpcGroup::update(uint32_t currentTick) {
    for (auto& m : memories_) {
        if (m.valid && currentTick > m.expireTick)
            m = SharedTargetMemory{};  // 슬롯 초기화
    }
}
```

`Room::tick()`에서 **NPC 업데이트 전**에 호출된다. `currentTick > expireTick`이면 해당 슬롯을 기본값으로 리셋(`valid = false`, `playerId = 0`)해 빈 슬롯으로 돌려놓는다. `reportSight()`가 불리지 않으면 180틱 뒤 자동 만료된다.

#### 3. 쿼리 — `getBestMemory()` / `getBestMemoryInsideActivityArea()`

```cpp
// 유효한 메모리 중 가장 최근 것 반환 (위치 무관)
const SharedTargetMemory* NpcGroup::getBestMemory(uint32_t currentTick) const {
    const SharedTargetMemory* best = nullptr;
    for (const auto& m : memories_) {
        if (!m.valid || currentTick > m.expireTick) continue;
        if (!best || m.lastSeenTick > best->lastSeenTick)
            best = &m;
    }
    return best;
}

// 구역 안에 위치한 메모리 중 가장 최근 것만 반환
const SharedTargetMemory* NpcGroup::getBestMemoryInsideActivityArea(uint32_t currentTick) const {
    // getBestMemory()와 동일하나 아래 필터 추가
    if (!isInsideActivityArea(m.lastKnownPosition)) continue;
    ...
}
```

둘 다 `nullptr`을 반환할 수 있다. "아직 유효하지만 구역 밖"인 메모리는 `getBestMemory()`만 반환하고, `getBestMemoryInsideActivityArea()`는 걸러낸다.

**`||` 단락 평가 동작:**

```cpp
if (!best || m.lastSeenTick > best->lastSeenTick)
    best = &m;
```

| `best` 상태 | `!best` | 오른쪽 평가 여부 |
|---|---|---|
| `nullptr` (첫 유효 슬롯) | `true` | 건너뜀 — 어차피 전체가 `true` |
| 이미 설정됨 | `false` | 실행 — `lastSeenTick` 비교해서 결정 |

풀어서 쓰면:

```cpp
if (best == nullptr) {
    best = &m;                               // 첫 유효 슬롯은 무조건 채택
} else if (m.lastSeenTick > best->lastSeenTick) {
    best = &m;                               // 더 최근 슬롯이면 교체
}
```

#### 4. 삭제 — `clearMemory()`

```cpp
void NpcGroup::clearMemory() {
    for (auto& m : memories_) m = SharedTargetMemory{};
}
```

전체 슬롯을 즉시 초기화한다. 현재 코드에서는 호출하는 곳이 없다. Troubleshooting [14]에서 업데이트 순서 경쟁 조건을 일으킨 원인이었으므로 제거됐고, 만료는 `update()`의 자연 소멸에 맡긴다.

#### 흐름 요약

```
NPC가 플레이어 감지
  └─ reportSight()  →  슬롯 등록/갱신, expireTick = now + 180

매 틱 Room::tick()
  └─ NpcGroup::update()  →  expireTick 초과 슬롯 자동 소멸

NPC 상태 판단 시
  ├─ getBestMemory()                    →  구역 무관, 가장 최근 메모리
  └─ getBestMemoryInsideActivityArea()  →  구역 안 메모리만
```

### NpcGroup 라이프사이클

```
Room::tick()
  ├── npcGroup.update(tick)   ← 만료된 메모리 슬롯 초기화
  └── NPC.update(dt, room)
        ├── Npc::update() 진입부: 메모리 위치가 활동 구역 밖 → clearMemory() + Return
        ├── updateIdle():
        │     직접 감지 성공 → reportSight() → Chase
        │     직접 감지 실패 + 유효 메모리 → Investigate
        ├── updateInvestigate():
        │     직접 감지 성공 → reportSight() → Chase
        │     메모리 위치로 이동; 도달 후 플레이어 없음 → Return
        │     메모리 만료 / 활동 구역 이탈 → Return
        └── updateChase():
              추격 중 매 틱 reportSight() 호출
              타겟 소실 + 유효 메모리 존재 → Investigate
```

### Room API

| 메서드 | 설명 |
|---|---|
| `createNpcGroup(center, radius, memoryDurationTick)` | 그룹 생성; Room이 소유. 반환 포인터는 Room 생존 기간 유효. |
| `getNpcGroup(groupId)` | groupId로 그룹 조회 |

### Npc API (그룹 연동)

| 메서드 / 필드 | 설명 |
|---|---|
| `groupId_ (-1)` | -1 = 독립 NPC; ≥ 0 = NpcGroup 소속 |
| `setGroupId(id)` | 그룹 id 설정 |
| `getGroupId()` | 그룹 id 반환 |

NPC를 그룹에 연결하려면 `setGroupId()`와 `NpcGroup::addMember()` 양쪽 모두 호출해야 한다.
`activityZone`과 `NpcGroup`의 center/radius는 **일치**시켜야 한다 — 구역 이탈 판정이 일관되게 유지된다.

### DebugSnapshot 확장

| 필드 | 설명 |
|---|---|
| `DebugNpcEntry::groupId` | -1 = 독립, ≥ 0 = 그룹 소속 |
| `DebugGroupEntry` | groupId, center, radius, hasMemory, memoryX/Z |
| `DebugSnapshot::groups` | `DebugGroupEntry` 벡터 |

### 시각화 (Renderer)

| 요소 | 설명 |
|---|---|
| 그룹 활동 구역 원 | 그룹별 색상 실선 (G0 청록 / G1 황금 / G2 보라 / G3 연두) |
| `G0` / `G1` 레이블 | 구역 원 위쪽 |
| 공유 메모리 위치 마커 | `×` (hasMemory == true 시 표시) |

---

## 4. 전술 NPC 시스템 (TacticalNpc / TacticalSquad / PlatoonLeader)

### 개요

보스 룸처럼 고정된 전투 공간을 위한 **명령 구동 전술 AI 계층**이다.
기존 `Npc` 클래스는 건드리지 않으며, `Actor`를 직접 상속하는 별도 클래스 계층으로 분리된다.

**핵심 설계 원칙:**
- `TacticalNpc`는 `detectionRange` 없음 — 플레이어를 스스로 감지하지 않는다.
- 활성화는 오직 `PlatoonLeader` 명령에만 의존한다.
- 전투 개시 이후 Attack 사이클(Windup/Recover)은 자율적으로 반복한다.
- `PlatoonLeader`는 전투 + 지휘를 겸행한다.

### 클래스 계층

```
Actor
├── Player
├── Npc              ← 기존 (변경 없음)
└── TacticalNpc      ← 신규: Squad 명령 소비 + FSM
    └── PlatoonLeader ← TacticalNpc 상속: 전투 FSM + evaluateTactics()

TacticalSquad        ← 비(非) Actor 코디네이터
  SquadOrder 수신 (from PlatoonLeader)
  → TacticalCommand 발행 (to TacticalNpc members)
```

---

### TacticalNpc 상태 머신

#### 상태 목록 (`TacticalNpcState`)

| 값 | 상태 | 설명 |
|---|---|---|
| 0 | `Idle` | 명령 대기. 자율 감지 없음. 명령이 올 때까지 아무것도 하지 않음. |
| 1 | `Chase` | `EngageTarget` 명령 후 타겟 추격. 분리 힘 블렌드 적용. |
| 2 | `AttackWindup` | 공격 선딜. 이동 없음. `windupTimer_` 완료 시 hit/miss 판정. 타겟 이탈해도 취소 없음. |
| 3 | `AttackRecover` | 공격 후딜. 거의 정지하며 아주 약한 겹침 해소 drift만 허용한다. `recoverTimer_` 완료 후 재공격 또는 PressureWait. |
| 4 | `Flank` | `FlankTarget` 명령. `assignedSlot_`(월드 좌표) 위치까지 이동. 도착 후 Chase 또는 AttackWindup. 타겟이 `abandonDist_` 이상 이탈하면 슬롯 포기 → Chase. |
| 5 | `ChargeThrough` | 쐐기 대형 돌진. 지정 방향으로 통과하며 충돌한 플레이어에게 피해를 준다. |
| 6 | `Confused` | PlatoonLeader 사망 직후 일정 시간 리더 사망 위치 주변에서 방황한다. |
| 7 | `Dead` | 종단 상태. |
| 8 | `HoldSlot` | `DenseHold` 명령. `assignedSlot_` 위치까지 이동 후 제자리 유지. 타겟이 범위 내여도 공격하지 않음 (경계 상태). |
| 9 | `PressureWait` | 공격 슬롯이 가득 찼을 때 타겟을 유지한 채 플레이어 전방의 좁은 탈출 틈을 제외한 주변 외곽에 seed 기반으로 분산 대기한다. 외곽 목표는 진입 시 정한 offset을 유지하고 플레이어 이동량만큼 평행 이동하며, 목표 근처에서 감속/정지하되 보수적인 overlap drift로 겹침 해소를 지속한다. 플레이어와 가까운 후보부터 공격권 예약을 받은 뒤에만 재진입하며, 재진입 추격 중에는 실제 상태는 유지하되 스냅샷/렌더 표시만 `Chase`로 내보낸다. |

#### 상태 전이 다이어그램

```
                          (명령: EngageTarget)
              ┌──────────────────────────────────────┐
              │                                      │
     ┌────────▼────────┐         (명령: Retreat)     │
     │      IDLE       │◄──────────────────── RETURN ┘
     └────────┬────────┘                      ▲
              │ (명령: EngageTarget)           │ 스폰 위치 도달 (dist < 0.3)
              │ (명령: FlankTarget)  ──► FLANK ─┤
              ▼                                │ 슬롯 도달 + 사정거리 이탈
     ┌────────────────┐                        │
  ┌─►│     CHASE      │◄───────────────────────┘
  │  └───────┬────────┘
  │           │ dist ≤ attackRange
  │           ▼
  │  ┌────────────────┐
  │  │ ATTACK WINDUP  │  windupTimer 완료
  │  └───────┬────────┘──── hit(범위 내) or miss(범위 밖) ──►┐
  │           │                                              │
  │           │                                    ┌─────────▼────────┐
  │           │                                    │ ATTACK RECOVER   │
  │           │                                    └────────┬─────────┘
  │           │ recoverTimer 완료                           │
  │           │  dist ≤ attackRange ───────────────────────►│
  └─────────── dist > attackRange ◄────────────────────────┘

AlternateWait: 명령 대기 → 다음 EngageTarget 수신 시 Chase로 전환
Dead: 종단 상태 (alive_ == false)
```

#### 자율 전이 (상태 내부 판단)

| 현재 상태 | 전이 대상 | 조건 |
|---|---|---|
| `Chase` | `AttackWindup` | `dist ≤ attackRange_` && 해당 플레이어 공격권 예약 보유 |
| `Chase` | `PressureWait` | 해당 플레이어 공격권 예약 실패 |
| `Chase` | `Idle` | 타겟 소실/사망 |
| `AttackWindup` | `AttackRecover` | `windupTimer_ ≥ attackWindupTime_` |
| `AttackWindup` | `Idle` | 타겟 소실/사망 (windup 도중) |
| `AttackRecover` | `AttackWindup` | `recoverTimer_` 완료 && `dist ≤ attackRange_` && 해당 플레이어 공격권 예약 보유 |
| `AttackRecover` | `PressureWait` | `recoverTimer_` 완료 && 즉시 재공격 조건이 아님 |
| `AttackRecover` | `Idle` | 타겟 소실/사망 (recover 도중) |
| `Flank` | `AttackWindup` | `assignedSlot_` 도달(dist < 0.5) && `dist to target ≤ attackRange_` && 해당 플레이어 공격권 예약 보유 |
| `Flank` | `Chase` | `assignedSlot_` 도달(dist < 0.5) && 공격권 예약 보유 && 사거리 밖 |
| `Flank` | `PressureWait` | `assignedSlot_` 도달(dist < 0.5) && 공격권 예약 실패 |
| `Flank` | `Chase` | 이동 중 타겟이 `slotRefTargetPos_`에서 `abandonDist_` 이상 이탈 (슬롯 포기) |
| `Flank` | `Idle` | 타겟 소실/사망 (Flank 도중) |
| `PressureWait` | `AttackWindup` | 최소 체류/ID stagger 이후 공격권 예약 보유 && `dist ≤ attackRange_` |
| `PressureWait` | `Idle` | 타겟 소실/사망 |
| `HoldSlot` | `Idle` | 타겟 소실/사망 |
| `AlternateWait` | `Idle` | 타겟 소실/사망 |
| `Return` | `Idle` | `dist to spawnPos_ < 0.3` |
| `Confused` | `Confused` | 명시적인 새 명령이나 사망 전까지 방황 |
| `Dead` | — | 종단 상태 (alive_ == false 감지 즉시) |

#### 명령 구동 전이 (TacticalSquad → TacticalNpc)

매 틱 `update()` 진입부에서 `pendingCmd_`를 소비한다. 명령은 어느 상태에서도 즉시 적용된다.

| 명령 타입 | 전이 대상 | 부수 효과 |
|---|---|---|
| `EngageTarget` | `Chase` | `targetId_` 갱신 |
| `FlankTarget` | `Flank` | `targetId_` + `assignedSlot_`(월드 좌표) + `slotRefTargetPos_` + `abandonDist_` 갱신 |
| `HoldSlot` | `HoldSlot` | `targetId_` + `assignedSlot_`(월드 좌표) 갱신. 도착 후 공격 없이 제자리 유지. |
| `GuardSlot` | `HoldSlot` | `targetId_` + `assignedSlot_` 갱신. 도착 후 가장 가까운 생존 플레이어를 바라봄. |
| `ChargeThrough` | `ChargeThrough` | `chargeDir`, `chargeId`, 충돌 피해 설정 |
| `Idle` | `Idle` | `targetId_ = 0` |
| `Confused` | `Confused` | `targetId_ = 0`, 리더 사망 위치 주변 방황 시작 |

#### TacticalNpcConfig 파라미터

| 파라미터 | 기본값 | 효과 |
|---|---|---|
| `maxHp` | 100.0 | 최대 HP |
| `moveSpeed` | 4.0 | 이동 속도 (units/s) |
| `attackRange` | 2.8 | 일반 TacticalNpc 공격 사정거리. 플레이어 몸에 과하게 붙지 않도록 기존보다 +0.8 조정 |
| `attackDamage` | 15.0 | 타격 데미지 |
| `attackWindupTime` | 0.4s | 공격 선딜 시간 |
| `attackRecoverTime` | 0.8s | 공격 후딜 시간 |
| `separationRadius` | 3.0 | 충돌 회피 감지 반경 |
| `separationWeight` | 0.5 | 분리 힘 강도 (이동 방향 대비 블렌드 비율) |

**`detectionRange` 없음** — TacticalNpc는 플레이어를 스스로 감지하지 않는다.
Return 상태에서도 재어그로 없음.

#### TacticalCommand 구조체

```cpp
struct TacticalCommand {
    TacticalCommandType type             = TacticalCommandType::None;
    uint32_t            targetId         = 0;
    Vec3                slotOffset       = {};     // Flank/HoldSlot: 목적지 월드 좌표
    Vec3                slotRefTargetPos = {};     // 슬롯 계산 시점의 타겟 위치 (유효성 체크)
    float               abandonDist      = 15.f;  // 타겟 이탈 시 슬롯 포기 거리 (Flank 전용)
};
```

`slotRefTargetPos_`와 `abandonDist_`는 FlankTarget 수신 시 저장된다.
Flank 이동 중 매 틱 `dist(target, slotRefTargetPos_) > abandonDist_`이면 Chase로 전환한다.

#### 이동 속도 특성

| 상태 | 속도 배율 | 비고 |
|---|---|---|
| `Return` | × 2.0 | 하드코딩 |
| `Flank` | × `TACTICAL_SPEED_MULT` (2.0) | 전술 슬롯으로 빠르게 전개 |
| `HoldSlot` | × `TACTICAL_SPEED_MULT` (2.0) | 경계 슬롯으로 빠르게 전개 |
| 그 외 | × 1.0 | 기본 속도 |

`TACTICAL_SPEED_MULT`는 `TacticalNpc` 클래스 상수.

#### 슬롯 도착 감지 (`isAtSlot()`)

```
Flank    → 도착 시 상태가 Chase/AttackWindup으로 전이되므로 항상 false 반환
HoldSlot → distToSlot < 0.5 이면 true (도착 후에도 상태 유지)
그 외    → true (슬롯 이동 중 아님)
```

`PlatoonLeader::allMembersArrived(room)`에서 전체 생존 멤버의 `isAtSlot()`을 확인한다.

---

### TacticalSquad

Squad는 비(非) Actor 코디네이터다. 소속 TacticalNpc들의 ID만 보관하며, PlatoonLeader의 `SquadOrder`를 받아 슬롯을 계산하고 각 NPC에 `TacticalCommand`를 발행한다.

#### SquadOrder 타입 (`SquadOrderType`)

| 타입 | 설명 |
|---|---|
| `Idle` | 전투 해제. 멤버 전체에 Idle 명령. |
| `Engage` | 정면 공격. 멤버 전체에 EngageTarget 명령. |
| `FlankLeft` | 좌측 측면 기동. 멤버에게 FlankTarget 명령 + 좌측 슬롯 좌표. 명령 수신 시 1회 계산. |
| `FlankRight` | 우측 측면 기동. 멤버에게 FlankTarget 명령 + 우측 슬롯 좌표. 명령 수신 시 1회 계산. |
| `Encircle` | 포위. 지정된 섹터 각도 범위 내 슬롯에 **HoldSlot** 명령 발행. greedy nearest-slot 할당으로 경로 교차 최소화. 명령 수신 시 1회 계산. |
| `DenseHold` | 밀집 대형 + 현재 위치 유지. 멤버 centroid 기준 그리드 슬롯에 HoldSlot 명령. 명령 수신 시 1회 계산. |
| `DenseAdvance` | 밀집 대형 + 지정 섹터 위치로 전진. `sectorPos` 기준 그리드 슬롯에 FlankTarget 명령. 명령 수신 시 1회 계산. |
| `WedgeCharge` | 쐐기 대형 + 타겟 돌진. V자 슬롯에 FlankTarget 명령. 타겟이 이동하므로 **매 틱** 슬롯 재계산. |
| `AlternateAttack` | 교대 공격. `attackTurn` 순번에 해당하는 멤버만 EngageTarget, 나머지 AlternateWait. |
| `Retreat` | 후퇴. 멤버 전체에 Retreat 명령. |

#### SquadOrder 필드

```cpp
struct SquadOrder {
    SquadOrderType type          = SquadOrderType::Idle;
    uint32_t       targetId      = 0;
    float          sectorAngle   = 0.f;  // Encircle: 이 Squad의 섹터 중심 각도 (라디안)
    float          sectorSpan    = 0.f;  // Encircle: 섹터 폭 (라디안)
    int            attackTurn    = 0;    // AlternateAttack: 공격 순번 (0부터)
    int            totalTurns    = 1;    // AlternateAttack: 전체 순번 수
    float          approachRadius = 5.f; // Flank/Encircle/WedgeCharge: 타겟 기준 접근 반경
    Vec3           leaderPos     = {};   // FlankLeft/Right/WedgeCharge: 방향 계산용 리더 위치
    Vec3           sectorPos     = {};   // DenseAdvance: 부대가 이동할 섹터 월드 좌표
};
```

#### 슬롯 계산

**FlankLeft/Right (`calcFlankSlots`):**
```
dir  = normalize(targetPos − leaderPos)          // 리더→타겟 방향
side = (+dir.z, 0, −dir.x)                       // 좌측 수직 (XZ 평면)
     = (−dir.z, 0, +dir.x)                       // 우측 수직
spacing = memberAttackRange + 1.5

slot[i] = targetPos + side * approachRadius + dir * (i * spacing)
```

**Encircle (`calcEncircleSlots`, center-of-subdivision):**
```
arc   = sectorSpan / count                         // 소구역 폭
start = sectorAngle − sectorSpan * 0.5 + arc * 0.5 // 첫 번째 소구역 중심

slot[i] = targetPos + { cos(start + arc*i), 0, sin(start + arc*i) } * approachRadius
```
경계(0번째, count-1번째)가 아닌 소구역 **중심**에 슬롯을 배치해 인접 Squad 간 동일 위치 중복을 방지한다.
할당: **greedy nearest-slot** — 각 NPC에서 가장 가까운 미사용 슬롯에 순차 배정.

**DenseHold / DenseAdvance (`calcDenseSlots`):**
```
cols    = ceil(sqrt(count))
spacing = max(memberAttackRange * 0.8, 1.2)
right   = (-forward.z, 0, forward.x)             // XZ 평면 우방향

// DenseHold: center = squad centroid,  forward = centroid → target 방향
// DenseAdvance: center = sectorPos,     forward = sectorPos → playerCentroid 방향
slot[i] = center + right * colOffset + forward * rowOffset
          // colOffset/rowOffset: 직사각형 그리드, 중심 정렬
```

**WedgeCharge (`calcWedgeSlots`):**
```
spacing = max(memberAttackRange * 1.2, 1.5)
forward = normalize(targetPos − fromPos)          // fromPos = 리더 위치
tip     = targetPos − forward * memberAttackRange  // 첨단 = 사정거리 바로 앞

// 행 0: 1명, 행 1: 2명, 행 2: 3명, ... (V자 대형)
slot    = tip − forward * (row * spacing * 1.5) + right * colOffset
```

#### update() 슬롯 갱신 정책

| 명령 타입 | 갱신 시점 | 이유 |
|-----------|-----------|------|
| `FlankLeft / FlankRight` | 명령 수신 시 1회 (`orderDirty_`) | 의도적 플랭크 위치 고정 |
| `Encircle` | 명령 수신 시 1회 | 포위 위치 고정 |
| `DenseHold` | 명령 수신 시 1회 | 경계 위치 고정 |
| `DenseAdvance` | 명령 수신 시 1회 (`orderDirty_`) | PlatoonLeader가 사이클 시작 또는 쿨타임 종료 시에만 재발행 |
| `WedgeCharge` | **매 틱** | 돌진 — 타겟이 이동하므로 추적 필요 |

```
1. removeDeadMembers()
2. orderDirty_ == true → pushCommandsToMembers(); orderDirty_ = false
3. 현재 명령이 WedgeCharge → 매 틱 pushCommandsToMembers() (타겟 위치 재조회)
```

#### PlatoonLeader 사망 처리

```cpp
void TacticalSquad::pushConfusedToMembers(Room& room) {
    // 소속 멤버 전체에 Confused 명령 발행
}
```

PlatoonLeader의 `update()`에서 `alive_`가 false로 바뀌는 틱에 `deathReported_` 플래그로 1회만 호출된다. Confused 명령을 받은 TacticalNpc는 즉시 `Confused(6)` 상태로 전환되어 리더 사망 위치 주변을 방황한다. 각 Squad는 6초 뒤 리더 없는 난투 모드로 전환하고, Squad 중심에서 가장 가까운 생존 플레이어에게 `EngageTarget`을 발행한다.

---

### PlatoonLeader

`TacticalNpc`를 상속하며 전투 FSM과 Squad 지휘를 겸행한다.

#### 핵심 설계

- **전투 겸행**: 자체 Chase/AttackWindup/AttackRecover 사이클을 동시에 실행한다.
- **명령 간섭 차단**: 매 틱 `pendingCmd_.type = None` 설정 후 `TacticalNpc::update()` 호출 → TacticalSquad의 명령이 리더 자신의 FSM에 영향을 주지 않는다.
- **항상 플레이어 인식**: 보스 룸 = 전체 활동 구역. `detectionRange` 없이 `room.getLivingPlayers()` 전부 평가.

#### 전술 발동 조건 (`checkTacticsConditions`)

전술은 기본적으로 비활성(`tacticsUnlocked_ = false`). 아래 중 하나가 충족되면 **영구 활성화**된다.

| 조건 | 임계값 |
|---|---|
| 리더 HP ≤ `maxHp * TACTIC_HP_THRESHOLD` | 70% 이하 |
| 어느 Squad든 생존 비율 < `TACTIC_SQUAD_RATIO` | 초기 인원의 80% 미만 |

조건 충족 전에는 모든 Squad에 **Engage**만 발행한다.

#### TacticalPhase 상태 전이

```
Encircle ──(플레이어 분산 감지)──► Vigilance ──(5초 경과)──► DivideAndConquer
    ▲                                                              │
    └──────────────────(플레이어 집합 감지)◄──────────────────────┘
```

| 페이즈 | Squad 명령 | 조건 |
|---|---|---|
| `Encircle` | Encircle 명령 → HoldSlot (인원 비율 비례 섹터, greedy nearest-slot, 반경 20) | 플레이어 군집 수 = 1 |
| `Vigilance` | DenseHold (전체 현 위치 유지) | 플레이어 군집 수 ≥ 2 (최초 전환 시 5초 대기 시작) |
| `DivideAndConquer` | Squad[0]: WedgeCharge / Squad[1,2]: DenseHold | Vigilance 5초 경과 |

**분산 판단**: `clusterPlayers()` — O(N²) 연결 컴포넌트. 플레이어 간 거리 ≤ `CLUSTER_RADIUS(10)` 이면 같은 군집으로 판정.

**포위 슬롯 고정**: `encircleSlotsAssigned_` 플래그로 한 사이클 내 슬롯을 **1회만** 발행한다.
플레이어가 이동해도 슬롯은 재할당되지 않으며, 쿨타임 종료 후 다음 사이클 시작 시에만 새 위치로 재발행한다.

#### evaluateTactics() — 전술 평가 (1초 주기)

```
1. removeDeadMembers() — 사망 멤버 제거 후 liveSquads 수집
2. selectPrimaryTarget(room) — 없으면 전체 Idle, 리턴
3. 리더 자신: targetId_ 갱신, Idle/Return이면 Chase 전환
4. checkTacticsConditions() → tacticsUnlocked_ = true (조건 충족 시, 단방향)
5. tacticsUnlocked_ == false || tacticsOnCooldown_ → 전체 Engage, 리턴

6. scattered = (clusterPlayers(room) >= 2)

7. if (!scattered):                          // 포위
       isNewPhase = (tacticalPhase_ != Encircle)
       if (isNewPhase || !encircleSlotsAssigned_):
           tacticalPhase_ = Encircle; encircleSlotsAssigned_ = true
           Encircle 명령 발행: 인원 비율 비례 섹터 각도/폭 계산
             sectorSpan_i = 2π × (memberCount_i / totalMembers)
             sectorAngle_i = 누적 각도 + sectorSpan_i * 0.5
             approachRadius = ENCIRCLE_RADIUS (20.0)
           → 각 Squad의 TacticalSquad가 HoldSlot 명령으로 변환 (greedy nearest-slot)
       // 슬롯 발행 완료 → 재발행 없음 (플레이어 이동 시에도 슬롯 고정)

8. else:                                     // 분산
       Encircle → Vigilance 전환: DenseHold 발행, vigilanceElapsed_ = 0
       Vigilance 유지: vigilanceElapsed_ >= 5.0 → DivideAndConquer 전환
       DivideAndConquer: Squad[0] WedgeCharge, Squad[1,2] DenseHold (매 evaluate 갱신)
```

#### 플레이어 점수 함수

```cpp
float score = distScore * 0.5f + hpScore * 0.5f;

distScore = 1.0f / (1.0f + dist)     // 가까울수록 높음
hpScore   = 1.0f - (hp / maxHp)      // HP 낮을수록 높음
```

최고 점수 플레이어 → `primaryTargetId_`.

#### PlatoonLeader 파라미터 상수

| 상수 | 값 | 설명 |
|---|---|---|
| `TACTIC_INTERVAL` | 1.0s | evaluateTactics() 호출 주기 |
| `APPROACH_RADIUS` | 4.5 | 슬롯 배치 반경 (타겟 기준, FlankLeft/Right용) |
| `VIGILANCE_DURATION` | 5.0s | Vigilance → DivideAndConquer 전환 시간 |
| `CLUSTER_RADIUS` | 10.0 | 플레이어 분산 판단 반경 |
| `ENCIRCLE_RADIUS` | 20.0 | 포위 섹터 배치 반경 |
| `TACTIC_HP_THRESHOLD` | 0.70 | 리더 HP 70% 이하 시 전술 발동 |
| `TACTIC_SQUAD_RATIO` | 0.80 | 부대원 80% 미만 생존 시 전술 발동 |
| `TACTIC_COOLDOWN_DURATION` | 8.0s | 쿨타임 길이. 전체 멤버 슬롯 도착 즉시 진입. Engage 복귀, 슬롯 초기화. |

#### 전술 쿨타임 시스템

전술이 한 번 발동된 이후에는 아래 사이클을 반복한다.

```
[전술 활성]
  │ 슬롯 발행 (encircleSlotsAssigned_ = true)
  │ NPC들 HoldSlot 명령으로 슬롯 위치 이동
  ↓
[전원 슬롯 도착 — allMembersArrived() == true]
  │ 즉시: tacticsOnCooldown_ = true
  │        encircleSlotsAssigned_ = false   ← 다음 사이클용 슬롯 초기화
  │        모든 Squad → Engage (정면 공격 복귀)
  ↓
[쿨타임 — TACTIC_COOLDOWN_DURATION (8초)]
  ↓
[쿨타임 종료]
  │ tacticsOnCooldown_ = false
  └→ 다음 evaluateTactics() 에서 !encircleSlotsAssigned_ 감지
       → 현재 플레이어 위치 기준 새 슬롯 발행
```

**핵심 보장:**
- 슬롯은 한 사이클 내에서 고정 — 플레이어 이동과 무관
- 슬롯 도착 즉시 쿨타임 진입 (타이머 대기 없음)
- 쿨타임 종료 시 현재 플레이어 위치를 기준으로 새 포위 슬롯 재발행

---

### 명령 흐름

```
PlatoonLeader::evaluateTactics()   (매 1초)
  │
  │  SquadOrder { type, targetId, leaderPos, sectorAngle, approachRadius, ... }
  ▼
TacticalSquad::receiveOrder()
  │
  │  슬롯 계산 (매 틱, FlankLeft/Right/Encircle은 타겟 이동 반영)
  │
  │  TacticalCommand { type, targetId, slotOffset }
  ▼
TacticalNpc::receiveCommand()   →  pendingCmd_ 저장
  │
  ▼
TacticalNpc::update() 진입부
  └─ consumePendingCommand()  →  상태 전이
```

**Room::tick() 내 업데이트 순서:**

```
7a. updatePlatoonLeaders(dt)
    — PlatoonLeader::update(): evaluateTactics() + 자체 전투 FSM
7b. updateTacticalSquads(dt)
    — TacticalSquad::update(): 사망 멤버 제거 + 슬롯 재계산 + TacticalCommand 발행
7c. updateTacticalNpcMembers(dt)
    — TacticalNpc::update(): pendingCmd_ 소비 + FSM 실행
    — PlatoonLeader는 typeName() 검사로 이 단계에서 제외 (7a에서 이미 처리됨)
```

---

### Room API (전술 NPC)

| 메서드 | 설명 |
|---|---|
| `addTacticalNpc(shared_ptr<TacticalNpc>)` | TacticalNpc 등록. `actors_`와 `tacticalNpcs_` 양쪽에 추가. |
| `addTacticalSquad(unique_ptr<TacticalSquad>)` | Squad 등록. Room이 소유. 반환 포인터는 Room 생존 기간 유효. |
| `registerPlatoonLeader(PlatoonLeader*)` | 리더 포인터를 `platoonLeaders_` 에 등록 (비소유). |
| `findActorById(id)` | `tacticalNpcs_`도 포함해 검색. |

TacticalNpc는 `actors_`와 `tacticalNpcs_` **양쪽**에 동시 등록되어 `findActorById()`와 전술 전용 반복 모두 지원한다.

---

### DebugSnapshot 확장

```cpp
struct DebugTacticalNpcEntry {
    uint32_t    id;
    float       x, z;
    float       dirX, dirZ;
    int         state;           // TacticalNpcState int 값
    uint32_t    targetId;
    std::string name;
    float       hp, maxHp;
    float       attackRange;
    bool        alive;
    float       homeX, homeZ;
    float       windupProgress;  // [0,1] — 렌더러 프로그레스 바용
    float       recoverProgress; // [0,1]
    int         squadId;
    bool        isLeader;
    float       slotX, slotZ;   // Flank 상태 목적지
};

// DebugSnapshot에 추가
std::vector<DebugTacticalNpcEntry> tacticalNpcs;
```

---

### 시각화 (Renderer)

#### 상태별 색상 (`tacticalStateColor`)

| 상태 | 색상 | RGB |
|---|---|---|
| `Idle(0)` | 회색 | (128, 128, 128) |
| `Chase(1)` | 빨강 | (220, 50, 50) |
| `AttackWindup(2)` | 주황 | (255, 165, 0) |
| `AttackRecover(3)` | 진주황 | (200, 100, 0) |
| `Flank(4)` | 청록 | (0, 200, 220) |
| `ChargeThrough(5)` | 파랑 | (50, 80, 220) |
| `Confused(6)` | 연보라 | (170, 120, 255) |
| `Dead(7)` | 거의 검정 | (40, 40, 40) |
| `HoldSlot(8)` | 노랑 | (255, 220, 0) |
| `PressureWait(9)` | 하늘색 | (80, 180, 255) |

`PressureWait`는 플레이어별 실공격자(`AttackWindup`/`AttackRecover`)와 예약자를 합쳐 5명 이하가 되도록 `Room`의 공격권 예약을 사용한다. 예약자는 같은 플레이어를 노리는 일반 `TacticalNpc` 후보 중 플레이어와 가까운 후보를 우선하되, 기존 예약자는 약간의 거리 마진 안에서 보존해 매 tick 교체되지 않게 한다. 슬롯 증가로 외곽 대기 인원 자체를 줄이고, 남은 외곽 대기자는 전방 좁은 cone을 제외한 넓은 각도와 반경 offset에 seed 기반으로 흩어진다. 플레이어가 움직일 때는 새 자리를 뽑지 않고 기존 상대 위치를 따라가며, 겹친 경우에는 안쪽으로 파고들지 않는 drift만 적용해 현재 위치 근처에서 벌어진다. 예약을 받은 재진입자는 실제 FSM 상태를 `PressureWait`로 유지하지만, `DebugTacticalNpcEntry.state`는 `Chase(1)`로 기록되어 화면에는 추격 상태로 보인다.

#### drawTacticalNpc() 시각화 요소

| 요소 | 조건 | 설명 |
|---|---|---|
| 상태 색상 원 | 항상 | 상태에 따른 색상 원형 |
| 이중 링 (금색) | `isLeader == true` | 외곽 링(반경+5px)을 금색(255,200,0)으로 추가 표시 |
| 점선 (슬롯 방향) | `state == Flank(4)` | NPC → `assignedSlot_` 방향 점선 |
| 타겟 방향 선 | `targetId != 0` | NPC → 타겟 연결선 |
| Windup/Recover 바 | 해당 상태 시 | 진행 상황 표시 바 |
| `[L]` 접두사 레이블 | `isLeader == true` | 이름 앞에 리더 표시 |

---

### 시나리오 (ScenarioTactical)

```
P1 (HumanControl)  at (0, 0, 0)
Boss (PlatoonLeader, HP=200)  at (25, 0, 0)
  Squad A (20명): A1~A20 at (20~22, 0, -3~-21)   squadId=0  — 우상단 2열
  Squad B (20명): B1~B20 at (26~30, 0, -4~+5)    squadId=1  — 정면 그리드
  Squad C (20명): C1~C20 at (20~22, 0, +3~+21)   squadId=2  — 우하단 2열
총 61명 (Boss 포함)
```

**기본 동작 (tacticsUnlocked_ == false)**: 모든 Squad가 Engage 발행.

**전술 발동 조건**: Boss HP ≤ 70% 또는 어느 Squad든 초기 인원의 80% 미만 생존.

**전술 발동 후 (플레이어 집합 = 포위)**:
- Squad A/B/C → Encircle 명령 (인원 비율 비례 섹터, 반경 20에 HoldSlot)
  - 슬롯 도달 후 플레이어 방향 facing 유지, 공격 없음
  - 전원 도달 즉시 8초 쿨타임 → Engage 복귀 → 재포위
- Boss → 정면 Chase + Attack

**플레이어 분산 시 (clusterPlayers ≥ 2)**:
- Vigilance → 전체 DenseHold (최대 5초)
- DivideAndConquer → Squad A(WedgeCharge), Squad B/C(DenseHold)
