# 트러블슈팅 기록

---

> **2026-04-26 — Squad/Platoon 제거 안내**
> [1]~[3]은 Squad/Platoon 계층(2026-04-26 삭제)에 관한 기록. 관련 코드는 현재 존재하지 않는다.
> [7]은 `chaseRange` 파라미터가 제거되어 해당 없음.
> [8][10]은 `leashBreak_`/`leashBreakCount_`가 제거되어 해당 없음.

---

## [1] Platoon이 시나리오에 적용되어 있지 않음

**발견:** Platoon 클래스 및 `Room::spawnPlatoon` 등 관련 인프라는 완전히 구현되어 있었으나,
`Application.cpp`의 씬 설정 함수(`setupSimulation`, `setupHumanSimulation`)에서
`room_.spawnSquad()`만 사용하고 있어 Goblin 스쿼드가 어떤 Platoon에도 속하지 않았다.

**원인:** `spawnPlatoon` API 미호출.

**수정:** `Application.cpp`

```cpp
// Before
room_.spawnSquad("Goblin", {
    Vec3{ 10.f, 0.f,  0.f },
    Vec3{ 13.f, 0.f,  3.f }
}, goblin);

// After
room_.spawnPlatoon("Goblin", {
    { Vec3{ 10.f, 0.f,  0.f },
      Vec3{ 13.f, 0.f,  3.f } }
}, goblin);
```

`spawnPlatoon`은 내부적으로 `createPlatoon → spawnSquad → addSquadToPlatoon`을 순서대로
호출하므로 명령 계층(Platoon → Squad → NPC)이 완성된다.

추가로 Orc는 Goblin AI 집중 테스트를 위해 주석 처리.

---

## [2] NPC가 Idle→Chase→Idle 루프를 반복하는 버그

**증상:**
```
[T:0656][NPC:GoblinS1_01] Idle -> Chase  (EngageTarget cmd)
[T:0656][NPC:GoblinS1_02] Idle -> Chase  (EngageTarget cmd)
[T:0656][NPC:GoblinS1_01] Chase -> Idle  (target beyond leash range)
[T:0656][NPC:GoblinS1_02] Chase -> Idle  (target beyond leash range)
```

같은 틱 안에서 Chase 전환 직후 chaseRange 초과로 즉시 Idle로 복귀.

### 원인 분석

`Platoon::issueOrderToAll()`은 **매 틱** 호출되어 Squad에 `setPlatoonOrder(Attack, targetId)`를
항상 전달한다. 플레이어가 `chaseRange(20)` 밖에 있어도 Platoon은 계속 동일 타겟을 지정한다.

```
매 틱:
  Platoon::update()
    → issueOrderToAll(Attack, P1_id)           ← 범위 무관하게 매 틱 호출
    → squad->setPlatoonOrder(Attack, P1_id)

  Squad::update()
    → targetPlayerId_ = P1_id                  ← 강제 오버라이드
    → selectTarget(): P1이 chaseRange 밖
        stillReachable = false
        targetMemoryTimer_ > 0 → 조기 return   ← 범위 검증 생략
    → pushCommandsToMembers()
        → NpcCommand::EngageTarget(P1) 발송    ← 범위 밖 타겟에 대해 발송!

  Npc::update()
    → applyCommand(EngageTarget): Idle → Chase  ← 1번째 로그
    → updateChase(): dist > chaseRange → Idle   ← 2번째 로그
```

`targetMemoryTimer_`는 "타겟이 잠깐 범위를 벗어났을 때 4초간 유지"하는 히스테리시스용이다.
그런데 Platoon이 매 틱 오버라이드하면 이 타이머가 계속 살아있어 range 검증이 매번 건너뛰어진다.

### 수정 1 — 타겟 변경 시 타이머 리셋 (`Squad.cpp`)

Platoon이 **이전과 다른** 타겟을 오버라이드할 때 `targetMemoryTimer_`를 리셋.
→ `selectTarget()`에서 즉시 chaseRange 검증이 실행되도록 보장.

```cpp
// Squad::update() 내 Platoon 오버라이드 적용 부분
if (platoonTargetOverride_ != 0) {
    if (platoonTargetOverride_ != targetPlayerId_)
        targetMemoryTimer_ = 0.f;   // 새 타겟은 즉시 range 검증
    targetPlayerId_ = platoonTargetOverride_;
}
```

### 수정 2 — EngageTarget 발송 전 거리 검증 (`Squad.cpp`)

`pushCommandsToMembers()`에서 leader 위치 기준으로 타겟이 `chaseRange` 내에 있는지 확인.
범위 밖이면 `EngageTarget` 대신 `HoldSlot` 발송.

이것이 **핵심 수정**이다. 수정 1만으로는 "같은 타겟이 범위를 벗어난 경우"를 커버하지 못한다.

```cpp
// pushCommandsToMembers() 앞단 추가
bool canEngage = false;
if (order_ == SquadOrderType::Attack && targetPlayerId_ != 0) {
    Npc*   leader = room.findNpcById(leaderNpcId_);
    Actor* tgt    = room.findActorById(targetPlayerId_);
    canEngage = leader && tgt && tgt->isAlive() &&
                Vec3::distance(leader->getPosition(), tgt->getPosition())
                    <= leader->getChaseRange();
}
// canEngage == false → HoldSlot, true → EngageTarget
```

두 수정이 보완적으로 동작:
- 수정 1: Platoon이 **새 타겟**을 지정했을 때 타이머 이월 방지
- 수정 2: 타겟이 범위 밖일 때 EngageTarget 자체를 차단 (근본 방어)

---

## [3] Squad NPC가 detectionRange 밖의 플레이어를 즉시 추적하는 문제

**증상:**
Platoon이 `Attack` 오더를 내리면 Squad NPC들이 리더의 **detectionRange(10.0f)** 바깥에
있는 플레이어도 곧바로 Chase 상태로 전환됨. 플레이어가 감지 범위 밖에서 접근할 때
감지 이벤트 없이 즉시 추적이 시작되었다.

### 원인 분석

`Squad::update()`에서 Platoon override를 적용할 때 `targetPlayerId_`를 직접 덮어썼다.

```cpp
// Squad.cpp (수정 전)
if (platoonTargetOverride_ != 0) {
    if (platoonTargetOverride_ != targetPlayerId_)
        targetMemoryTimer_ = 0.f;
    targetPlayerId_ = platoonTargetOverride_;  // ← 거리 확인 없이 바로 세팅
}
```

이 상태에서 `selectTarget()`이 호출되면 `targetPlayerId_ != 0`이므로
hysteresis 분기로 진입하여 **chaseRange(22.0f)** 만 확인한다.
결과적으로 detectionRange(10.0f) 바깥에 있는 플레이어도 chaseRange 안에만 있으면
NPC가 즉시 추적을 시작했다.

```
Platoon::selectPrimaryTarget()  ← 거리 제한 없이 플레이어 선택
  → issueOrderToAll(Attack, targetId)
  → Squad::setPlatoonOrder(Attack, targetId)

Squad::update()
  → targetPlayerId_ = platoonTargetOverride_  ← detectionRange 검증 없이 세팅
  → selectTarget()
      targetPlayerId_ != 0 → hysteresis 분기 진입
      chaseRange(22) 확인만 → detectionRange(10) 밖이어도 통과
  → pushCommandsToMembers() → EngageTarget 발송

Npc::applyCommand(EngageTarget) → Idle → Chase  ← 감지 없이 추적 시작
```

### 수정

`targetPlayerId_`를 직접 쓰지 않고 `platoonSuggestedTargetId_`에 hint로만 저장한 뒤,
`selectTarget()`의 신규 타겟 획득 루프에서 **detectionRange 통과 후** 채택하도록 변경.

**Squad.hpp** — 멤버 추가:
```cpp
uint32_t platoonSuggestedTargetId_{ 0 };  // hint; adopted only after detectionRange
```

**Squad.cpp** `update()` — 직접 세팅 제거:
```cpp
// 수정 후
platoonSuggestedTargetId_ = platoonTargetOverride_;
```

**Squad.cpp** `selectTarget()` — 신규 획득 루프에서 hint 우선 반영:
```cpp
for (Player* p : players) {
    float d = Vec3::distance(leaderPos, p->getPosition());
    if (d > detectRange) continue;                                     // detectionRange 검증
    if (p->getId() == platoonSuggestedTargetId_) { best = p; break; } // platoon 추천 우선
    if (d < bestDist) { bestDist = d; best = p; }
}
```

- Platoon 추천 플레이어가 detectionRange 안에 있으면 즉시 선택
- 없으면 기존 동작대로 가장 가까운 플레이어 선택
- 어떤 경우든 detectionRange를 넘어선 플레이어는 절대 선택 안 됨
- 감지 후에는 기존 hysteresis(chaseRange) 동작 그대로 유지

---

## [4] 독립 NPC가 Chase↔Return 무한 루프에 빠지는 버그

**증상:**
```
[T:0333][NPC:Goblin01] Chase -> Return  (too far from home)
[T:0334][NPC:Goblin01] Return -> Chase  (re-aggro on P1 dist=5.3)
[T:0335][NPC:Goblin01] Chase -> Return  (too far from home)
[T:0336][NPC:Goblin01] Return -> Chase  (re-aggro on P1 dist=5.3)
```

### 원인 분석

`updateReturn()`의 re-aggro 조건이 Return에 진입한 **이유**를 구분하지 않았다.

두 가지 경우가 존재한다:
- **타겟 소멸로 Return 진입** → 복귀 중 플레이어가 접근하면 re-aggro가 자연스러움
- **leash 위반으로 Return 진입** → 완전히 귀환할 때까지 re-aggro를 차단해야 함

leash 위반 시 NPC는 `maxChaseDistance_`(24) 경계 바로 밖에 있는데, Return 1틱 만에
0.09 유닛 이동해 경계 안으로 들어오면 re-aggro → Chase → 플레이어 방향으로 이동
→ 다시 경계 밖 → Return을 매 틱 반복한다.

첫 번째 수정 시도(`&& !isTooFarFromHome()`)가 효과 없었던 이유도 이 때문이다.
`maxChaseDistance_` 자체가 Chase→Return 임계값이므로 바운스 폭이 한 틱 이동량(~0.09)에 불과해
히스테리시스 역할을 할 수 없다.

### 수정

Return 진입 원인을 `leashBreak_` 플래그로 명시적으로 기록한다.

**`sim/Npc.hpp`** — 멤버 추가:
```cpp
bool leashBreak_{ false };  // set when Return is triggered by leash violation; blocks re-aggro until home
```

**`sim/Npc.cpp`** — "too far from home" → Return 전이 4곳 (`updateChase`, `updateAttackWindup`,
`updateAttackRecover`, `updateReposition`) 모두 플래그 세팅:
```cpp
if (isTooFarFromHome()) {
    targetId_ = 0;
    leashBreak_ = true;   // ← 추가
    transitionTo(...NpcState::Return, "too far from home");
    return;
}
```

**`sim/Npc.cpp`** `updateReturn()` — re-aggro 조건 교체 및 귀환 완료 시 해제:
```cpp
// re-aggro 조건: leashBreak_ 중에는 차단
if (canReAggroOnReturn_ && squadId_ == -1 && !leashBreak_) { ... }

// 귀환 완료 시 플래그 해제
if (distToHome < 0.3f) {
    position_   = spawnPos_;
    leashBreak_ = false;   // ← 추가
    transitionTo(NpcState::Idle, "reached home");
}
```

leash 위반으로 Return에 진입하면 실제로 home에 도달(`distToHome < 0.3`)해야만
`leashBreak_`이 해제되어 이후 re-aggro가 허용된다.

---

## [5] AttackWindup 취소가 회피 메커니즘 의도와 반대로 동작하는 문제

**발견:** AttackWindup 중 플레이어가 `attackRange × 1.2` 밖으로 이동하면 windupTimer를 리셋하고 Chase로 복귀했다. 의도한 설계는 "플레이어가 windup 모션을 보고 피할 수 있는" 회피 가능 공격이었다.

**원인:** 이탈 거리 체크가 windupTimer보다 먼저 평가되어, 플레이어가 살짝 물러나기만 해도 공격이 취소되고 NPC가 다시 쫓아오는 Chase → Windup → Chase 반복이 발생했다.

**수정:** `updateAttackWindup()`에서 `dist > attackRange_ × 1.2` 이탈 체크 블록 제거. windupTimer 완료 시 사거리 체크로 hit/miss 판정:
- 범위 내: 데미지 적용 → AttackRecover
- 범위 밖: miss 로그 → AttackRecover (targetId_ 유지, 이후 Chase 재진입)

`windupTime`이 플레이어의 유효 회피 창이 된다.

---

## [6] Reposition 고정 슬롯이 이동하는 타겟에 반응하지 못하는 문제

**발견:** `calcRepositionTarget()`이 진입 시점의 타겟 위치를 스냅샷으로 찍어 고정 좌표를 슬롯으로 사용했다. 플레이어가 이동하면 NPC가 타겟이 있었던 빈 자리로 이동하고, 슬롯 도착 후 Chase로 전환되어 Reposition이 실질적으로 "잠깐 엉뚱한 방향으로 달리기"가 됐다.

추가로 `repositionRadius_ > attackRange_` (Goblin: 3.0 > 1.8, Orc: 4.0 > 3.0)이라 슬롯 도착 시 항상 sattackRange 밖에 위치, `Reposition → AttackWindup` 전이가 사실상 dead code였다.

**수정:** 고정 슬롯 개념 전면 제거. 진입 시 타겟 방향 수직 벡터(`repositionDir_`)를 한 번 계산하고, 매 틱 `toTarget + repositionDir_ × 0.8`을 블렌드해 이동. 타겟이 이동해도 NPC가 계속 접근하면서 군집을 탈출한다. `isOvercrowded()` 해소 또는 `REPOSITION_TIMEOUT(1.5s)` 초과 시 종료.

**제거된 것:** `NpcConfig::repositionRadius`, `calcRepositionTarget()`, `repositionTarget_`, `hasRepositionTarget_`, DebugSnapshot reposition 필드, Renderer 슬롯 시각화

---

## [7] NpcConfig detectionRange > chaseRange 설정 역전 미감지

**발견:** `evaluateTargetScore()`의 내부 컷오프가 `chaseRange_` 기준이고, `updateIdle()`은 사전에 `detectionRange_`로 필터링한다. 두 값이 정상 관계(`detectionRange_ <= chaseRange_`)이면 문제없지만, 설정 실수로 역전될 경우 동작 이상이 생겨도 컴파일 및 런타임에 아무런 경고가 없었다.

**수정:** `Npc::Npc()` 생성자 끝에 Logger 경고 추가.

```cpp
if (detectionRange_ > chaseRange_) {
    char buf[128];
    std::snprintf(buf, sizeof(buf),
        "[CONFIG] %s: detectionRange(%.1f) > chaseRange(%.1f) -- invalid config",
        name_.c_str(), detectionRange_, chaseRange_);
    Logger::get().log("NPC", buf);
}
```

assert 대신 Logger를 사용해 릴리스 빌드에서도 확인 가능하다.

---

## [8] leashBreak_ 귀환 중 NPC 무방비 + Chase/Return 경계 진동

**증상 (1단계 — 무방비):**
NPC가 `isTooFarFromHome()` 위반으로 Return 진입 시 `leashBreak_ = true`가 세트된다.
기존 re-aggro 조건 `!leashBreak_`으로 인해 NPC가 집에 완전히 도달(`distToHome < 0.3`)할 때까지
플레이어가 `detectionRange_` 안에 접근해도 반격하지 못하는 무방비 구간이 발생했다.

**수정 시도 1 — `!isTooFarFromHome()` 교체 (진동 발생):**
`!leashBreak_`을 `!isTooFarFromHome()`으로 교체했더니 `maxChaseDistance_` 경계에서 진동이 발생했다.

```
[T:1300][NPC:Goblin09] Chase -> Return  (too far from home)
[T:1302][NPC:Goblin09] Return -> Chase  (re-aggro on P1 dist=9.4)
[T:1304][NPC:Goblin09] Chase -> Return  (too far from home)
[T:1306][NPC:Goblin09] Return -> Chase  (re-aggro on P1 dist=9.4)
```

원인: NPC가 `maxChaseDistance_` 경계(위치 C)에서 re-aggro → Chase 진입 → `isTooFarFromHome()` 재발동 → Return. Chase에서 이동은 leash 체크 이후에 일어나므로 NPC가 위치 C에 고정된 채 매 2틱 진동한다.

**최종 수정 — 히스테리시스(LEASH_REAGGRO_RATIO):**
re-aggro 허용 기준을 `maxChaseDistance_`보다 25% 안쪽으로 분리.

`sim/Npc.hpp`:
```cpp
static constexpr float LEASH_REAGGRO_RATIO = 0.75f; // 리쉬 위반 귀환 중 re-aggro 허용 기준 (maxChaseDistance_ 대비 비율)
```

`sim/Npc.cpp` `updateReturn()`:
```cpp
bool insideSafeZone = Vec3::distance(position_, spawnPos_) <= maxChaseDistance_ * LEASH_REAGGRO_RATIO;
if (canReAggroOnReturn_ && squadId_ == -1 && insideSafeZone) {
    ...
    leashBreak_ = false;  // re-aggro 시 리셋
    transitionTo(NpcState::Chase, ...);
}
```

NPC가 경계에서 최소 25% 이상 안으로 이동한 뒤에만 re-aggro가 허용되므로, Chase 재진입 후 `isTooFarFromHome()`이 바로 발동되지 않아 구조적으로 진동이 불가능하다.

---

## [9] Return 상태 이동 속도 증가

**발견:** 귀환 중 NPC 이동 속도가 `moveSpeed_`와 동일해 플레이어가 쉽게 따라붙었다.
`canReAggroOnReturn_ = false`인 NPC(Orc)도 귀환이 매우 느려 전투 흐름이 어색했다.

**수정:**

`sim/Npc.hpp` `NpcConfig`:
```cpp
float returnSpeedMult = 2.5f;  // Return 상태 이동 속도 배율
```

`sim/Npc.cpp` `updateReturn()` 이동 코드:
```cpp
position_ += moveDir * (moveSpeed_ * returnSpeedMult_ * dt);
```

배율 2.5f는 Goblin(speed 5.5) 기준 귀환 속도 13.75, Orc(speed 3.0) 기준 7.5로,
플레이어가 귀환 중인 NPC를 쫓아가기 어렵게 만든다.

---

## [10] 다중 플레이어 리쉬 핑퐁 exploit 방어 및 leashBreak_ 제거

**문제:** 플레이어 2명 이상이 NPC를 `isTooFarFromHome()` 경계 근방에서 교대로 유인하면,
NPC가 귀환 중 한 명에게 re-aggro → 다시 경계 위반 → 귀환 → 또 다른 플레이어에게 re-aggro를
무한 반복할 수 있었다. `leashBreak_` + `LEASH_REAGGRO_RATIO` 조합만으로는 이 패턴을 막지 못했다.

**수정:**

`sim/Npc.hpp` — 카운터 멤버 추가, 상수 추가, `leashBreak_` 제거:
```cpp
int  leashBreakCount_{ 0 };       // 이번 귀환 중 누적 리쉬 위반 횟수
bool countedThisEngage_{ false }; // 이번 교전에서 이미 카운트했으면 true (중복 방지)

static constexpr int MAX_LEASH_BREAKS = 2; // 이 횟수 이상 리쉬 위반 시 강제 귀환 (re-aggro 차단)
```

`sim/Npc.cpp` `transitionTo()` — Chase 진입 시 교전 플래그 리셋:
```cpp
if (next == NpcState::Chase) countedThisEngage_ = false;
```

`sim/Npc.cpp` `isTooFarFromHome()` 트리거 4곳(`updateChase`, `updateAttackWindup`,
`updateAttackRecover`, `updateReposition`) — 카운트 누적:
```cpp
if (!countedThisEngage_) {
    leashBreakCount_++;
    countedThisEngage_ = true;
}
transitionTo(...NpcState::Return, "too far from home");
```

`dist > chaseRange_` 로 인한 Return 진입은 카운트하지 않는다.
타겟과의 거리 이탈은 exploit 의도 없는 정상 이탈이기 때문이다.

`sim/Npc.cpp` `updateReturn()` — re-aggro 조건에 카운터 추가, 귀환 완료 시 전체 리셋:
```cpp
bool insideSafeZone = Vec3::distance(position_, spawnPos_) <= maxChaseDistance_ * LEASH_REAGGRO_RATIO;
bool underBreakLimit = (leashBreakCount_ < MAX_LEASH_BREAKS);
if (canReAggroOnReturn_ && squadId_ == -1 && insideSafeZone && underBreakLimit) {
    ...
    transitionTo(NpcState::Chase, ...);
    return;
}

// 귀환 완료
position_          = spawnPos_;
leashBreakCount_   = 0;
countedThisEngage_ = false;
transitionTo(NpcState::Idle, "reached home");
```

**leashBreak_ 제거:** 카운터 시스템 도입 후 `leashBreak_`는 어디서도 조건으로 읽히지 않고
세트/리셋만 되는 잉여 플래그임이 확인됐다. 선언 1곳, 세트 4곳, 리셋 2곳 전부 제거.

---

## [11] NpcGroup 공유 메모리 위치가 활동 구역 밖을 가리키는 문제 (→ [14]에서 수정)

> **수정됨:** 여기서 도입한 `clearMemory()` 프리앰블이 [14]에서 발견된 업데이트 순서
> 경쟁 조건의 원인이었다. 현재 코드는 [14]의 수정을 적용한 상태이다.


**설계 배경:**

플레이어가 그룹 활동 구역 안에서 감지된 후 이동해 구역 밖으로 나갔을 때, 공유 메모리의
`lastKnownPosition`은 구역 밖 좌표를 가리킬 수 있다.
이 상태를 방치하면 그룹 NPC들이 조사(`updateIdle` 이동) 또는 추격(`updateChase`) 중
자신의 `activityZoneRadius`를 초과해 `Return` 전이를 반복하는 루프가 발생한다.

**처리 방식:**

`Npc::update()` 진입부(FSM 분기 전)에서 선제 체크한다.

```cpp
// Npc.cpp — update() 시작부
if (groupId_ >= 0) {
    NpcGroup* group = room.getNpcGroup(groupId_);
    if (group && group->hasValidMemory(room.getTickCount())) {
        const SharedTargetMemory* mem = group->getBestMemory(room.getTickCount());
        if (mem && !group->isInsideActivityArea(mem->lastKnownPosition)) {
            group->clearMemory();   // 구역 이탈 메모리 전체 초기화
            if (targetId_ != 0) {
                targetId_ = 0;
                transitionTo(NpcState::Return, "그룹 메모리 구역 이탈");
                return;
            }
        }
    }
}
```

`clearMemory()`는 그룹 전체 메모리를 날리므로, 이 틱에 다른 그룹원이 해당 플레이어를
`reportSight()`로 다시 등록하지 않는 한 다음 틱부터는 조사 이동이 발동되지 않는다.

**주의:** `isInsideActivityArea()`는 `NpcGroup`의 center/radius를 기준으로 판정한다.
`Npc::setActivityZone()`에 전달하는 값과 `createNpcGroup()`에 전달하는 값이 **반드시 일치**해야 한다.
불일치하면 그룹 체크와 개별 NPC 체크가 서로 다른 경계를 사용해 예상치 못한 귀환이 발생한다.

---

## [12] Idle 상태에서 이동하는 "조사" 행동의 설계 결정 (→ [13]에서 번복)

**맥락:**

`updateIdle()`의 조사 행동은 Idle 상태를 유지하면서 위치를 이동시킨다. 이는 직관에 반한다
(Idle = 정지 대기 이미지). 아래 이유로 별도 상태를 만들지 않고 Idle 내 이동으로 처리했다.

- 조사 이동은 상태 전이 로그를 남기지 않는 것이 적합하다 — 플레이어를 감지한 것이 아니므로
  Chase 진입이 아님.
- 조사 도중 플레이어를 실제 감지하면 즉시 `Chase`로 전이된다 — 정상 경로 유지.
- 별도 `Investigate` 상태를 도입하면 Renderer 색상 테이블, DebugSnapshot, 범례 등
  연쇄 수정이 필요해 현재 단계에서 불필요한 복잡도가 생긴다.

**동작 요약:**
```
Idle (이동 중) → 공유 메모리 위치 도달 → 플레이어 감지 → Chase
                                        → 플레이어 없음  → Return (스폰 귀환)
```

> **번복:** [13]에서 `Investigate` 상태를 정식 도입하고 이 설계를 폐기함.

---

## [13] Idle 내 조사 이동 → Investigate 상태 분리

**문제:**

[12]의 "Idle 유지하며 이동" 방식은 두 가지 문제를 낳았다.

1. **로그 불투명** — 상태 전이가 없으므로 NPC가 실제로 이동 중인지, 그냥 대기 중인지
   로그만으로 구분할 수 없었다.
2. **구역 이탈 체크 중복 · 불완전** — [11]의 preamble 처리와 `updateIdle()` 내부 처리가
   겹쳐 있어, 이후 [14]에서 발견된 `clearMemory()` 경쟁 조건의 온상이 됐다.

**수정 (`NpcState::Investigate = 7`):**

`updateIdle()`의 그룹 메모리 이동 코드를 `updateInvestigate()`로 완전히 이관한다.
Idle은 탐지 또는 Investigate 전이만 담당한다.

```
Idle   → 감지 성공                          → Chase
       → 그룹 메모리 존재 && 위치가 구역 안  → Investigate
       → 그룹 메모리 위치가 구역 밖           → (Idle 유지, 메모리 자연 만료 대기)

Investigate → 직접 감지 성공               → Chase
            → 메모리 위치 도달, 플레이어 없음 → Return
            → 메모리 만료 (getBestMemory=null)→ Return  "메모리 만료, 귀환"
            → 메모리 위치가 구역 밖           → Return  "조사 목표 구역 이탈"
            → NPC 자신이 구역 이탈            → Return  "활동 구역 이탈 (조사 중단)"

Chase  → 타겟 소실 && 그룹 메모리가 구역 안  → Investigate  "target lost, 조사 시작"
```

`Renderer`에 노란색(`RGB(220,200,50)`) 색상과 `"Invest"` 레이블 추가.

---

## [14] clearMemory() 업데이트 순서 경쟁 조건

**증상:**

플레이어가 활동 구역을 벗어날 때, 그룹 NPC들이 서로 다른 이유 메시지로 Return에 진입했다.
또한 추격 중인 NPC(A1)가 구역 밖으로 멀리 쫓아가는 현상이 나타났다.

```
[T:85]  GoblinA1 Idle -> Chase      (target=P1 dist=6.7)
[T:85]  GoblinA2 Idle -> Investigate
[T:85]  GoblinA3 Idle -> Investigate
[T:117] GoblinA2 Investigate -> Return  (메모리 만료, 귀환)   ← 원인 잘못 기록
[T:117] GoblinA3 Investigate -> Return  (메모리 만료, 귀환)   ← 원인 잘못 기록
[T:228] GoblinA1 Chase -> Return        (outside activity zone) ← 구역 밖까지 추격
```

**원인:**

[11]에서 도입한 `update()` 프리앰블의 `group->clearMemory()` 호출이
NPC **업데이트 순서**에 따라 다른 결과를 냈다.

```
P1 구역 이탈 → A1이 구역 밖 위치를 reportSight()

다음 틱, 업데이트 순서가 [A2, A3, A1]인 경우:

  A2 프리앰블:  메모리 위치가 구역 밖
                → clearMemory() 호출
                → targetId_==0 이므로 Return 강제 없음
  A2 updateInvestigate(): getBestMemory() = null
                → "메모리 만료, 귀환"   ← 실제 원인은 '구역 이탈'인데 '만료'로 기록됨

  A3 프리앰블:  메모리 이미 지워짐 → 아무 처리 없음
  A3 updateInvestigate(): getBestMemory() = null → "메모리 만료, 귀환"

  A1 프리앰블:  메모리 이미 지워짐 → 아무 처리 없음
  A1 updateChase(): 타겟 살아있음 + 자신은 아직 구역 안
                → 계속 추격 → 직접 구역 경계를 밟을 때까지 반복
```

즉 A2/A3의 `clearMemory()` 호출이 A1이 프리앰블에서 메모리를 볼 기회를 빼앗았다.
`clearMemory()`는 그룹 공유 상태를 부수효과로 변경하므로 업데이트 순서에 의해
동작이 결정됐다.

**수정 (4곳):**

### 1. `update()` 프리앰블 — `clearMemory()` 제거, Chase 전용으로 범위 축소

```cpp
// 변경 전
if (groupId_ >= 0) {
    ...
    if (mem && !group->isInsideActivityArea(mem->lastKnownPosition)) {
        group->clearMemory();      // ← 전체 NPC에서 호출, 순서 의존
        if (targetId_ != 0) { ... Return; }
    }
}

// 변경 후
if (groupId_ >= 0 && state_ == NpcState::Chase && targetId_ != 0) {
    ...
    if (mem && !group->isInsideActivityArea(mem->lastKnownPosition)) {
        // clearMemory() 없음 — 각 상태가 독자 판단
        targetId_ = 0;
        transitionTo(NpcState::Return, "그룹 메모리 구역 이탈");
        return;
    }
}
```

Chase 상태인 NPC만 이 경로를 밟는다. 다른 상태의 NPC가 메모리를 지우지 않으므로
업데이트 순서와 관계없이 Chase NPC가 항상 메모리를 볼 수 있다.

### 2. `updateInvestigate()` — 메모리 위치 구역 체크 추가

```cpp
if (!group->isInsideActivityArea(mem->lastKnownPosition)) {
    transitionTo(NpcState::Return, "조사 목표 구역 이탈");  // 정확한 원인 기록
    return;
}
```

### 3. `updateIdle()` — 구역 밖 메모리는 Investigate 진입 차단

```cpp
} else if (!group->isInsideActivityArea(mem->lastKnownPosition)) {
    // 메모리 위치가 구역 밖 — Idle 유지 (메모리 자연 만료 대기)
} else {
    transitionTo(NpcState::Investigate, "공유 메모리 조사");
}
```

### 4. `updateChase()` 타겟 소실 → Investigate 분기 — 위치 검증 추가

```cpp
// 변경 전: hasValidMemory()만 확인
if (group && group->hasValidMemory(room.getTickCount())) { ... Investigate; }

// 변경 후: 메모리 위치가 구역 안일 때만 Investigate
const SharedTargetMemory* mem = group->getBestMemory(room.getTickCount());
if (mem && group->isInsideActivityArea(mem->lastKnownPosition)) { ... Investigate; }
```

**수정 후 기대 로그:**

```
T:85   GoblinA1 Idle -> Chase
T:85   GoblinA2 Idle -> Investigate
T:85   GoblinA3 Idle -> Investigate
T:N+1  GoblinA2 Investigate -> Return  (조사 목표 구역 이탈)  ← 정확한 원인
T:N+1  GoblinA3 Investigate -> Return  (조사 목표 구역 이탈)  ← 정확한 원인
T:N+1  GoblinA1 Chase -> Return        (그룹 메모리 구역 이탈) ← 업데이트 순서 무관
```

세 NPC가 거의 같은 틱에 Return에 진입하며, 메시지가 실제 원인을 정확히 반영한다.

**메모리 잔류 처리:**

`clearMemory()` 미호출로 메모리가 자연 만료(`memoryDurationTick`, 기본 180틱)까지 남는다.
그 동안 Idle NPC는 "메모리 위치가 구역 밖" 경로로 Investigate 진입이 차단되므로
추가 추격이 발생하지 않는다.

---

## [15] 그룹 NPC Chase↔Return 매 틱 oscillation

**증상:**

```
[T:154][NPC:GoblinA1] Chase -> Return  (그룹 메모리 구역 이탈)
[T:155][NPC:GoblinA1] Return -> Chase  (re-aggro on P1 dist=4.5)
[T:156][NPC:GoblinA1] Chase -> Return  (그룹 메모리 구역 이탈)
[T:157][NPC:GoblinA1] Return -> Chase  (re-aggro on P1 dist=4.5)
...
```

동일한 거리(4.5)에서 매 틱 반복. 메모리 만료(기본 180틱 ≈ 3초)까지 지속된다.

### 원인 분석

세 가지 조건이 충돌해 루프를 형성했다.

1. **프리앰블** (`Npc.cpp`, `update()` 71-83행): Chase 중 그룹 공유 메모리의
   `lastKnownPosition`이 그룹 활동 구역 밖이면 즉시 Return. 이 경로에서는
   `reportSight()`가 호출되지 않아 메모리가 갱신되지 않는다.

2. **`updateReturn()` re-aggro**: `canReAggroOnReturn=true`이고 플레이어가
   `detectionRange` 이내에 있으면 재어그로. 이때 실제 플레이어 위치(구역 내)와
   메모리 위치(구역 밖, 오래된 값)가 불일치한다.

3. **메모리 갱신 공백**: re-aggro 후 Chase로 돌아와도 프리앰블이 `updateChase()`보다
   먼저 실행되므로 `reportSight()`가 불릴 기회가 없다.

```
Tick N   : Chase → 프리앰블 → 메모리 위치 구역 밖 → Return (메모리 갱신 없음)
Tick N+1 : Return → updateReturn() → P1 dist=4.5 ≤ 7.0 → Chase (메모리 여전히 오래됨)
Tick N+2 : Chase → 프리앰블 → 같은 오래된 메모리 → Return
           ... 메모리 만료(180틱)까지 반복
```

### 수정 (`sim/Npc.cpp`, `updateReturn()`)

그룹 NPC의 재어그로 허용 전에 두 가지를 추가했다.

1. **구역 확인**: `selectBestVisibleTarget()`으로 교체해 후보 플레이어가 그룹 활동 구역 안에 있을 때만 재어그로 허용. 구역 밖 플레이어는 차단(원래 Return 이유와 일관성 유지).

2. **메모리 즉시 갱신**: 재어그로가 허용되는 경우 Chase 전환 전에 `group->reportSight()`로 메모리를 현재 위치로 덮어씀. 다음 틱 프리앰블이 신선한 구역 내 위치를 보게 되어 루프 차단.

```cpp
// 수정 전
void Npc::updateReturn(float dt, Room& room) {
    if (canReAggroOnReturn_ && !isOutsideActivityZone()) {
        Player* candidate = selectBestTarget(room);
        if (candidate &&
            Vec3::distance(position_, candidate->getPosition()) <= detectionRange_) {
            targetId_ = candidate->getId();
            ...
            transitionTo(NpcState::Chase, buf);
            return;
        }
    }

// 수정 후
void Npc::updateReturn(float dt, Room& room) {
    if (canReAggroOnReturn_ && !isOutsideActivityZone()) {
        Player* candidate = selectBestVisibleTarget(room);  // 구역·거리 동시 필터
        if (candidate) {
            // 재어그로 시 메모리 갱신 — 다음 틱 프리앰블에서 구역 재진입 인식
            if (groupId_ >= 0) {
                NpcGroup* group = room.getNpcGroup(groupId_);
                if (group)
                    group->reportSight(id_, candidate->getId(), candidate->getPosition(),
                                       room.getTickCount());
            }
            targetId_ = candidate->getId();
            ...
            transitionTo(NpcState::Chase, buf);
            return;
        }
    }
```

`selectBestVisibleTarget()`의 도입은 [16] 참고.

---

## [16] selectBestTarget() 감지 범위 필터 누락 + 메모리 쿼리 방어 강화

**문제 1 — `selectBestTarget()` 감지 범위 필터 없음:**

`updateChase()`의 재타겟팅(`TARGET_EVAL_INTERVAL = 0.5s`마다)이 `selectBestTarget()`을
사용했는데, 이 함수는 `detectionRange_` 필터 없이 모든 살아있는 플레이어를 대상으로
점수를 계산했다. `evaluateTargetScore()`는 `activityZoneRadius_ × 2` 거리까지 양수 점수를
부여하므로, 이론상 멀리 있는 플레이어로 타겟이 교체될 수 있었다.

또한 `updateReturn()` re-aggro, `updateIdle()`, `updateInvestigate()`의 직접 감지 루프에서
그룹 NPC가 그룹 활동 구역 밖의 플레이어를 감지해 어그로/Chase로 진입하는 경우도 있었다.

**문제 2 — `getBestMemory()` + `isInsideActivityArea()` 조합 반복:**

`updateChase()` target lost 분기, `updateIdle()` 공유 메모리 분기, `updateInvestigate()`
메모리 체크 등 3곳에서 `getBestMemory()` 반환값에 `isInsideActivityArea()`를 별도로 적용했다.
호출 측에서 개별적으로 필터링하므로 한 곳에서 누락 시 구역 밖 메모리를 사용하는 버그로
이어질 수 있었다.

### 수정

**`NpcGroup.hpp / .cpp` — `getBestMemoryInsideActivityArea()` 추가:**

```cpp
// NpcGroup.hpp
const SharedTargetMemory* getBestMemoryInsideActivityArea(uint32_t currentTick) const;

// NpcGroup.cpp
const SharedTargetMemory* NpcGroup::getBestMemoryInsideActivityArea(uint32_t currentTick) const {
    const SharedTargetMemory* best = nullptr;
    for (const auto& m : memories_) {
        if (!m.valid || currentTick > m.expireTick) continue;
        if (!isInsideActivityArea(m.lastKnownPosition)) continue;
        if (!best || m.lastSeenTick > best->lastSeenTick)
            best = &m;
    }
    return best;
}
```

구역 밖 메모리를 절대 반환하지 않는 안전한 쿼리. 구역 내 메모리가 없으면 `nullptr` 반환.

**`Npc.hpp / .cpp` — `selectBestVisibleTarget()` 추가:**

```cpp
// Npc.hpp (private)
Player* selectBestVisibleTarget(Room& room) const;

// Npc.cpp
Player* Npc::selectBestVisibleTarget(Room& room) const {
    Player* best      = nullptr;
    float   bestScore = -999.f;
    for (Player* p : room.getLivingPlayers()) {
        if (Vec3::distance(position_, p->getPosition()) > detectionRange_) continue;
        if (groupId_ >= 0) {
            NpcGroup* group = room.getNpcGroup(groupId_);
            if (group && !group->isInsideActivityArea(p->getPosition())) continue;
        }
        float s = evaluateTargetScore(p, room);
        if (s > bestScore) { bestScore = s; best = p; }
    }
    return best;
}
```

`detectionRange_` 필터와 그룹 활동 구역 필터를 동시에 적용. 독립 NPC(`groupId_ == -1`)는
구역 필터를 건너뛴다.

**`Npc.cpp` — 호출부 교체 및 단순화:**

| 위치 | 변경 전 | 변경 후 |
|---|---|---|
| `updateIdle()` 직접 감지 | 수동 루프 + `detectionRange_` 필터 | `selectBestVisibleTarget()` |
| `updateIdle()` 공유 메모리 | `getBestMemory()` + `isInsideActivityArea()` | `getBestMemoryInsideActivityArea()` |
| `updateChase()` 재타겟팅 | `selectBestTarget()` (필터 없음) | `selectBestVisibleTarget()` |
| `updateChase()` target lost | `getBestMemory()` + `isInsideActivityArea()` | `getBestMemoryInsideActivityArea()` |
| `updateReturn()` re-aggro | `selectBestTarget()` + 수동 거리/구역 체크 + `canAggro` bool | `selectBestVisibleTarget()` (필터 통합) |
| `updateInvestigate()` 직접 감지 | 수동 루프 + `detectionRange_` 필터 | `selectBestVisibleTarget()` |
| `updateInvestigate()` 메모리 | `getBestMemory()` + `isInsideActivityArea()` | `getBestMemoryInsideActivityArea()` |

`selectBestTarget()`(필터 없는 원본)은 잠재적 용도를 위해 유지.

---