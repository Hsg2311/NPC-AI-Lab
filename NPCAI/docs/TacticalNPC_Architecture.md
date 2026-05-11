# Tactical NPC 시스템 구조

> 갱신: 2026-05-11  
> 대상: `sim/PlatoonLeader`, `sim/TacticalSquad`, `sim/TacticalNpc`, `sim/Room`, `sim/ScenarioTactical`

Tactical NPC 시스템은 **지휘관 - 부대 - 개별 NPC**의 3계층 구조로 구성된다.
상위 계층은 전술 판단과 명령 발행을 담당하고, 하위 계층은 명령을 실제 슬롯 이동과 상태 전이로 실행한다.

```text
PlatoonLeader
  전술 판단: 목표 선택, 전술 발동 조건 확인, 단계 전환
  -> SquadOrder

TacticalSquad
  부대 명령 변환: SquadOrder를 멤버별 슬롯과 TacticalCommand로 변환
  -> TacticalCommand

TacticalNpc
  개별 실행: 명령을 받아 FSM으로 이동, 공격, 정지, 주시를 수행
```

---

## 1. 시뮬레이션 갱신 순서

`Room::tick(dt)`는 전술 지휘가 먼저 일어나고, 그 결과를 부대와 멤버가 같은 tick 안에서 실행하도록 순서를 고정한다.

```text
1. Logger tick 동기화
2. DummyPlayerController 갱신
3. Player 업데이트
4. NpcGroup 공유 시야 메모리 만료
5. livingPlayers / aggroCount / spatialGrid 캐시 재구성
6. 일반 Npc 업데이트
7. PlatoonLeader 업데이트
8. TacticalSquad 업데이트
9. TacticalNpc 멤버 업데이트 (PlatoonLeader 제외)
10. tick 증가
```

전술 NPC 관점에서는 7~9번이 핵심이다.

```text
PlatoonLeader::update()
  -> evaluateTactics()가 SquadOrder 발행

TacticalSquad::update()
  -> SquadOrder를 TacticalCommand로 변환

TacticalNpc::update()
  -> pendingCmd_ 소비 후 FSM 실행
```

---

## 2. 시나리오 구성

`ScenarioTactical`은 전술 AI 검증용 시나리오다.

| 구성 요소 | 수량 | 초기 위치 / 설정 |
|---|---:|---|
| Player P1 | 1 | `(0, 0, 0)`, HP 10000, 이동 속도 20 |
| Player P2 | 1 | `(3, 0, 3)`, HP 9999, 더미 경로 이동 |
| Boss / PlatoonLeader | 1 | `(50, 0, 0)`, HP 200 |
| Squad A | 20 | 좌측/상단 배치 |
| Squad B | 20 | 중앙/정면 배치 |
| Squad C | 20 | 우측/하단 배치 |

일반 TacticalNpc 기본값:

| 파라미터 | 값 |
|---|---:|
| `maxHp` | 80 |
| `moveSpeed` | 10 |
| `attackRange` | 2 |
| `attackDamage` | 10 |
| `attackWindupTime` | 0.35s |
| `attackRecoverTime` | 0.70s |
| `separationRadius` | 6 |
| `separationWeight` | 1.5 |

Boss는 같은 설정을 기반으로 HP 200, 공격 사거리 2.5를 사용한다. Boss는 일반 공격 FSM을 직접 수행하기보다 거리 유지와 부대 지휘에 집중한다.

---

## 3. PlatoonLeader

### 3-1. 역할

`PlatoonLeader`는 `TacticalNpc`를 상속하지만 일반 멤버처럼 독립 전투를 수행하지 않는다. 주요 책임은 다음과 같다.

- 살아 있는 Squad 목록 관리
- 주 목표 플레이어 선택
- 보스 중심 박스 대형 명령 발행
- 전술 발동 조건 확인
- 공통 후퇴, 포위, 경계 단계 전환
- 리더 사망 시 모든 Squad에 `Confused` 명령 발행

### 3-2. 목표 선택

Boss는 살아 있는 모든 플레이어를 점수화하고 가장 높은 점수의 플레이어를 primary target으로 선택한다.

```text
distScore = 1 / (1 + distance(bossPos, playerPos))
hpScore   = 1 - playerHp / playerMaxHp
score     = 0.5 * distScore + 0.5 * hpScore
```

### 3-3. LeaderPhase 흐름

현재 리더 단계는 다음 6개다.

```text
BoxAdvance
Engage
TacticalRetreat
Encircle
Vigilance
Cooldown
```

기본 흐름:

```text
전술 미발동:
  BoxAdvance -> Engage

전술 조건 충족:
  Engage 또는 BoxAdvance -> TacticalRetreat -> BoxAdvance -> Encircle 또는 Vigilance

포위 완료:
  Encircle -> Cooldown

쿨타임 종료:
  전술 해금 상태면 TacticalRetreat부터 다시 시작
```

모든 단계 전환은 `enterPhase(next, reason)`을 통해 이루어진다. 이 함수는 `leaderPhase_`를 갱신하고 `phaseOrderIssued_`를 `false`로 초기화하며 로그를 남긴다.

### 3-4. 전술 발동 조건

전술은 플레이어 분산 여부로 발동하지 않는다. 다음 조건 중 하나를 만족할 때만 해금된다.

| 조건 | 판정 | 상수 |
|---|---|---|
| Boss 체력 감소 | `bossHp / bossMaxHp <= 0.70` | `TACTIC_HP_THRESHOLD` |
| Squad 손실 | `aliveMembers / initialMembers <= 0.80` | `TACTIC_SQUAD_RATIO` |

`tacticsUnlocked_`는 단방향 래치다. 한 번 켜지면 이후 전술 사이클은 쿨타임 뒤에도 공통 후퇴부터 다시 시작한다.

### 3-5. TacticalRetreat

전술이 해금되면 먼저 Platoon 전체가 플레이어들로부터 후퇴한다.

```text
playerCentroid = 살아 있는 플레이어 위치 평균
awayDir        = normalize(bossPos - playerCentroid)
retreatTarget  = playerCentroid + awayDir * REGROUP_DIST
```

Boss는 `retreatTarget`으로 이동한다. 이때 일반 NPC의 슬롯 이동과 속도감을 맞추기 위해 `moveSpeed * TACTICAL_SPEED_MULT`를 사용한다.

Squad 멤버는 새 대형을 만들지 않는다. 리더의 시작 위치와 후퇴 목표의 차이를 공통 이동량으로 사용한다.

```text
retreatDelta = retreatTarget - leaderStartPos
npcSlot      = npcCurrentPos + retreatDelta
```

후퇴 완료 조건은 다음 두 가지를 모두 만족해야 한다.

- Boss가 `retreatTarget` 근처에 도착
- 모든 살아 있는 Squad 멤버가 각자 `HoldSlot` 후퇴 슬롯에 도착

후퇴 완료 후에는 다시 `BoxAdvance`로 전환한다.

### 3-6. BoxAdvance

`BoxAdvance`는 이름과 달리 플레이어 근처 접근 대형이 아니라, **보스 앞쪽의 박스 집결 대형**이다. 박스 대형은 초기 정렬과 전술 판단 전 준비 자세 모두에 사용된다.

```text
playerCentroid = 살아 있는 플레이어 위치 평균
forward        = normalize(playerCentroid - bossPos)
right          = (-forward.z, 0, forward.x)
boxCenter      = bossPos + forward * BOX_FRONT_OFFSET
```

Squad별 상대 오프셋은 `calcSquadBoxOffsets(numSquads)`로 계산한다.

```text
rows    = max(1, floor(sqrt(numSquads)))
cols    = ceil(numSquads / rows)
colOff  = (col - (cols - 1) / 2) * BOX_SQUAD_SPACING
rowOff  = (row - (rows - 1) / 2) * BOX_SQUAD_SPACING
latFrac = abs(col - (cols - 1) / 2) / ((cols - 1) / 2)
arcZ    = rowOff - BOX_ARC_DEPTH * latFrac
sectorPos = (colOff, 0, arcZ)
```

각 Squad 중심은 보스 앞쪽 박스 중심을 기준으로 잡는다.

```text
squadCenter = boxCenter
            + right   * sectorPos.x
            - forward * sectorPos.z
```

Squad 내부 멤버 슬롯은 기존 `calcDenseSlots(squadCenter, faceDir, count)`로 만든다. 멤버들은 `HoldSlot` 명령을 받아 슬롯까지 이동하고, 도착 후 플레이어 방향을 바라본다.

`BoxAdvance` 완료 순간에만 플레이어 군집 수를 판정한다.

```text
clusterPlayers(room) == 1  -> Encircle
clusterPlayers(room) >= 2  -> Vigilance
```

즉, 후퇴 중이나 박스 대형 진행 중에는 포위/경계를 선택하지 않는다.

### 3-7. Encircle

`Encircle`은 플레이어가 한 군집으로 모여 있을 때 선택된다. 포위 중심은 발행 시점의 플레이어 centroid다.

전체 생존 멤버 수를 기준으로 Squad별 포위 섹터를 나눈다.

```text
fraction_s   = squadMemberCount / totalMemberCount
sectorSpan_s = 2π * fraction_s
sectorAngle  = angleAccum + sectorSpan_s / 2
```

각 Squad는 `Encircle` 명령을 받고, 내부에서 `calcEncircleSlots()`로 포위 슬롯을 만든 뒤 가장 가까운 슬롯을 greedy 방식으로 배정한다.

포위 슬롯에 모든 멤버가 도착하면 `Cooldown`으로 전환한다.

### 3-8. Vigilance

`Vigilance`는 박스 대형 완료 시점에 플레이어 군집이 2개 이상일 때 선택된다. 이 단계는 보스를 중심으로 한 경계 대형이며, 한 번 진입하면 플레이어가 다시 모여도 포위로 전환하지 않는다.

리더는 각 Squad에 `GuardBoss` 명령을 보낸다.

```text
playerCentroid = 살아 있는 플레이어 위치 평균
forward        = normalize(playerCentroid - bossPos)
baseAngle      = atan2(forward.z, forward.x)
squadAngle_i   = baseAngle + 2π * i / numSquads
squadCenter_i  = bossPos + direction(squadAngle_i) * VIGILANCE_GUARD_RADIUS
```

Squad 내부는 `calcDenseSlots()`로 밀집 슬롯을 만들고, 멤버에게 `GuardSlot`을 발행한다. `GuardSlot`은 `HoldSlot` 상태를 재사용하지만 공격하지 않고 가장 가까운 살아 있는 플레이어를 바라본다.

### 3-9. Cooldown

포위 완료 후에는 `Cooldown`에 진입한다.

```text
leaderPhase_ == Encircle && phaseOrderIssued_ && allMembersArrived()
  -> tacticCooldown_ = TACTIC_COOLDOWN_DURATION
  -> enterPhase(Cooldown)
```

쿨타임이 끝났고 전술이 이미 해금된 상태라면 `TacticalRetreat`부터 다시 시작한다.

---

## 4. TacticalSquad

### 4-1. 역할

`TacticalSquad`는 Actor가 아니라 지휘 보조 객체다. 멤버 TacticalNpc의 ID 목록을 보유하고, 리더가 발행한 `SquadOrder`를 실제 멤버별 `TacticalCommand`로 변환한다.

주요 책임:

- 죽은 멤버 제거
- `SquadOrder` 저장
- 대형별 슬롯 계산
- 멤버에게 `TacticalCommand` 발행

### 4-2. SquadOrderType

현재 Squad 명령은 다음과 같다.

| SquadOrderType | 멤버 명령 | 설명 |
|---|---|---|
| `Idle` | `Idle` | 전투 해제 |
| `Engage` | `EngageTarget` | 지정 타겟 추격/공격 |
| `Encircle` | `HoldSlot` | 플레이어 centroid 주변 포위 슬롯으로 이동 |
| `DenseHold` | `HoldSlot` | 현재 Squad 중심 기준 밀집 대형 |
| `BoxAdvance` | `HoldSlot` | 보스 앞쪽 박스 대형 슬롯으로 이동 |
| `GuardBoss` | `GuardSlot` | 보스 중심 경계 대형 |
| `RetreatFormUp` | `HoldSlot` | 현재 배치를 유지한 채 공통 후퇴 |

`receiveOrder()`는 명령을 저장하고 `orderDirty_ = true`로 표시한다. 다음 `update()`에서 명령을 슬롯으로 변환한다.

`BoxAdvance`는 멤버가 공격 사이클 후 `Chase`로 돌아오는 상황을 막기 위해 현재 명령을 반복 발행할 수 있다. 포위, 후퇴, 경계 슬롯은 명령 수신 시점에 고정된다.

### 4-3. 슬롯 계산

#### Encircle

```text
slot_i = tacticCenter + (cos(theta_i), 0, sin(theta_i)) * approachRadius
```

포위 슬롯은 Squad 섹터 안에서 균등 분배한다. 멤버와 슬롯 매칭은 greedy nearest-slot 방식이다.

#### Dense / Box / Guard

밀집 슬롯은 직사각형 격자로 만든다.

```text
cols    = ceil(sqrt(count))
rows    = ceil(count / cols)
spacing = max(memberSeparationRadius, 1.2)
right   = (-forward.z, 0, forward.x)

slot_i = center
       + right   * (col - (cols - 1) / 2) * spacing
       + forward * (row - (rows - 1) / 2) * spacing
```

`BoxAdvance`는 보스 앞쪽 박스 중심과 Squad offset을 이용해 Squad 중심을 잡고, `GuardBoss`는 보스 주변 반경에 Squad 중심을 둔다.

#### RetreatFormUp

후퇴는 대형 계산을 하지 않는다.

```text
retreatDelta = retreatTarget - leaderStartPos
slot         = npcCurrentPos + retreatDelta
```

---

## 5. TacticalNpc

### 5-1. 역할

`TacticalNpc`는 개별 전투 유닛이다. 스스로 목표를 탐색하지 않고 Squad가 내려준 명령을 `pendingCmd_`에 저장한 뒤 다음 `update()` 시작 시 소비한다.

```text
receiveCommand(cmd)
  -> pendingCmd_ = cmd

update(dt)
  -> consumePendingCommand()
  -> state별 update 함수 실행
```

같은 tick에 여러 명령이 들어오면 마지막 명령만 남는다.

### 5-2. 상태와 명령

| 값 | 상태 | 동작 |
|---:|---|---|
| 0 | `Idle` | 명령 대기 |
| 1 | `Chase` | 타겟 추격 |
| 2 | `AttackWindup` | 공격 준비, 이동 없음 |
| 3 | `AttackRecover` | 공격 후 회복 |
| 4 | `Flank` | 지정 슬롯으로 고속 이동 후 교전 |
| 7 | `Dead` | 사망 |
| 8 | `HoldSlot` | 슬롯까지 이동 후 위치 유지 |

| TacticalCommandType | 전환 상태 | 저장 데이터 |
|---|---|---|
| `EngageTarget` | `Chase` | `targetId_` |
| `FlankTarget` | `Flank` | `targetId_`, `assignedSlot_`, `slotRefTargetPos_`, `abandonDist_`, `speedMult_` |
| `HoldSlot` | `HoldSlot` | `targetId_`, `assignedSlot_` |
| `GuardSlot` | `HoldSlot` | `targetId_`, `assignedSlot_`, `guardNearestPlayer_ = true` |
| `Idle` | `Idle` | `targetId_ = 0` |
| `Confused` | `Idle` | `targetId_ = 0` |

`GuardSlot`은 `HoldSlot` 상태를 재사용한다. 차이는 도착 후 고정 타겟이 아니라 가장 가까운 살아 있는 플레이어를 바라본다는 점이다.

### 5-3. 이동과 슬롯 도착

`Chase`와 `Flank`는 진행 방향과 수직인 separation 성분만 더해, 서로 겹치지 않으면서 목표 방향을 유지한다.

`HoldSlot`은 현재 위치에서 슬롯까지 직선 이동한다. 도착 후에는 공격하지 않고 타겟 방향만 바라본다.

```text
if distance(position, assignedSlot) < separationRadius * 0.25:
    facing = normalize(targetPos - position)
else:
    position += normalize(assignedSlot - position) * moveSpeed * TACTICAL_SPEED_MULT * dt
```

`isAtSlot()`은 다음 경우 true를 반환한다.

| 상태 | 조건 |
|---|---|
| `HoldSlot` | `distance(pos, slot) < separationRadius * 0.25` |
| `AttackWindup` | true |
| `AttackRecover` | true |

그 외 상태는 false다.

---

## 6. 디버그 시각화

`Room::buildSnapshot()`은 TacticalNpc 정보를 `DebugTacticalNpcEntry`로 변환한다. 렌더러는 다음 정보를 표시한다.

- TacticalNpc 위치, 방향, HP
- 상태 색상
- Squad ID
- Boss 여부
- 타겟 ID
- 할당 슬롯 좌표
- Windup / Recover 진행률

슬롯 마커와 NPC 방향선을 보면 현재 대형이 어느 지점을 목표로 하는지 확인할 수 있다. 박스 대형은 보스 앞쪽에 슬롯 마커가 형성되어야 한다.

---

## 7. 주요 상수

### PlatoonLeader

| 상수 | 값 | 의미 |
|---|---:|---|
| `TACTIC_INTERVAL` | 1.0s | 전술 평가 주기 |
| `CLUSTER_RADIUS` | 20.0 | 플레이어 군집 판단 거리 |
| `ENCIRCLE_RADIUS` | 50.0 | 포위 반경 |
| `TACTIC_HP_THRESHOLD` | 0.70 | Boss HP 기반 전술 발동 임계값 |
| `TACTIC_SQUAD_RATIO` | 0.80 | Squad 생존 비율 기반 전술 발동 임계값 |
| `TACTIC_COOLDOWN_DURATION` | 8.0s | 포위 완료 후 쿨타임 |
| `TACTIC_FAIL_COOLDOWN_DURATION` | 5.0s | 예외/실패 쿨타임 |
| `BOX_FRONT_OFFSET` | 15.0 | 보스 앞쪽 박스 중심 거리 |
| `BOX_SQUAD_SPACING` | 35.0 | 박스 대형 Squad 간격 |
| `BOX_ARC_DEPTH` | 10.0 | 측면 Squad를 앞쪽으로 당기는 호형 깊이 |
| `BOSS_KEEP_DIST` | 18.0 | 일반 교전 중 Boss 유지 거리 |
| `BOSS_KEEP_TOL` | 2.0 | Boss 거리 유지 허용 오차 |
| `REGROUP_DIST` | 70.0 | 공통 후퇴 거리 |
| `VIGILANCE_GUARD_RADIUS` | 20.0 | 경계 대형의 보스 주변 거리 |

### TacticalNpc

| 상수 | 값 | 의미 |
|---|---:|---|
| `TACTICAL_SPEED_MULT` | 3.0 | `Flank`, `HoldSlot`, Boss 후퇴 이동 속도 배율 |

