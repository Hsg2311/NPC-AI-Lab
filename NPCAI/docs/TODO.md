# NPCAI Project — TODO

> 마지막 갱신: 2026-05-06 (슬램 기믹 제거, 순수 원형 포위 완성, Squad당 20명)

---

## 완료된 항목

### [v1] 콘솔 기반 NPC AI 시뮬레이터

- [o] `sim/Vec3.hpp` — POD 3D 벡터 (length, normalize, distance, lerp, 연산자)
- [o] `sim/Logger.hpp/.cpp` — 싱글톤 콘솔 로거 `[T:tick][CAT] message` 포맷
- [o] `sim/Actor.hpp/.cpp` — 추상 베이스 클래스 (id 자동 발급, takeDamage, facing_)
- [o] `sim/Player.hpp/.cpp` — Actor 파생, 웨이포인트 이동, facing_ 갱신
- [o] `sim/Npc.hpp/.cpp` — 5-state AI 머신 (Idle / Chase / Attack / Return / Dead)
  - [o] Idle → Chase (detectionRange 내 플레이어 감지)
  - [o] Chase → Attack (attackRange 진입)
  - [o] Chase → Return (target dead/gone 또는 chaseRange 초과)
  - [o] Attack → Chase (gap 발생: dist > attackRange × 1.5)
  - [o] Attack → Return (target dead/gone)
  - [o] Return → Chase (복귀 중 재감지)
  - [o] Return → Idle (spawnPos 복귀)
  - [o] Dead (terminal, takeDamage 시 자동 전이)
  - [o] 모든 상태 전이 Logger 출력
- [o] `sim/DummyPlayerController.hpp/.cpp` — 플레이어별 순환 웨이포인트 제어
- [o] `sim/Room.hpp/.cpp` — tick 루프, actor 관리, snapshot dump, buildSnapshot()
- [o] `sim/DebugSnapshot.hpp` — AI/렌더링 경계 데이터 구조

### [v2] WinAPI + GDI 2D 시각화 뷰어

- [o] `viz/Renderer.hpp/.cpp` — GDI double-buffered 2D 렌더러 (XZ 평면)
  - [o] 배경 + 격자 (5-unit 간격, 축선)
  - [o] Player: 파란 원 + 이동 방향 화살표 + 이름 레이블
  - [o] NPC: 상태별 색상 원 + detection/attack range 원 + 방향 화살표 + 레이블
  - [o] target line (NPC → Player, 노란 점선)
  - [o] HUD: tick 카운터, RUNNING/PAUSED 상태 표시, 상태 범례
- [o] `viz/Application.hpp/.cpp` — WinAPI 창, 타이머 기반 메인 루프
  - [o] WM_TIMER (16ms) → tick → buildSnapshot → InvalidateRect
  - [o] WM_PAINT → double-buffer render
  - [o] Space: pause / resume
  - [o] S: step 1 tick (paused 상태에서)
  - [o] Esc: 종료
- [o] `main.cpp` — 콘솔(Logger) + WinAPI 창 동시 실행

### [v3] NPC AI 고도화

#### 상태 머신 확장 (5→7 state)

- [o] **AttackWindup** — 공격 전 준비 단계 (회피 가능한 공격 모션)
  - [o] `windupTimer_` 누적, 완료 시 사거리 체크 → hit(데미지) or miss (둘 다 AttackRecover 전이)
  - [o] Windup 중 **위치 이동 없음**, **separation force 미적용** — 스윙 commit 중 방향 고정
  - [o] 타겟이 도망쳐도 스윙 취소 없음 — NPC가 끝까지 commit
  - [o] `transitionTo()` 진입 시 `windupTimer_ = 0` 리셋 (entry timer reset 패턴)
- [o] **AttackRecover** — 공격 후 경직 단계
  - [o] `recoverTimer_` 누적, 경직 중 **체반경(BODY_RADIUS=0.8) 기반 하드 충돌 push**만 허용
    - `BODY_RADIUS × 2` 소반경 쿼리 → 실제 겹침 시에만 `push.normalized() × speed × 0.15 × dt`
  - [o] 완료 시 `separationRadius_` 풀 쿼리 1회 → `isOvercrowded()` 판단 → Reposition 또는 Chase/AttackWindup 전이
  - [o] `transitionTo()` 진입 시 `recoverTimer_ = 0` 리셋
- [o] **Reposition** — 과밀 탈출 비켜서기
  - [o] 진입 시 수직 방향 계산 (`repositionDir_`): 홀수 id → 왼쪽, 짝수 id → 오른쪽
  - [o] 매 틱 `toTarget + repositionDir_ × 0.8` 블렌드 이동 (타겟 추적 유지)
  - [o] `isOvercrowded()` 해소 시 Chase / AttackWindup 전이
  - [o] `REPOSITION_TIMEOUT (1.5s)` 초과 시 Chase 강제 전환

#### Separation Force

- [o] `Actor::calcSeparationForce(separationRadius, nearby)` — `Actor` protected 메서드로 통합, `Npc` / `TacticalNpc` 공유
  - [o] 거리 비례 강도: `strength = 1 - (d / separationRadius)`
  - [o] 완전 겹침(d < 1e-4) 처리: id 기반 결정론적 방향 (`cosf(id × 1.2)`)
- [o] **수직 투영(Perpendicular Projection)** — 이동 방향과 평행한 분리 성분을 제거하고 수직 성분만 사용
  - `sepPerp = sep - primaryDir × (sep · primaryDir)`
  - 역방향 이동 없이 옆으로만 밀어냄, `primaryDir`는 상태마다 다름

| 클래스 | 상태 | 적용 방식 |
|---|---|---|
| Npc | Chase | 수직 투영 (`primaryDir = chaseDir`) |
| Npc | AttackWindup | 미적용 (스윙 commit 중 방향 고정) |
| Npc | AttackRecover | 위치 drift (`sep × weight × 0.3 × speed × dt`) |
| Npc | Return | 수직 투영 (`primaryDir = homeDir`) |
| Npc | Reposition | 복합 블렌드에 전체 벡터 합산 |
| TacticalNpc | Chase | 수직 투영 (`primaryDir = chaseDir`) |
| TacticalNpc | AttackRecover | 위치 drift (`sep × weight × 0.3 × speed × dt`) |
| TacticalNpc | Flank | 수직 투영 (`primaryDir = slotDir`) |
| TacticalNpc | Return | 수직 투영 (`primaryDir = homeDir`) |

#### Home Position + Return 개선

- [o] `spawnPos_` 기록, `isTooFarFromHome()` — `Vec3::distance(pos, spawnPos) > maxChaseDistance_`
- [o] Chase / AttackWindup / AttackRecover / Reposition 모든 상태에서 home 거리 체크
- [o] Return 중 재어그로는 `detectionRange_` 이내로만 제한 (chaseRange 전체 적용 방지)
- [o] `canReAggroOnReturn` 플래그 — Goblin(true) / Orc(false) 구분

#### 점수 기반 타겟 선택

- [o] `evaluateTargetScore()`:
  - 거리 점수: `(1 - dist / chaseRange) × 50`
  - 현재 타겟 유지 히스테리시스: `+20`
  - 공격 사거리 내 보너스: `+15`
  - 어그로 분산 패널티: `aggro수 × -8` (자신은 제외)
- [o] `selectBestTarget()` — 전체 생존 플레이어 대상 최고 점수 선택
- [o] Chase 상태에서 0.5초(`TARGET_EVAL_INTERVAL`) 주기 재평가

#### 플레이어 어그로 분산

- [o] `Room::countNpcsTargeting(playerId)` — Chase / AttackWindup / AttackRecover / Reposition 4개 상태 카운트
- [o] `Room::getLivingPlayers()` — 전체 생존 플레이어 목록 반환
- [o] `Room::findNearbyNpcPositions()` — 반경 내 타 NPC 위치 수집 (excludeId 지원)
- [o] `DebugPlayerEntry::aggroCount` — buildSnapshot 시 채워짐

#### NPC 설정 프리셋 (Application::setupSimulation)

- [o] **Goblin** (×2): speed 5.5, detectionRange 12, windup 0.3s / recover 0.6s, separationRadius 3.5, canReAggro=true, overlapThreshold 2
- [o] **Orc** (×1): speed 3.0, detectionRange 8, attackRange 3.0, windup 0.6s / recover 1.4s, separationRadius 5.0, canReAggro=false, overlapThreshold 1

#### 시각화 개선

- [o] **Home 마커** — NPC 스폰 위치에 `×` (drawHomeMarker, 회색 X 십자)
- [o] **Return 점선** — Return 상태일 때 NPC → home 녹색 점선
- [x] **Reposition 마커** — 제거됨 (슬롯 좌표 개념 삭제로 불필요)
- [o] **Windup/Recover 게이지** — 몸통 위 20×4 px 진행 바 (주황/갈색)
- [o] **7색 상태 범례** — Idle(회)/Chase(빨)/Windup(주황)/Recover(진주황)/Return(초록)/Repos(보라)/Dead(검)
- [o] **플레이어 어그로 카운트** — 플레이어 옆 빨간 `x2` 레이블
- [o] **HUD 어그로 요약** — 상단 좌측에 `P1 aggro: 2` 형식으로 플레이어별 출력

---

## 아키텍처 결정 사항

### standalone NPC chaseRange_ 경계 핑퐁 grace timer — Squad AI 이후 판단 (검토: 2026-04-23)

chaseRange_ 경계에서 두 플레이어가 NPC를 교대로 유인해 Chase↔Return을 반복하는 패턴에 대해
standalone NPC용 타겟 grace timer 도입을 검토했으나 **보류**.

- chaseRange_ 경계 핑퐁은 두 플레이어가 정확히 22u 경계에 동시에 위치하면서 re-aggro 타이밍을
  맞춰야 해 실전 빈도가 낮음
- Squad::selectTarget()의 TARGET_MEMORY_DURATION(4s) 히스테리시스가 동등한 역할을 하며,
  다중 플레이어 시나리오는 대부분 Squad 컨텍스트에서 발생
- grace timer 추가 시 standalone 전용 상태가 늘어나고, Squad AI 설계 후 구조가 바뀌면
  재수정 가능성이 있음
- **결론:** Squad AI 설계 완료 후 standalone NPC grace timer 필요 여부를 함께 판단

---

### FSM 유지 결정 (검토: 2026-04-23)

NPC 개별 행동 AI를 FSM에서 Behavior Tree(BT)로 전환하는 방안을 검토했으나 **현행 FSM 유지**로 결정.

- 현재 11개 상태 FSM은 Squad/Platoon 계층과 명확히 분리되어 있어 BT 도입 이점이 크지 않음
- BT가 FSM 대비 유리해지는 시점은 행동 조합이 폭발적으로 늘어날 때인데, 상위 의사결정은 Squad/Platoon이 담당하고 NPC 개별 행동은 전투 루틴에 집중되어 있어 FSM으로 충분한 복잡도
- 외부 라이브러리 금지 원칙상 BT 프레임워크를 직접 구현해야 하는 부담이 큼
- CLAUDE.md에 "No Behavior Tree" 제약이 명시되어 있음

---

## 미완료 / 다음 단계

### [v4] 전술 NPC 시스템 — 2단계

#### 구현 완료 (2026-05-04)

- [o] **HoldSlot(8) 상태** — 슬롯까지 이동 후 제자리 유지. 타겟이 범위 내여도 공격 않음. 노란색(255,220,0).
- [o] **slotRefTargetPos_ + abandonDist_** — FlankTarget 수신 시 타겟 위치 저장. Flank 이동 중 타겟이 abandonDist_ 초과 이탈 시 Chase 전환.
- [o] **DenseHold 명령** — 멤버 centroid 기준 직사각형 그리드 슬롯 계산 → HoldSlot 명령 발행.
- [o] **DenseAdvance 명령** — sectorPos 기준 직사각형 그리드 슬롯 계산 → FlankTarget 명령 발행.
- [o] **WedgeCharge 명령** — V자 대형. 타겟 추적이므로 매 틱 슬롯 재계산. FlankTarget 발행.
- [o] **슬롯 갱신 정책 개선** — FlankLeft/Right/Encircle/DenseHold/DenseAdvance는 명령 수신 시 1회만 계산 (기존 매 틱 → 변경). WedgeCharge만 매 틱 유지.
- [o] **전술 발동 조건** — `tacticsUnlocked_` 단방향 플래그. 리더 HP 70% 이하 또는 어느 Squad든 초기 인원의 80% 미만 생존 시 활성화. 활성 전에는 Engage만 발행.
- [o] **TacticalPhase 3단계 전술** — `Encircle(포위) → Vigilance(경계) → DivideAndConquer(각개격파)` 자동 전환.
- [o] **clusterPlayers()** — 플레이어 O(N²) 연결 컴포넌트. 반경 10 내 군집화. 2개 이상 군집 = 분산 상태.
- [o] **포위 재발행 쓰로틀** — `lastEncircleCentroid_` + `ENCIRCLE_RECALC_THRESHOLD(12)`. 플레이어 centroid 이동 거리 초과 시에만 DenseAdvance 재발행 (Chase→Flank 루프 방지).
- [o] **ScenarioTactical 재구성** — P1(0,0,0) + Boss(25,0,0) + Squad A/B/C 각 4명 (총 13명).

#### 미구현 — 다음 단계

- [o] **전술 이동 속도 부스트** — Flank/HoldSlot 상태에서 `moveSpeed * TACTICAL_SPEED_MULT(2.0)` 적용.
- [o] **전술 쿨타임** — 전체 멤버 슬롯 도착(`allMembersArrived()`) 즉시 8초 쿨타임 → Engage 복귀 → 재발동.
- [ ] **포위 이동속도 추가 상승** — 포위 전술 발동 시 `TACTICAL_SPEED_MULT`를 현재 2.0보다 대폭 높이는 것 검토 (예: 4.0 ~ 5.0)
- [o] **Confused 방황 구현** — 리더 사망 시 `Confused(6)`로 6초간 방황 후 가장 가까운 플레이어를 향해 난투.
- [ ] **전투 효율 기반 Retreat** — 살아있는 Squad 멤버 비율 < 임계값이면 전체 Retreat 명령
- [ ] **TacticalNpc 상태 범례** — HUD에 TacticalNpcState 색상 범례 별도 추가 (현재 Npc 범례만 있음)
- [ ] **Squad 연결선** — Squad 멤버 간 얇은 선으로 소속 표시

### [v3] DummyPlayerController 패턴 확장

- [ ] `PatrolCircle` — 반경 R의 원 궤도 순환 이동
- [ ] `WanderRandom` — 일정 반경 내 랜덤 이동 (seed 고정 가능)

### [v3] NPC 추가 개선

- [ ] **Respawn** — `updateDead()`에 respawnTimer 추가, Dead → Idle 전이
- [ ] **HP 회복** — Return 상태에서 spawnPos 복귀 중 hp 점진 회복
- [o] **공격 받기** — `Room::applyDamageToActorsInRange()` + Player Z키 구현 완료 (2026-05-02)

### [v3] 시각화 추가 개선

- [o] **HP 바** — Player / Npc / TacticalNpc 전체 HP 게이지 구현 완료 (2026-05-02)
- [ ] **카메라 pan/zoom** — 마우스 드래그(pan), 휠(zoom)으로 Camera 조정
- [ ] **시뮬레이션 속도 조절** — `+` / `-` 키로 tick rate 배율 변경 (×0.5 / ×1 / ×2 / ×4)

### [v4] 이벤트 / 로그 개선

- [ ] `EventBus` 또는 콜백 훅 — 상태 전이, 데미지 이벤트를 외부에서 구독 가능하게
- [ ] JSON / CSV 로그 덤프 — 시뮬레이션 결과를 파일로 저장해 오프라인 분석 지원

---

## 파일 구조 (현재)

```
NPCAI/
  sim/
    Vec3.hpp
    Logger.hpp / Logger.cpp
    Actor.hpp / Actor.cpp
    Player.hpp / Player.cpp
    Npc.hpp / Npc.cpp                       ← 8-state FSM (Investigate 포함), activityZone, separation, groupId
    NpcGroup.hpp / NpcGroup.cpp             ← 시야 공유 그룹 (SharedTargetMemory, reportSight, getBestMemory)
    TacticalNpc.hpp / TacticalNpc.cpp       ← Actor 직접 상속, 명령 구동 8-state FSM (Flank 포함), detectionRange 없음
    TacticalSquad.hpp / TacticalSquad.cpp   ← 코디네이터 (SquadOrder → 슬롯 계산 → TacticalCommand 발행)
    PlatoonLeader.hpp / PlatoonLeader.cpp   ← TacticalNpc 상속, evaluateTactics(), 점수 기반 타겟 선택
    DummyPlayerController.hpp / .cpp
    Room.hpp / Room.cpp                     ← tacticalNpcs_/tacticalSquads_/platoonLeaders_ 추가, tick 7a-7c
    DebugSnapshot.hpp                       ← DebugTacticalNpcEntry, DebugGroupEntry 포함
    Scenario.hpp                            ← 시나리오 추상 베이스 클래스
    ScenarioSoloNpc.hpp / .cpp              ← 독립 NPC 시나리오
    ScenarioSharedSight.hpp / .cpp          ← 시야 공유 그룹 NPC 시나리오
    ScenarioTactical.hpp / .cpp             ← 전술 NPC 시나리오 (Boss + Squad A/B/C 각 20명, 총 61명) ← 현재 활성
  viz/
    Renderer.hpp / Renderer.cpp             ← drawTacticalNpc(), tacticalStateColor(), drawGroups
    Application.hpp / Application.cpp       ← Scenario 시스템
  mathUtil.hpp               (기존 DirectXMath 수학 라이브러리, 독립 유지)
  main.cpp
  NPCAI.vcxproj
  CLAUDE.md
```

---

## NPC 상태 전이 요약 (현재)

```
Idle   → Chase          : detectionRange 내 플레이어 감지 (score 기반 선택)
Chase  → AttackWindup   : dist ≤ attackRange
Chase  → Return         : 타겟 소실 / isOutsideActivityZone
AttackWindup → AttackRecover : windupTimer 완료 → hit(범위 내) or miss(범위 밖)
AttackWindup → Return        : 타겟 소실 / isOutsideActivityZone
AttackRecover → AttackWindup : 경직 완료, in range, 혼잡 없음
AttackRecover → Chase        : 경직 완료, out of range, 혼잡 없음
AttackRecover → Reposition   : 경직 완료, isOvercrowded()
AttackRecover → Return       : 타겟 소실 / isOutsideActivityZone
Reposition → AttackWindup    : isOvercrowded() 해소 && dist ≤ attackRange
Reposition → Chase           : isOvercrowded() 해소 && dist > attackRange, 또는 REPOSITION_TIMEOUT(1.5s) 초과
Reposition → Return          : 타겟 소실 / isOutsideActivityZone
Return → Chase               : detectionRange 내 플레이어 재감지 (canReAggroOnReturn=true 시)
Return → Idle                : dist to spawnPos < 0.3
Dead   → (none)              : terminal
```

---

## 수학적 계산 정리

### 1. Vec3 기본 연산 (`sim/Vec3.hpp`)

| 연산 | 수식 |
|---|---|
| 길이 | `\|v\| = √(x² + y² + z²)` |
| 정규화 | `v̂ = v / \|v\|` (분모 < 1e-6 시 영벡터 반환) |
| 거리 | `dist(a, b) = \|a − b\|` |
| 거리²  | `distSq(a, b) = \|a − b\|²` (sqrt 없음 — 범위 비교 전용) |
| 내적 | `a · b = ax·bx + ay·by + az·bz` |
| 선형 보간 | `lerp(a, b, t) = a + (b − a) × t` |

---

### 2. 플레이어 이동 (`sim/Player.cpp`)

```
dir      = moveTarget − position
facing   = normalize(dir)
step     = moveSpeed × dt
position += facing × step       // step ≥ dist 이면 position = target으로 클램프
```

`dt`(delta-time, 16 ms)를 곱해 프레임 독립적 이동을 보장한다. 정지 임계값 0.05 units.

---

### 3. NPC 타겟 점수 함수 (`sim/Npc.cpp` — `evaluateTargetScore`)

```
score = (1 − dist / activityZoneRadius × 2) × 50   // 거리 점수: 가까울수록 최대 50점
      + 20                                           // 현재 타겟 유지 히스테리시스
      + 15                                           // dist ≤ attackRange 이면 사거리 내 보너스
      − aggro × 8                                    // 해당 플레이어를 추적 중인 NPC 수 × 패널티
```

Chase 상태에서 0.5초(`TARGET_EVAL_INTERVAL`) 주기로 재평가된다.

---

### 4. Separation Force (`sim/Actor.cpp` — `Actor::calcSeparationForce`)

`separationRadius` 내 인접 NPC 각각에 대해 반발 벡터를 누적한다.
`Npc` / `TacticalNpc` 공통 구현 (`Actor` protected 메서드).

```
// 일반 경우 (d ≥ 1e-4)
strength = 1 − (d / separationRadius)    // 선형 감쇠: 가까울수록 강함
force   += (pos − neighbor) / d × strength

// 완전 겹침 (d < 1e-4) — 결정론적 방향
angle  = id × 1.2 rad
force += { cos(angle), 0, sin(angle) }
```

**공간 분할 그리드** — `Npc`와 `TacticalNpc` 모두 `rebuildSpatialGrid()`에 등록되어
`findNearbyNpcPositions()`에서 상호 감지된다 (클래스 경계를 넘어 분리력이 작용).

---

### 5. Separation Force 상태별 적용 방식

**수직 투영(Perpendicular Projection) — 이동 방향이 명확한 상태:**

추격·이동 방향(`primaryDir`)과 평행한 분리 성분을 제거하고 수직 성분만 사용한다.
역방향 이동 없이 옆으로만 밀어내는 효과를 얻는다.

```
sepPerp = sep − primaryDir × (sep · primaryDir)
moveDir = normalize(primaryDir + sepPerp × separationWeight)
```

**체반경 하드 충돌 push — AttackRecover:**

`separationRadius_` 전체 소프트 drift 대신, 실제 몸 크기에 해당하는 소반경만 쿼리해
겹쳤을 때만 최소한의 반발을 적용한다. `isOvercrowded()` 체크는 timer 만료 시 풀 쿼리로 수행.

```
// per-tick (겹침 방지)
queryRadius = BODY_RADIUS × 2   // BODY_RADIUS = 0.8
push        = calcSeparationForce(queryRadius, nearby)
position   += push.normalized() × moveSpeed × 0.15 × dt   (push.length > 0.1 시에만)

// timer 만료 시 (Reposition 판정용)
fullQuery   = findNearbyNpcPositions(separationRadius_)
isOvercrowded(fullQuery) → Reposition
```

**상태별 적용 표:**

| 클래스 | 상태 | primaryDir | 방식 |
|---|---|---|---|
| Npc | Chase | `chaseDir` | 수직 투영 |
| Npc | AttackWindup | — | 미적용 (스윙 commit 중 방향 고정) |
| Npc | AttackRecover | — | 체반경 하드 push (BODY_RADIUS=0.8) |
| Npc | Return | `homeDir` | 수직 투영 |
| Npc | Reposition | — | 복합 블렌드 (`toTarget + repositionDir × 0.8 + sep × weight`) |
| TacticalNpc | Chase | `chaseDir` | 수직 투영 |
| TacticalNpc | AttackRecover | — | 체반경 하드 push (BODY_RADIUS=0.8) |
| TacticalNpc | Flank | `slotDir` | 수직 투영 |
| TacticalNpc | Return | `homeDir` | 수직 투영 |

---

### 6. Reposition 이동 (`sim/Npc.cpp` — `updateReposition`)

황금각 기반 고정 슬롯 방식은 2026-04-22에 제거됐다.
현재는 타겟 방향 + 수직 이탈 방향 블렌드로 군집을 탈출하면서 타겟을 추적한다:

```
toTarget     = normalize(target.pos − position)
repositionDir = perpendicular(toTarget, id)   // id 홀수→좌측, id 짝수→우측
moveDir      = normalize(toTarget + repositionDir × 0.8)
```

`REPOSITION_TIMEOUT(1.5s)` 초과 시 Chase로 강제 전환한다.

---

### 7. Windup / Recover 진행도 (`sim/Npc.cpp`, `sim/TacticalNpc.cpp`)

```
windupProgress  = clamp(windupTimer  / attackWindupTime,  0, 1)
recoverProgress = clamp(recoverTimer / attackRecoverTime, 0, 1)
```

---

### 8. 렌더러 좌표 변환 (`viz/Renderer.cpp` — `worldToScreen`)

월드 XZ 좌표를 스크린 픽셀로 변환한다:

```
screenX = clientW × 0.5 + (worldX − camera.centerX) × scale
screenY = clientH × 0.5 + (worldZ − camera.centerZ) × scale
```

기본값: `centerX=20`, `centerZ=0`, `scale=12 px/unit` → 920×660 px 창에서 약 −5…45 X 범위 커버.

---

### 9. 화살표 날개 (`viz/Renderer.cpp` — `drawArrow`)

방향 단위벡터 `(nx, ny)`를 ±90° 회전해 날개 두 점을 산출한다 (`s = 5 px`):

```
L = tip − (nx×2s + ny×s,  ny×2s − nx×s)
R = tip − (nx×2s − ny×s,  ny×2s + nx×s)
```

---

### 10. Progress Bar 채우기 (`viz/Renderer.cpp`)

```
fillWidth = floor(BAR_W × progress)    // BAR_W = 20 px
```

---

### 11. PlatoonLeader 플레이어 점수 (`sim/PlatoonLeader.cpp` — `evaluatePlayerScore`)

```
distScore = 1 / (1 + dist)         // 가까울수록 높음, 거리=0이면 최대 1.0
hpScore   = 1 − (hp / maxHp)       // HP 낮을수록 높음, 사망 직전이면 최대 1.0
score     = distScore × 0.5 + hpScore × 0.5
```

전체 생존 플레이어 중 최고 점수 → `primaryTargetId_`. `TACTIC_INTERVAL(1.0s)` 주기로 평가.

---

### 12. FlankLeft / FlankRight 슬롯 (`sim/TacticalSquad.cpp` — `calcFlankSlots`)

```
dir  = normalize(targetPos − leaderPos)        // 리더 → 타겟 방향
side = (+dir.z, 0, −dir.x)                    // XZ 평면 좌측 수직 (FlankLeft)
     = (−dir.z, 0, +dir.x)                    // XZ 평면 우측 수직 (FlankRight)
spacing = memberAttackRange + 1.5

slot[i] = targetPos
         + side    × approachRadius
         + dir     × (i × spacing)            // 타겟에 가까운 순서 배치
```

`approachRadius`(기본 4.5)가 타겟 기준 측면 거리, `spacing`이 멤버 간 전후 간격을 결정한다.

---

### 13. Encircle 슬롯 (`sim/TacticalSquad.cpp` — `calcEncircleSlots`)

Squad마다 섹터(sectorAngle ± sectorSpan/2) 안에 멤버를 균등 배치한다.
**center-of-subdivision** 방식 — 경계가 아닌 소구역 중심에 슬롯을 배치해 인접 Squad 간 슬롯 겹침을 방지한다.

```
arc   = sectorSpan / count                // 소구역 폭
start = sectorAngle − sectorSpan × 0.5 + arc × 0.5   // 첫 번째 소구역 중심

slot[i] = targetPos + { cos(start + arc×i), 0, sin(start + arc×i) } × approachRadius
```

할당 방식: **greedy nearest-slot** — 각 NPC에서 가장 가까운 미사용 슬롯에 배정해 경로 교차를 최소화한다.

PlatoonLeader가 Squad N개에 대해 인원 비율에 따라 `sectorSpan`을 배분해 360° 포위를 구성한다.

---

### 전체 수식 한눈에 보기

```
[공통 분산 강도]   strength = 1 − d / separationRadius
[수직 투영 이동]   sepPerp = sep − axis × (sep·axis)
                  moveDir = normalize(axis + sepPerp × w)   // Chase, Flank, Return
[body push]       position += push.normalized() × speed × 0.15 × dt  // AttackRecover (BODY_RADIUS=0.8)
[HoldSlot 감쇠]   sepScale = min(1, distToSlot / separationRadius)    // 도착 직전 진동 방지
[Npc 타겟 점수]   score    = (1−d/range)×50 + 20 + 15 − aggro×8
[PL 타겟 점수]    score    = 1/(1+d)×0.5 + (1−hp/maxHp)×0.5
[Flank 슬롯]      slot[i]  = target + side×r + dir×(i×spacing)
[Encircle 슬롯]   arc = sectorSpan/count,  start = center − span/2 + arc/2
                  slot[i]  = target + {cos(start+arc×i), sin(start+arc×i)} × r
[좌표 변환]       screenX  = W/2 + (worldX − centerX) × scale
[진행도]          progress = clamp(timer / totalTime, 0, 1)
```

---

## 갱신: 2026-04-13 — Squad AI 버그 수정 + Regroup 상태 추가

### 문제 요약

Squad 기반 NPC에서 두 가지 버그가 있었음:

1. **follower bounce loop** — follower가 `squadTargetId_`로 Chase 진입 → 개인 leash 초과 →
   Return → Home → Idle → squad override → Chase 무한 반복
2. **squad 전체 멈춤** — 리더가 `maxChaseDistance_` 초과 Return 시 `selectTarget`이 리더 위치 기준
   재탐색 → 플레이어 미감지 → `targetPlayerId_=0` → 전 멤버 target 소실 동시 발생

---

### 변경 1: `NpcState::Regroup` 추가 (`sim/Npc.hpp`, `sim/Npc.cpp`)

Return을 "임시 이탈(Regroup)"과 "완전 귀환(Return)"으로 분리했다.

- [o] enum에 `Regroup = 6` 추가 (`Dead`는 6 → 7로 시프트)
- [o] `updateRegroup()` 구현
  - `squadTargetId_ == 0` → `Return` (squad 교전 종료 시 완전 귀환)
  - target이 `chaseRange_` 이내로 접근 → `targetId_ = squadTargetId_`, `Chase` 재개
  - 그 외 → `chaseRange_` / `maxChaseDistance_` 무시하고 squad target 방향으로 이동
- [o] `update()` switch에 `Regroup` case 추가
- [o] `npcStateStr()`에 `"Regroup"` 추가

**전이 조건 (수정):**

```
// 수정 전 — 항상 Return
transitionTo(NpcState::Return, "...");

// 수정 후 — squad 교전 중이면 Regroup, solo 또는 비교전이면 Return
transitionTo(
    (squadId_ != -1 && squadTargetId_ != 0) ? NpcState::Regroup : NpcState::Return,
    "...");
```

적용 함수: `updateChase` (3곳), `updateAttackWindup` (3곳), `updateAttackRecover` (2곳), `updateReposition` (2곳)

- [o] `updateReturn()` 최상단에 squad 재교전 체크 추가
  - `squadId_ != -1 && squadTargetId_ != 0` → 즉시 `Regroup` 전이

---

### 변경 2: Squad target memory (`sim/Squad.hpp`, `sim/Squad.cpp`)

`selectTarget`이 매 틱 `detectionRange_`(10u)만 기준으로 탐색해 target이 자주 소멸됐음.
히스테리시스를 추가해 target 유지 조건을 완화했다.

- [o] `targetMemoryTimer_{ 0.f }` 멤버 추가
- [o] `TARGET_MEMORY_DURATION = 4.0f` 상수 추가
- [o] `selectTarget(Room&)` → `selectTarget(float dt, Room&)` 시그니처 변경
- [o] `update()`에서 `selectTarget(dt, room)` 으로 dt 전달

**selectTarget 로직 (재작성):**

```
기존 target 있음:
  - 리더 기준 chaseRange(22u) 이내 → 유지, targetMemoryTimer_ = 4.0s 리셋
  - chaseRange 초과 → 타이머 카운트다운
    - 타이머 > 0 → 아직 target 유지
    - 타이머 만료 → targetPlayerId_ = 0, 신규 탐색

기존 target 없음:
  - 기존과 동일: detectionRange(10u) 내 최근접 플레이어 선택
  - 선택 시 targetMemoryTimer_ = TARGET_MEMORY_DURATION 세팅
```

핵심: **최초 획득**은 `detectionRange_`(10u), **유지**는 `chaseRange_`(22u) — 전형적인 aggro 히스테리시스 패턴.

---

### 변경 3: Renderer 상태 매핑 업데이트 (`viz/Renderer.cpp`)

`Dead`의 int 값이 6 → 7로 바뀌어 색상/레이블이 어긋나던 문제 수정.

- [o] `npcStateColor()` — case 6 = Regroup(하늘색 `RGB(70,160,230)`), case 7 = Dead(near-black)
- [o] `stateNames[]` — `"Regroup"` 추가, 범위 체크 `< 7` → `< 8`
- [o] legend 배열 — `"Regroup"` 항목 추가 (7항목 → 8항목)
- [o] legend 시작 y — `h-145` → `h-162` (8항목 전부 표시되도록 조정)

---

### 수정 후 전체 상태 전이 요약

```
Idle   → Chase          : detectionRange 내 플레이어 감지 (score 기반)
                          OR squadTargetId_ != 0 (squad override)
Chase  → AttackWindup   : dist ≤ attackRange
Chase  → Regroup        : (squad 교전 중) target 소멸 / dist > chaseRange / too far from home
Chase  → Return         : (solo 또는 squad 비교전) target 소멸 / dist > chaseRange / too far from home
AttackWindup → AttackRecover : windupTimer 완료 → 피해 발동
AttackWindup → Chase         : dist > attackRange × 1.5 (타겟 이탈)
AttackWindup → Regroup/Return: target 소멸 / too far from home  (squad 여부로 분기)
AttackRecover → AttackWindup : 경직 완료, in range, 혼잡 없음
AttackRecover → Chase        : 경직 완료, out of range, 혼잡 없음
AttackRecover → Reposition   : 경직 완료, isOvercrowded()
AttackRecover → Regroup/Return: target 소멸 / too far from home  (squad 여부로 분기)
Reposition → AttackWindup    : 슬롯 도착, dist ≤ attackRange
Reposition → Chase           : 슬롯 도착, dist > attackRange
Reposition → Regroup/Return  : target 소멸 / too far from home  (squad 여부로 분기)
Regroup → Chase              : squadTargetId_ 대상이 chaseRange_ 이내
Regroup → Return             : squadTargetId_ == 0 (squad 교전 종료)
Return  → Regroup            : updateReturn 진입 시 squadTargetId_ != 0 감지
Return  → Chase              : detectionRange 내 플레이어 재감지 (canReAggroOnReturn=true)
Return  → Idle               : dist to spawnPos < 0.3
Dead    → (none)             : terminal
```

---

## 갱신: 2026-04-21 — Scenario 클래스 시스템 도입

### 배경

`Application::setupHumanSimulation()` / `setupSimulation()`에 시뮬레이션 구성이 하드코딩되어
상황별로 바꾸기 불편했다. Scenario 추상 클래스를 도입해 원하는 시나리오를 갈아끼울 수 있게 했다.

### 변경 내용

#### 신규: `sim/Scenario.hpp`

- [o] `setup(Room&)` 순수 가상 메서드
- [o] `controlledPlayer()` 접근자 — 시나리오가 생성한 플레이어 포인터를 Application으로 전달

#### 신규: `sim/ScenarioSoloNpc.hpp/.cpp`

- [o] 스쿼드/플래툰 없이 독립 NPC만 배치 (squadId == -1)
- [o] Player 1명 (인간 조작, `(0, 0, 20)`)
- [o] Goblin 3마리: `(10,0,5)`, `(15,0,-3)`, `(8,0,-8)`
- [o] Orc 2마리: `(25,0,0)`, `(28,0,8)`

#### 수정: `viz/Application.hpp/.cpp`

- [o] `SimMode` 열거형 및 `setupSimulation()` / `setupHumanSimulation()` 제거
- [o] `std::unique_ptr<sim::Scenario> scenario_` 멤버 추가
- [o] `init()` 에서 `ScenarioSoloNpc` 생성 후 `setup(room_)` 호출
- [o] `stepOneTick()` 의 `simMode_` 분기 제거 — `controlledPlayer_ != nullptr` 체크로 단순화

---

## 갱신: 2026-04-22 — AttackWindup 회피 메커니즘 + Reposition 방식 변경

### AttackWindup — commit to swing

windupTimer 완료 시 사거리 체크로 hit/miss 판정. 타겟이 도망쳐도 스윙 취소 없음.
miss여도 `targetId_` 유지 → AttackRecover 후 자동으로 Chase 재진입.
`windupTime`이 플레이어의 회피 가능 시간이 된다 (Goblin 0.3s, Orc 0.6s).

**제거:** `AttackWindup → Chase (dist > attackRange × 1.2)` 전이

### Reposition — 고정 슬롯 → 수직 비켜서기

황금각 기반 고정 좌표 슬롯 이동 방식을 제거. 타겟 방향 + 수직 이탈 방향 블렌드로 교체.
플레이어가 이동해도 NPC가 타겟을 계속 추적하면서 군집을 탈출한다.

**제거:** `NpcConfig::repositionRadius`, `calcRepositionTarget()`, `repositionTarget_/hasRepositionTarget_`
**제거:** `DebugSnapshot`의 reposition 좌표 필드, Renderer의 슬롯 시각화

### 새 시나리오 추가 방법

```cpp
// 1. Scenario 상속 클래스 작성
class ScenarioMySetup : public sim::Scenario {
public:
    void setup(sim::Room& room) override { /* ... */ }
};

// 2. Application::init() 한 줄 교체
scenario_ = std::make_unique<ScenarioMySetup>();
```

---

## 갱신: 2026-04-26 — Squad / Platoon 계층 제거 + NPC 단독 행동 전환

### 배경

Squad/Platoon 계층은 분대 전술을 구현하려는 목적으로 도입됐으나, 개별 NPC 행동 AI 자체를
먼저 완성하기 위해 일단 전면 제거. 모든 NPC는 standalone(단독 행동)으로 동작.

### 제거된 파일

- `sim/Squad.hpp` / `Squad.cpp` — 분대 타겟 선택, NpcCommand 발행, Confused/Broken 상태 관리
- `sim/Platoon.hpp` / `Platoon.cpp` — 전술 조율, 전투 효율 임계값 후퇴 명령

### NpcState 단순화 (11 → 7 상태)

| 제거된 상태 | 이유 |
|---|---|
| `Regroup (6)` | Squad 소속 NPC 전용; Squad 제거로 불필요 |
| `Confused (7)` | Squad 리더 사망 시 발동; Squad 제거로 불필요 |
| `MoveToSlot (8)` | Formation 확장 예약 상태; 미사용 |
| `Retreat (9)` | Squad 명령 기반 후퇴; Squad 제거로 불필요 |

`Dead`가 10 → 6으로 이동. `Renderer` 색상 테이블 및 범례 7색으로 갱신.

### NpcConfig 변경

| 제거 | 대체 |
|---|---|
| `chaseRange` (22.0) | `activityZoneRadius` (28.0) — 스폰 중심 활동 반경으로 통합 |
| `maxChaseDistance` (26.0) | 同上 |

### Npc 내부 필드 제거

`squadId_`, `squadTargetId_`, `isLeader_`, `leashBreak_`, `leashBreakCount_`,
`countedThisEngage_`, `LEASH_REAGGRO_RATIO`, `MAX_LEASH_BREAKS`

### Room 단순화

- `updatePlatoons()` / `updateSquads()` 제거
- `spawnSquad()` / `spawnPlatoon()` 제거
- `findNpcById()` 제거 (Squad가 유일한 호출처였음)

### DebugSnapshot 변경

- `DebugNpcEntry`에 `activityZoneCenterX/Z`, `activityZoneRadius` 추가
- Squad / Platoon 관련 항목 전부 제거

---

## 갱신: 2026-04-26 — NpcGroup 시야 공유 시스템 + ScenarioSharedSight

### 배경

Squad/Platoon 제거 후 그룹 전술의 첫 단계로 **시야 공유**를 구현.
NPC끼리 명령을 내리지 않고, 감지 정보만 공유해 자연스러운 협동 반응을 이끌어낸다.

### 신규: `sim/NpcGroup.hpp/.cpp`

- [o] `SharedTargetMemory` 구조체 — 플레이어당 슬롯 1개, tick 기반 만료
- [o] `NpcGroup::reportSight()` — 그룹원 NPC가 플레이어 발견 시 호출
- [o] `NpcGroup::getBestMemory()` — 가장 최근 유효 메모리 반환
- [o] `NpcGroup::hasValidMemory()` — 유효한 메모리 슬롯 존재 여부
- [o] `NpcGroup::isInsideActivityArea()` — 위치가 활동 구역 내부인지 판정
- [o] `NpcGroup::update(tick)` — 만료 슬롯 초기화 (Room::tick 내 NPC 업데이트 전 호출)
- [o] `NpcGroup::clearMemory()` — 전체 메모리 초기화 (구역 이탈 시 사용)

### 신규: `sim/ScenarioSharedSight.hpp/.cpp`

- [o] 그룹 A (G0, 청록): 고블린 3마리, 중심 (13,0,2), 반경 16
- [o] 그룹 B (G1, 황금): 고블린 3마리, 중심 (32,0,-4), 반경 14
- [o] `detectionRange = 7` — 좁게 설정해 공유 시야 없이는 일부 NPC가 반응 불가
- [o] `Application::init()`에서 `ScenarioSharedSight`로 전환 (이전: `ScenarioSoloNpc`)

### 수정: `sim/Npc.hpp/.cpp`

- [o] `groupId_` 멤버 추가 (`-1` = 독립, `≥ 0` = 그룹 소속)
- [o] `setGroupId()` / `getGroupId()` 접근자
- [o] `update()` 진입부: 공유 메모리 위치가 활동 구역 밖 → `clearMemory()` + `Return`
- [o] `updateIdle()`: 직접 감지 성공 시 `reportSight()` 호출; 실패 시 공유 메모리로 조사 이동
- [o] `updateChase()`: 추격 중 매 틱 `reportSight()` 호출; 타겟 소실 + 유효 메모리 → `Idle`(재조사)

### 수정: `sim/Room.hpp/.cpp`

- [o] `npcGroups_` (`std::vector<std::unique_ptr<NpcGroup>>`) 추가
- [o] `createNpcGroup(center, radius, memoryDurationTick)` — 그룹 생성 후 Room 소유
- [o] `getNpcGroup(groupId)` — id로 그룹 포인터 반환
- [o] `tick()` 내 NPC 업데이트 전 `npcGroup.update(tick)` 호출

### 수정: `sim/DebugSnapshot.hpp`

- [o] `DebugNpcEntry::groupId` 추가 (`-1` = 독립)
- [o] `DebugGroupEntry` 신규 — groupId, center, radius, hasMemory, memoryX/Z
- [o] `DebugSnapshot::groups` 벡터 추가

### 수정: `viz/Renderer.hpp/.cpp`

- [o] `groupColor(groupId)` 색상 테이블 (G0 청록 / G1 황금 / G2 보라 / G3 연두)
- [o] `drawGroups()` — 그룹 활동 구역 원, `G0`/`G1` 레이블, 공유 메모리 위치 `×` 마커

---

## 갱신: 2026-05-01 — 전술 NPC 시스템 1단계 구현

### 배경

NpcGroup이 시야 공유만 담당하는 수준에서 한 단계 올라, 물리적 지휘관(PlatoonLeader)이
Squad들에게 포위/협공 전술 명령을 내리는 계층형 전술 AI를 구현했다.

기존 `Npc` 클래스는 건드리지 않고, `Actor`를 직접 상속하는 별도 클래스 계층으로 분리했다.

### 설계 원칙

- **TacticalNpc**: `detectionRange` 없음. 플레이어를 스스로 감지하지 않는다. 보스 룸 고정 배치 전제.
  활성화는 오직 `TacticalSquad` 명령(EngageTarget / FlankTarget 등)에 의존.
  전투 중 `AttackWindup/Recover` 사이클은 자율.
- **TacticalSquad**: 비(非)Actor 코디네이터. `SquadOrder`를 소비해 슬롯을 계산하고
  각 멤버에게 `TacticalCommand`를 발행한다.
- **PlatoonLeader**: `TacticalNpc` 상속. 자체 전투 FSM + `evaluateTactics()` 겸행.
  보스 맵 전체를 활동 구역으로 간주해 `activationRange` 없이 항상 전체 플레이어를 인식한다.
  플레이어 선택은 거리 + HP 가중 점수 기반.

### 신규: `sim/TacticalNpc.hpp/.cpp`

- [o] `Actor` 직접 상속 (Npc와 별개 계층)
- [o] 8개 상태: `Idle / Chase / AttackWindup / AttackRecover / Flank / AlternateWait / Return / Dead`
- [o] `TacticalCommand` 구조체 — `EngageTarget / FlankTarget / AlternateWait / Retreat / Idle / Confused`
- [o] `receiveCommand()` — `TacticalSquad`가 호출, `pendingCmd_` 에 저장
- [o] `consumePendingCommand()` — 매 틱 최우선 소비, 상태 전이 트리거
- [o] `detectionRange_` 없음 — `Idle` 상태에서 자율 감지 없이 순수 대기
- [o] Flank 상태: `assignedSlot_` (월드 좌표) 방향으로 이동 → 도착 시 Chase 또는 AttackWindup
- [o] `calcSeparationForce()` — 인접 TacticalNpc 분리력 (chase/flank/return 이동 시 적용)
- [o] `getAssignedSlot()`, `getWindupProgress()`, `getRecoverProgress()` — 렌더러용 접근자

### 신규: `sim/TacticalSquad.hpp/.cpp`

- [o] `SquadOrderType` — `Idle / Engage / FlankLeft / FlankRight / Encircle / AlternateAttack / Retreat`
- [o] `SquadOrder` 구조체 — `targetId / sectorAngle / sectorSpan / attackTurn / totalTurns / approachRadius / leaderPos`
- [o] `receiveOrder()` — PlatoonLeader가 매 평가 주기 호출
- [o] `update()` — `removeDeadMembers()` → `pushCommandsToMembers()`
- [o] `calcFlankSlots()` — 리더→타겟 방향 기준 좌/우 수직 벡터로 슬롯 계산
  ```
  FlankLeft:  dir = normalize(target - leader),  side = (dir.z, 0, -dir.x)
              slot_i = target + side × radius + dir × (i × spacing)
  FlankRight: 동일, side = (-dir.z, 0, dir.x)
  ```
- [o] `calcEncircleSlots()` — `sectorAngle ± sectorSpan/2` 범위에서 균등 분산
  ```
  slot_i = target + {cos(start + arc×i), 0, sin(start + arc×i)} × radius
  arc = sectorSpan / (count - 1)
  ```
- [o] `AlternateAttack` — `i % totalTurns == attackTurn` 조건으로 공격자/대기자 분배
- [o] `pushConfusedToMembers()` — PlatoonLeader 사망 시 멤버 전체에 Confused 명령

### 신규: `sim/PlatoonLeader.hpp/.cpp`

- [o] `TacticalNpc` 상속 (전투 FSM + 지휘 겸행)
- [o] `addSquad(TacticalSquad*)` — Squad 등록 (비소유 참조)
- [o] `evaluateTactics()` — TACTIC_INTERVAL(1초) 주기 호출
  ```
  Squad 0개:   모든 Squad에 Idle
  Squad 1개:   Engage (정면 공격)
  Squad 2개:   FlankLeft + FlankRight (좌우 협공)
  Squad 3개+:  Encircle (360° / squad수 로 각도 배분)
  ```
- [o] `selectPrimaryTarget()` — 전체 생존 플레이어 대상 점수 기반 선택
  ```
  score = distScore × 0.5 + hpScore × 0.5
  distScore = 1 / (1 + dist)          -- 가까울수록 높음
  hpScore   = 1 - (hp / maxHp)        -- HP 낮을수록 높음
  ```
- [o] 사망 시 `deathReported_` 플래그로 1회만 `pushConfusedToMembers()` 호출
- [o] 자체 전투 FSM: `pendingCmd_`를 틱마다 `None`으로 차단해 Squad 명령이 리더에게 간섭하지 않도록

### 신규: `sim/ScenarioTactical.hpp/.cpp`

- [o] P1 시작: `(-10, 0, 0)` — 화살표키 조작
- [o] Boss(PlatoonLeader): `(15, 0, 0)`, HP 200
- [o] Squad A 2명 — SoldierA1/A2: `(12, 0, -3)`, `(12, 0, -6)` (좌측 배치)
- [o] Squad B 2명 — SoldierB1/B2: `(12, 0, 3)`, `(12, 0, 6)` (우측 배치)
- [o] Application에서 ScenarioSharedSight 대신 ScenarioTactical 활성

### 수정: `sim/Room.hpp/.cpp`

- [o] 멤버 추가:
  ```cpp
  unordered_map<uint32_t, shared_ptr<TacticalNpc>>  tacticalNpcs_
  vector<unique_ptr<TacticalSquad>>                  tacticalSquads_
  vector<PlatoonLeader*>                             platoonLeaders_  // 비소유
  ```
- [o] `addTacticalNpc()`, `addTacticalSquad()`, `registerPlatoonLeader()` 추가
- [o] `findActorById()` — `tacticalNpcs_` 맵도 검색하도록 확장
- [o] `tick()` 순서 확장:
  ```
  7-a. updatePlatoonLeaders(dt)    -- evaluateTactics + 자체 전투 FSM
  7-b. updateTacticalSquads(dt)    -- 슬롯 계산 + TacticalCommand 발행
  7-c. updateTacticalNpcMembers(dt) -- 멤버(비 Leader) FSM 실행
  ```
- [o] `buildSnapshot()` — `tacticalNpcs_` 순회하여 `DebugTacticalNpcEntry` 채움

### 수정: `sim/DebugSnapshot.hpp`

- [o] `DebugTacticalNpcEntry` 신규:
  ```
  id, x, z, dirX, dirZ, state(TacticalNpcState int),
  targetId, name, hp, maxHp, attackRange, alive,
  homeX, homeZ, windupProgress, recoverProgress,
  squadId, isLeader, slotX, slotZ  -- Flank 목적지 좌표
  ```
- [o] `DebugSnapshot::tacticalNpcs` 벡터 추가

### 수정: `viz/Renderer.hpp/.cpp`

- [o] `tacticalStateColor(int state)` 신규 — TacticalNpcState 색상 테이블:
  ```
  Idle(0)          RGB(140,140,140)  회색
  Chase(1)         RGB(220, 50, 50)  빨간색
  AttackWindup(2)  RGB(255,140,  0)  주황색
  AttackRecover(3) RGB(160, 70,  0)  진한 주황색
  Flank(4)         RGB(  0,200,220)  청록색
  AlternateWait(5) RGB( 50, 80,220)  파란색
  Return(6)        RGB( 50,200, 80)  초록색
  Dead(7)          RGB( 40, 40, 40)  거의 검정
  ```
- [o] `drawTacticalNpc()` 신규:
  - Leader: 원 두 겹 (반경 12 + 외곽 링 5px, 황금색)
  - 비리더: 반경 9 원
  - Flank 상태: NPC → 슬롯 목적지 점선 + 슬롯 마커 원
  - 타겟 선: NPC → 플레이어 노란 점선
  - Windup/Recover 진행 바
  - 레이블: `[L]Boss [Chase]` 형식 (리더 앞 `[L]` 접두사)
- [o] `render()` — `snapshot.tacticalNpcs` 순회 후 `drawTacticalNpc()` 호출

### 수정: `NPCAI.vcxproj`

- [o] ClInclude: `TacticalNpc.hpp`, `TacticalSquad.hpp`, `PlatoonLeader.hpp`, `ScenarioTactical.hpp` 추가
- [o] ClCompile: `TacticalNpc.cpp`, `TacticalSquad.cpp`, `PlatoonLeader.cpp`, `ScenarioTactical.cpp` 추가

---

## 갱신: 2026-05-04 — 홉 고블린 전술 3종 구현

### 배경

홉 고블린 설계 문서에 정의된 포위(가)/경계(나)/각개격파(다) 3가지 전술을 구현했다.
기본 Engage → 조건 충족 → 전술 발동의 흐름으로 동작하며,
플레이어 집합/분산 상태를 감지해 자동으로 전술을 전환한다.

### 수정: `sim/TacticalNpc.hpp/.cpp`

- [o] `TacticalNpcState::HoldSlot = 8` 추가 — 슬롯까지 이동 후 공격 없이 대기 (경계 상태)
- [o] `TacticalCommandType::HoldSlot` 추가
- [o] `TacticalCommand` 구조체에 `slotRefTargetPos`, `abandonDist` 필드 추가
- [o] `TacticalNpc` 데이터 멤버: `slotRefTargetPos_{}`, `abandonDist_{ 15.f }` 추가
- [o] `updateFlank()`: 이동 중 매 틱 `drift > abandonDist_` 체크 → Chase 전환 (슬롯 포기)
- [o] `updateHoldSlot()` 구현 — 슬롯 이동 후 제자리 유지, 공격 전환 없음

### 수정: `sim/TacticalSquad.hpp/.cpp`

- [o] `SquadOrderType`에 `DenseHold / DenseAdvance / WedgeCharge` 추가
- [o] `SquadOrder`에 `Vec3 sectorPos` 필드 추가 (DenseAdvance용 섹터 월드 좌표)
- [o] `calcDenseSlots(center, forward, count)` — 직사각형 그리드 슬롯 계산
  - `cols = ceil(sqrt(count))`, `spacing = max(attackRange * 0.8, 1.2)`
- [o] `calcWedgeSlots(targetPos, fromPos, count)` — V자 쐐기 대형 슬롯 계산
  - 행 i에 (i+1)명 배치, `spacing = max(attackRange * 1.2, 1.5)`, 행 간격 `spacing * 1.5`
- [o] `pushCommandsToMembers()`: DenseHold/DenseAdvance/WedgeCharge case 추가
- [o] `update()` 갱신 정책 변경 — WedgeCharge만 매 틱. 나머지 명령은 `orderDirty_` 시 1회.

### 수정: `sim/PlatoonLeader.hpp/.cpp`

- [o] `TacticalPhase` 열거형 추가 — `Encircle / Vigilance / DivideAndConquer`
- [o] 멤버 추가: `tacticalPhase_`, `vigilanceElapsed_`, `tacticsUnlocked_`, `initialSizesSet_`, `initialSquadSizes_`, `lastEncircleCentroid_`
- [o] 상수 추가: `VIGILANCE_DURATION(5s)`, `CLUSTER_RADIUS(10)`, `ENCIRCLE_RADIUS(10)`, `TACTIC_HP_THRESHOLD(0.70)`, `TACTIC_SQUAD_RATIO(0.80)`, `ENCIRCLE_RECALC_THRESHOLD(12)`
- [o] `checkTacticsConditions()` — 리더 HP 70% 이하 또는 Squad 생존율 80% 미만
- [o] `clusterPlayers(room)` — O(N²) 연결 컴포넌트 카운트 (CLUSTER_RADIUS=10)
- [o] `calcPlayerCentroid(room)` — 생존 플레이어 평균 위치
- [o] `evaluateTactics()` 전면 재작성 — 조건부 발동 + 3단계 TacticalPhase 전환 로직

### 수정: `sim/ScenarioTactical.cpp`

- [o] 시나리오 재구성: P1(0,0,0) + Boss(25,0,0) + Squad A/B/C 각 4명 (총 13명)
  - Squad A (4명): (22,0,-8~-12), Squad B (4명): (26-28,0,±2), Squad C (4명): (22,0,5~12)

### 수정: `viz/Renderer.cpp`

- [o] `tacticalStateColor()` — `case 8: return RGB(255, 220, 0)` (HoldSlot 노란색) 추가

---

## 갱신: 2026-05-06 — 슬램 기믹 제거 + 순수 원형 포위 완성 + NPC 60명

### 배경

이전에 구현한 "포위 → 무적 잠금 → 보스 점프 슬램" 기믹을 전면 제거하고,
NPC들이 플레이어를 순수하게 원형으로 둘러싸는 것만 남겼다.
포위 이동 품질(슬롯 겹침/진동/경로 교차)도 함께 개선했다.

### 제거: 슬램/무적/봉쇄 코드

- [o] `sim/Actor.hpp/.cpp` — `invincible_` 필드, `isInvincible()`, `setInvincible()` 제거. `takeDamage()` 단순화.
- [o] `sim/TacticalNpc.hpp/.cpp` — `CircleGuard(9)` 상태, `TacticalCommandType::CircleGuard`, `updateCircleGuard()` 전부 제거.
- [o] `sim/TacticalSquad.hpp/.cpp` — `issueCircleGuard()` 선언 및 구현 제거.
- [o] `sim/PlatoonLeader.hpp/.cpp` — `circleGuardActive_`, `circleGuardTimer_`, `jumpTimer_`, `circleCenter_`, `launchPos_` 및 관련 접근자/상수 제거. 슬램 시퀀스 블록 제거.
- [o] `sim/Room.hpp/.cpp` — `applyDamageToPlayersInRange()`, `applyEncircleContainment()` 제거. `tick()` 호출 제거.
- [o] `sim/DebugSnapshot.hpp` — `invincible`, `isJumping`, `jumpProgress`, `encircleActive/Center/Radius` 필드 제거.
- [o] `viz/Renderer.cpp` — 무적 골드 링, 점프 표시, 포위 원 렌더링 블록 제거.

### 변경: 포위(Encircle) 명령 방식

- [o] Encircle 명령 → **HoldSlot** 발행 (기존: FlankTarget/DenseAdvance).
  - 슬롯 도착 후 플레이어 방향 `facing_` 유지, 공격 전환 없음.
- [o] `calcEncircleSlots()` — **center-of-subdivision** 공식으로 교체.
  - `arc = sectorSpan / count`, `start = sectorAngle - sectorSpan/2 + arc/2`
  - 인접 Squad 경계 슬롯 겹침 문제 해결.
- [o] 슬롯 할당 방식: **greedy nearest-slot** — 각 NPC가 가장 가까운 미사용 슬롯을 배정 (경로 교차 최소화).
- [o] `HoldSlot` 이동 중 분리력 감쇠: `sepScale = min(1, distToSlot / separationRadius)` → 목적지 도착 직전 진동 방지.

### 변경: 쿨타임 트리거

- [o] `TACTIC_ACTIVE_DURATION` 제거. 전체 멤버 슬롯 도착(`allMembersArrived()`) 즉시 쿨타임(8초) 진입.
- [o] 쿨타임 중 `encircleSlotsAssigned_ = false` → 쿨타임 종료 후 현재 플레이어 위치 기준 새 슬롯 재발행.

### 변경: 기타

- [o] `ENCIRCLE_RADIUS` 10.0f → **20.0f** (더 넓은 포위 반경).
- [o] `ScenarioTactical` — Squad당 4명 → **20명** (A1~A20 / B1~B20 / C1~C20, 총 60명 + Boss).

---

## 갱신: 2026-05-02 — 플레이어 공격 시스템 + HP Bar 시각화 + NPC Dead 활성화

### 배경

플레이어가 직접 공격해 NPC를 처치할 수 있는 인터랙티브 전투 시스템을 구현했다.
공격 입력(Z키), 선딜/후딜 단계, 범위 피해 적용을 포함하며,
전체 Actor에 HP Bar 시각화를 추가해 전투 상황을 직관적으로 파악할 수 있게 했다.
플레이어 공격으로 인한 NPC Dead 상태 활성화 과정에서 발견된 버그 2건도 함께 수정했다.

### 수정: `sim/Player.hpp/.cpp`

- [o] `PlayerAttackConfig` 구조체 — `damage(25) / range(5.5) / windupTime(0.2s) / recoverTime(0.3s)`
- [o] `AttackState` 열거형 — `None / Windup / Recover`
- [o] `requestAttack()` — 외부(Application)에서 공격 요청; `attackState_ == None`일 때만 진입
- [o] `update()` — **공격 선딜/후딜 중 이동 차단** (`attackState_ != None`이면 이동 블록 전체 스킵)
- [o] `updateAttack()` — Windup 완료 시 `applyDamageToActorsInRange()` 호출, Recover 완료 시 `None` 복귀
- [o] `getAttackState()`, `getAttackProgress()`, `getAttackRange()` — 렌더러용 접근자

### 수정: `sim/Room.hpp/.cpp`

- [o] `applyDamageToActorsInRange(center, radius, damage)` — `npcs_` + `tacticalNpcs_` 순회,
  alive NPC에 데미지 적용 후 피격 수 반환

### 수정: `sim/DebugSnapshot.hpp`

- [o] `DebugPlayerEntry` 확장 — `attackState(int)`, `attackProgress(float)`, `attackRange(float)` 3필드 추가

### 수정: `viz/Application.hpp/.cpp`

- [o] `keysHeld_[5]` — 인덱스 4 = Z(공격) 키 추가
- [o] `WM_KEYDOWN / WM_KEYUP` — Z 키 처리 추가
- [o] `stepOneTick()` — `keysHeld_[4]`가 true면 `controlledPlayer_->requestAttack()` 호출
- [o] 창 타이틀바에 `Z = Attack` 안내 추가

### 수정: `viz/Renderer.hpp/.cpp`

- [o] `drawHpBar(hdc, center, barY, hp, maxHp)` — HP% 기반 색상(>50% 초록 / 25~50% 노랑 / <25% 빨강) 게이지
- [o] `drawProgressBar(hdc, center, barY, progress, fillCol)` — 일반 진행 바 (windup/recover 공용)
- [o] `drawNpc()` — HP 바(center.y − 26) 추가; Windup/Recover 게이지를 `drawProgressBar()`로 리팩토링
- [o] `drawTacticalNpc()` — HP 바(center.y − 30), Windup/Recover 게이지(center.y − 22) 추가
- [o] `drawPlayer()`:
  - 공격 사거리 원 항상 표시 (얇은 파란 원)
  - HP 바(center.y − 20)
  - 공격 진행 바(center.y − 14): Windup 주황 / Recover 갈색
  - 이름 레이블 위치 조정(center.y − 30)
- [o] `drawHUD()` — 플레이어 HP `%.0f/%.0f` 표시 + `[DEAD]` 태그 추가

### 버그 수정

- [o] **`Npc::updateDead()` targetId_ 잔류** (`Npc.cpp:416`) — Dead 전환 시 `targetId_ = 0` 클리어.
  플레이어 공격으로 사망한 NPC가 이전 플레이어 ID를 계속 가리키는 데이터 불일치 수정.
- [o] **`TacticalNpc::updateDead()` 동일 수정** (`TacticalNpc.cpp:298`)
- [o] **`TacticalSquad::isEmpty()` 타이밍 오류** — `removeDeadMembers()`를 public으로 변경하고
  `PlatoonLeader::evaluateTactics()` 첫 줄에서 호출. 플레이어 공격으로 Squad 전체가 사망해도
  PlatoonLeader가 빈 squad에 Engage 명령을 발행하는 1틱 오평가 방지.
  (`TacticalSquad.hpp:52`, `PlatoonLeader.cpp:54`)

---

## 갱신: 2026-05-25 — TacticalSquad 멤버 순회 최적화

### 배경

TacticalSquad 멤버 루프 전반의 map 탐색 + RTTI 비용 제거. 졸업작품 이식 대비.

### 변경 내용

- [o] **[Opt-G] TacticalNpc* 포인터 캐시** — `memberCache_` / `wedgeMemberCache_` 추가.
  `addMember(uint32_t)` → `addMember(TacticalNpc*)` API 변경.
  `removeDeadMembers()` / `calcCentroid()` / `areMembersAtSlots()` / `areChargeMembersComplete()` Room 인자 제거.
  호출 측 5곳(ScenarioTactical/GrandBaum/Isis, MidBossTactics 2곳) 모두 교체.
  `addMember`에 null/size assert 추가.
- [o] **[Opt-H] 중복 removeDeadMembers 제거** — `TacticalSquad::update()` 첫 줄 호출 제거.
  step 7(tactic)이 이미 호출하므로 step 8 호출은 항상 no-op.
- [o] **[Opt-I] BoxAdvance 10Hz 제한** — `boxRefreshTimer_` 추가. 슬롯 계산 60fps → 10Hz.
- [o] **[Opt-J] distanceSq 변환** — Encircle/WedgeCharge/RingGuard 슬롯 배정, `assignSquadsToPlayers`, BoxAdvance drift check에서 sqrt 제거.
