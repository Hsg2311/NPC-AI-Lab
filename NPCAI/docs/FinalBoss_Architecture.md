# FinalBoss Architecture (Behavior Tree)

> 갱신: 2026-06-12  
> 대상: `sim/BehaviorTree`, `sim/FinalBoss`, `sim/BossBtActions`, `sim/ScenarioFinalBoss`, `sim/Room`, `viz/Renderer`

최종보스는 일반 NPC(FSM), 전술 NPC(PlatoonLeader 지휘 체계)와 달리
**자체 구현 Behavior Tree**로 행동하는 독립 시스템이다.

```text
Room
  -> FinalBoss (Actor 직접 상속, 스쿼드/Platoon 인프라 미사용)
      -> BtNode 트리 (생성자에서 1회 조립)
          -> BossChargeAction / BossMeleeComboAction / BossChaseAction / BossIdleAction
```

핵심 설계 결정:

- **BT 채택 근거**: 보스전은 인터럽트(피격 반응, 페이즈 전환)가 계속 늘어나는 전투다.
  FSM은 상태 x 인터럽트의 곱셈 비용이지만, BT는 우선순위 가지 추가라는 덧셈 비용으로 수용된다.
- **BT는 최종보스 한정**: 일반 NPC와 미드보스는 기존 FSM을 유지한다.
  외부 BT 라이브러리는 사용하지 않는다 (자체 구현, `sim/BehaviorTree.hpp`).
- **detectionRange 없음**: 보스전은 전용 맵에서 진행되므로 감지 단계가 불필요하다.
  살아있는 플레이어가 존재하면 무조건 점수 기반으로 타겟을 선택한다.
  Idle은 플레이어 전멸 시에만 동작하는 폴백이다.
- **다수 플레이어 대비 원칙** (1대1 단계부터 적용):
  - 패턴 피해는 항상 영역 기반 (`Room::applyDamageToPlayersInRange`) - 단일 타겟 직접 타격 코드 없음
  - 타겟 선택은 살아있는 플레이어 전원을 후보로 하는 점수 함수 (1인이면 자동으로 그 1인)
  - 점수 함수에 위협(threat) 가중치 자리를 마련해 둠

---

## 1. BT 프레임워크 (`sim/BehaviorTree.hpp/.cpp`)

약 200줄의 소형 프레임워크. 템플릿/CRTP 없이 가상함수 +
`std::vector<std::unique_ptr<BtNode>>` 로 구성된다.

| 노드 | 역할 |
|---|---|
| `BtNode` | 추상 베이스. `name()` / `tick(dt, boss, room)` / `reset()` |
| `BtSelector` | 자식 순서대로 평가, Success/Running이면 즉시 반환 |
| `BtSequence` | 자식 순서대로 평가, Failure/Running이면 즉시 반환 |
| `BtCondition` | `std::function<bool(FinalBoss&, Room&)>` 래퍼. true=Success |
| `BtCooldown` | 데코레이터. 자식 Success 후 N초 동안 Failure 반환 |

`BtStatus`는 `Success / Failure / Running` 3종.

### Running 기억과 인터럽트 규칙

- **BtSequence**: Running을 반환한 자식 인덱스를 기억하고 **다음 틱에 그 자식부터 재개**한다.
  앞선 조건 노드는 재검사하지 않는다. 패턴이 시작되면 도중에 조건이 바뀌어도 커밋된다.
- **BtSelector**: 매 틱 **첫 자식부터 재평가**한다. 기억해 둔 Running 자식보다
  앞선(높은 우선순위) 가지가 Success/Running이 되면 기억한 자식을 `reset()` 하고 선점한다.
  이 규칙이 인터럽트의 기반이며, 추후 회피/페이즈 가지를 상단에 추가하면
  진행 중인 패턴을 자연스럽게 끊을 수 있다.
- **BtCooldown**: 쿨다운 중에는 자식을 tick하지 않고 Failure를 반환한다.
  자식이 Failure로 끝나면(조건 미충족 등) 쿨다운을 시작하지 않는다.
  `reset()` 시 쿨다운 타이머는 유지하고 자식 진행 상태만 초기화한다.
- **reset()**: Running 도중 다른 가지에 선점당했을 때 내부 상태(단계, 타이머)를 초기화하는 훅.

---

## 2. 트리 구조

`FinalBoss::buildTree()` 가 생성자에서 1회 조립한다.

```text
Root (Selector)
+- EngageSeq (Sequence)
|   +- [Cond: HasTarget]                          타겟 있음 (살아있는 플레이어 존재)
|   +- Combat (Selector)
|       +- ChargeCooldown (Cooldown 8s)
|       |   +- ChargeSeq (Sequence)
|       |       +- [Cond: TargetFar]              dist >= chargeMinRange (9)
|       |       +- BossChargeAction               Aim -> Dash -> Stop
|       +- MeleeCooldown (Cooldown 3s)
|       |   +- MeleeComboSeq (Sequence)
|       |       +- [Cond: TargetNear]             dist <= attackRange (3.5)
|       |       +- BossMeleeComboAction           Windup -> Hit -> Linger x3
|       +- BossChaseAction                        폴백, 타겟에게 이동 (항상 Running)
+- BossIdleAction                                 폴백, 플레이어 전멸 시 (항상 Running)
```

### 커밋과 폴백 동작

- **돌진은 Dash 진입 후 중단 불가**: ChargeSeq가 Running을 기억하므로 TargetFar를
  재검사하지 않고, Combat Selector에서 돌진보다 높은 가지가 없다.
  Dash 중 타겟이 죽어도 고정된 목적지까지 끝까지 진행한다 (타겟 검사는 Aim 단계에서만).
- **콤보는 타겟 소실 시 Failure 폴백**: MeleeCombo는 매 틱 타겟을 검사하고,
  소실되면 `reset()` 후 Failure를 반환해 Chase/Idle로 복귀한다.
- **HasTarget 조건은 EngageSeq 신규 진입 시에만 검사**된다 (Sequence의 Running 기억).
  전투 중 타겟이 죽으면 Combat의 모든 가지가 Failure가 되어 EngageSeq 전체가
  Failure로 끝나고, Root Selector가 Idle로 넘어간다.

---

## 3. 패턴 상세 (`sim/BossBtActions.hpp/.cpp`)

### BossMeleeComboAction (근접 콤보)

```text
[Windup 0.45s] -> [Hit] -> [Linger 0.25s]   x comboHitCount(3), 마지막 타 피해 x1.5
```

- Windup 시작 시 타겟 방향으로 회전하고 **타격 지점을 고정**한다
  (`보스 위치 + facing x attackRange x 0.8`). 플레이어가 예고를 보고 회피할 수 있다.
- Windup 동안 이동하지 않는다. 타격 예고는 `Room::addDebugTelegraph` 로 등록하며,
  telegraphs 버퍼가 **틱마다 clear되므로 매 틱 재등록**한다 (kind=0).
- Hit은 고정된 타격 지점 중심 `applyDamageToPlayersInRange(hitCenter, radius, damage)`.
  영역 피해이므로 다수 플레이어에 자동 대응한다.
- 3타 완료 시 Success (-> MeleeCooldown 3초 시작). 타겟 소실 시 Failure + reset().

### BossChargeAction (돌진)

```text
[Aim 0.9s] -> [Dash (moveSpeed x4)] -> [Stop 0.5s]
```

- **Aim**: 매 틱 타겟을 추적하며 목적지를 갱신한다
  (`보스 위치 + 방향 x (타겟 거리 + chargeOvershoot)`). 경로는 진행 방향을 따라
  원형 텔레그래프 연속(kind=1)으로 표시한다. Aim 종료 시점의 목적지로 고정된다.
- **Dash**: Aim 종료 시 `Room::beginWedgeCharge()` 로 돌진 세션을 열고,
  매 틱 `chargeHitRadius` 내 플레이어에게 `tryApplyWedgeChargeHit()` 를 시도한다.
  WedgeCharge 레지스트리가 **같은 돌진에서 플레이어당 1회만** 피해를 보장한다
  (전술 NPC WedgeStrike와 동일 인프라 재사용, `sim/Room.hpp`).
- **Stop**: 목적지 도달 시 `endWedgeCharge()` 후 0.5초 경직, 이후 Success
  (-> ChargeCooldown 8초 시작).

### BossChaseAction / BossIdleAction

- Chase: 타겟에게 이동하는 폴백. `attackRange x 0.9` 안쪽까지 접근하면 방향만 유지하며
  패턴 쿨다운을 기다린다. 타겟이 있는 한 항상 Running.
- Idle: 아무것도 하지 않는 최종 폴백. 항상 Running.

---

## 4. FinalBoss 본체 (`sim/FinalBoss.hpp/.cpp`)

### update() 흐름

```text
FinalBoss::update(dt, room)
  1. !alive_ 이면 return (사망 처리는 Actor::takeDamage가 담당)
  2. evaluateTarget(dt, room)     타겟 재평가 (주기 0.5s, 타겟 소실 시 즉시)
  3. root_->tick(dt, *this, room)
  4. 활성 리프 이름이 직전 틱과 다르면 Logger 출력:
     [T:0136][BOSS:Demon] BT leaf: Chase -> MeleeCombo
```

### 블랙보드

별도 클래스 없이 FinalBoss 멤버가 블랙보드를 겸한다. 리프 노드는 `FinalBoss&` 를 통해
읽고 쓴다.

- `targetId_` - 현재 타겟 (0 = 없음)
- `activeLeaf_` / `currentLeaf_` - 리프 전환 로그용. 리프는 실행 시
  `reportActiveLeaf(name())` 를 호출한다
- `actionProgress_` - 0~1 진행률, 렌더러 진행 바용. 리프 전환 시 0으로 초기화
- 리프용 헬퍼: `resolveTarget(room)` (생존 검증 포함), `distanceToTarget(room)`,
  `moveToward(dest, speedMult, dt)` (오버슈트 방지), `faceToward(pos)`

### 타겟 선택 (scoreTarget)

```text
score = 100 / (dist + 1)            가까울수록 높음
      + (1 - hpRatio) * 20          저HP 가중치
      (+ threat 가중치)              다수 플레이어 확장 시 이 함수에 추가
```

범위 제한 없이 `room.getLivingPlayers()` 전원이 후보다.

---

## 5. Room / viz 연동

### Room (`sim/Room.cpp`)

- `addFinalBoss(std::shared_ptr<FinalBoss>)` 로 등록, `finalBoss_` 단일 보관
- `tick()` 에서 일반 `npcs_` 루프 다음, 전술 NPC 시스템 이전에 `finalBoss_->update()`
- `findActorById()` 가 보스를 포함한다
- 플레이어 공격(Z키)의 피해 경로인 `applyDamageToActorsInRange()` 가 보스를 포함한다
- `adjustPlayerMoveForNpcSoftBlock()` 이 보스를 포함해 플레이어가 보스를 통과하지 못한다

### 스냅샷 / 렌더러

- `DebugBossEntry` (`sim/DebugSnapshot.hpp`): id, 위치/방향, name,
  **activeLeaf**(상태 int 대신 활성 BT 리프 이름), hp/maxHp, attackRange,
  targetId, actionProgress, alive
- `Renderer::drawBoss()` (`viz/Renderer.cpp`): 일반 NPC보다 큰 원(반지름 16px) +
  넓은 HP바 + 액션 진행 바 + `이름 [활성리프]` 라벨.
  돌진/타격 예고는 기존 `drawTelegraphs` 가 그대로 처리한다.

### 시나리오 전환

`viz/Application.cpp` 상단의 define으로 전환한다.

```cpp
#define USE_FINALBOSS_SCENARIO    // ScenarioFinalBoss (현재 활성)
//#define USE_ISIS_SCENARIO
//#define USE_GRANDBAUM_SCENARIO
// (모두 주석 처리 시 ScenarioTactical)
```

`ScenarioFinalBoss`: P1 (HP 1000, 이동 12, HumanControl) + 보스 Demon (24, 0, 0) 1기.

---

## 6. FinalBossConfig 밸런스 상수

`sim/FinalBoss.hpp` 의 기본값. 튜닝 기준점이다.

| 상수 | 값 | 의미 |
|---|---|---|
| maxHp | 1000 | 플레이어 기본 공격(25) 기준 40회 |
| moveSpeed | 8.5 | 플레이어(12)보다 느림, 돌진으로 거리 보정 |
| attackRange | 3.5 | 근접 패턴 발동 거리 |
| targetEvalInterval | 0.5s | 타겟 재평가 주기 |
| comboHitCount | 3 | 콤보 타수 |
| comboWindupTime / LingerTime | 0.45s / 0.25s | 타별 예고 / 후딜 |
| comboHitRadius | 3.0 | 타격 지점 영역 반경 |
| comboDamage / FinisherMult | 12 / x1.5 | 1~2타 12, 마무리 18 |
| comboCooldown | 3s | |
| chargeMinRange | 9 | 이 거리 이상에서만 돌진 |
| chargeAimTime | 0.9s | 조준 + 경로 텔레그래프 |
| chargeSpeedMult | x4 | 돌진 속도 = 34 |
| chargeOvershoot | 4 | 타겟 위치를 지나치는 거리 |
| chargeHitRadius / Damage | 2.2 / 25 | |
| chargeStopTime | 0.5s | 돌진 후 경직 |
| chargeCooldown | 8s | |

---

## 7. 검증 기록 (2026-06-12)

Debug x64 빌드 후 ScenarioFinalBoss 실행 로그 (플레이어 무입력 상태):

```text
[T:0]   타겟 선택: P1                  감지 단계 없이 즉시 타겟팅
[T:0]   BT leaf: None -> Charge        거리 24 >= 9, 쿨다운 없음
[T:54]  돌진 시작 -> (-4.0,0.0,0.0)    Aim 0.9s = 54틱
[T:93]  돌진 피격: P1 (피해 25)        WedgeCharge 1회 피해
[T:134] BT leaf: Charge -> Chase
[T:136] BT leaf: Chase -> MeleeCombo   근접 도달
[T:162] 콤보 1타: 1명 피격 (피해 12)
[T:204] 콤보 2타: 1명 피격 (피해 12)
[T:246] 콤보 3타 (마무리): 피해 18
[T:262] BT leaf: MeleeCombo -> Chase   쿨다운 진입
[T:443] BT leaf: Chase -> MeleeCombo   정확히 180틱(3s) 후 재발동
```

확인된 동작: 즉시 타겟팅, 돌진 우선 발동, 돌진 중복 피해 차단, 콤보 마무리 배율,
쿨다운 순환, 근접 상태에서 돌진 미발동(TargetFar 조건).

---

## 8. 2단계 로드맵 (가지 추가만으로 수용)

현재 트리 구조는 아래 확장을 가지 추가만으로 받도록 설계되어 있다.

- **회피(Dodge)**: Root 최상단 가지. 조건 = 타겟이 공격 Windup 중 + 보스가 사거리 내
  (`DebugPlayerEntry::attackState` 와 동일한 Player 상태 재사용).
  Selector의 상위 가지 재평가 덕에 콤보/추격 중에도 인터럽트된다.
- **경계(Strafe)**: Combat Selector 내 Chase 위, 패턴 아래.
  조건 = 모든 패턴 쿨다운 중 + 타겟 근접. 거리 유지 + 선회 액션.
- **페이즈/광폭화**: Root 상단에 HP 조건 가지 추가.
- **다수 플레이어**: `scoreTarget()` 에 threat 가중치 추가. 피해/타겟 인터페이스는
  이미 다수 기준이므로 시나리오에 플레이어만 추가하면 된다.

회피/경계는 패턴 타이밍(windup, 쿨다운)이 확정된 후에 튜닝이 의미가 있으므로
밸런스 조정 이후 진행한다.
