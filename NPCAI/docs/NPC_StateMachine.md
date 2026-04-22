# NPC AI 상태 전이 문서

## 목차
1. [NPC 개별 상태 머신](#1-npc-개별-상태-머신)
2. [분대(Squad) 상태 머신](#2-분대squad-상태-머신)
3. [소대(Platoon) 계층](#3-소대platoon-계층)
4. [NPC ↔ Squad ↔ Platoon 연동 메커니즘](#4-npc--squad--platoon-연동-메커니즘)
5. [주요 파라미터](#5-주요-파라미터)

---

## 1. NPC 개별 상태 머신

### 상태 목록 (`NpcState`)

| 값 | 상태 | 설명 |
|---|---|---|
| 0 | `Idle` | 대기. **분대 소속 NPC는 자율 탐지 불가** — `EngageTarget` 명령 대기. |
| 1 | `Chase` | 타겟 추격 |
| 2 | `AttackWindup` | 공격 선딜 (이동 없음) |
| 3 | `AttackRecover` | 공격 후딜 |
| 4 | `Return` | 스폰 위치로 귀환 (standalone NPC 전용) |
| 5 | `Reposition` | 과밀 탈출 비켜서기. 타겟 방향 + 수직 이탈 방향 블렌드로 이동. |
| 6 | `Regroup` | 분대 타겟 방향으로 합류 이동 (standalone NPC 레거시) |
| 7 | `Confused` | 리더 사망 후 혼란. 타겟 없이 방향 진동 이동 (`CONFUSION_DURATION = 3s`) |
| 8 | `MoveToSlot` | 대형 슬롯 위치로 이동 (Formation 확장 예약) |
| 9 | `Retreat` | 명령 기반 후퇴. `destination`까지 이동 후 Idle |
| 10 | `Dead` | 종단 상태 |

### 핵심 행동 원칙

- **분대 소속 NPC (`squadId_ != -1`)**: `Idle`에서 자율 aggro 불가. `NpcCommand::EngageTarget` 없이 Chase 진입 불가.
- **standalone NPC (`squadId_ == -1`)**: 기존과 동일하게 탐지 범위 내 플레이어 자율 추적.
- **타겟 소실 시**: 분대 소속 → `Idle` 대기, standalone → `Return` (스폰 귀환).

### 전이 다이어그램

```
                    ┌──────────────────────────────────────────────────────┐
                    │  [EngageTarget cmd] OR standalone & 탐지 범위 내 적   │
             ┌──────▼──────┐
             │    IDLE     │◄──────────────────────────────────────────────────┐
             └──────┬──────┘                                                    │
                    │ 타겟 존재                                                  │ 스폰 위치 도달 (dist < 0.3)
                    ▼                                                            │
             ┌──────────────┐   타겟 사거리 이탈 (> attackRange×1.2)            │
        ┌───►│    CHASE     │◄─────────────────────────────────┐               │
        │    └──────┬───────┘                                  │               │
        │           │ dist ≤ attackRange                       │               │
        │           ▼                                          │               │
        │    ┌──────────────┐   dist > attackRange × 1.2      │               │
        │    │ ATTACK       ├──────────────────────────────────┘               │
        │    │ WINDUP       │                                                   │
        │    └──────┬───────┘                                                   │
        │           │ windupTimer 완료 → hit(범위 내) or miss(범위 밖)          │
        │           ▼                                                            │
        │    ┌──────────────┐                                                   │
        │    │ ATTACK       │                                                   │
        │    │ RECOVER      │                                                   │
        │    └──┬───┬───────┘                                                   │
        │       │   │ recoverTimer 만료                                          │
        │       │   ├── [isOvercrowded()] ──► REPOSITION ──► Chase / Windup    │
        │       │   ├── [dist ≤ attackRange] ──► AttackWindup                  │
        │       │   └── [dist > attackRange] ──► Chase                         │
        │       │                                                                │
        │    ┌──▼────────────────┐   standalone & squadTargetId_ == 0          │
        │    │   REGROUP         ├──────────────────────────────────────────────┘
        │    │  (standalone only)│
        │    └──────────────┬────┘
        │                   │ dist ≤ chaseRange
        └───────────────────┘

어느 상태에서든:
  타겟 소실/사망 & 분대 소속 (squadId_ != -1) → IDLE
  타겟 소실/사망 & standalone (squadId_ == -1) → RETURN
  dist > maxChaseDistance (26) → RETURN or REGROUP
  [NpcCommand::Confused 수신] → CONFUSED
  [NpcCommand::Retreat 수신]  → RETREAT
```

### 명령 기반 전이

```
[NpcCommand::EngageTarget(targetId)]
  Idle / Return 에서 → Chase (target = targetId)
  Chase 중이면 타겟만 교체

[NpcCommand::Confused]
  어느 상태에서든 → Confused
    confusedTimer_ 진행, 방향 sinf(timer×2.5) 진동, 이동속도 50%
    confusedTimer_ ≥ 3.0s → Idle

[NpcCommand::Retreat(destination, speedMultiplier)]
  어느 상태에서든 → Retreat
    destination 방향으로 speedMultiplier 속도 이동
    dist < 0.3 → Idle

[NpcCommand::HoldSlot]
  → Idle (현 위치 대기)

[NpcCommand::Idle]
  → Idle
```

### 전이 조건 상세

#### Idle → Chase

| 조건 | 비고 |
|---|---|
| `NpcCommand::EngageTarget` 수신 | 분대/standalone 공통. 명령 우선. |
| 탐지 범위(`detectionRange = 10`) 내 플레이어 존재 | **standalone NPC 전용.** 분대 소속 NPC는 자율 aggro 불가. |

#### Chase → *

| 전이 대상 | 조건 |
|---|---|
| `AttackWindup` | `dist ≤ attackRange_` |
| `Idle` | 타겟 소실/사망 && 분대 소속 (`squadId_ != -1`) |
| `Regroup` | 타겟 소실/사망 && standalone && `squadTargetId_ != 0` |
| `Return` | 타겟 소실/사망 && standalone && `squadTargetId_ == 0` |
| `Return / Regroup` | `dist > maxChaseDistance_` 또는 `dist > chaseRange_` |

#### AttackWindup → *

NPC는 windupTimer가 완료될 때까지 스윙을 commit한다. 타겟이 회피해도 취소하지 않는다.

| 전이 대상 | 조건 |
|---|---|
| `AttackRecover` | `windupTimer_` 완료 → `dist ≤ attackRange_` 이면 hit(데미지 적용), 초과 시 miss (둘 다 AttackRecover) |
| `Idle / Return` | 타겟 사망/소실, 또는 `isTooFarFromHome()` |

#### AttackRecover → *

| 전이 대상 | 조건 |
|---|---|
| `Reposition` | `recoverTimer_` 만료 && `isOvercrowded()` (주변 NPC ≥ overlapThreshold) |
| `AttackWindup` | `recoverTimer_` 만료 && `dist ≤ attackRange_` |
| `Chase` | `recoverTimer_` 만료 && `dist > attackRange_` |
| `Idle / Return` | 타겟 사망/소실 |

#### Confused → *

| 전이 대상 | 조건 |
|---|---|
| `Idle` | `confusedTimer_ ≥ CONFUSION_DURATION (3.0s)` |

#### Retreat → *

| 전이 대상 | 조건 |
|---|---|
| `Idle` | `dist to destination < 0.3` |

---

## 2. 분대(Squad) 상태 머신

### 상태 목록

**`SquadStatus`**

| 상태 | 설명 |
|---|---|
| `Normal` | 정상 작동. Platoon 명령 수행. |
| `Confused` | 리더 사망 직후 혼란 상태. 3초간 모든 Platoon 명령 무시. |
| `Broken` | 혼란 해소 후 지휘 체계 붕괴 상태. 리더 재선출 없음. 개별 행동. |

**`SquadOrderType`** (Platoon → Squad 명령)

| 명령 | 효과 |
|---|---|
| `Attack` | 지정 타겟 공격. Squad가 멤버에 `EngageTarget` 발행. |
| `Hold` | 위치 유지. Squad가 멤버에 `HoldSlot` 발행. |
| `Retreat` | 집결지 후퇴. Squad가 멤버에 `Retreat` 발행. |

**`NpcCommandType`** (Squad → NPC 명령)

| 명령 | 발행 주체 | 효과 |
|---|---|---|
| `EngageTarget` | Squad (Normal/Broken) | Chase + 공격 사이클 진입 |
| `HoldSlot` | Squad (Normal, Hold 명령 시) | 대기. 현 위치 유지. |
| `Retreat` | Squad (Broken 귀환 / Disband) | destination으로 이동 후 Idle |
| `Confused` | Squad (Confused 진입 시 1회) | NpcState::Confused 진입 |
| `Idle` | Squad (기본) | 아무것도 안 함 |

### 전이 다이어그램

```
┌──────────┐  리더 사망                   ┌──────────────┐
│  NORMAL  ├────────────────────────────►│   CONFUSED   │
└──────────┘  checkLeaderValidity()       └──────┬───────┘
                                                 │ confusionTimer_ ≥ 3.0s
                                                 ▼
                                          ┌──────────────┐
                                          │    BROKEN    │
                                          └──────┬───────┘
                                                 │ aliveCount < MIN_MEMBERS_TO_DISBAND (2)
                                                 ▼
                                           【DISBAND】
                                     생존 멤버 전원 Retreat(spawnPos, 0.5×)
                                     Squad 멤버 목록 초기화 → Room에서 제거
```

어느 상태에서든: `aliveCount < MIN_MEMBERS_TO_DISBAND (2)` → **DISBAND**

### 전이 조건 상세

#### Normal → Confused
- `checkLeaderValidity()`에서 리더 NPC 사망 감지
- 전 멤버에 `NpcCommand::Confused` 1회 발행
- `leaderNpcId_` 초기화 (리더 재선출 없음)
- `confusionTimer_ = 0`

#### Confused 동작
- `confusionTimer_` 매 틱 증가
- Platoon의 모든 명령(`Attack`/`Hold`/`Retreat`) 무시
- NPC들은 개별적으로 Confused 상태에서 방향 진동 이동

#### Confused → Broken
- `confusionTimer_ ≥ CONFUSION_DURATION (3.0s)`
- `status_ = Broken`

#### Broken 동작 (매 틱, `updateBroken()`)
1. Platoon `Retreat` 명령 수신 시 → 전 멤버 `Retreat(platoonDestination_)` 발행 후 종료
2. 각 NPC 개별 스캔:
   - 이미 전투 중 (Chase / AttackWindup / AttackRecover / Reposition) → 유지
   - `detectionRange` 내 적 발견 → `NpcCommand::EngageTarget` 발행
   - Idle 상태 && 스폰에서 멀리 이탈 → `NpcCommand::Retreat(spawnPos, 0.5×)` 발행

**Broken이 수락하는 Platoon 명령:** `Retreat`만. `Attack`/`Hold`는 무시.

#### Disband
- `aliveCount < 2` 감지 시 어느 상태에서든 즉시 진입
- 생존 멤버 전원 `NpcCommand::Retreat(spawnPos, 0.5×)` 발행
- Squad 멤버 목록 초기화 → Room에서 Squad 제거

### 분대 타겟 선택 (Normal + Attack 명령 시 매 틱)

| 시나리오 | 동작 |
|---|---|
| Platoon이 `targetId != 0` 지정 (새 타겟) | 타이머 리셋 후 해당 타겟 설정. `selectTarget()`에서 즉시 chaseRange 검증. |
| Platoon이 `targetId != 0` 지정 (기존 타겟 유지) | 그대로 사용. 타이머 유지. |
| Platoon `targetId == 0` | Squad 자율 선택: 리더 위치 기준 `detectionRange` 내 최근접 플레이어 |
| 현재 타겟 `chaseRange` 이내 | 유지 (히스테리시스) |
| 현재 타겟 범위 이탈 | `TARGET_MEMORY_DURATION (4s)` 카운트 후 해제 |
| 타겟 사망 | 즉시 해제 → `SquadReport::engagedTarget = 0` 보고 |

### EngageTarget 발송 조건 (`pushCommandsToMembers`)

`EngageTarget` 명령은 아래 조건이 모두 충족될 때만 발송. 그 외에는 `HoldSlot`.

```
order_ == Attack
AND targetPlayerId_ != 0
AND leader 존재
AND target 생존
AND distance(leader, target) <= leader.chaseRange
```

> **배경:** Platoon은 매 틱 `issueOrderToAll()`을 호출하므로 Squad는 매 틱 타겟 오버라이드를
> 받는다. 거리 검증 없이 `EngageTarget`을 발송하면 NPC가 Idle→Chase→Idle 루프에 빠진다.

---

## 3. 소대(Platoon) 계층

### 개요

Platoon은 물리적 NPC가 없는 **개념 객체**. 복수의 Squad를 소유하며 매 틱 `SquadOrderType` 명령을 발행. 모든 Squad가 Disband되면 Room에서 제거.

### 전술 평가 (`evaluateTactics`)

`combatEfficiency = aliveCount / totalCount × avgHpRatio`

| 조건 | 행동 |
|---|---|
| `avgEfficiency < RETREAT_EFFICIENCY_THRESHOLD (0.25)` | `currentOrder_ = Retreat` |
| `avgEfficiency ≥ 0.35` && 현재 Retreat 중 | `currentOrder_ = Attack` (히스테리시스) |

### 타겟 선택 (`selectPrimaryTarget`)

| 기준 | 점수 |
|---|---|
| 현재 타겟 유지 (히스테리시스) | +30 |
| Squad가 이미 교전 중인 타겟 | +10 / Squad |
| 낮은 HP 선호 | `(1 - hpRatio) × 20` |

타겟 변경 쿨다운: `TARGET_CHANGE_COOLDOWN = 1.0s`

### 데이터 흐름 (1 tick)

```
[수집]
  NPC → NpcReport → Squad
  Squad → SquadReport → Platoon

[판단]
  Platoon: SquadReport 소비 → evaluateTactics + selectPrimaryTarget
           → Squad에 setPlatoonOrder(SquadOrderType, targetId, destination)
  Squad:   PlatoonOrder 수신 → NpcCommand 계산 → Npc에 applyCommand()

[실행]
  NPC: NpcCommand 수신 → 상태 머신 실행
```

### Room::tick() 실행 순서

```
1. Logger::setTick(tickCount_)
2. DummyPlayerController::update(dt)        ← Player 목표 할당
3. Player::update(dt)                       ← Player 이동
4. Room::updatePlatoons(dt)                 ← 이전 틱 SquadReport 소비 + SquadOrder 발행
5. Room::updateSquads(dt)                   ← PlatoonOrder 수신 + NpcCommand 발행
                                               + 새 SquadReport 생성 → Platoon 전달
6. Npc::update(dt)                          ← NpcCommand 실행
7. tickCount_++
8. 주기적 dumpSnapshot()
```

---

## 4. NPC ↔ Squad ↔ Platoon 연동 메커니즘

### 공유 필드

| 구조체/필드 | NPC 쪽 | Squad 쪽 | 역할 |
|---|---|---|---|
| 소속 분대 | `squadId_` (-1 = standalone) | `members_` (NPC ID 목록) | 분대 멤버십 |
| 분대 타겟 (레거시) | `squadTargetId_` | `targetPlayerId_` | 분대가 NPC 타겟을 지시 |
| 리더 여부 | `isLeader_` | `leaderNpcId_` | 리더 지정 |

### 보고 구조체

**`NpcReport`** (NPC → Squad, 매 틱 `Squad::buildReport()` 호출)

| 필드 | 타입 | 설명 |
|---|---|---|
| `npcId` | `uint32_t` | NPC 식별자 |
| `state` | `int` | `NpcState` 정수값 |
| `position` | `Vec3` | 현재 위치 |
| `currentTarget` | `uint32_t` | 0 = 타겟 없음 |
| `hpRatio` | `float` | hp / maxHp |
| `alive` | `bool` | 생존 여부 |
| `isLeader` | `bool` | 리더 여부 |

**`SquadReport`** (Squad → Platoon, `Room::updateSquads()`에서 전달)

| 필드 | 타입 | 설명 |
|---|---|---|
| `squadId` | `int` | Squad 식별자 |
| `aliveCount` | `int` | 생존 멤버 수 |
| `totalCount` | `int` | 전체 멤버 수 |
| `avgHpRatio` | `float` | 생존 멤버 평균 HP 비율 |
| `status` | `SquadStatus` | Normal / Confused / Broken |
| `center` | `Vec3` | 생존 멤버 무게중심 |
| `engagedTarget` | `uint32_t` | 현재 교전 중인 타겟 (0 = 없음) |
| `inCombat` | `bool` | 전투 중 여부 |
| `leaderAlive` | `bool` | 리더 생존 여부 |
| `combatEfficiency` | `float` | `aliveCount / totalCount × avgHpRatio` |

### 타겟 결정 우선순위

```
1. Platoon이 targetId 지정 (targetId != 0) → Squad는 해당 타겟 우선 사용
2. Platoon targetId == 0 → Squad 자율 선택 (리더 기준 detectionRange 내 최근접)
3. Squad가 NpcCommand::EngageTarget(targetId)으로 NPC에 전달
4. NPC는 자체적으로 타겟을 선택하거나 변경하지 않음 (분대 소속 시)
```

---

## 5. 주요 파라미터

### NPC 파라미터 (`NpcConfig`)

| 파라미터 | 기본값 | 효과 |
|---|---|---|
| `detectionRange` | 10.0 | 탐지 범위 (standalone Idle 자율 aggro, Broken Squad 스캔) |
| `attackRange` | 2.0 | 근접 사거리 |
| `chaseRange` | 22.0 | 추격 해제 거리 (leash) |
| `maxChaseDistance` | 26.0 | 스폰 기준 최대 이동 거리 |
| `moveSpeed` | 4.0 | 이동 속도 (units/s) |
| `attackWindupTime` | 0.4s | 공격 선딜 시간 |
| `attackRecoverTime` | 0.6s | 공격 후딜 시간 |
| `separationRadius` | 4.0 | 충돌 회피 반경 |
| `separationWeight` | 0.6 | 분리력 강도 |
| `overlapThreshold` | 2 | Reposition 트리거 주변 NPC 수 |
| `canReAggroOnReturn` | true | standalone NPC 전용: 귀환 중 재교전 가능 여부 |

### NPC 상수

| 상수 | 값 | 효과 |
|---|---|---|
| `CONFUSION_DURATION` | 3.0s | Confused 상태 지속 시간 |
| `TARGET_EVAL_INTERVAL` | 0.5s | standalone 타겟 재평가 주기 |
| `REPOSITION_TIMEOUT` | 1.5s | Reposition 최대 지속 시간 (초과 시 Chase 전환) |

### 분대 상수 (`Squad`)

| 상수 | 값 | 효과 |
|---|---|---|
| `CONFUSION_DURATION` | 3.0s | 리더 사망 후 Squad Confused 지속 시간 |
| `TARGET_MEMORY_DURATION` | 4.0s | 타겟 범위 이탈 후 기억 유지 시간 |
| `MIN_MEMBERS_TO_DISBAND` | 2 | Disband 트리거 최소 생존 멤버 수 |

### 소대 상수 (`Platoon`)

| 상수 | 값 | 효과 |
|---|---|---|
| `TARGET_CHANGE_COOLDOWN` | 1.0s | Platoon 타겟 변경 최소 간격 |
| `RETREAT_EFFICIENCY_THRESHOLD` | 0.25 | Retreat 트리거 전투효율 임계값 |
