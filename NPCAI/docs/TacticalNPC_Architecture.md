# Tactical NPC Architecture

> 갱신: 2026-05-17  
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

`MidBossTacticBase`는 전술 phase를 갖지 않는다. `PlayerCluster` 생성, 플레이어 centroid/facing 계산, 가장 가까운 플레이어 선택, Squad별 플레이어 타겟 분배, 전체 Squad `Engage`/`Idle` 발행, 리더 사망 시 `Confused` 발행만 담당한다. `Confused`를 받은 멤버는 `TacticalNpcState::Confused(6)`으로 전환되어 리더 사망 위치 주변을 방황하고, 6초 뒤 각 Squad는 가장 가까운 생존 플레이어를 향한 리더 없는 난투로 전환한다.

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
BossSolo
```

### 4-1-1. 고블린 보스 개인 전투

`GoblinMidBossTactic`은 고블린 중간보스 본체의 개인 전투 FSM도 함께 소유한다. 공용 `PlatoonLeader`는 종족별 상태를 모르고 지휘 실행자로만 유지되며, 고블린 전용 보스 상태는 전술 객체 내부에 둔다.

- 보스는 살아 있는 플레이어 군집을 평가해 가장 우선순위가 높은 타겟을 추격한다. 점수는 `clusterSize * 1000 - distanceToBoss`를 사용한다.
- 추격 중에는 0.5초마다 타겟을 재평가하되, 새 타겟 점수가 현재 타겟보다 120 이상 높을 때만 교체한다.
- 개인 전투 FSM은 `EvaluateTarget -> ChaseTarget -> AttackWindup -> AttackRecover` 흐름이다. 표시 상태는 기존 `Idle`, `Chase`, `AttackWindup`, `AttackRecover`를 재사용한다.
- `TacticalRetreat` 중에는 개인 추격보다 전술 후퇴 이동이 우선한다.
- 모든 Squad가 전멸하면 phase가 `BossSolo`로 바뀐다. 이 상태에서는 SquadOrder 발행을 중단하고, 보스가 개인 추격/공격 루프만으로 단독 전투를 계속한다.

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
- 플레이어 군집 1개일 때만 발동하며, 군집 크기별 최소 생존 부대원이 필요하다. 기준은 플레이어 1/2/3/4명 이상에 대해 6/8/10/12명이다.
- 포위 반경은 생존 부대원 수와 `ENCIRCLE_SLOT_SPACING`으로 계산해 `ENCIRCLE_MIN_RADIUS`와 `ENCIRCLE_RADIUS` 사이로 제한한다. 부대원이 줄면 원을 줄여 빈틈을 줄인다.
- 발동 조건을 만족하지 못하거나 포위 중 최소 인원 아래로 떨어지면 `Engage` fallback과 짧은 fail cooldown으로 전환한다.
- Squad는 `Encircle` 명령을 받아 멤버별 `HoldSlot`으로 이동한다.
- 모든 멤버가 슬롯에 도착하면 `Engage` 후 `Cooldown`에 들어간다.

`Vigilance`:

- 리더 주변에 Guard 대형을 만든다.
- 모든 멤버가 도착하면 플레이어 군집 수를 다시 판단한다.

`DivideAndConquer`:

- 가장 위협적인 플레이어 군집을 고정 포획 대상으로 선택하고, 해당 군집에 가장 가까운 Squad를 `WedgeCharge` 돌진조로 배정한다.
- 나머지 Squad 중 가까운 2개를 좌우 차단조로 배정한다. 차단조는 돌진 축과 평행한 한 줄 `FormationGuard`를 만들며, NPC 소프트 블로킹으로 플레이어가 통로 옆으로 빠져나가기 어렵게 한다.
- 통로 반폭은 돌진조의 실제 쐐기 대형 반폭과 여유 폭으로 계산한다. 차단선 길이는 생존 인원과 슬롯 간격으로 계산하며 최소 길이가 필요하면 슬롯 간격을 늘린다.
- 돌진조는 쐐기 준비 슬롯에 도착해도 즉시 돌진하지 않는다. 양쪽 차단선과 쐐기 대형이 모두 완성된 시점에 전술 객체가 `releaseWedgeCharge()`를 호출한다.
- 준비 중 고정 대상 군집의 생존 플레이어 centroid가 예정 통로 밖으로 벗어나거나, 돌진조/차단조가 전멸하면 전술을 취소하고 `Engage` fallback과 fail cooldown으로 전환한다.
- 돌진 중 차단조는 슬롯을 유지한다. 돌진 완료 후 일반 교전 배정 캐시를 초기화하고, 모든 생존 Squad를 플레이어별 담당 Squad 수, 거리, 플레이어 ID 순으로 균형 재배정한 뒤 일정 시간 후 `Cooldown`에 들어간다.

일반 `Engage`와 전술 종료 후 `Cooldown` 교전에서는 Squad별 담당 플레이어를 고정한다. 최초 배정은 플레이어별 담당 Squad 수를 우선 균등하게 맞춘 뒤 거리와 ID로 결정하며, 담당 플레이어가 살아 있는 동안 거리 순위가 바뀌어도 재배정하지 않는다. 담당 플레이어가 사망하거나 소실되면 해당 Squad만 살아 있는 플레이어 중 현재 담당 Squad 수가 적고 가까운 대상 순으로 다시 배정한다. 전술 대형 phase에 진입하면 일반 교전 배정 캐시를 비우고, 전술 종료 후 새 교전 배정을 시작한다.

---

## 5. GrandBaumMidBossTactic

`GrandBaumMidBossTactic`은 그랜드밤 종족 전술을 담당한다. 현재 구현 범위는 `방패벽`, 원본 `(ㄹ)` 뱀 부대 보존/부활, 임시 외곽 뱀 웨이브, 그리고 본체의 위협자 우선 근접 추격이다.

### 5-1. Phase

```text
Engage
ShieldWall
Cooldown
```

기본 흐름:

```text
시작:
  모든 일반 Squad -> Engage
  원본 뱀 보존 단계라면 (ㄹ) 뱀 부대는 개별 분산 회피/배회

HP 단계 통과:
  Engage -> ShieldWall

ShieldWall 완료:
  임시 외곽 뱀 웨이브 cleanup
  죽은 원본 뱀 부활 및 원본 squad 재등록
  Cooldown

Cooldown 종료:
  Engage 복귀
```

`Cooldown`과 `Engage` 중에는 본체가 플레이어를 직접 추격/공격한다. 타겟 우선순위는 `SnakeThreat > SlimeThreat > Nearest`이며, 방패벽 `ShieldWall` 중에는 본체 추격/공격 패턴을 중단한다.

일반 부대 `Engage`는 공용 타겟 분배 helper를 사용한다. 다만 원본 `(ㄹ)` 뱀 부대는 두 번째 방패벽 단계가 소비되기 전까지 보존 대상이므로 일반 `Engage`에서 제외되고, 전술 전/전술 사이에는 개별 `HoldSlot` 기반 분산 회피/배회를 수행한다. 두 번째 방패벽 이후에는 원본 뱀 보존이 끝나고 일반 전투에 참여할 수 있다.

### 5-2. 방패벽 발동

방패벽은 HP 단계 기반으로 최대 2회 발동한다.

```text
1차: leader.hp / leader.maxHp <= 0.66
2차: leader.hp / leader.maxHp <= 0.33
```

이전 tick HP 비율과 현재 HP 비율을 비교해 새로 통과한 단계를 소비한다. HP가 한 번에 `33%` 이하로 떨어져 `66%`와 `33%` 단계를 동시에 통과하면 방패벽은 1회만 발동하고 두 단계를 모두 소비한다. 방패벽 자원이 부족해 실패해도 해당 HP 단계는 재시도하지 않는다.

방패벽 발동 직전 `(ㄱ)/(ㄴ)/(ㄷ)` 슬라임 부대의 생존 수를 합산한다. 생존 슬라임 총합이 `MIN_SHIELD_WALL_SLIME_COUNT = 10` 미만이면 슬라임 링, 넉백, 하드 블록, 피해 감소, 뱀 웨이브를 적용하지 않고 실패 처리한 뒤 `Cooldown`으로 들어간다.

방패벽 반지름은 발동 순간 생존 슬라임 수로 한 번만 계산하고, 방패벽 지속 중에는 재계산하지 않는다.

```text
shieldWallRingCenter = grandBaumPos
shieldWallRingRadius = clamp(liveSlimeCount * 4.5 / 2π, 7, 12)
slimeFacing          = normalize(slimeSlot - grandBaumPos)
```

슬라임 링 슬롯은 방패벽 발동 시점에 계산된다. `(ㄱ)/(ㄴ)/(ㄷ)` 슬라임은 `RingGuard` 명령을 받고, 슬롯 간격과 lane 간격을 사용해 너무 촘촘하게 겹치지 않도록 배치된다.

방패벽 발동 순간 원형 방패벽 안쪽에 있는 플레이어는 중심에서 바깥쪽으로 짧고 강한 넉백을 받는다. 이는 순간이동 보정이 아니라 `Player`의 넉백 상태로 처리되는 강제 밀림 연출이며, 기본 넉백 속도는 `SHIELD_WALL_KNOCKBACK_SPEED = 120.f`다.

방패벽 중 `(ㄱ)/(ㄴ)/(ㄷ)` 슬라임은 플레이어 이동에 대한 물리 차단체로 등록된다. 플레이어가 슬라임 충돌 반경 안으로 이동하려 하면 이동 결과가 링 바깥 경계로 보정되어 중간보스 안쪽으로 뚫고 들어갈 수 없다.

방패벽 중 `GrandBaum` 중간보스와 살아 있는 `(ㄱ)/(ㄴ)/(ㄷ)` 슬라임은 받는 피해가 `90%` 감소한다. 구현상 피해 배율은 `SHIELDWALL_DAMAGE_MULT = 0.1f`이며, 원본 뱀과 임시 웨이브 뱀에는 적용하지 않는다.

부대 해석:

| 부대 순서 | 의미 | 평상시/방패벽 명령 |
|---|---|---|
| `squads[0]` | (ㄱ) 슬라임 | `Engage`, 방패벽 중 `RingGuard` |
| `squads[1]` | (ㄴ) 슬라임 | `Engage`, 방패벽 중 `RingGuard` |
| `squads[2]` | (ㄷ) 슬라임 | `Engage`, 방패벽 중 `RingGuard` |
| `squads[3]` | (ㄹ) 원본 뱀 | 보존 중 개별 `HoldSlot`, 방패벽 중 외곽 후퇴/웨이브 자원, 2차 이후 `Engage` 가능 |

### 5-3. 원본 뱀 보존과 외곽 웨이브

- 원본 `(ㄹ)` 뱀 부대 roster는 처음 확인될 때 별도로 저장한다. 죽어서 squad 멤버 목록에서 빠져도 원본 뱀 ID를 잃지 않기 위해서다.
- 전술 전/전술 사이 원본 뱀은 squad 단위 대형 명령이 아니라 멤버별 `HoldSlot` 명령으로 분산 회피/배회한다.
- 개인 회피는 각 뱀 위치 기준으로 플레이어 위협을 계산한다. 가까운 플레이어가 있으면 반대 방향으로 도망가고, 멀면 대기 중심 주변에서 중간 폭으로 배회한다.
- 방패벽 발동 순간 살아 있는 원본 뱀 수가 `0`이면 방패벽은 실패한다.
- 생존 원본 뱀이 있으면 원본 뱀은 `shieldWallRingCenter` 기준 외곽 반경 `SNAKE_OUTER_RADIUS = 64.f`로 후퇴한다.
- 원본 뱀이 외곽 슬롯에 도착하거나 `SNAKE_RETREAT_MAX_TIME = 1.5f`가 지나면 임시 외곽 뱀 웨이브가 등장한다.
- 웨이브 수는 `min(생존 원본 뱀 수 * 10, 60)`을 4의 배수로 내림 정렬한다.

```text
0마리 생존 -> 0마리 소환, 방패벽 실패
1마리 생존 -> 8마리 소환
2마리 생존 -> 20마리 소환
3마리 생존 -> 28마리 소환
4마리 생존 -> 40마리 소환
5마리 생존 -> 48마리 소환
6마리 이상 -> 60마리 소환
```

- 임시 웨이브 뱀은 `shieldWallRingCenter` 기준 원 위에 균등 배치된다.
- 웨이브 뱀은 `DistributedEngage` 명령으로 살아 있는 플레이어에게 id 정렬 후 round-robin 분배된다.
- 방패벽 종료 조건은 임시 외곽 웨이브 전멸이다.
- 방패벽 종료 후 임시 웨이브 NPC/squad는 `Room` cleanup API로 제거된다.
- 방패벽 종료 후 죽은 원본 뱀은 max HP로 부활하고 원본 squad에 다시 등록된다. 임시 웨이브 뱀은 부활 대상이 아니다.

### 5-4. 본체 타겟팅

본체는 `Cooldown`과 `Engage` 중 기본 근접 전투를 수행한다. 새 돌진, 넉백, 범위 공격은 추가하지 않고 기존 공격 수치와 FSM의 `Chase`, `AttackWindup`, `AttackRecover`를 사용한다.

타겟 우선순위:

```text
SnakeThreat > SlimeThreat > Nearest
```

- `SnakeThreat`는 살아 있는 원본 뱀 개별 위치 기준 `SNAKE_STOP_EVADE_RANGE = 24.f` 안에 있는 플레이어다. 여러 후보가 있으면 가장 가까운 플레이어-뱀 쌍의 플레이어를 선택한다.
- `SlimeThreat`는 전체 생존 슬라임 centroid 기준 `BOSS_SLIME_THREAT_RANGE = 12.f` 안에 있는 플레이어다.
- 자원 위협자가 없으면 본체에서 가장 가까운 살아 있는 플레이어를 선택한다.
- 타겟 획득 또는 우선순위 상승 교체 후 `BOSS_TARGET_LOCK_DURATION = 1.4f` 동안 타겟을 유지한다.
- 같은 우선순위 타겟은 `BOSS_SAME_PRIORITY_RETARGET_INTERVAL = 2.5f`마다 현재 선택 기준의 최우선 후보가 다를 때 교체할 수 있다.
- Chase 속도는 우선순위와 무관하게 `BOSS_CHASE_SPEED_MULT = 8.0f`를 사용한다.

---

## 6. TacticalSquad

`TacticalSquad`는 Actor가 아닌 지휘 보조 객체다. 멤버 `TacticalNpc`의 id 목록(`memberIds_`)과 raw 포인터 캐시(`memberCache_`)를 병행 보유하고, `SquadOrder`를 개별 `TacticalCommand`로 변환한다.

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

`FormationHold`, `FormationGuard`, `RingGuard`, `DistributedEngage`는 종족을 모르는 공용 명령이다. 그랜드밤 방패벽은 이 중 `RingGuard`와 `DistributedEngage`를 사용하지만, `TacticalSquad`는 그 명령이 그랜드밤 전술인지 알지 않는다.

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
dynamicRadius = clamp(liveMembers * ENCIRCLE_SLOT_SPACING / 2π,
                      ENCIRCLE_MIN_RADIUS,
                      ENCIRCLE_RADIUS)
slot_i = tacticCenter + direction(theta_i) * dynamicRadius
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

`SquadOrder::waitForChargeRelease`의 기본값은 `false`이므로 기존 고블린 외 전술과 이시스는 준비 완료 즉시 돌진한다. 고블린 포획 통로 전술만 이 값을 `true`로 지정하고, `isWedgePrepared()`로 준비 완료를 확인한 뒤 `releaseWedgeCharge()`로 돌진을 시작한다.

---

## 7. TacticalNpc

`TacticalNpc`는 개별 전투 실행 FSM이다. 스스로 목표를 탐색하지 않고, Squad가 내려준 `TacticalCommand`를 실행한다.

### 7-1. 상태

| 값 | 상태 | 의미 |
|---:|---|---|
| 0 | `Idle` | 명령 대기 |
| 1 | `Chase` | 타겟 추격 |
| 2 | `AttackWindup` | 공격 준비 |
| 3 | `AttackRecover` | 공격 후 회복. 거의 정지하며 약한 겹침 해소만 수행 |
| 4 | `Flank` | 지정 측면 슬롯으로 이동 |
| 5 | `ChargeThrough` | 쐐기 돌진 |
| 6 | `Confused` | 리더 사망 후 방황 |
| 7 | `Dead` | 사망 |
| 8 | `HoldSlot` | 지정 슬롯 이동/유지 |
| 9 | `PressureWait` | 공격 슬롯 대기 |

### 7-2. 명령

| TacticalCommandType | 전환 상태 | 주요 데이터 |
|---|---|---|
| `EngageTarget` | 비전투 상태에서는 `PressureWait`, 전투 중 타겟 교체에서는 `Chase` | `targetId` |
| `FlankTarget` | `Flank` | `targetId`, `slotOffset`, `slotRefTargetPos`, `abandonDist`, `speedMult` |
| `HoldSlot` | `HoldSlot` | `targetId`, `slotOffset`, `speedMult` |
| `GuardSlot` | `HoldSlot` | `targetId`, `slotOffset`, `speedMult`, `guardNearestPlayer_ = true` |
| `ChargeThrough` | `ChargeThrough` | `targetId`, `slotOffset`, `chargeDir`, `chargeId`, 피해 설정 |
| `Idle` | `Idle` | 타겟 초기화 |
| `Confused` | `Confused` | 타겟 초기화 후 리더 사망 위치 주변 방황 시작 |

`GuardSlot`은 상태 자체는 `HoldSlot`을 사용하지만, 도착 후 고정 타겟 대신 가장 가까운 생존 플레이어를 바라본다.

일반 `TacticalNpc` 부대원의 근접 공격은 플레이어당 최대 5명까지 동시에 허용된다. 여러 Squad가 같은 플레이어를 담당해도 슬롯은 Squad별로 분리하지 않고 함께 사용한다. 제한은 실공격자(`AttackWindup`/`AttackRecover`)와 `Room`의 공격권 예약자를 합산해 관리한다. 신규 예약 후보와 기존 예약은 모두 타겟 18m 이내로 제한한다. 예약자는 타겟과의 거리가 0.5m 이상 줄어들 때마다 3초 lease를 갱신하며, 공격 사거리 안이나 공격 동작 중에는 lease를 유지한다. 3초간 접근 진척이 없으면 예약을 반환하고, 반환 거리보다 0.5m 이상 가까워지기 전에는 같은 타겟을 재예약하지 못한다. 플레이어와 가까운 후보 우선 정렬은 빈 슬롯을 채울 때만 사용한다. 비전투 상태에서 최초 `EngageTarget`을 받은 부대원은 `Chase`를 경유하지 않고 `PressureWait`으로 진입해 최소 체류/ID stagger 이후 공격권을 평가한다. 예약을 받지 못한 부대원은 같은 타겟을 유지한 채 플레이어 전방의 좁은 탈출 틈을 제외한 주변 외곽에 seed 기반으로 흩어져 압박 대기한다. `PressureWait`은 진입 시 정한 외곽 목표 offset을 유지하고 플레이어 이동 시 목표를 평행 이동하며, 목표 근처에서 감속/정지한다. 겹친 대기자는 위치를 재추첨하지 않고 현재 위치 근처에서 더 적극적인 overlap drift로 벌어지며, 안쪽으로 밀고 들어가지 않게 보정한다. 외곽 간격은 약간 넓게 조정했고, `Chase`로 왕복하지 않고 상태 내부에서 예약 성공자만 순차 재진입한다. 재진입 추격 중에는 실제 상태를 `PressureWait`으로 유지하고 유효한 예약으로 접근 중일 때만 스냅샷/렌더 표시 상태를 `Chase`로 내보낸다. `PressureWait` 중 담당 플레이어가 정당하게 교체되면 공격 예약과 외곽 목표만 새 타겟 기준으로 초기화하고 FSM 상태는 유지한다. 일반 부대원 공격 사거리는 기존보다 +0.8 늘려 플레이어 몸에 과하게 붙지 않도록 조정한다. `PlatoonLeader` 보스 본체의 개인 공격은 이 제한과 사거리 조정에 포함하지 않는다.

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
| `GoblinMidBossTactic` | `TACTIC_INTERVAL`, `CLUSTER_RADIUS`, `ENCIRCLE_RADIUS`, `ENCIRCLE_MIN_RADIUS`, `ENCIRCLE_SLOT_SPACING`, `TACTIC_HP_THRESHOLD`, `BOX_FRONT_OFFSET`, `REGROUP_DIST` |
| `GrandBaumMidBossTactic` | `FIRST_SHIELD_WALL_HP_RATIO`, `SECOND_SHIELD_WALL_HP_RATIO`, `MIN_SHIELD_RING_RADIUS`, `MAX_SHIELD_RING_RADIUS`, `SLIME_RING_SLOT_SPACING`, `MIN_SHIELD_WALL_SLIME_COUNT`, `SHIELDWALL_DAMAGE_MULT`, `BOSS_CHASE_SPEED_MULT`, `BOSS_TARGET_LOCK_DURATION`, `BOSS_SAME_PRIORITY_RETARGET_INTERVAL`, `BOSS_SLIME_THREAT_RANGE`, `SNAKE_OUTER_RADIUS`, `SNAKE_EVASION_RADIUS`, `SNAKE_EVASION_SPEED_MULT`, `SNAKE_STOP_EVADE_RANGE`, `SNAKE_WAVE_MAX_COUNT`, `SNAKE_WAVE_MULTIPLIER` |
| `TacticalSquad` | `WEDGE_EXIT_DISTANCE`, `WEDGE_PREP_APEX_DISTANCE`, `WEDGE_IMPACT_RADIUS`, `WEDGE_SPEED_MULT` |
| `TacticalNpc` | `TACTICAL_SPEED_MULT` |
| `Room` | `SOFT_BLOCK_RADIUS`, `SOFT_BLOCK_MIN_SPEED`, `SOFT_BLOCK_PUSH_SPEED`, `SHIELD_WALL_HARD_BLOCK_RADIUS`, `SHIELD_WALL_KNOCKBACK_SPEED` |

고블린 전술 상수는 더 이상 `PlatoonLeader`에 있지 않다.

---

## 11. IsisMidBossTactic

`IsisMidBossTactic`은 `docs/이시스.pdf`의 부대 손실 기반 반복 편대 전술을 구현한다. Squad 순서는 전술 계약으로 고정한다.

```text
squads[0] = Buddy left column
squads[1] = Buddy right column
squads[2] = Bomber left wedge
squads[3] = Bomber right wedge
```

기본 흐름:

```text
Engage
  -> 어느 부대든 aliveMembers / initialMembers < 0.80 이면 전술 해금
  -> 쿨타임 종료 및 살아있는 Bomber 부대가 있으면 RetreatForPincer

RetreatForPincer
  -> 플레이어 군집 중심 기준 반대 방향 max(현재 보스 거리 + 35m, 90m)에 후퇴 목표 고정
  -> 모든 부대는 보스 후퇴 목표 주변의 역할별 FormationHold로 집결
  -> Bomber 부대는 보스 전방 좌우, Buddy 부대는 보스 후방/측면 좌우에 정렬
  -> 부대 후퇴 speedMult 1.15, 이시스 본체 후퇴 speedMult 15.5 적용
  -> 이시스 본체도 Chase 상태로 표시되며 같은 후퇴 목표로 빠르게 이동
  -> 이시스와 부대가 도착하거나 5초가 지나면 RegroupBombers

RegroupBombers
  -> Bomber 부대: 선택 군집 좌우 대각선 랠리 지점에 FormationHold
  -> Bomber 전열 이동은 Isis 전술 전용 speedMult 0.75 사용
  -> Bomber가 랠리 슬롯에 도착하거나 3.5초가 지나면 FirstBomberWedge

FirstBomberWedge
  -> Bomber 부대: 상위 최대 2개 플레이어 군집에 WedgeCharge
  -> WedgeCharge가 공용 쐐기 준비 슬롯을 만든 뒤 ChargeThrough 돌진
  -> 1차 대상 군집의 플레이어 ID를 저장
  -> 1차 Bomber 돌진 시작과 동시에 Buddy 2차 대상 군집을 평가하고, Buddy만 2차 쐐기 준비 위치로 이동 시작
  -> 각 Bomber 부대는 자기 돌진이 끝나는 즉시 해당 군집 우선 Engage로 복귀
  -> 모든 Bomber 부대가 Engage 복귀하거나 7초 타임아웃 시 RegroupBuddies

RegroupBuddies
  -> 1차 Bomber가 끝난 뒤 Buddy 준비 완료와 이시스 선두 합류를 기다리는 phase
  -> 2차 준비 명령이 아직 없으면 플레이어 군집을 평가하되 1차 대상 군집에는 약한 반복 페널티 적용
  -> Buddy 부대: retreatTargetPos_ 주변 후퇴 진영 앞쪽 좌우 랠리 지점에 FormationHold
  -> 선택 군집 중심은 Buddy의 바라보는 방향과 이후 WedgeCharge 돌진 목표로만 사용
  -> Buddy 전열 이동은 Isis 전술 전용 speedMult 0.75 사용
  -> 살아있는 Buddy 부대 중 랜덤으로 1개를 선택하고, 이시스가 해당 Buddy 쐐기의 선두/apex 위치로 이동
  -> Buddy가 랠리 슬롯에 도착하고 이시스도 선두/apex에 도착하면 SecondBuddyWedge
  -> 이시스 합류 대기에는 타임아웃을 두지 않으며, 선택 Buddy가 전멸하면 살아있는 Buddy를 다시 선택

SecondBuddyWedge
  -> Buddy 부대: 2차 준비 시작 시점에 저장한 상위 최대 2개 플레이어 군집에 WedgeCharge
  -> 점수는 playerCount * 1000 - distanceToIsis - repeatPenalty
  -> 1차 대상과 플레이어 ID가 겹치는 군집에는 SECOND_STRIKE_REPEAT_PENALTY = 350 적용
  -> Buddy WedgeCharge에는 ISIS_BUDDY_WEDGE_SPACING_MULT = 1.90을 적용해 쐐기 간격을 넓힘
  -> 이시스가 합류한 Buddy WedgeCharge는 reserveWedgeApex로 첫 apex 슬롯을 비우고, Buddy 멤버는 그 뒤 슬롯부터 배치
  -> 이시스 본체는 Buddy가 WedgeCharge 준비 슬롯을 만든 뒤 실제 ChargeThrough를 시작할 때까지 선두/apex에서 대기
  -> 선택 Buddy 부대의 WedgeCharge가 active 상태가 되면 이시스도 같은 방향으로 ChargeThrough 표시와 함께 돌진
  -> 2차 보스 합류 중 이시스 본체는 타겟 플레이어가 아니라 쐐기 돌진 방향을 바라봄
  -> 이시스 본체의 2차 합류 돌진 speedMult는 28.0으로, Buddy 실속도와 맞춰 apex 선두를 유지
  -> 이시스가 합류한 Buddy WedgeCharge는 ISIS_BOSS_JOINED_WEDGE_DAMAGE_MULT = 1.50으로 피해 강화
  -> 각 Buddy 부대는 자기 돌진이 끝나는 즉시 해당 군집 우선 Engage로 복귀
  -> 모든 Buddy 부대가 Engage 복귀하거나 7초 타임아웃 시 Cooldown

Cooldown
  -> 매번 uniform(7.0, 13.0) 초로 랜덤 계산
  -> 종료 후 Engage 복귀
```

플레이어 군집이 3개 이상이면 모든 군집을 동시에 처리하지 않는다. 군집 내 플레이어 수가 많은 순서, 이시스에게 가까운 순서, 대표 플레이어 ID가 낮은 순서로 상위 2개만 선택한다. 2차 Buddy 대상은 1차 Bomber 돌진이 시작되는 시점에 다시 평가하고, 1차 Bomber 대상과 겹치는 군집에만 약한 반복 페널티를 적용한다. 이때 저장한 군집을 2차 돌진 발행 시 그대로 사용하므로 준비 위치와 돌진 대상이 어긋나지 않는다. 이 페널티는 같은 인원수 군집 사이의 대상 변화를 유도하지만, 2명 이상이 뭉친 큰 군집 우선 원칙을 깨지는 않는다.

돌진 완료 후 Engage 복귀는 부대 단위로 처리한다. 같은 웨이브 안에서 먼저 돌진을 끝낸 부대는 다른 부대를 기다리지 않고 즉시 `Engage`를 받는다. Engage 대상은 해당 부대가 돌진했던 군집의 살아있는 플레이어 중 부대 중심에 가장 가까운 대상을 우선하고, 없으면 기존 이시스 주 타겟 선택으로 대체한다.

Bomber/Buddy 돌진은 이시스 전용 공격 로직을 만들지 않고 고블린이 사용하던 공용 `WedgeCharge -> ChargeThrough` 실행 경로를 그대로 사용한다. 따라서 피해량, 충돌 반경, 플레이어별 1회 히트 판정은 `TacticalSquad`와 `TacticalNpc`의 기존 `WedgeCharge` 규칙을 따른다.
단, `SquadOrder::chargeSpeedMult`가 지정되면 해당 `WedgeCharge`의 돌진 속도 배율만 덮어쓴다. 이시스 1차 Bomber와 2차 Buddy 돌진은 `ISIS_WEDGE_SPEED_MULT = 1.50`을 지정하고, 고블린은 값을 지정하지 않아 공용 기본 `WEDGE_SPEED_MULT`를 그대로 사용한다.
또한 `SquadOrder::wedgeSpacingMult`가 지정되면 해당 `WedgeCharge`의 쐐기 준비 슬롯 간격만 덮어쓴다. 이시스 2차 Buddy 돌진은 `1.90`을 지정하고, 1차 Bomber와 고블린은 기본 간격을 사용한다.
`SquadOrder::wedgeDamageMult`가 지정되면 해당 `WedgeCharge`의 공용 충돌 피해만 배율 적용한다. 이시스 본체가 합류한 2차 Buddy 쐐기는 `1.50`을 지정해 기본 피해 35를 52.5로 강화한다. 보스는 별도 추가 히트를 넣지 않고, 합류한 대형의 강화 피해로만 표현한다.

### 11.1 Isis boss personal combat

이시스 본체는 그랜드밤처럼 전술 클래스 내부의 별도 보스 개인 FSM을 사용한다. 이 개인 전투는 `Engage`와 `Cooldown` phase에서만 실행되며, `RetreatForPincer`, `RegroupBombers`, `FirstBomberWedge`, `RegroupBuddies`, `SecondBuddyWedge` 중에는 일시 중단된다. 다만 표시 상태는 전술 상황을 보여주도록 유지한다. `RetreatForPincer`는 `Chase`, 전열 정비와 쐐기 대기 구간은 `HoldSlot`, 2차 보스 합류 돌진 중에는 `ChargeThrough`로 표시한다.

개인 전투 흐름:

```text
EvaluateTarget
  -> 가장 위협적인 플레이어를 선택
  -> 큰 군집 소속 우선, 이시스에게 가까운 플레이어 우선, 낮은 ID 우선

ChaseTarget
  -> TacticalNpcState::Chase로 표시
  -> 선택한 플레이어가 일반 공격 사거리 안에 들어올 때까지 접근
  -> 추격 중 0.5초마다 타겟을 재평가하되, 현재 타겟보다 점수가 120 이상 높은 후보가 있을 때만 교체

AttackWindup
  -> TacticalNpcState::AttackWindup으로 표시
  -> leader.getConfig().attackWindupTime 동안 준비
  -> 공격 시점에 target이 leader.getAttackRange() 안이면 leader.getAttackDamage() 적용

AttackRecover
  -> TacticalNpcState::AttackRecover로 표시
  -> leader.getConfig().attackRecoverTime 동안 회복
  -> 완료 후 EvaluateTarget으로 돌아가 루프 반복

Backstep / Retreat
  -> 마지막 백스탭 이후 누적 피해가 60 이상이고 3초 내부 쿨타임이 끝났으면 발동
  -> 플레이어 반대 방향으로 18m 크게 백스탭
  -> 이후 타겟과 28m 이상 거리를 벌릴 때까지 후퇴
  -> 완료 후 EvaluateTarget으로 복귀
```

이 개인 전투의 일반 공격은 기본 `TacticalNpc::updateChase()`를 직접 호출하지 않고, 그랜드밤 보스 근접 전투처럼 전술 클래스 안에서 동일한 windup/recover/damage 규칙을 재현한다. 기존 원형 경고 폭격은 이시스 기본 개인 전투 루프에서 제외한다.
타겟 점수는 `clusterSize * 1000 - distanceToIsis`를 사용하며, `AttackWindup`, `AttackRecover`, `Backstep`, `Retreat` 중에는 공격 연출이 흔들리지 않도록 재평가하지 않는다.
`ChaseTarget`은 기본 이동속도의 5.35배, `Retreat`은 5.35배, `Backstep`은 20.0배를 사용한다.
