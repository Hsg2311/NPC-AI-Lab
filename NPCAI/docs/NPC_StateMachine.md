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
| 0 | `Idle` | 대기. `detectionRange` 내 플레이어 자율 감지 후 Chase 진입. |
| 1 | `Chase` | 타겟 추격. 분리 힘(separation force)과 추격 방향 블렌드로 이동. |
| 2 | `AttackWindup` | 공격 선딜 (이동 없음). `windupTimer` 완료 시 hit/miss 판정. 타겟 이탈해도 취소 없음. |
| 3 | `AttackRecover` | 공격 후딜. 약한 separation drift 허용. |
| 4 | `Return` | 스폰 위치로 귀환. `returnSpeedMult_` 배율 적용. |
| 5 | `Reposition` | 과밀 탈출 비켜서기. 타겟 방향 + 수직 이탈 블렌드 이동. |
| 6 | `Dead` | 종단 상태. |

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

#### Chase → *

| 전이 대상 | 조건 |
|---|---|
| `AttackWindup` | `dist ≤ attackRange_` |
| `Return` | 타겟 소실/사망 |
| `Return` | `isOutsideActivityZone()` |

Chase 중 0.5s(`TARGET_EVAL_INTERVAL`) 주기로 타겟 재평가. 더 높은 점수의 타겟으로 교체 가능.

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
Idle   → Chase          : detectionRange 내 플레이어 감지 (score 기반)
Chase  → AttackWindup   : dist ≤ attackRange
Chase  → Return         : 타겟 소실 / isOutsideActivityZone
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
