# Tactical NPC 시스템 구조

> 갱신: 2026-05-07  
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

### 3-3. 초기 BoxAdvance 대형

시뮬레이션 시작 시 `boxAdvanceActive_ = true`이다.
전술이 아직 발동되지 않았거나 전술 쿨타임 중이면 Boss는 먼저 Squad 단위의 박스 대형을 만든다.

중요한 점은 **BoxAdvance 목표 위치는 대형 시작 시점에 한 번 고정**된다는 것이다.

```text
boxAdvanceTargetPos_ = currentPlayerPosition
boxAdvanceOrderIssued_ = true
```

이후 플레이어가 움직여도 이미 발행된 BoxAdvance 슬롯은 재계산하지 않는다.
따라서 NPC들은 계속 흔들리는 플레이어 위치가 아니라, 최초에 잡힌 대형 위치로 이동한다.

BoxAdvance가 완료되면:

```text
allMembersArrived() == true
  → boxAdvanceActive_ = false
  → 모든 Squad에 Engage 명령 발행
```

### 3-4. BoxAdvance 슬롯 기준

고정된 플레이어 위치를 `P0`, Boss 위치를 `B`라고 할 때:

```text
forward = normalize(P0 - B)
right   = (-forward.z, 0, forward.x)
```

Squad별 상대 오프셋은 `calcSquadBoxOffsets(numSquads)`로 계산한다.
3개 Squad일 때 개념적으로는 좌측, 중앙, 우측에 배치된다.

```text
cols = ceil(numSquads / rows)
rows = floor(sqrt(numSquads))

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

### 3-5. 전술 발동 조건

전술은 처음부터 켜져 있지 않고, 다음 조건 중 하나를 만족하면 영구적으로 해금된다.

| 조건 | 식 | 상수 |
|---|---|---|
| Boss 체력 감소 | `bossHp / bossMaxHp < 0.70` | `TACTIC_HP_THRESHOLD` |
| Squad 피해 누적 | `aliveMembers / initialMembers < 0.80` | `TACTIC_SQUAD_RATIO` |

현재 구현은 전술 해금 후 다시 잠그지 않는다.

### 3-6. 전술 페이즈

전술 해금 후 Boss는 플레이어 분산 여부에 따라 페이즈를 전환한다.

```text
Encircle
  플레이어 군집이 1개일 때 포위 슬롯 발행
  └─ 플레이어가 분산되면 Vigilance

Vigilance
  모든 Squad가 DenseHold로 경계
  └─ 5초 경과 후 DivideAndConquer

DivideAndConquer
  첫 번째 Squad는 WedgeCharge
  나머지 Squad는 DenseHold
```

플레이어 군집 수는 `clusterPlayers(room)`로 판단한다.
두 플레이어 사이 거리가 `CLUSTER_RADIUS = 10` 이하이면 같은 군집으로 본다.
현재 시나리오처럼 플레이어가 1명이면 항상 군집 수는 1이다.

### 3-7. 포위 완료와 쿨타임

포위 명령이 발행되면 `encircleSlotsAssigned_ = true`가 된다.
모든 생존 멤버가 슬롯에 도착하면 전술 쿨타임에 들어간다.

```text
allMembersArrived() == true
  → tacticsOnCooldown_ = true
  → tacticCooldown_ = 8.0s
  → encircleSlotsAssigned_ = false
```

쿨타임이 끝나면 `boxAdvanceActive_ = true`가 되어 다시 BoxAdvance 대형을 만들 수 있다.
이때 `boxAdvanceOrderIssued_ = false`로 리셋되어 다음 사이클의 플레이어 위치를 새로 고정한다.

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
| `Idle`, `Engage`, `Retreat`, `AlternateAttack` | 명령 수신 시 1회 | 슬롯 없음 |
| `FlankLeft`, `FlankRight` | 명령 수신 시 1회 | 측면 슬롯 고정 |
| `Encircle` | 명령 수신 시 1회 | 포위 슬롯 고정 |
| `DenseHold` | 명령 수신 시 1회 | 현재 Squad 중심 기준 대기 |
| `DenseAdvance` | 명령 수신 시 1회 | 지정 섹터로 밀집 이동 |
| `BoxAdvance` | 명령 수신 시 1회 | 초기 대형 목표 고정 |
| `WedgeCharge` | 매 틱 | 움직이는 타겟을 추적하는 돌진 대형 |

`BoxAdvance`는 플레이어가 움직여도 재계산하지 않는다.
`WedgeCharge`만 매 틱 슬롯을 갱신한다.

### 4-3. SquadOrder → TacticalCommand

| SquadOrderType | TacticalCommandType | 슬롯 계산 |
|---|---|---|
| `Idle` | `Idle` | 없음 |
| `Engage` | `EngageTarget` | 없음 |
| `FlankLeft` / `FlankRight` | `FlankTarget` | `calcFlankSlots()` |
| `Encircle` | `HoldSlot` | `calcEncircleSlots()` + greedy nearest-slot |
| `DenseHold` | `HoldSlot` | `calcDenseSlots(center=squadCentroid)` |
| `DenseAdvance` | `FlankTarget` | `calcDenseSlots(center=sectorPos)` |
| `WedgeCharge` | `FlankTarget` | `calcWedgeSlots()` |
| `BoxAdvance` | `HoldSlot` | 고정 `formationTargetPos` 기준 `calcDenseSlots()` |
| `AlternateAttack` | `EngageTarget` 또는 `AlternateWait` | 순번 기반 |
| `Retreat` | `Retreat` | 없음 |

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

`DenseHold`, `DenseAdvance`, `BoxAdvance`가 이 계산을 공유한다.

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
| 5 | `AlternateWait` | 교대 공격 대기 |
| 6 | `Return` | 스폰 위치 복귀 |
| 7 | `Dead` | 사망 |
| 8 | `HoldSlot` | 슬롯까지 이동 후 위치 유지 |

| TacticalCommandType | 전환 상태 | 저장 데이터 |
|---|---|---|
| `EngageTarget` | `Chase` | `targetId_` |
| `FlankTarget` | `Flank` | `targetId_`, `assignedSlot_`, `slotRefTargetPos_`, `abandonDist_`, `speedMult_` |
| `HoldSlot` | `HoldSlot` | `targetId_`, `assignedSlot_` |
| `AlternateWait` | `AlternateWait` | `targetId_` |
| `Retreat` | `Return` | 없음 |
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

`isAtSlot()`은 `HoldSlot` 상태에서 슬롯까지의 거리가 `separationRadius * 0.25`보다 작으면 true를 반환한다.
현재 시나리오의 `separationRadius = 6`이므로 도착 허용 거리는 1.5다.

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
| `CONFUSED_DURATION` | 3.0s | Confused용 예약 상수 |

---

## 8. 졸업작품 보고서 작성 가이드

### 8-1. 추천 서술 구조

보고서에는 다음 순서로 쓰면 자연스럽다.

1. 문제 정의: 다수 NPC가 단순 추적만 하면 겹침, 비효율 경로, 전술성 부족이 발생한다.
2. 구조 제안: Boss가 전술 판단을 담당하고, Squad가 대형 위치를 계산하며, 개별 NPC는 FSM으로 실행한다.
3. 초기 행동: 시뮬레이션 시작 시 Squad 단위 BoxAdvance 대형을 만든 뒤 플레이어를 추적한다.
4. 전술 발동: Boss HP 또는 Squad 피해 조건을 만족하면 포위 전술을 발동한다.
5. 수식 설명: 타겟 선택, 대형 슬롯 계산, 포위 섹터 분할, 도착 판정을 제시한다.
6. 검증: 디버그 뷰에서 슬롯 마커, 상태 색상, HP, 타겟선을 확인했다고 설명한다.

### 8-2. 보고서용 문단 예시

```text
본 프로젝트의 전술 NPC 시스템은 중간보스가 상위 지휘관 역할을 수행하고,
NPC들은 Squad 단위로 명령을 받아 움직이는 계층형 AI 구조로 설계하였다.
중간보스는 일정 주기마다 플레이어와 아군 상태를 평가하여 SquadOrder를 생성하고,
각 Squad는 이를 실제 월드 좌표상의 대형 슬롯으로 변환한다.
개별 TacticalNpc는 Squad가 전달한 TacticalCommand를 소비하여 Chase, HoldSlot,
Flank, AttackWindup 등의 FSM 상태를 실행한다.

시뮬레이션 초기에 중간보스는 BoxAdvance 명령을 통해 3개 Squad를 플레이어 전방의
박스형 대형으로 배치한다. 이때 대형 목표점은 명령 발행 시점의 플레이어 위치로
고정하여, 플레이어가 이동하더라도 NPC들이 흔들리지 않고 최초 대형 슬롯에 도착하도록 하였다.
대형이 완성되면 Squad는 Engage 상태로 전환되어 플레이어를 추적한다.

전술 조건은 중간보스의 체력 비율 또는 Squad 생존 비율로 판단한다.
중간보스 HP가 70% 미만이거나 어느 Squad의 생존 비율이 80% 미만이면 전술이 해금된다.
전술이 해금된 상태에서 플레이어가 하나의 군집으로 판단되면, 중간보스는 전체 NPC 수에 비례하여
각 Squad에 원형 포위 섹터를 배분하고, 각 NPC는 자신에게 할당된 포위 슬롯으로 이동한다.
```

### 8-3. 보고서에 넣기 좋은 핵심 수식

타겟 선택:

```text
S(p) = 0.5 * 1 / (1 + ||B - p||)
     + 0.5 * (1 - HP(p) / HPmax(p))
```

전술 발동 조건:

```text
HPboss / HPboss,max < 0.70
or
aliveSquadMembers / initialSquadMembers < 0.80
```

초기 BoxAdvance 대형:

```text
P0 = player position at order issue time
f = normalize(P0 - B)
r = (-fz, 0, fx)

C_s = P0 - f * d_approach + r * offset_x - f * offset_z - f * halfDepth
```

포위 섹터 분할:

```text
sectorSpan_s = 2π * n_s / N
sectorAngle_s = angleAccum + sectorSpan_s / 2
```

포위 슬롯:

```text
theta_i = sectorAngle_s - sectorSpan_s / 2 + (i + 0.5) * sectorSpan_s / n_s
slot_i = P + R * (cos(theta_i), 0, sin(theta_i))
```

도착 판정:

```text
arrived_i = ||pos_i - slot_i|| < separationRadius * 0.25
```

### 8-4. 강조하면 좋은 설계 포인트

- 전술 판단과 개별 이동을 분리해 확장성이 높다.
- Squad 단위 명령을 사용해 60명 NPC를 개별로 직접 제어하지 않아도 된다.
- 초기 BoxAdvance는 목표점을 고정해 플레이어 이동으로 인한 대형 미완성 문제를 줄인다.
- 포위는 Squad 인원 비율에 따라 섹터를 배분하므로 Squad 크기가 달라도 자연스럽게 확장된다.
- `Room::buildSnapshot()`과 GDI 렌더러로 슬롯, 상태, 타겟을 시각화해 디버깅 가능하다.
