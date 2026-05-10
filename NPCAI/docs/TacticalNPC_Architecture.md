# Tactical NPC 시스템 구조

> 갱신: 2026-05-10  
> 대상: `sim/PlatoonLeader`, `sim/TacticalSquad`, `sim/TacticalNpc`, `sim/Room`, `sim/ScenarioTactical`

Tactical NPC 시스템은 **지휘관-분대-개별 NPC**의 3계층 구조로 구성된다.
상위 계층은 전략적 판단을 하고, 하위 계층은 이를 위치와 상태 전이로 변환한다.

```text
PlatoonLeader
  전술 판단: 어떤 목표를 공격할지, 어떤 전술을 발동할지 결정
  └─ SquadOrder

TacticalSquad
  대형 변환: SquadOrder를 실제 슬롯 좌표로 변환
  └─ TacticalCommand

TacticalNpc
  개별 실행: 명령을 소비하고 FSM으로 이동/공격/대기 수행
```

---

## 1. 시뮬레이션 갱신 순서

`Room::tick(dt)`는 전술 계층이 같은 틱 안에서 지휘 → 대형 계산 → NPC 실행까지 끝나도록 순서를 고정한다.

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

전술 NPC 입장에서는 7~9번이 핵심이다.

```text
PlatoonLeader::update()
  └─ evaluateTactics()가 SquadOrder 발행

TacticalSquad::update()
  └─ SquadOrder를 TacticalCommand로 변환

TacticalNpc::update()
  └─ pendingCmd_ 소비 후 FSM 실행
```

---

## 2. 시나리오 구성

`ScenarioTactical`은 전술 AI 검증용 시나리오다.

| 구성 요소 | 수량 | 초기 위치 / 설정 |
|---|---:|---|
| Player P1 | 1 | `(0, 0, 0)`, HP 300, 이동 속도 20 |
| Boss / PlatoonLeader | 1 | `(50, 0, 0)`, HP 200 |
| Squad A | 20 | 우상단 배치 |
| Squad B | 20 | 정면 배치 |
| Squad C | 20 | 우하단 배치 |

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

Boss는 같은 설정을 기반으로 HP 200, 공격 사거리 2.5를 사용한다.
현재 Boss는 직접 공격 FSM을 돌리지 않고, 플레이어와 거리를 유지하며 Squad 지휘에 집중한다.

---

## 3. PlatoonLeader

### 3-1. 역할

`PlatoonLeader`는 `TacticalNpc`를 상속하지만, 일반 멤버처럼 자율 전투를 수행하지 않는다.
주요 책임은 다음과 같다.

- 생존 Squad 목록 유지
- 주 타겟 플레이어 선택
- 초기 박스 대형 발행
- 전술 발동 조건 검사
- 포위 / 경계 / 각개격파 페이즈 전환
- 리더 사망 시 모든 Squad에 `Confused` 명령 발행

### 3-2. 타겟 선택

Boss는 살아 있는 모든 플레이어를 평가해 점수가 가장 높은 플레이어를 주 타겟으로 삼는다.

```text
distScore = 1 / (1 + distance(bossPos, playerPos))
hpScore   = 1 - playerHp / playerMaxHp
score     = 0.5 * distScore + 0.5 * hpScore
```

현재 시나리오에서는 플레이어가 1명이므로 항상 P1이 선택된다.

### 3-3. LeaderPhase — 마스터 단계 사이클

PlatoonLeader는 `LeaderPhase` enum으로 전체 전술 사이클을 추적한다.

```text
BoxAdvance  → Engage    : allMembersArrived() && !tacticsUnlocked_
BoxAdvance  → Encircle  : allMembersArrived() && tacticsUnlocked_
Engage      → Encircle  : checkTacticsConditions() 충족 (같은 evaluateTactics 틱 내 즉시 전환)
Encircle    → Cooldown  : phaseOrderIssued_ && allMembersArrived()
Cooldown    → BoxAdvance: tacticCooldown_ <= 0
```

단계 전환은 모두 `enterPhase(next, reason)`을 통해 이루어진다.
이 함수는 `leaderPhase_`를 갱신하고 `phaseOrderIssued_`를 false로 초기화한다.

```text
// enterPhase 역할
leaderPhase_      = next
phaseOrderIssued_ = false
Logger::log(reason)
```

`tacticsUnlocked_`는 단방향 래치다. 한 번 true가 되면 되돌아가지 않으며,
BoxAdvance 완성 후 Engage로 갈지 Encircle로 갈지만 결정한다.

### 3-4. BoxAdvance 단계

시뮬레이션 시작 시 `leaderPhase_ = LeaderPhase::BoxAdvance`다.
전술이 아직 발동되지 않았거나 전술 쿨타임 종료 후에도 이 단계로 진입한다.

중요한 점은 **BoxAdvance 목표 위치는 명령 발행 시점에 한 번 고정**된다는 것이다.

```text
boxAdvanceTargetPos_ = currentPlayerPosition   // 발행 시점에 고정
phaseOrderIssued_    = true
```

이후 플레이어가 움직여도 이미 발행된 BoxAdvance 슬롯은 재계산하지 않는다.

BoxAdvance가 완료되면:

```text
allMembersArrived() == true
  if !tacticsUnlocked_:
    enterPhase(Engage, ...)  → 모든 Squad에 Engage 명령 발행
  else:
    enterPhase(Encircle, ...)  → 다음 evaluateTactics에서 포위 슬롯 발행
```

BoxAdvance 중에는 Boss 이동이 없다. 플레이어 방향만 바라본다.

### 3-5. BoxAdvance 슬롯 기준

고정된 플레이어 위치를 `P0`, Boss 위치를 `B`라고 할 때:

```text
forward = normalize(P0 - B)
right   = (-forward.z, 0, forward.x)
```

Squad별 상대 오프셋은 `calcSquadBoxOffsets(numSquads)`로 계산한다.
3개 Squad일 때 개념적으로는 좌측, 중앙, 우측에 배치된다.

```text
rows = max(1, floor(sqrt(numSquads)))
cols = ceil(numSquads / rows)

colOff  = (col - (cols - 1) / 2) * BOX_SQUAD_SPACING
rowOff  = (row - (rows - 1) / 2) * BOX_SQUAD_SPACING
latFrac = abs(col - (cols - 1) / 2) / ((cols - 1) / 2)
arcZ    = rowOff - BOX_ARC_DEPTH * latFrac

sectorPos = (colOff, 0, arcZ)
```

각 Squad 중심은 다음 식으로 구한다.

```text
halfDepth   = (rowsInSquad - 1) * 0.5 * memberSeparationRadius
squadCenter = P0
            - forward * BOX_APPROACH_DIST
            + right   * sectorPos.x
            - forward * sectorPos.z
            - forward * halfDepth
```

이후 `calcDenseSlots(squadCenter, faceDir, count)`가 Squad 내부의 격자 슬롯을 만든다.

### 3-6. 전술 발동 조건

전술은 처음부터 켜져 있지 않고, 다음 조건 중 하나를 만족하면 영구적으로 해금된다.

| 조건 | 식 | 상수 |
|---|---|---|
| Boss 체력 감소 | `bossHp / bossMaxHp < 0.70` | `TACTIC_HP_THRESHOLD` |
| Squad 피해 누적 | `aliveMembers / initialMembers < 0.80` | `TACTIC_SQUAD_RATIO` |

현재 구현은 전술 해금 후 다시 잠그지 않는다.

### 3-7. Encircle 단계 — 포위 전술

`leaderPhase_ == Encircle`일 때 `evaluateTactics()`는 플레이어 분산 여부에 따라
`TacticalPhase` 서브-단계를 전환한다.

```text
TacticalPhase::Encircle
  플레이어 군집이 1개일 때 포위 슬롯 발행 (phaseOrderIssued_ == false일 때만)
  └─ 플레이어가 분산되면 TacticalPhase::Vigilance

TacticalPhase::Vigilance
  모든 Squad가 DenseHold로 경계
  └─ 5초 경과 후 TacticalPhase::DivideAndConquer

TacticalPhase::DivideAndConquer
  첫 번째 Squad는 WedgeCharge
  나머지 Squad는 DenseHold
```

플레이어 군집 수는 `clusterPlayers(room)`로 판단한다.
두 플레이어 사이 거리가 `CLUSTER_RADIUS = 10` 이하이면 같은 군집으로 본다.
현재 시나리오처럼 플레이어가 1명이면 항상 군집 수는 1이다.

포위 슬롯 발행 조건:

```text
isNewTactical = (tacticalPhase_ != TacticalPhase::Encircle)
if isNewTactical || !phaseOrderIssued_:
  → 슬롯 재발행, phaseOrderIssued_ = true
```

`isNewTactical`은 플레이어 분산→재집결 시 포위 슬롯을 다시 배치하기 위한 조건이다.

### 3-8. Cooldown 단계

모든 생존 멤버가 포위 슬롯에 도착하면 Cooldown으로 전환된다.

```text
leaderPhase_ == Encircle && phaseOrderIssued_ && allMembersArrived()
  → enterPhase(Cooldown, "포위 완성 — 쿨타임 진입")
  → tacticCooldown_ = 8.0s
```

쿨타임 중에는 Squad에 Engage 명령을 계속 발행해 전투를 유지한다.
쿨타임이 끝나면 BoxAdvance로 전환되어 다음 사이클이 시작된다.

```text
tacticCooldown_ <= 0
  → enterPhase(BoxAdvance, "전술 쿨타임 종료 — 박스 대형 재개")
  → phaseOrderIssued_ = false  (다음 BoxAdvance 명령 발행 허용)
```

---

## 4. TacticalSquad

### 4-1. 역할

`TacticalSquad`는 Actor가 아니다.
월드에 물리적으로 존재하지 않는 지휘 보조 객체이며, 멤버 TacticalNpc의 ID 목록만 가진다.

주요 책임:

- 죽은 멤버 제거
- `SquadOrder` 저장
- 슬롯 좌표 계산
- 각 멤버에게 `TacticalCommand` 발행

### 4-2. 명령 갱신 정책

`receiveOrder()`는 명령을 저장하고 `orderDirty_ = true`로 표시한다.
다음 `update()`에서 명령을 한 번 처리한다.

| SquadOrderType | 슬롯 재계산 시점 | 설명 |
|---|---|---|
| `Idle`, `Engage` | 명령 수신 시 1회 | 슬롯 없음 |
| `Encircle` | 명령 수신 시 1회 | 포위 슬롯 고정 |
| `DenseHold` | 명령 수신 시 1회 | 현재 Squad 중심 기준 대기 |
| `BoxAdvance` | 명령 수신 시 1회 | 초기 대형 목표 고정 |
| `WedgeCharge` | 매 틱 | 움직이는 타겟을 추적하는 돌진 대형 |

`BoxAdvance`는 플레이어가 움직여도 재계산하지 않는다.
`WedgeCharge`만 매 틱 슬롯을 갱신한다.

### 4-3. SquadOrder → TacticalCommand

| SquadOrderType | TacticalCommandType | 슬롯 계산 |
|---|---|---|
| `Idle` | `Idle` | 없음 |
| `Engage` | `EngageTarget` | 없음 |
| `Encircle` | `HoldSlot` | `calcEncircleSlots()` + greedy nearest-slot |
| `DenseHold` | `HoldSlot` | `calcDenseSlots(center=squadCentroid)` |
| `WedgeCharge` | `FlankTarget` | `calcWedgeSlots()` |
| `BoxAdvance` | `HoldSlot` | 고정 `formationTargetPos` 기준 `calcDenseSlots()` |

### 4-4. Encircle 슬롯 수식

포위는 Squad별 섹터를 나눈 뒤, 각 섹터 내부를 멤버 수만큼 균등 분할한다.

전체 생존 멤버 수를 `N`, Squad `s`의 멤버 수를 `n_s`라 하면:

```text
fraction_s = n_s / N
sectorSpan_s = 2π * fraction_s
sectorAngle_s = angleAccum + sectorSpan_s / 2
```

Squad 내부 슬롯:

```text
arc   = sectorSpan / count
start = sectorAngle - sectorSpan / 2 + arc / 2
theta_i = start + arc * i

slot_i = targetPos + (cos(theta_i), 0, sin(theta_i)) * ENCIRCLE_RADIUS
```

`start`에 `arc / 2`를 더하므로 슬롯이 섹터 경계가 아니라 각 소구간의 중앙에 놓인다.
인접 Squad와 같은 각도에 슬롯이 겹치는 일을 줄이기 위한 방식이다.

슬롯 배정은 greedy nearest-slot이다.

```text
for each npc:
  아직 사용하지 않은 slot 중 distance(npc.pos, slot)이 가장 작은 slot 선택
```

### 4-5. Dense 슬롯 수식

밀집 대형은 직사각형 격자로 만든다.

```text
cols = ceil(sqrt(count))
rows = ceil(count / cols)
spacing = max(memberSeparationRadius, 1.2)
right = (-forward.z, 0, forward.x)

slot_i = center
       + right   * (col - (cols - 1) / 2) * spacing
       + forward * (row - (rows - 1) / 2) * spacing
```

`DenseHold`와 `BoxAdvance`가 이 계산을 공유한다.

### 4-6. Wedge 슬롯 수식

쐐기 대형은 타겟 앞의 tip을 기준으로 1명, 2명, 3명 순서의 행을 만든다.

```text
forward = normalize(targetPos - fromPos)
right   = (-forward.z, 0, forward.x)
spacing = max(memberAttackRange * 1.2, 1.5)
tip     = targetPos - forward * memberAttackRange

rowCenter_r = tip - forward * (r * spacing * 1.5)
slot        = rowCenter_r + right * lateralOffset
```

---

## 5. TacticalNpc

### 5-1. 역할

`TacticalNpc`는 개별 전투 유닛이다.
스스로 타겟을 탐색하지 않고, Squad가 내려준 명령을 `pendingCmd_`에 저장했다가 다음 `update()` 시작 시 소비한다.

```text
receiveCommand(cmd)
  → pendingCmd_ = cmd

update(dt)
  → consumePendingCommand()
  → state별 update 함수 실행
```

같은 틱에 여러 명령이 들어오면 마지막 명령만 남는다.

### 5-2. 상태와 명령

| 값 | 상태 | 동작 |
|---:|---|---|
| 0 | `Idle` | 명령 대기 |
| 1 | `Chase` | 타겟 추적 |
| 2 | `AttackWindup` | 공격 준비, 이동 없음 |
| 3 | `AttackRecover` | 공격 후 회복 |
| 4 | `Flank` | 지정 슬롯까지 고속 이동 후 교전 |
| 7 | `Dead` | 사망 |
| 8 | `HoldSlot` | 슬롯까지 이동 후 위치 유지 |

Dead(7)과 Flank(4) 사이의 int 값 5, 6은 삭제된 상태가 차지하던 번호다. 렌더러 색상 테이블은 int 값으로 인덱싱하므로 뒤쪽 값을 당기지 않는다.

| TacticalCommandType | 전환 상태 | 저장 데이터 |
|---|---|---|
| `EngageTarget` | `Chase` | `targetId_` |
| `FlankTarget` | `Flank` | `targetId_`, `assignedSlot_`, `slotRefTargetPos_`, `abandonDist_`, `speedMult_` |
| `HoldSlot` | `HoldSlot` | `targetId_`, `assignedSlot_` |
| `Idle` | `Idle` | `targetId_ = 0` |
| `Confused` | `Idle` | `targetId_ = 0` |

### 5-3. 이동과 분리

`Chase`와 `Flank`는 주변 NPC와 겹치지 않도록 separation force를 사용한다.
이 힘은 주변 유닛으로부터 멀어지는 방향의 합이다.

```text
away_j = selfPos - neighborPos_j
d_j = |away_j|
strength_j = 1 - d_j / separationRadius
force = Σ normalize(away_j) * strength_j
```

추적 방향과 같은 축으로 밀려 뒤로 물러나는 현상을 줄이기 위해, 실제 이동에는 진행 방향에 수직인 성분만 더한다.

```text
sepPerp = sep - moveDir * dot(sep, moveDir)
finalDir = normalize(moveDir + sepPerp * separationWeight)
```

`HoldSlot`은 현재 구현상 슬롯까지 직선 이동한다.
슬롯에 도착하면 공격하지 않고 타겟 방향으로 바라보기만 한다.

```text
if distance(position, assignedSlot) < separationRadius * 0.25:
    facing = normalize(targetPos - position)
else:
    position += normalize(assignedSlot - position) * moveSpeed * TACTICAL_SPEED_MULT * dt
```

`isAtSlot()`은 다음 세 가지 경우에 true를 반환한다.

| 상태 | 조건 | 이유 |
|---|---|---|
| `HoldSlot` | `distance(pos, slot) < separationRadius * 0.25` | 슬롯 도착 판정 |
| `AttackWindup` | 무조건 true | 전투 중 = 대형 완성으로 간주 |
| `AttackRecover` | 무조건 true | 전투 중 = 대형 완성으로 간주 |

그 외 상태(`Idle`, `Chase`, `Flank`, `Dead`)는 false.
현재 시나리오의 `separationRadius = 6`이므로 HoldSlot 도착 허용 거리는 1.5다.

---

## 6. 디버그 시각화

`Room::buildSnapshot()`은 TacticalNpc 정보를 `DebugTacticalNpcEntry`로 변환한다.
렌더러는 다음 정보를 표시한다.

- TacticalNpc 위치, 방향, HP
- 상태 색상
- Squad ID
- Boss 여부 (`isLeader`)
- 타겟 ID
- 할당 슬롯 좌표 (`assignedSlot_`)
- Windup / Recover 진행률

시각화에서 슬롯 마커와 NPC→슬롯 방향선을 보면 현재 대형이 어떤 좌표를 목표로 하는지 확인할 수 있다.
초기 BoxAdvance 고정 동작을 확인할 때는 플레이어를 움직여도 슬롯 마커가 최초 목표점 주변에 남아 있는지 보면 된다.

---

## 7. 주요 상수

### PlatoonLeader

| 상수 | 값 | 의미 |
|---|---:|---|
| `TACTIC_INTERVAL` | 1.0s | 전술 평가 주기 |
| `VIGILANCE_DURATION` | 5.0s | 경계 후 각개격파 전환 시간 |
| `CLUSTER_RADIUS` | 10.0 | 플레이어 군집 판단 거리 |
| `ENCIRCLE_RADIUS` | 50.0 | 포위 반경 |
| `TACTIC_HP_THRESHOLD` | 0.70 | Boss HP 기반 전술 발동 임계값 |
| `TACTIC_SQUAD_RATIO` | 0.80 | Squad 생존 비율 기반 전술 발동 임계값 |
| `TACTIC_COOLDOWN_DURATION` | 8.0s | 전술 완료 후 쿨타임 |
| `BOX_APPROACH_DIST` | 20.0 | 초기 박스 대형의 타겟 전방 거리 |
| `BOX_SQUAD_SPACING` | 35.0 | Squad 사이 간격 |
| `BOX_ARC_DEPTH` | 10.0 | 측면 Squad를 앞으로 당기는 호형 깊이 |
| `BOSS_KEEP_DIST` | 18.0 | Boss가 유지하려는 플레이어 거리 |
| `BOSS_KEEP_TOL` | 2.0 | 거리 유지 허용 오차 |

### TacticalNpc

| 상수 | 값 | 의미 |
|---|---:|---|
| `TACTICAL_SPEED_MULT` | 3.0 | `Flank`, `HoldSlot` 이동 속도 배율 |

---