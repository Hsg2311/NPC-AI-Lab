# NPCAI Project — TODO

> 마지막 갱신: 2026-04-11

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
- [o] `CLAUDE.md` — 현재 구조 반영 갱신
- [o] `NPCAI.vcxproj` — 모든 새 파일 등록 완료

---

## 미완료 / 다음 단계

### [v3] DummyPlayerController 패턴 확장

- [ ] `PatrolCircle` — 반경 R의 원 궤도 순환 이동
- [ ] `WanderRandom` — 일정 반경 내 랜덤 이동 (seed 고정 가능)
- [ ] `StandStill(Idle)` — 현재는 웨이포인트 목록이 비어있으면 그냥 멈추는 암묵적 구현; 명시적 패턴으로 정리

### [v3] NPC 개선

- [ ] **Respawn** — `updateDead()`에 respawnTimer 추가, Dead → Idle 전이
- [ ] **HP 회복** — Return 상태에서 spawnPos 복귀 중 hp 점진 회복
- [ ] **공격 받기** — 현재 NPC는 데미지를 받지 않음; Player 공격 이벤트 인터페이스 추가
- [ ] **Confused state** — 플레이어 갑작스러운 소멸(로그아웃 시뮬레이션) 시 짧은 혼란 상태
- [ ] **NPC Config 다양화** — Ranged NPC (원거리 공격, attackRange > detectionRange)

### [v3] 시각화 개선

- [ ] **HP 바** — 각 actor 아래 체력 게이지 표시
- [ ] **카메라 pan/zoom** — 마우스 드래그(pan), 휠(zoom)으로 Camera 조정
- [ ] **Actor 선택** — 클릭 시 해당 actor 정보 사이드 패널 표시
- [ ] **로그 오버레이** — 최근 N개 상태 전이 로그를 창 오른쪽에 표시 (Logger 출력 미러링)
- [ ] **시뮬레이션 속도 조절** — `+` / `-` 키로 tick rate 배율 변경 (×0.5 / ×1 / ×2 / ×4)
- [ ] **스폰 위치 표시** — NPC spawnPos에 작은 × 마커 표시

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
    Npc.hpp / Npc.cpp
    DummyPlayerController.hpp / DummyPlayerController.cpp
    Room.hpp / Room.cpp
    DebugSnapshot.hpp
  viz/
    Renderer.hpp / Renderer.cpp
    Application.hpp / Application.cpp
  mathUtil.hpp               (기존 DirectXMath 수학 라이브러리, 독립 유지)
  main.cpp
  NPCAI.vcxproj
  CLAUDE.md
  TODO.md
```
