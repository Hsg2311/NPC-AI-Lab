# 다음 세션 구현 과제 — 전술 이동 속도 부스트 + 전술 쿨타임

> 작성: 2026-05-04  
> 이전 세션 완료 항목: 3가지 홉 고블린 전술 (포위/경계/각개격파) 전체 구현 완료

---

## 현재 상태 요약

전술 3종 (포위 → 경계 → 각개격파)이 동작 중이다.  
이번에 추가할 기능 2가지:

1. **전술 이동 속도 부스트** — Flank/HoldSlot 상태에서 이동속도 2× 상승
2. **전술 쿨타임** — 전술 활성 10초 후 8초간 Engage로 복귀, 이후 전술 재활성

---

## 기능 1 — 전술 이동 속도 부스트

### 수정 파일: `NPCAI/sim/TacticalNpc.hpp`

클래스 내 상수 추가 (line 127 `CONFUSED_DURATION` 근처):

```cpp
static constexpr float TACTICAL_SPEED_MULT = 2.0f;
```

### 수정 파일: `NPCAI/sim/TacticalNpc.cpp`

**`updateFlank()` — line 278 부근:**
```cpp
// 변경 전
position_ += moveDir * (moveSpeed_ * dt);

// 변경 후
position_ += moveDir * (moveSpeed_ * TACTICAL_SPEED_MULT * dt);
```

**`updateHoldSlot()` — line 302 부근:**
```cpp
// 변경 전
position_ += moveDir * (moveSpeed_ * dt);

// 변경 후
position_ += moveDir * (moveSpeed_ * TACTICAL_SPEED_MULT * dt);
```

---

## 기능 2 — 전술 쿨타임

### 수정 파일: `NPCAI/sim/PlatoonLeader.hpp`

**멤버 변수 추가** (기존 `lastEncircleCentroid_{}` 아래):
```cpp
float tacticPhaseTimer_  { 0.f };   // 현재 전술 활성 경과 시간
float tacticCooldown_    { 0.f };   // 쿨타임 카운트다운
bool  tacticsOnCooldown_ { false }; // 쿨타임 중 플래그
```

**상수 추가** (기존 `ENCIRCLE_RECALC_THRESHOLD` 아래):
```cpp
static constexpr float TACTIC_ACTIVE_DURATION   = 10.0f; // 전술 활성 지속 시간(초)
static constexpr float TACTIC_COOLDOWN_DURATION =  8.0f; // 쿨타임 길이(초)
```

### 수정 파일: `NPCAI/sim/PlatoonLeader.cpp`

**`update()` — 기존 `vigilanceElapsed_` 누적 블록 바로 아래에 추가:**
```cpp
// 쿨타임 관리
if (tacticsOnCooldown_) {
    tacticCooldown_ -= dt;
    if (tacticCooldown_ <= 0.f) {
        tacticsOnCooldown_ = false;
        tacticPhaseTimer_  = 0.f;
        Logger::get().log(name_, "전술 쿨타임 종료 — 전술 재활성");
    }
} else if (tacticsUnlocked_) {
    tacticPhaseTimer_ += dt;
    if (tacticPhaseTimer_ >= TACTIC_ACTIVE_DURATION) {
        tacticsOnCooldown_ = true;
        tacticCooldown_    = TACTIC_COOLDOWN_DURATION;
        tacticPhaseTimer_  = 0.f;
        Logger::get().log(name_, "전술 쿨타임 시작 — Engage 복귀");
    }
}
```

**`evaluateTactics()` — 기존 `!tacticsUnlocked_` 체크를 아래로 교체:**
```cpp
// 기존 (삭제)
if (!tacticsUnlocked_) {

// 교체 후
if (!tacticsUnlocked_ || tacticsOnCooldown_) {
```
나머지 블록 내용(Engage 발행 후 return)은 동일.

---

## 검증 방법

1. `MSBuild NPCAI.sln /p:Configuration=Debug /p:Platform=x64` — 빌드 오류 없음 확인
2. 실행 후 Boss HP를 70% 이하로 낮춤 → 전술 활성 로그 확인
3. Flank 상태 NPC가 Chase 상태보다 눈에 띄게 빠르게 슬롯으로 이동하는지 확인
4. 전술 활성 10초 후 `전술 쿨타임 시작` 로그 + 모든 NPC가 Engage(Chase) 전환 확인
5. 8초 후 `전술 쿨타임 종료` 로그 + 전술 재발동 확인

---

## 관련 파일 위치

| 파일 | 수정 내용 |
|------|-----------|
| `sim/TacticalNpc.hpp` | `TACTICAL_SPEED_MULT` 상수 추가 |
| `sim/TacticalNpc.cpp` | `updateFlank()`, `updateHoldSlot()` 이동 라인 ×2 |
| `sim/PlatoonLeader.hpp` | 멤버 3개 + 상수 2개 추가 |
| `sim/PlatoonLeader.cpp` | `update()` 쿨타임 관리, `evaluateTactics()` 조건 1줄 수정 |
