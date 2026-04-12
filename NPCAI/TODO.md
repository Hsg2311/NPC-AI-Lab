# NPCAI Project — TODO

> 마지막 갱신: 2026-04-12

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

- [o] **AttackWindup** — 공격 전 준비 단계
  - [o] `windupTimer_` 누적, 완료 시 피해 발동 후 AttackRecover 전이
  - [o] Windup 중 **위치 이동 없음** — separation force는 `facing_`만 조정
  - [o] 타겟이 `attackRange × 1.5` 이상 벗어나면 Chase로 복귀
  - [o] `transitionTo()` 진입 시 `windupTimer_ = 0` 리셋 (entry timer reset 패턴)
- [o] **AttackRecover** — 공격 후 경직 단계
  - [o] `recoverTimer_` 누적, 경직 중 약한 separation drift 허용 (`weight × 0.3 × speed × dt`)
  - [o] 완료 시 `isOvercrowded()` 판단 → Reposition 또는 Chase/AttackWindup 전이
  - [o] `transitionTo()` 진입 시 `recoverTimer_ = 0` 리셋
- [o] **Reposition** — 혼잡 시 공격 위치 재배치
  - [o] `calcRepositionTarget()`: golden angle (`id_ × 2.399963 rad`) 기반 균등 분산
  - [o] 슬롯 도착 시 거리 판단 → AttackWindup 또는 Chase 전이
  - [o] `repositionRadius >= separationRadius × 0.7` 조건 보장 (진동 방지)

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
- [o] **Reposition 마커** — 재배치 슬롯에 보라색 원 + NPC → 슬롯 점선
- [o] **Windup/Recover 게이지** — 몸통 위 20×4 px 진행 바 (주황/갈색)
- [o] **7색 상태 범례** — Idle(회)/Chase(빨)/Windup(주황)/Recover(진주황)/Return(초록)/Repos(보라)/Dead(검)
- [o] **플레이어 어그로 카운트** — 플레이어 옆 빨간 `x2` 레이블
- [o] **HUD 어그로 요약** — 상단 좌측에 `P1 aggro: 2` 형식으로 플레이어별 출력

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

### [v4] Squad AI

- [ ] `sim/Squad.hpp/.cpp` — Squad: 멤버 NPC 목록 + Commander 포인터
- [ ] `sim/Commander.hpp/.cpp` — Npc 파생; 매 tick 멤버에게 `assignTarget()` / `assignFormationPos()` 발행
- [ ] `sim/Formation.hpp` — 오프셋 배열 정의 (`Triangle`, `Line`, `Wedge`)
- [ ] `Flank` 행동 — target-commander 벡터 수직 방향을 flanker에 할당
- [ ] `Room::tick()` 수정 — Squad::update() → 개별 NPC update() 순서 보장
- [ ] `DebugSnapshot` 확장 — `DebugSquadEntry` 추가 (formation hull 렌더링용)
- [ ] Renderer 확장 — Squad 외곽선(볼록 hull) 및 formation 오프셋 점 표시

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
    Npc.hpp / Npc.cpp                  ← v3: 7-state, separation, home, score target, reposition
    DummyPlayerController.hpp / .cpp
    Room.hpp / Room.cpp                ← v3: getLivingPlayers, findNearbyNpcPositions, countNpcsTargeting
    DebugSnapshot.hpp                  ← v3: aggroCount, homeX/Z, windupProgress, recoverProgress, reposition fields
  viz/
    Renderer.hpp / Renderer.cpp        ← v3: home marker, return line, reposition dot, progress bar, 7-color legend
    Application.hpp / Application.cpp  ← v3: Goblin/Orc NpcConfig 프리셋
  mathUtil.hpp               (기존 DirectXMath 수학 라이브러리, 독립 유지)
  main.cpp
  NPCAI.vcxproj
  CLAUDE.md
  TODO.md
```

---

## NPC 상태 전이 요약 (v3 현재)

```
Idle   → Chase          : detectionRange 내 플레이어 감지 (score 기반 선택)
Chase  → AttackWindup   : dist ≤ attackRange
Chase  → Return         : target 소멸 / dist > chaseRange / too far from home
AttackWindup → AttackRecover : windupTimer 완료 → 피해 발동
AttackWindup → Chase         : dist > attackRange × 1.5 (타겟 이탈)
AttackWindup → Return        : target 소멸 / too far from home
AttackRecover → AttackWindup : 경직 완료, in range, 혼잡 없음
AttackRecover → Chase        : 경직 완료, out of range, 혼잡 없음
AttackRecover → Reposition   : 경직 완료, isOvercrowded()
AttackRecover → Return       : target 소멸 / too far from home
Reposition → AttackWindup    : 슬롯 도착, dist ≤ attackRange
Reposition → Chase           : 슬롯 도착, dist > attackRange
Reposition → Return          : target 소멸 / too far from home
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
