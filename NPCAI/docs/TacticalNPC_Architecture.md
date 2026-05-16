# Tactical NPC Architecture

> 갱신: 2026-05-15  
> 대상: `sim/IMidBossTactic`, `sim/MidBossTactics`, `sim/PlatoonLeader`, `sim/TacticalSquad`, `sim/TacticalNpc`, `sim/Room`

Tactical NPC 시스템은 **전술 전략 - 지휘관 - 부대 - 개별 NPC**의 4계층 구조다.

```text
Room
  -> PlatoonLeader
      -> IMidBossTactic
          -> MidBossTacticBase
              -> GoblinMidBossTactic
              -> GrandBaumMidBossTactic
      -> TacticalSquad
          -> TacticalNpc
```

핵심 원칙은 다음과 같다.

- `IMidBossTactic` 구현체가 종족별 전술 판단과 전술 상태를 소유한다.
- `MidBossTacticBase`는 군집 계산, 평균 위치, Engage/Idle 발행 같은 종족 무관 helper만 제공한다.
- `PlatoonLeader`는 종족을 모르는 공용 지휘관이며, 부대 목록과 전술 객체를 보유한다.
- `TacticalSquad`는 부대 명령을 멤버별 슬롯과 `TacticalCommand`로 변환한다.
- `TacticalNpc`는 명령을 받아 개별 FSM으로 이동, 공격, 대기, 돌진을 실행한다.

---

## 1. 업데이트 순서

`Room::tick(dt)`는 전술 NPC 시스템이 같은 tick 안에서 상위 판단부터 하위 실행까지 진행되도록 순서를 고정한다.

```text
1. Logger tick 동기화
2. DummyPlayerController 업데이트
3. Player 업데이트
4. NpcGroup 공유 시야 메모리 업데이트
5. livingPlayers / aggroCount / spatialGrid 캐시 재구성
6. 일반 Npc 업데이트
7. PlatoonLeader 업데이트
8. TacticalSquad 업데이트
9. TacticalNpc 멤버 업데이트 (PlatoonLeader 제외)
10. tick 증가
```

전술 명령 흐름은 다음과 같다.

```text
PlatoonLeader::update()
  -> tactic_->update(dt, room, leader)
  -> 종족별 전술이 SquadOrder 발행

TacticalSquad::update()
  -> SquadOrder를 슬롯과 TacticalCommand로 변환
  -> 각 TacticalNpc::receiveCommand(cmd)

TacticalNpc::update()
  -> pendingCmd_ 소비
  -> TacticalNpcState별 FSM 실행
```

이 구조 때문에 리더가 같은 tick에 내린 명령은 먼저 Squad에서 변환되고, 그 다음 개별 NPC가 실행한다.

---

## 2. PlatoonLeader

`PlatoonLeader`는 `TacticalNpc`를 상속하지만, 현재 구조에서는 종족별 전술을 직접 소유하지 않는다. 고블린 전술 phase, 그랜드밤 방패벽 phase, 각개격파 상태 등은 모두 `IMidBossTactic` 구현체 안에 있다.

`PlatoonLeader`의 책임:

- 소속 `TacticalSquad*` 목록 관리
- `std::unique_ptr<IMidBossTactic>` 보유
- 매 tick 전술 객체에 업데이트 위임
- 사망 시 전술 객체의 `onLeaderDead()` 호출
- 전술 객체가 필요로 하는 공용 조작 API 제공

주요 API:

```cpp
void addSquad(TacticalSquad* squad);
const std::vector<TacticalSquad*>& getSquads() const;
void setTactic(std::unique_ptr<IMidBossTactic> tactic);

void removeDeadMembersFromSquads(Room& room);
void pushConfusedToSquads(Room& room);
void setTacticalTarget(uint32_t targetId);
void transitionTacticalState(TacticalNpcState next, const char* reason);
float getLeaderMoveSpeed() const;
```

주의할 점:

- `PlatoonLeader`에는 `LeaderPhase`, `TACTIC_*`, `BOX_*`, `BOSS_KEEP_*` 같은 고블린 전용 상태와 상수가 없다.
- 새 종족을 추가할 때 `PlatoonLeader`를 상속하거나 수정하지 않고, 새 `IMidBossTactic` 구현체를 만든다.

---

## 3. IMidBossTactic

`IMidBossTactic`은 중간보스 종족별 전술의 공통 인터페이스다.

```cpp
class IMidBossTactic {
public:
    virtual ~IMidBossTactic() = default;

    virtual const char* name() const = 0;
    virtual void update(float dt, Room& room, PlatoonLeader& leader) = 0;
    virtual void onLeaderDead(Room& room, PlatoonLeader& leader) = 0;
};
```

구현체의 책임:

- 전술 발동 조건 판단
- phase/state 관리
- 타겟 선정
- 플레이어 군집 분석
- `SquadOrder` 발행
- 리더 사망 시 부대 후속 처리

현재 구현체:

| 구현체 | 역할 |
|---|---|
| `MidBossTacticBase` | 종족 무관 공용 helper |
| `GoblinMidBossTactic` | 기존 고블린 중간보스 전술 전체 |
| `GrandBaumMidBossTactic` | 그랜드밤 방패벽 및 (ㄹ) 외곽 웨이브 기습 |

`MidBossTacticBase`는 전술 phase를 갖지 않는다. `PlayerCluster` 생성, 플레이어 centroid/facing 계산, 가장 가까운 플레이어 선택, Squad별 플레이어 타겟 분배, 전체 Squad `Engage`/`Idle` 발행, 리더 사망 시 `Confused` 발행만 담당한다.

---

## 4. GoblinMidBossTactic

`GoblinMidBossTactic`은 기존 고블린 중간보스 전술을 완전히 소유한다.

### 4-1. Phase

```text
BoxAdvance
Engage
TacticalRetreat
Encircle
Vigilance
DivideAndConquer
Cooldown
```

기본 흐름:

```text
전술 미해금:
  BoxAdvance -> Engage

전술 조건 충족:
  Engage 또는 BoxAdvance -> TacticalRetreat -> BoxAdvance

BoxAdvance 완료 후:
  플레이어 군집 1개 -> Encircle
  플레이어 군집 2개 이상 -> Vigilance

Vigilance 완료 후:
  플레이어 군집 1개 -> Encircle
  플레이어 군집 2개 이상 -> DivideAndConquer

Encircle 또는 DivideAndConquer 완료:
  Cooldown

Cooldown 종료:
  전술 해금 상태면 TacticalRetreat부터 재시작
```

### 4-2. 발동 조건

고블린 전술 해금은 다음 중 하나를 만족하면 발생한다.

| 조건 | 판정 | 상수 |
|---|---|---|
| 리더 체력 감소 | `leader.hp / leader.maxHp <= 0.70` | `TACTIC_HP_THRESHOLD` |
| 부대 손실 | `aliveMembers / initialMembers <= 0.80` | `TACTIC_SQUAD_RATIO` |

### 4-3. 주요 전술

`BoxAdvance`:

- 플레이어 centroid 방향으로 리더 앞쪽에 박스 대형 중심을 만든다.
- 각 Squad는 박스 offset을 받아 `SquadOrderType::BoxAdvance`를 수행한다.
- 완료 시 플레이어 군집 수에 따라 `Encircle` 또는 `Vigilance`로 갈라진다.

`TacticalRetreat`:

- 플레이어 centroid 반대 방향으로 `REGROUP_DIST`만큼 후퇴한다.
- Squad는 `RetreatFormUp`으로 현재 배치를 유지한 채 같은 방향으로 이동한다.

`Encircle`:

- 플레이어 centroid 주변 원형 섹터를 Squad별 생존 인원 비율로 나눈다.
- Squad는 `Encircle` 명령을 받아 멤버별 `HoldSlot`으로 이동한다.
- 모든 멤버가 슬롯에 도착하면 `Engage` 후 `Cooldown`에 들어간다.

`Vigilance`:

- 리더 주변에 Guard 대형을 만든다.
- 모든 멤버가 도착하면 플레이어 군집 수를 다시 판단한다.

`DivideAndConquer`:

- 가장 위협적인 플레이어 군집에 가장 가까운 Squad를 `WedgeCharge`로 배정한다.
- 나머지 Squad는 `GuardBoss`를 활용해 군집 사이 차단선을 만든다.
- 돌진/차단 임무가 끝나면 일정 시간 `Engage`를 유지한 뒤 `Cooldown`에 들어간다.

---

## 5. GrandBaumMidBossTactic

`GrandBaumMidBossTactic`은 그랜드밤 종족 전술만 담당한다. 현재 구현 범위는 `방패벽`과 `(ㄹ) 부대 외곽 웨이브형 주의-기습`이다.

### 5-1. Phase

```text
Engage
ShieldWall
Cooldown
```

기본 흐름:

```text
시작:
  모든 Squad -> Engage
  여러 플레이어가 있으면 Squad별로 타겟 분산

전술 조건 충족:
  Engage -> ShieldWall

ShieldWall 완료:
  (ㄹ) 뱀 부대 전멸
  모든 Squad -> Engage
  Cooldown

Cooldown 종료:
  Engage 복귀
```

`ShieldWall` 안에서 `(ㄹ) 부대 주의-기습`이 함께 진행된다. `(ㄱ)/(ㄴ)/(ㄷ)` 슬라임 부대는 중간보스를 원 형태로 포위하고 바깥쪽을 바라보며 지킨다. `(ㄹ)` 원본 뱀 부대는 외곽으로 빠르게 후퇴하고, 생존 원본 뱀 수에 비례한 임시 외곽 웨이브가 플레이어를 압박한다. 방패벽은 임시 외곽 웨이브가 전멸하면 종료된다.

일반 `Engage` 상태에서는 고블린과 같은 공용 타겟 분배 helper를 사용한다. 각 Squad 중심과 플레이어 위치를 비교해 가까운 플레이어에게 배정하되, 한 플레이어에게 모든 Squad가 몰리지 않도록 플레이어별 최대 배정 수를 제한한다.

### 5-2. 방패벽 발동

발동 조건:

```text
leader.hp / leader.maxHp <= grandBaumA
```

기본 `grandBaumA` 값은 `0.5f`다.

전술 재발동은 `TACTIC_COOLDOWN_DURATION` 동안 막힌다. 쿨타임 중에도 부대는 일반 `Engage` 상태를 유지한다.

방패벽 배치:

```text
playerCentroid = 살아있는 플레이어 위치 평균
forward        = normalize(playerCentroid - grandBaumPos)

slimeRingCenter = grandBaumPos
slimeRingRadius = SHIELD_RING_RADIUS
slimeFacing     = normalize(slimeSlot - grandBaumPos)
```

슬라임 링 슬롯은 방패벽 발동 시점에 한 번만 계산한다. 이후 플레이어가 이동하더라도 `(ㄱ)/(ㄴ)/(ㄷ)` 슬라임의 슬롯은 재배정되지 않으며, 방패벽이 종료될 때까지 같은 링 위치를 유지한다.

방패벽 발동 순간 GrandBaum의 원형 방패벽 안쪽에 있는 플레이어는 중심에서 바깥쪽으로 짧고 강한 넉백을 받는다. 이는 순간이동 보정이 아니라 `Player`의 넉백 상태로 처리되는 강제 밀림 연출이며, 넉백 중에는 일반 이동 목표보다 밀림 방향이 우선하지만 플레이어의 facing은 넉백 직전 방향을 유지한다. 넉백 종료 후에도 약 `0.25`초 동안 일반 이동을 잠깐 막아 밀려난 충격이 남도록 한다. 내부 플레이어가 슬라임 하드 블록에 다시 막히지 않도록 방패벽 발동 넉백 중에는 방패벽 하드 블록만 무시하고, 기본 넉백 속도는 `SHIELD_WALL_KNOCKBACK_SPEED = 120.f`다.

방패벽 중 `(ㄱ)/(ㄴ)/(ㄷ)` 슬라임은 플레이어 이동에 대한 물리 차단체로 등록된다. 플레이어가 슬라임 충돌 반경 안으로 이동하려 하면 이동 결과가 링 바깥 경계로 보정되어 중간보스 안쪽으로 뚫고 들어갈 수 없다.

방패벽 중 `GrandBaum` 중간보스와 `(ㄱ)/(ㄴ)/(ㄷ)` 슬라임 부대는 받는 피해가 `70%` 감소한다. 구현상 피해 배율은 `SHIELDWALL_DAMAGE_MULT = 0.3f`이며, `(ㄹ)` 뱀 부대에는 적용하지 않는다.

부대 해석:

| 부대 순서 | 의미 | 명령 |
|---|---|---|
| `squads[0]` | (ㄱ) 슬라임 원형 호위 | `RingGuard` |
| `squads[1]` | (ㄴ) 슬라임 원형 호위 | `RingGuard` |
| `squads[2]` | (ㄷ) 슬라임 원형 호위 | `RingGuard` |
| `squads[3]` | (ㄹ) 뱀족 외곽 회피/웨이브 기습 | `FormationHold` 후퇴 후 임시 웨이브 `DistributedEngage` |

### 5-3. (ㄹ) 주의-기습

- 평상시 `(ㄹ)` 뱀 부대는 플레이어와 정면 교전하지 않고 플레이어 centroid 외곽을 `FormationHold`로 계속 움직인다.
- 회피 이동은 플레이어가 추격하면 잡을 수 있도록 `SNAKE_EVASION_SPEED_MULT = 0.75f`를 사용한다.
- 방패벽 발동 직전 살아 있는 원본 뱀 수를 먼저 확인한다. 생존 수가 `0`이면 슬라임 링/넉백/피해 감소/하드 블록을 적용하지 않고 방패벽 전술을 즉시 실패 처리한 뒤 쿨다운에 들어간다.
- 생존 원본 뱀이 있으면 방패벽을 정상 발동하고, 원본 뱀 부대는 `shieldWallRingCenter` 기준 외곽 반경 `SNAKE_OUTER_RADIUS = 64.f`로 빠르게 후퇴한다.
- 원본 뱀이 외곽 슬롯에 도착하거나 `SNAKE_RETREAT_MAX_TIME = 1.5f`가 지나면 임시 외곽 뱀 웨이브가 등장한다.
- 웨이브 수는 `min(생존 원본 뱀 수 * 10, 60)`을 4의 배수로 내림 정렬한다.

```text
0마리 생존 -> 0마리 소환, 방패벽 즉시 실패
1마리 생존 -> 8마리 소환
2마리 생존 -> 20마리 소환
3마리 생존 -> 28마리 소환
4마리 생존 -> 40마리 소환
5마리 생존 -> 48마리 소환
6마리 이상 -> 60마리 소환
```

- 외곽 웨이브 뱀은 `shieldWallRingCenter` 기준 원 위에 균등 배치된다.
- 웨이브 뱀은 `DistributedEngage` 명령으로 살아 있는 플레이어에게 id 정렬 후 round-robin 분배된다.
- 방패벽 종료 조건은 임시 외곽 웨이브 전멸이다.
- 웨이브 전멸 시 피해 감소와 하드 블록을 해제하고, `(ㄱ)/(ㄴ)/(ㄷ)` 슬라임과 살아남은 원본 `(ㄹ)` 뱀 부대는 다시 `Engage`로 복귀한다.
- 방패벽 종료 후 임시 웨이브 NPC/squad는 `Room` cleanup API로 제거된다.

---

## 6. TacticalSquad

`TacticalSquad`는 Actor가 아닌 지휘 보조 객체다. 멤버 `TacticalNpc`의 id 목록을 보유하고, `SquadOrder`를 개별 `TacticalCommand`로 변환한다.

### 6-1. 주요 책임

- 생존 멤버 정리
- SquadOrder 저장
- 대형 슬롯 계산
- 멤버별 TacticalCommand 발행
- WedgeCharge 준비/돌진 상태 관리

### 6-2. SquadOrderType

| SquadOrderType | 멤버 명령 | 설명 |
|---|---|---|
| `Idle` | `Idle` | 전투 해제 |
| `Engage` | `EngageTarget` | 지정 타겟 추격/공격 |
| `Encircle` | `HoldSlot` | 플레이어 centroid 주변 원형 섹터 슬롯으로 이동 |
| `DenseHold` | `HoldSlot` | 현재 Squad 중심 기준 밀집 대형 |
| `BoxAdvance` | `HoldSlot` | 리더 앞쪽 박스 대형 슬롯으로 이동 |
| `GuardBoss` | `GuardSlot` | 중심점 주변 Guard 대형 |
| `RetreatFormUp` | `HoldSlot` | 현재 배치를 유지한 채 후퇴 |
| `WedgeCharge` | `HoldSlot` -> `ChargeThrough` | 쐐기 대형 준비 후 돌진 |
| `FormationHold` | `HoldSlot` | 지정 중심/방향 밀집 대형 후 대기 |
| `FormationGuard` | `GuardSlot` | 지정 중심/방향 밀집 대형 후 경계 |
| `RingGuard` | `HoldSlot` | 지정 중심을 원형으로 둘러싸고 바깥쪽을 바라봄 |
| `DistributedEngage` | `EngageTarget` | 멤버 순서대로 `targetIds`를 round-robin 분배해 공격 |

`FormationHold`, `FormationGuard`, `RingGuard`, `DistributedEngage`는 종족을 모르는 공용 명령이다. 그랜드밤 방패벽은 이 명령들을 사용하지만, `TacticalSquad`는 그 명령이 그랜드밤 전술인지 알지 않는다.

### 6-3. 슬롯 계산

Dense 계열 슬롯:

```text
spacing = max(memberSeparationRadius * spacingScale, 1.2)
cols    = fixedColumnCount 또는 ceil(sqrt(count) * columnScale)
rows    = ceil(count / cols)
right   = (-forward.z, 0, forward.x)

slot_i = center
       + right   * colOffset
       + forward * rowOffset
```

Encircle 슬롯:

```text
slot_i = tacticCenter + direction(theta_i) * approachRadius
```

RingGuard 슬롯:

```text
slot_i = tacticCenter + direction(theta_i) * approachRadius
facing = normalize(slot_i - tacticCenter)
```

WedgeCharge 슬롯:

```text
prepareApex = squadCentroid + forward * WEDGE_PREP_APEX_DISTANCE
exitApex    = targetCenter  + forward * WEDGE_EXIT_DISTANCE
```

---

## 7. TacticalNpc

`TacticalNpc`는 개별 전투 실행 FSM이다. 스스로 목표를 탐색하지 않고, Squad가 내려준 `TacticalCommand`를 실행한다.

### 7-1. 상태

| 값 | 상태 | 의미 |
|---:|---|---|
| 0 | `Idle` | 명령 대기 |
| 1 | `Chase` | 타겟 추격 |
| 2 | `AttackWindup` | 공격 준비 |
| 3 | `AttackRecover` | 공격 후 회복 |
| 4 | `Flank` | 지정 측면 슬롯으로 이동 |
| 5 | `ChargeThrough` | 쐐기 돌진 |
| 7 | `Dead` | 사망 |
| 8 | `HoldSlot` | 지정 슬롯 이동/유지 |

### 7-2. 명령

| TacticalCommandType | 전환 상태 | 주요 데이터 |
|---|---|---|
| `EngageTarget` | `Chase` | `targetId` |
| `FlankTarget` | `Flank` | `targetId`, `slotOffset`, `slotRefTargetPos`, `abandonDist`, `speedMult` |
| `HoldSlot` | `HoldSlot` | `targetId`, `slotOffset`, `speedMult` |
| `GuardSlot` | `HoldSlot` | `targetId`, `slotOffset`, `speedMult`, `guardNearestPlayer_ = true` |
| `ChargeThrough` | `ChargeThrough` | `targetId`, `slotOffset`, `chargeDir`, `chargeId`, 피해 설정 |
| `Idle` | `Idle` | 타겟 초기화 |
| `Confused` | `Idle` | 타겟 초기화 |

`GuardSlot`은 상태 자체는 `HoldSlot`을 사용하지만, 도착 후 고정 타겟 대신 가장 가까운 생존 플레이어를 바라본다.

### 7-3. 명령 소비

```text
receiveCommand(cmd)
  -> pendingCmd_ = cmd

update(dt, room)
  -> consumePendingCommand()
  -> state별 update 함수 실행
```

같은 tick에 여러 명령이 들어오면 마지막 `pendingCmd_`만 남는다.

### 7-4. 슬롯 도착 판정

`isAtSlot()`은 Squad가 대형 완료 여부를 판단할 때 사용한다.

| 상태 | true 조건 |
|---|---|
| `HoldSlot` | `distance(position, assignedSlot) < separationRadius * 0.25` |
| `ChargeThrough` | `chargeComplete_ == true` |
| `AttackWindup` | 항상 true |
| `AttackRecover` | 항상 true |

---

## 8. 디버그/시각화

`Room::buildSnapshot()`은 `DebugTacticalNpcEntry`를 만들어 렌더러에 전달한다.

표시 정보:

- 위치, 방향, HP
- TacticalNpc state
- squadId
- leader 여부
- targetId
- assignedSlot
- windup/recover 진행률

슬롯 마커는 전술 대형이 어느 지점을 목표로 하는지 확인할 때 중요하다.

---

## 9. 확장 규칙

새 중간보스 종족을 추가할 때 권장 순서:

1. `IMidBossTactic`을 상속한 새 전술 클래스를 만든다.
2. 전술 phase와 상수는 해당 클래스 내부에 둔다.
3. 기존 `SquadOrderType` 조합으로 표현 가능한지 먼저 확인한다.
4. 표현이 부족할 때만 공용 `SquadOrderType` 또는 `TacticalCommandType`을 추가한다.
5. `PlatoonLeader`에는 종족별 상태나 분기문을 추가하지 않는다.

좋은 예:

```text
OrcMidBossTactic
  -> FormationGuard, Engage, WedgeCharge 조합
```

피해야 할 예:

```cpp
if (race == Goblin) { ... }
else if (race == GrandBaum) { ... }
```

종족별 전술은 `IMidBossTactic` 구현체에 캡슐화하고, `PlatoonLeader`, `TacticalSquad`, `TacticalNpc`는 공용 실행 계층으로 유지한다.

---

## 10. 주요 상수 위치

| 위치 | 상수 예 |
|---|---|
| `MidBossTacticBase` | 공용 helper, 종족별 전술 상수 없음 |
| `GoblinMidBossTactic` | `TACTIC_INTERVAL`, `CLUSTER_RADIUS`, `ENCIRCLE_RADIUS`, `TACTIC_HP_THRESHOLD`, `BOX_FRONT_OFFSET`, `REGROUP_DIST` |
| `GrandBaumMidBossTactic` | `ENGAGE_REFRESH_INTERVAL`, `ORDER_REFRESH_INTERVAL`, `TACTIC_COOLDOWN_DURATION`, `SHIELD_RING_RADIUS`, `SHIELDWALL_DAMAGE_MULT`, `SNAKE_OUTER_RADIUS`, `SNAKE_EVASION_RADIUS`, `SNAKE_EVASION_SPEED_MULT`, `SNAKE_RETREAT_MAX_TIME`, `SNAKE_WAVE_MAX_COUNT` |
| `TacticalSquad` | `WEDGE_EXIT_DISTANCE`, `WEDGE_PREP_APEX_DISTANCE`, `WEDGE_IMPACT_RADIUS`, `WEDGE_SPEED_MULT` |
| `TacticalNpc` | `TACTICAL_SPEED_MULT` |
| `Room` | `SOFT_BLOCK_RADIUS`, `SOFT_BLOCK_MIN_SPEED`, `SOFT_BLOCK_PUSH_SPEED`, `SHIELD_WALL_HARD_BLOCK_RADIUS`, `SHIELD_WALL_KNOCKBACK_SPEED` |

고블린 전술 상수는 더 이상 `PlatoonLeader`에 있지 않다.
