# 트러블슈팅 기록

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
