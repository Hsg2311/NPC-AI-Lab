# NPC AI 상태 전이 문서

## 목차
1. [NPC 상태 머신 (기존 Npc 클래스)](#1-npc-상태-머신-기존-npc-클래스)
2. [NPC 주요 파라미터](#2-npc-주요-파라미터)
3. [NpcGroup 시야 공유 시스템](#3-npcgroup-시야-공유-시스템)
4. [전술 NPC 시스템 (TacticalNpc / TacticalSquad / PlatoonLeader)](#4-전술-npc-시스템)

> **2026-04-26:** Squad / Platoon 계층 전면 제거. 모든 Npc 클래스 인스턴스는 단독 행동(standalone).
> **2026-05-01:** 전술 NPC 시스템 추가 (섹션 4). 기존 Npc 클래스는 변경 없음.

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
| 3 | `AttackRecover` | 공격 후딜. 약한 separation drift 허용. `recoverTimer_` 완료 후 재공격 또는 Chase. |
| 4 | `Flank` | `FlankTarget` 명령. `assignedSlot_`(월드 좌표) 위치까지 이동. 도착 후 Chase 또는 AttackWindup. |
| 5 | `AlternateWait` | 교대 공격에서 자기 순번이 아닌 대기 상태. 다음 EngageTarget 명령 수신 시 전환. |
| 6 | `Return` | `Retreat` 명령 또는 타겟 소실 시 `spawnPos_`로 귀환. |
| 7 | `Dead` | 종단 상태. |

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
| `Chase` | `AttackWindup` | `dist ≤ attackRange_` |
| `Chase` | `Idle` | 타겟 소실/사망 |
| `AttackWindup` | `AttackRecover` | `windupTimer_ ≥ attackWindupTime_` |
| `AttackWindup` | `Idle` | 타겟 소실/사망 (windup 도중) |
| `AttackRecover` | `AttackWindup` | `recoverTimer_` 완료 && `dist ≤ attackRange_` |
| `AttackRecover` | `Chase` | `recoverTimer_` 완료 && `dist > attackRange_` |
| `AttackRecover` | `Idle` | 타겟 소실/사망 (recover 도중) |
| `Flank` | `AttackWindup` | `assignedSlot_` 도달(dist < 0.5) && `dist to target ≤ attackRange_` |
| `Flank` | `Chase` | `assignedSlot_` 도달(dist < 0.5) && `dist to target > attackRange_` |
| `Flank` | `Idle` | 타겟 소실/사망 (Flank 도중) |
| `AlternateWait` | `Idle` | 타겟 소실/사망 |
| `Return` | `Idle` | `dist to spawnPos_ < 0.3` |
| `Dead` | — | 종단 상태 (alive_ == false 감지 즉시) |

#### 명령 구동 전이 (TacticalSquad → TacticalNpc)

매 틱 `update()` 진입부에서 `pendingCmd_`를 소비한다. 명령은 어느 상태에서도 즉시 적용된다.

| 명령 타입 | 전이 대상 | 부수 효과 |
|---|---|---|
| `EngageTarget` | `Chase` | `targetId_` 갱신 |
| `FlankTarget` | `Flank` | `targetId_` + `assignedSlot_`(월드 좌표) 갱신 |
| `AlternateWait` | `AlternateWait` | `targetId_` 갱신 |
| `Retreat` | `Return` | — |
| `Idle` | `Idle` | `targetId_ = 0` |
| `Confused` | `Idle` | `targetId_ = 0` (PlatoonLeader 사망 시 발행) |

#### TacticalNpcConfig 파라미터

| 파라미터 | 기본값 | 효과 |
|---|---|---|
| `maxHp` | 100.0 | 최대 HP |
| `moveSpeed` | 4.0 | 이동 속도 (units/s) |
| `attackRange` | 2.0 | 공격 사정거리 |
| `attackDamage` | 15.0 | 타격 데미지 |
| `attackWindupTime` | 0.4s | 공격 선딜 시간 |
| `attackRecoverTime` | 0.8s | 공격 후딜 시간 |
| `separationRadius` | 3.0 | 충돌 회피 감지 반경 |
| `separationWeight` | 0.5 | 분리 힘 강도 (이동 방향 대비 블렌드 비율) |

**`detectionRange` 없음** — TacticalNpc는 플레이어를 스스로 감지하지 않는다.
Return 상태에서도 재어그로 없음.

#### Return 이동 특성

`Return` 상태에서 이동 속도는 `moveSpeed_ * 2.0`이 적용된다. 분리 힘은 `separationWeight * 0.25` 배율로 약화된다.

---

### TacticalSquad

Squad는 비(非) Actor 코디네이터다. 소속 TacticalNpc들의 ID만 보관하며, PlatoonLeader의 `SquadOrder`를 받아 슬롯을 계산하고 각 NPC에 `TacticalCommand`를 발행한다.

#### SquadOrder 타입 (`SquadOrderType`)

| 타입 | 설명 |
|---|---|
| `Idle` | 전투 해제. 멤버 전체에 Idle 명령. |
| `Engage` | 정면 공격. 멤버 전체에 EngageTarget 명령. |
| `FlankLeft` | 좌측 측면 기동. 멤버에게 FlankTarget 명령 + 좌측 슬롯 좌표. |
| `FlankRight` | 우측 측면 기동. 멤버에게 FlankTarget 명령 + 우측 슬롯 좌표. |
| `Encircle` | 포위. 지정된 섹터 각도 범위 내 슬롯에 FlankTarget 발행. |
| `AlternateAttack` | 교대 공격. `attackTurn` 순번에 해당하는 멤버만 EngageTarget, 나머지 AlternateWait. |
| `Retreat` | 후퇴. 멤버 전체에 Retreat 명령. |

#### SquadOrder 필드

```cpp
struct SquadOrder {
    SquadOrderType type        = SquadOrderType::Idle;
    uint32_t       targetId    = 0;
    float          sectorAngle = 0.f;   // Encircle: 이 Squad의 섹터 중심 각도 (라디안)
    float          sectorSpan  = 0.f;   // Encircle: 섹터 폭 (라디안)
    int            attackTurn  = 0;     // AlternateAttack: 공격 순번 (0부터)
    int            totalTurns  = 1;     // AlternateAttack: 전체 순번 수
    float          approachRadius = 5.f; // Flank/Encircle: 타겟 기준 접근 반경
    Vec3           leaderPos   = {};    // FlankLeft/Right: 방향 계산용 리더 위치
};
```

#### 슬롯 계산

**FlankLeft/Right:**
```
dir  = normalize(targetPos − leaderPos)          // 리더→타겟 방향
side = (+dir.z, 0, −dir.x)                       // 좌측 수직 (XZ 평면)
     = (−dir.z, 0, +dir.x)                       // 우측 수직
spacing = memberAttackRange + 1.5

slot[i] = targetPos
         + side    * approachRadius
         + dir     * (i * spacing)                // 타겟에 가까운 멤버 순서
```

**Encircle (멤버 2명 이상):**
```
arc   = sectorSpan / (count − 1)
start = sectorAngle − sectorSpan * 0.5

slot[i] = targetPos + { cos(start + arc*i), 0, sin(start + arc*i) } * approachRadius
```
멤버 1명이면 `{ cos(sectorAngle), 0, sin(sectorAngle) } * approachRadius`로 단일 슬롯.

#### update() 처리 순서

1. `removeDeadMembers()` — `findActorById(id)->isAlive()` 검사, 사망 NPC ID 제거
2. 새 명령(`orderDirty_`)이 있거나 Flank/Encircle 유형이면 `pushCommandsToMembers()` 호출
3. `pushCommandsToMembers()` 내에서 타겟 위치를 매 틱 재조회 → 슬롯 갱신 (타겟 이동 반영)

Flank/Encircle 유형은 `orderDirty_` 없이도 매 틱 슬롯을 재계산해 이동 중인 타겟을 추적한다.

#### PlatoonLeader 사망 처리

```cpp
void TacticalSquad::pushConfusedToMembers(Room& room) {
    // 소속 멤버 전체에 Confused 명령 발행
}
```

PlatoonLeader의 `update()`에서 `alive_`가 false로 바뀌는 틱에 `deathReported_` 플래그로 1회만 호출된다. Confused 명령을 받은 TacticalNpc는 즉시 `Idle`로 전환된다.

---

### PlatoonLeader

`TacticalNpc`를 상속하며 전투 FSM과 Squad 지휘를 겸행한다.

#### 핵심 설계

- **전투 겸행**: 자체 Chase/AttackWindup/AttackRecover 사이클을 동시에 실행한다.
- **명령 간섭 차단**: 매 틱 `pendingCmd_.type = None` 설정 후 `TacticalNpc::update()` 호출 → TacticalSquad의 명령이 리더 자신의 FSM에 영향을 주지 않는다.
- **항상 플레이어 인식**: 보스 룸 = 전체 활동 구역. `detectionRange` 없이 `room.getLivingPlayers()` 전부 평가.

#### evaluateTactics() — 전술 평가 (1초 주기)

```
1. selectPrimaryTarget(room)  →  점수 기반 primary 선택
2. 살아있는 Squad 수에 따른 전술 결정:
   Squad 1개  →  Engage  (정면 공격)
   Squad 2개  →  FlankLeft + FlankRight  (좌/우 협공)
   Squad 3개+ →  Encircle  (360° / N 균등 분할, sectorAngle = span * i)
3. 각 Squad에 SquadOrder 발행 (receiveOrder)
4. 리더 자신: targetId_ 갱신, Idle/Return 상태이면 Chase로 전환
```

플레이어 없음 또는 활성 Squad 없음: 전체 Squad에 Idle 명령, 리더 자신도 Idle/Return 유지.

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
| `APPROACH_RADIUS` | 4.5 | 슬롯 배치 반경 (타겟 기준) |

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
| `AlternateWait(5)` | 파랑 | (50, 80, 220) |
| `Return(6)` | 초록 | (50, 180, 50) |
| `Dead(7)` | 거의 검정 | (40, 40, 40) |

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
P1 (HumanControl)  at (-10, 0, 0)
Boss (PlatoonLeader, HP=200)  at (15, 0, 0)
  Squad A: SoldierA1(12,0,-3)  SoldierA2(12,0,-6)   squadId=0
  Squad B: SoldierB1(12,0, 3)  SoldierB2(12,0, 6)   squadId=1
```

Squad 2개이므로 PlatoonLeader는 FlankLeft + FlankRight를 발행한다.
- Squad A → 좌측 측면 기동(cyan)
- Squad B → 우측 측면 기동(cyan)
- Boss → 정면 Chase + Attack
