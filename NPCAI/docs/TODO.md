# NPCAI Project — TODO

> 마지막 갱신: 2026-04-26 (NpcGroup 시야 공유 시스템 추가)

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
  - [o] Windup 중 **위치 이동 없음** — separation force는 `facing_`만 조정
  - [o] 타겟이 도망쳐도 스윙 취소 없음 — NPC가 끝까지 commit
  - [o] `transitionTo()` 진입 시 `windupTimer_ = 0` 리셋 (entry timer reset 패턴)
- [o] **AttackRecover** — 공격 후 경직 단계
  - [o] `recoverTimer_` 누적, 경직 중 약한 separation drift 허용 (`weight × 0.3 × speed × dt`)
  - [o] 완료 시 `isOvercrowded()` 판단 → Reposition 또는 Chase/AttackWindup 전이
  - [o] `transitionTo()` 진입 시 `recoverTimer_ = 0` 리셋
- [o] **Reposition** — 과밀 탈출 비켜서기
  - [o] 진입 시 수직 방향 계산 (`repositionDir_`): 홀수 id → 왼쪽, 짝수 id → 오른쪽
  - [o] 매 틱 `toTarget + repositionDir_ × 0.8` 블렌드 이동 (타겟 추적 유지)
  - [o] `isOvercrowded()` 해소 시 Chase / AttackWindup 전이
  - [o] `REPOSITION_TIMEOUT (1.5s)` 초과 시 Chase 강제 전환

#### Separation Force

- [o] `calcSeparationForce()` — `findNearbyNpcPositions(separationRadius_)` 반경 내 NPC 반발
  - [o] 거리 비례 강도: `strength = 1 - (d / separationRadius_)`
  - [o] 완전 겹침(< 1e-4) 처리: id 기반 결정론적 밀어냄 방향 (`cosf(id × 1.2)`)
- [o] Chase: `moveDir = (chaseDir + sepForce × separationWeight_).normalized()`
- [o] Return 중에는 separation 영향 감소 (`× 0.25`)

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

### [v3] DummyPlayerController 패턴 확장

- [ ] `PatrolCircle` — 반경 R의 원 궤도 순환 이동
- [ ] `WanderRandom` — 일정 반경 내 랜덤 이동 (seed 고정 가능)
- [ ] `StandStill(Idle)` — 웨이포인트 없는 경우를 명시적 패턴으로 정리

### [v3] NPC 추가 개선

- [ ] **Respawn** — `updateDead()`에 respawnTimer 추가, Dead → Idle 전이
- [ ] **HP 회복** — Return 상태에서 spawnPos 복귀 중 hp 점진 회복
- [ ] **공격 받기** — 현재 NPC는 데미지를 받지 않음; Player 공격 이벤트 인터페이스 추가
- [ ] **Confused state** — 플레이어 갑작스러운 소멸(로그아웃 시뮬레이션) 시 짧은 혼란 상태
- [ ] **Ranged NPC** — 원거리 공격 (attackRange > meleeRange, AttackWindup 중 이동 없음 유지)

### [v3] 시각화 추가 개선

- [ ] **HP 바** — 각 actor 아래 체력 게이지 표시
- [ ] **카메라 pan/zoom** — 마우스 드래그(pan), 휠(zoom)으로 Camera 조정
- [ ] **Actor 선택** — 클릭 시 해당 actor 정보 사이드 패널 표시
- [ ] **로그 오버레이** — 최근 N개 상태 전이 로그를 창 오른쪽에 표시 (Logger 출력 미러링)
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
    Npc.hpp / Npc.cpp                       ← 7-state FSM, activityZone, separation, score target, reposition, groupId
    NpcGroup.hpp / NpcGroup.cpp             ← 시야 공유 그룹 (SharedTargetMemory, reportSight, getBestMemory)
    DummyPlayerController.hpp / .cpp
    Room.hpp / Room.cpp                     ← getLivingPlayers, findNearbyNpcPositions, countNpcsTargeting, createNpcGroup
    DebugSnapshot.hpp                       ← activityZone, groupId, DebugGroupEntry 포함
    Scenario.hpp                            ← 시나리오 추상 베이스 클래스
    ScenarioSoloNpc.hpp / .cpp              ← 독립 NPC 시나리오
    ScenarioSharedSight.hpp / .cpp          ← 시야 공유 그룹 NPC 시나리오 (활성 시나리오)
  viz/
    Renderer.hpp / Renderer.cpp             ← 7-color legend, home marker, progress bar, drawGroups
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
score = (1 − dist / chaseRange) × 50    // 거리 점수: 가까울수록 최대 50점
      + 20                               // 현재 타겟 유지 히스테리시스
      + 15                               // dist ≤ attackRange 이면 사거리 내 보너스
      − aggro × 8                        // 해당 플레이어를 이미 추적 중인 NPC 수 × 패널티
```

Chase 상태에서 0.5초(`TARGET_EVAL_INTERVAL`) 주기로 재평가된다.

---

### 4. Separation Force (`sim/Npc.cpp` — `calcSeparationForce`)

`separationRadius` 내 인접 NPC 각각에 대해 반발 벡터를 누적한다:

```
// 일반 경우 (d ≥ 1e-4)
strength = 1 − (d / separationRadius)    // 선형 감쇠: 가까울수록 강함
force   += normalize(pos − neighbor) × strength

// 완전 겹침 (d < 1e-4) — 결정론적 방향
angle  = id × 1.2 rad
force += { cos(angle), 0, sin(angle) }
```

**합성 이동 벡터:**

```
moveDir = normalize(chaseDir + sepForce × separationWeight)
```

Return 상태에서는 separation 영향을 25%로 감소:

```
moveDir = normalize(homeDir + sep × separationWeight × 0.25)
```

---

### 5. Reposition 슬롯 위치 (`sim/Npc.cpp` — `calcRepositionTarget`)

**황금각(Golden Angle)** 기반으로 NPC id마다 균등 분산:

```
angle  = id × 2.399963 rad    // ≈ 137.508°, 피보나치 수열 극한 2π(1 − 1/φ)
slot.x = target.x + cos(angle) × repositionRadius
slot.z = target.z + sin(angle) × repositionRadius
```

연속된 정수를 원 위에 매핑할 때 가장 균등하게 분산되는 각도다.
`repositionRadius ≥ separationRadius × 0.7` 조건으로 슬롯 간 진동을 방지한다.

---

### 6. Windup / Recover 진행도 (`sim/Npc.cpp`)

```
windupProgress  = clamp(windupTimer  / attackWindupTime,  0, 1)
recoverProgress = clamp(recoverTimer / attackRecoverTime, 0, 1)
```

---

### 7. AttackRecover 중 separation drift

Windup과 달리 이동을 완전히 막지 않고, 일반 이동의 약 30% 강도로 drift를 허용한다:

```
position += sep × (separationWeight × 0.3 × moveSpeed × dt)
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

### 10. Progress Bar 채우기 (`viz/Renderer.cpp` — `drawNpc`)

```
fillWidth = floor(BAR_W × progress)    // BAR_W = 20 px
```

---

### 전체 수식 한눈에 보기

```
[이동]        position  += normalize(chaseDir + sep×w) × speed × dt
[분산 강도]   strength   = 1 − d / separationRadius
[타겟 점수]   score      = (1 − d/range)×50 + hyst + bonus − aggro×8
[황금각 슬롯] angle      = id × 2.399963,  slot = target + {cos, sin} × r
[좌표 변환]   screenX    = W/2 + (worldX − centerX) × scale
[진행도]      progress   = clamp(timer / totalTime, 0, 1)
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
