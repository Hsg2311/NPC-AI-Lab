# NPC AI 상태 전이 문서

## 목차
1. [NPC 상태 머신](#1-npc-상태-머신)
2. [주요 파라미터](#2-주요-파라미터)

> **2026-04-26:** Squad / Platoon 계층 전면 제거. 모든 NPC는 단독 행동(standalone).
> 구 2~4절(분대 / 소대 / 연동 메커니즘) 삭제.

---

## 1. NPC 상태 머신

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

## 2. 주요 파라미터

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
