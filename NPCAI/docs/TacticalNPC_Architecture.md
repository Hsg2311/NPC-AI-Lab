# Tactical NPC 시스템 구조

> 작성: 2026-05-06
> 대상 파일: `sim/PlatoonLeader`, `sim/TacticalSquad`, `sim/TacticalNpc`

전술 NPC 시스템은 **3계층 구조**다.
각 계층은 서로 다른 추상 수준에서 동작하며, 위에서 아래로 단방향으로 명령을 전달한다.

```
PlatoonLeader   ← 전술 판단. "어떤 전술을 쓸 것인가"
      │  SquadOrder (매 1초)
      ▼
TacticalSquad   ← 대형 변환. "어느 위치에 배치할 것인가"
      │  TacticalCommand (매 틱 또는 명령 수신 시)
      ▼
TacticalNpc     ← 개별 행동. "나는 지금 무엇을 할 것인가"
```

---

## 1. tick() 내 업데이트 순서

`Room::tick(dt)` 안에서 아래 순서로 실행된다. **순서가 곧 설계 의도**다.

```
7a. updatePlatoonLeaders(dt)
    └─ PlatoonLeader::update()
         ├─ evaluateTactics() 호출 (매 1초 간격)
         │   └─ 각 TacticalSquad에 SquadOrder 발행
         └─ 자체 전투 FSM (Chase / AttackWindup / AttackRecover)

7b. updateTacticalSquads(dt)
    └─ TacticalSquad::update()
         ├─ removeDeadMembers()
         └─ orderDirty_ == true → pushCommandsToMembers()
              └─ 각 TacticalNpc에 TacticalCommand 발행

7c. updateTacticalNpcMembers(dt)
    └─ TacticalNpc::update()   (PlatoonLeader 제외)
         ├─ consumePendingCommand()  ← 7b에서 저장된 명령 소비
         └─ 현재 state_ 에 맞는 updateXxx() 실행
```

이 순서 덕분에 같은 틱 안에서 리더 결정 → 스쿼드 변환 → NPC 실행이 완료된다.

---

## 2. PlatoonLeader — 전술 평가

### 2-1. evaluateTactics() 호출 주기

`PlatoonLeader::update()`에서 `tacticTimer_`를 감산하고, 0 이하가 되면 `evaluateTactics()`를 호출한다.

```cpp
tacticTimer_ -= dt;
if (tacticTimer_ <= 0.f) {
    tacticTimer_ = TACTIC_INTERVAL;   // 1.0초
    evaluateTactics(room);
}
```

전술 평가는 **1초에 1번**만 한다. 매 틱 하지 않는 이유는 슬롯 재계산 비용과 NPC 상태 안정성 때문이다.

### 2-2. evaluateTactics() 전체 흐름

```
① 사망 멤버 제거 + liveSquads 수집
        │
        ▼
② 플레이어 선택 (selectPrimaryTarget)
   → 없으면 전체 Idle 명령 후 종료
        │
        ▼
③ 리더 자신의 targetId_ 갱신 (항상 primary 추격)
        │
        ▼
④ 전술 잠금 해제 확인 (checkTacticsConditions)
   tacticsUnlocked_ = false 라면 → 전체 Engage 발행 후 종료
        │
        ▼
⑤ 쿨타임 확인
   tacticsOnCooldown_ == true 라면 → 전체 Engage 발행 후 종료
        │
        ▼
⑥ 플레이어 분산 여부 판단 (clusterPlayers)
   │
   ├─ 집합(군집 1개) → 포위(Encircle) 분기
   └─ 분산(군집 2개+) → 경계/각개격파 분기
```

### 2-3. 전술 잠금 해제 조건 (checkTacticsConditions)

전술은 초기에 잠겨 있다(`tacticsUnlocked_ = false`). 아래 중 하나라도 충족되면 영구 잠금 해제된다.

| 조건 | 임계값 상수 |
|---|---|
| 리더 HP ≤ maxHp × 0.70 | `TACTIC_HP_THRESHOLD = 0.70f` |
| 어느 Squad든 생존 비율 < 초기 인원 × 0.80 | `TACTIC_SQUAD_RATIO = 0.80f` |

잠금 해제는 단방향(`false → true`)이다. 조건이 사라져도 다시 잠기지 않는다.

### 2-4. 쿨타임 시스템

```
[전술 발동] → 슬롯 발행 (encircleSlotsAssigned_ = true)
                   │
         NPC들 슬롯으로 이동 중
                   │
     allMembersArrived() == true
                   │ 즉시
         tacticsOnCooldown_ = true
         tacticCooldown_ = 8.0초
         encircleSlotsAssigned_ = false
         모든 Squad → Engage
                   │
         8초 경과
                   │
         tacticsOnCooldown_ = false
                   │
         다음 evaluateTactics()에서
         !encircleSlotsAssigned_ 감지
         → 현재 플레이어 위치 기준 새 슬롯 발행
```

`allMembersArrived()`는 모든 생존 Squad 멤버의 `TacticalNpc::isAtSlot()`을 확인한다.

### 2-5. TacticalPhase 전환

```
         플레이어 집합
              │
         ┌────▼────┐
         │Encircle │◄──────────────────────────┐
         └────┬────┘     플레이어 다시 집합      │
              │ 플레이어 분산                    │
         ┌────▼─────┐                          │
         │Vigilance │                          │
         └────┬─────┘                          │
              │ 5초 경과                        │
         ┌────▼─────────────┐                  │
         │DivideAndConquer  │──────────────────┘
         └──────────────────┘
```

각 페이즈가 발행하는 SquadOrder:

| 페이즈 | Squad 명령 | 조건 |
|---|---|---|
| `Encircle` | `Encircle` — 비례 섹터 포위 | 플레이어 군집 = 1 |
| `Vigilance` | `DenseHold` — 현재 위치 유지 | 플레이어 군집 ≥ 2 (전환 직후) |
| `DivideAndConquer` | Squad[0]: `WedgeCharge`, Squad[1+]: `DenseHold` | Vigilance 5초 후 |

### 2-6. SquadOrder 발행 — Encircle 예시 (3개 부대)

```cpp
// 전체 인원 수 집계
int totalMembers = 합(liveSquads[i]->getMembers().size());

// 각 Squad에 인원 비율에 따른 섹터 배분
float angleAccum = 0.f;
for (int i = 0; i < numSquads; ++i) {
    float fraction   = memberCount_i / totalMembers;
    float sectorSpan = 2π × fraction;    // 이 Squad의 섹터 폭
    float sectorAngle = angleAccum + sectorSpan × 0.5;  // 섹터 중심 각도

    SquadOrder ord;
    ord.type          = SquadOrderType::Encircle;
    ord.targetId      = primary->getId();
    ord.sectorAngle   = sectorAngle;
    ord.sectorSpan    = sectorSpan;
    ord.approachRadius = 20.0f;           // ENCIRCLE_RADIUS
    squad[i]->receiveOrder(ord);

    angleAccum += sectorSpan;
}
```

20명 Squad가 3개(동일 인원)라면 각 Squad는 120°(2π/3) 섹터를 담당한다.

---

## 3. TacticalSquad — 명령 변환

### 3-1. SquadOrder를 받으면

```cpp
void TacticalSquad::receiveOrder(const SquadOrder& order) {
    currentOrder_ = order;
    orderDirty_   = true;   // 다음 update()에서 1회 pushCommandsToMembers() 호출
}
```

`orderDirty_`가 세워지면 다음 `update()`에서 `pushCommandsToMembers()`를 호출하고 플래그를 내린다.

### 3-2. 갱신 정책

| SquadOrderType | 슬롯 재계산 시점 | 이유 |
|---|---|---|
| `Engage`, `Idle`, `Retreat`, `AlternateAttack` | 명령 수신 시 1회 | 슬롯 없음, 매 틱 재발행해도 동일 |
| `FlankLeft`, `FlankRight` | 명령 수신 시 1회 | 의도적 포지션 고정 |
| `Encircle` | 명령 수신 시 1회 | 포위 슬롯 고정 (플레이어 이동 무시) |
| `DenseHold` | 명령 수신 시 1회 | 경계 위치 고정 |
| `DenseAdvance` | 명령 수신 시 1회 | PlatoonLeader가 새 사이클에만 재발행 |
| `WedgeCharge` | **매 틱** | 타겟이 이동하므로 슬롯 추적 필요 |

```cpp
void TacticalSquad::update(float dt, Room& room) {
    removeDeadMembers(room);
    if (orderDirty_) {
        pushCommandsToMembers(room);
        orderDirty_ = false;
    } else if (currentOrder_.type == SquadOrderType::WedgeCharge) {
        pushCommandsToMembers(room);  // 매 틱
    }
}
```

### 3-3. SquadOrder → TacticalCommand 변환표

| SquadOrderType | 발행되는 TacticalCommandType | 슬롯 계산 함수 |
|---|---|---|
| `Idle` | `Idle` | — |
| `Engage` | `EngageTarget` | — |
| `FlankLeft` | `FlankTarget` + 슬롯 좌표 | `calcFlankSlots(leftSide=true)` |
| `FlankRight` | `FlankTarget` + 슬롯 좌표 | `calcFlankSlots(leftSide=false)` |
| `Encircle` | `HoldSlot` + 슬롯 좌표 (greedy nearest-slot) | `calcEncircleSlots()` |
| `DenseHold` | `HoldSlot` + 슬롯 좌표 | `calcDenseSlots(center=centroid)` |
| `DenseAdvance` | `FlankTarget` + 슬롯 좌표 | `calcDenseSlots(center=sectorPos)` |
| `WedgeCharge` | `FlankTarget` + 슬롯 좌표 | `calcWedgeSlots()` |
| `AlternateAttack` | `EngageTarget` (공격 차례) / `AlternateWait` (대기 차례) | — |
| `Retreat` | `Retreat` | — |

### 3-4. 슬롯 계산 상세

#### calcEncircleSlots — 원호 배치 (center-of-subdivision)

```
arc   = sectorSpan / count           // 소구역 폭
start = sectorAngle - sectorSpan/2 + arc/2   // 첫 소구역 중심

slot[i] = targetPos + { cos(start + arc*i), 0, sin(start + arc*i) } * radius
```

경계(0번, count-1번)가 아닌 **소구역 중심**에 배치하므로 인접 Squad 슬롯이 같은 각도에 겹치지 않는다.

할당은 **greedy nearest-slot**: 각 NPC마다 미사용 슬롯 중 가장 가까운 슬롯을 배정한다.

```cpp
for (int i = 0; i < count; ++i) {
    // NPC i에서 모든 미사용 슬롯 중 최소 거리 슬롯 탐색
    int   bestSlot = -1;
    float bestDist = -1.f;
    for (int j = 0; j < count; ++j) {
        if (slotUsed[j]) continue;
        float d = distance(npc[i].pos, slots[j]);
        if (d < bestDist || bestDist < 0) { bestDist = d; bestSlot = j; }
    }
    slotUsed[bestSlot] = true;
    // HoldSlot 명령 발행
}
```

#### calcFlankSlots — 측면 선형 배치

```
dir     = normalize(targetPos - leaderPos)    // 리더 → 타겟 방향
side    = 좌측: (+dir.z, 0, -dir.x)
          우측: (-dir.z, 0, +dir.x)
spacing = attackRange + 1.5

slot[i] = targetPos + side * radius + dir * (i * spacing)
```

타겟에서 측면으로 `radius`만큼 떨어진 지점부터 정면 방향으로 `spacing` 간격으로 나열.

#### calcDenseSlots — 직사각형 그리드

```
cols    = ceil(sqrt(count))
spacing = max(attackRange * 0.8, 1.2)
right   = (-forward.z, 0, forward.x)    // XZ 평면 우방향

slot[i] = center
        + right   * (col - (cols-1)/2) * spacing
        + forward * (row - (rows-1)/2) * spacing
```

`DenseHold`: center = Squad 멤버 centroid, forward = centroid → 타겟 방향
`DenseAdvance`: center = sectorPos, forward = sectorPos → 플레이어 centroid 방향

#### calcWedgeSlots — V자 쐐기

```
forward = normalize(targetPos - fromPos)    // fromPos = 리더 위치
tip     = targetPos - forward * attackRange  // 첨단

row 0: 1명  (tip)
row 1: 2명  (tip - forward * spacing*1.5)
row 2: 3명  (tip - forward * spacing*3.0)
...

각 행 내 좌우 간격 = spacing = max(attackRange*1.2, 1.5)
```

---

## 4. TacticalNpc — 개별 FSM

### 4-1. pendingCmd_ 소비 메커니즘

TacticalNpc는 명령을 **저장**만 한다. 명령이 실제로 적용되는 것은 다음 `update()` 시작 시점이다.

```
// TacticalSquad가 호출
void TacticalNpc::receiveCommand(const TacticalCommand& cmd) {
    pendingCmd_ = cmd;   // 덮어쓰기 — 가장 최신 명령만 유효
}

// 다음 tick에서
void TacticalNpc::update(float dt, Room& room) {
    if (!alive_) { updateDead(); return; }

    if (pendingCmd_.type != None)
        consumePendingCommand();   // 명령 소비 → 상태 전이

    switch (state_) { ... }       // 현재 상태 업데이트
}
```

같은 틱에 여러 명령이 오면 **마지막 것만** 남는다.
명령은 **어떤 상태에서든** 즉시 적용된다 (AttackWindup 중에도 HoldSlot 명령이 오면 강제 전환).

### 4-2. 명령 → 상태 전이 표

| TacticalCommandType | 전환되는 상태 | 저장되는 데이터 |
|---|---|---|
| `EngageTarget` | `Chase` | `targetId_` |
| `FlankTarget` | `Flank` | `targetId_`, `assignedSlot_`, `slotRefTargetPos_`, `abandonDist_`, `speedMult_` |
| `HoldSlot` | `HoldSlot` | `targetId_`, `assignedSlot_` |
| `AlternateWait` | `AlternateWait` | `targetId_` |
| `Retreat` | `Return` | — |
| `Idle` | `Idle` | `targetId_ = 0` |
| `Confused` | `Idle` | `targetId_ = 0` |

### 4-3. 상태 목록

| 값 | 상태 | 자율 행동 | 탈출 조건 |
|---|---|---|---|
| 0 | `Idle` | 아무것도 안 함 | 명령 수신 시 |
| 1 | `Chase` | 타겟 추격 + separation force | 사정거리 진입 → Windup / 타겟 소실 → Idle |
| 2 | `AttackWindup` | 이동 없음, windupTimer 누적 | 완료 → AttackRecover / 타겟 소실 → Idle |
| 3 | `AttackRecover` | 체반경 하드 push만 허용, recoverTimer 누적 | 완료 → Windup 또는 Chase / 타겟 소실 → Idle |
| 4 | `Flank` | 슬롯까지 이동 (속도 × TACTICAL_SPEED_MULT) | 슬롯 도착 → Chase 또는 Windup / 타겟 이탈 → Chase |
| 5 | `AlternateWait` | 제자리 대기 | 명령 수신 시 (EngageTarget → Chase) |
| 6 | `Return` | 스폰 위치로 귀환 (속도 × 2.0) | 도착 → Idle |
| 7 | `Dead` | 종단 상태 | 없음 |
| 8 | `HoldSlot` | 슬롯까지 이동 후 타겟 방향 facing 유지, 공격 없음 | 타겟 소실 → Idle |

### 4-4. 상태별 핵심 코드

#### Chase — 분리력 수직 투영

추격 방향과 **수직인** 성분만 분리력에 적용한다. 역방향(뒤로) 이동이 발생하지 않는다.

```cpp
Vec3 chaseDir = (target.pos - position_).normalized();
Vec3 sep      = calcSeparationForce(separationRadius_, nearby);
Vec3 sepPerp  = sep - chaseDir * sep.dot(chaseDir);  // 수직 성분만
Vec3 moveDir  = (chaseDir + sepPerp * separationWeight_).normalized();
position_    += moveDir * moveSpeed_ * dt;
```

#### AttackRecover — 체반경 최소 push

경직 중에는 큰 분리력이 아닌, 실제로 겹쳤을 때만 최소한의 반발을 준다.

```cpp
constexpr float BODY_RADIUS = 0.8f;
room.findNearbyNpcPositions(position_, BODY_RADIUS * 2.f, ...);
Vec3 push = calcSeparationForce(BODY_RADIUS * 2.f, nearby);
if (push.length() > 0.1f)
    position_ += push.normalized() * (moveSpeed_ * 0.15f * dt);
```

#### Flank — 속도 배율 + 슬롯 포기

```cpp
// 타겟이 슬롯 계산 시점 위치에서 abandonDist_ 이상 이탈하면 포기
float drift = distance(target.pos, slotRefTargetPos_);
if (drift > abandonDist_) {
    transitionTo(Chase, "타겟 이탈, 슬롯 포기");
    return;
}
position_ += moveDir * (moveSpeed_ * TACTICAL_SPEED_MULT * speedMult_ * dt);
```

#### HoldSlot — 도착 직전 진동 방지

분리력에 거리 기반 감쇠를 적용한다. 슬롯에 가까울수록 분리력이 줄어든다.

```cpp
float distToSlot = distance(position_, assignedSlot_);
if (distToSlot < 0.5f) {
    // 도착 — 플레이어 방향 유지만
    facing_ = (target.pos - position_).normalized();
    return;
}
float sepScale = min(1.f, distToSlot / separationRadius_);
Vec3  moveDir  = (slotDir + sep * (separationWeight_ * sepScale)).normalized();
position_     += moveDir * (moveSpeed_ * TACTICAL_SPEED_MULT * dt);
```

#### isAtSlot() — allMembersArrived 판정용

```cpp
bool TacticalNpc::isAtSlot() const {
    if (state_ == Flank)    return false;          // Flank는 도착 시 상태 전환됨
    if (state_ == HoldSlot) return distToSlot < 0.5f;
    return true;                                   // 다른 상태는 슬롯 이동 중 아님
}
```

---

## 5. 전체 데이터 흐름 요약

```
PlatoonLeader::evaluateTactics()          (매 1초)
│
│ 판단: 플레이어 1명, 전술 활성, 쿨타임 아님
│
│  SquadOrder {
│      type         = Encircle
│      targetId     = P1.id
│      sectorAngle  = 0.0  (Squad A)
│      sectorSpan   = 2.09 (= 2π/3)
│      approachRadius = 20.0
│  }
│
▼
TacticalSquad::receiveOrder()
  → currentOrder_ = ord
  → orderDirty_   = true

TacticalSquad::update()   (다음 tick, 7b단계)
  → pushCommandsToMembers()
      → calcEncircleSlots(targetPos, 0.0, 2.09, 20.0, 20)
          → 20개 슬롯 좌표 생성
      → greedy nearest-slot 할당
      → 각 TacticalNpc에:
           TacticalCommand {
               type       = HoldSlot
               targetId   = P1.id
               slotOffset = (23.4, 0, -8.1)   // 예시
           }
           tnpc->receiveCommand(cmd)           // pendingCmd_ 에 저장

TacticalNpc::update()   (같은 tick, 7c단계)
  → consumePendingCommand()
      → assignedSlot_ = (23.4, 0, -8.1)
      → transitionTo(HoldSlot)
  → updateHoldSlot()
      → slotDir = normalize(slot - pos)
      → 분리력 감쇠 적용
      → position_ += slotDir * moveSpeed_ * TACTICAL_SPEED_MULT * dt
```

---

## 6. 파라미터 한눈에 보기

### PlatoonLeader 상수 (`sim/PlatoonLeader.hpp`)

| 상수 | 값 | 의미 |
|---|---|---|
| `TACTIC_INTERVAL` | 1.0s | evaluateTactics() 호출 간격 |
| `VIGILANCE_DURATION` | 5.0s | Vigilance → DivideAndConquer 전환 시간 |
| `CLUSTER_RADIUS` | 10.0 | 플레이어 군집 판단 반경 |
| `ENCIRCLE_RADIUS` | 20.0 | 포위 슬롯 배치 반경 |
| `TACTIC_HP_THRESHOLD` | 0.70 | 전술 발동 리더 HP 임계값 |
| `TACTIC_SQUAD_RATIO` | 0.80 | 전술 발동 생존 비율 임계값 |
| `ENCIRCLE_RECALC_THRESHOLD` | 12.0 | (미사용 — 레거시) |
| `TACTIC_COOLDOWN_DURATION` | 8.0s | 포위 완성 후 쿨타임 길이 |

### TacticalNpc 상수 (`sim/TacticalNpc.hpp`)

| 상수 | 값 | 의미 |
|---|---|---|
| `CONFUSED_DURATION` | 3.0s | Confused 상태 지속 시간 |
| `TACTICAL_SPEED_MULT` | 3.0 | Flank / HoldSlot 이동 속도 배율 |

### TacticalNpcConfig (ScenarioTactical 기본값)

| 파라미터 | 일반 NPC | Boss(PlatoonLeader) |
|---|---|---|
| `maxHp` | 80 | 200 |
| `moveSpeed` | 9.0 | 10.0 |
| `attackRange` | 2.0 | 2.5 |
| `attackDamage` | 10.0 | 10.0 |
| `attackWindupTime` | 0.35s | 0.35s |
| `attackRecoverTime` | 0.70s | 0.70s |
| `separationRadius` | 6.0 | 6.0 |
| `separationWeight` | 1.5 | 1.5 |
