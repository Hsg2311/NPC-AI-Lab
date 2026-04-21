# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

**MSBuild (command line — use full path, `msbuild` alone is not on PATH in bash):**
```bash
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" \
    NPCAI.sln /p:Configuration=Debug /p:Platform=x64

"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" \
    NPCAI.sln /p:Configuration=Release /p:Platform=x64
```

**Supported configurations:** Debug|Win32, Release|Win32, Debug|x64, Release|x64

**Via Visual Studio 2022:** Open `NPCAI.sln`, select configuration/platform, build.

There are no automated tests and no lint tooling configured.

---

## Project Overview

**NPCAI** is a **standalone NPC AI simulator + 2D visual debug viewer** for validating
server-side Room AI without a game client.

- `NPCAI/sim/` — pure AI/simulation logic (no Windows headers, no rendering)
- `NPCAI/viz/` — WinAPI + GDI 2D visualization (depends on sim/, not the other way)
- `NPCAI/mathUtil.hpp` — original DirectXMath-based math library (kept, not used by sim/)

**Entry point:** `NPCAI/main.cpp` — launches a console window (Logger output) and a WinAPI
window (2D visualizer) side by side.

**Keys:** `Arrows` = move player (HumanControl mode) · `Space` = pause/resume · `S` = step one tick (while paused) · `Esc` = quit

---

## Module Architecture

```
sim/                        (no #include <windows.h>)
  Vec3.hpp                  POD 3D vector, header-only, no external deps
  Logger.hpp / .cpp         Singleton console logger  [T:tick][CAT] format
  Actor.hpp / .cpp          Abstract base: id, name, position, facing, hp
  Player.hpp / .cpp         Actor subclass; waypoint-driven movement
  Npc.hpp / .cpp            Actor subclass; 11-state AI machine
  Squad.hpp / .cpp          Mid-tier coordinator; owns NpcCommand dispatch + target selection
  Platoon.hpp / .cpp        Top-tier commander; issues SquadOrderType to Squads each tick
  DummyPlayerController     Per-player cyclic waypoint routes
  Room.hpp / .cpp           Simulation container; tick loop; buildSnapshot()
  DebugSnapshot.hpp         Plain-data boundary struct (sim → viz handoff)

viz/
  Renderer.hpp / .cpp       GDI double-buffered 2D renderer (XZ plane)
  Application.hpp / .cpp    WinAPI window, WM_TIMER loop, key handling
```

**Command hierarchy:**
```
Platoon  ──(SquadOrderType)──▶  Squad  ──(NpcCommand)──▶  Npc
```

**Separation rule:** `sim/` code must never include `viz/` headers.
`viz/` code must never read AI object internals directly — only via `DebugSnapshot`.

---

## sim/ Module Details

### Room::tick(float dt) — update order
1. `Logger::get().setTick(tickCount_)`
2. `dummyCtrl_.update(dt)` — assign waypoint targets to players
3. All living **Players** → `update(dt, room)`
4. `updatePlatoons(dt)` — Platoons evaluate tactics, issue `SquadOrderType` to Squads
5. `updateSquads(dt)` — Squads select targets, push `NpcCommand` to each member NPC
6. All **NPCs** → `update(dt, room)` — consume commands + run FSM
7. `tickCount_++`
8. If `tickCount_ % dumpInterval_ == 0` → `dumpSnapshot()`

### Room::buildSnapshot() — renderer handoff
Iterates `actors_`, casts to `Player*` / `Npc*`, fills `DebugSnapshot`.
Called every frame by `Application` after `tick()`.

### Actor::facing_
Updated whenever an actor moves. Used for the direction arrow in the renderer.

### Spawn helpers
- `Room::spawnSquad(namePrefix, positions, cfg)` — creates a Squad and one NPC per position; first NPC is leader
- `Room::spawnPlatoon(namePrefix, squadPositions, cfg)` — creates a Platoon with one Squad per inner vector

---

## NPC State Machine

**State int values** are used by `Renderer` (color table) and `DebugSnapshot` — do not reorder.

```
 0  Idle          waiting; squad members await EngageTarget command
 1  Chase         pursuing target (separation forces applied)
 2  AttackWindup  pre-attack phase (no movement, windupTimer_)
 3  AttackRecover post-attack cooldown (light separation drift, recoverTimer_)
 4  Return        walking back to spawnPos (standalone NPCs)
 5  Reposition    orbiting to a less crowded attack slot (golden-angle placement)
 6  Regroup       squad-member detour: move toward squad target without leash limits
 7  Confused      disoriented after leader death; wanders for CONFUSION_DURATION (3s)
 8  MoveToSlot    formation slot movement (infrastructure present, not yet wired)
 9  Retreat       command-driven retreat to destination
10  Dead          terminal state
```

**Autonomous FSM transitions:**
```
Idle   → Chase          : player within detectionRange (score-based selection)
                          OR squadTargetId_ != 0 (squad override)
Chase  → AttackWindup   : dist ≤ attackRange
Chase  → Regroup        : squad active + target gone/leash exceeded/too far from home
Chase  → Return         : standalone (squadId_==-1) target gone/leash exceeded/too far from home
AttackWindup → AttackRecover : windupTimer complete → damage applied
AttackWindup → Chase         : dist > attackRange × 1.5
AttackWindup → Regroup/Return: target gone / too far from home
AttackRecover → AttackWindup : timer done, in range, not overcrowded
AttackRecover → Chase        : timer done, out of range, not overcrowded
AttackRecover → Reposition   : timer done, isOvercrowded()
AttackRecover → Regroup/Return: target gone / too far from home
Reposition → AttackWindup    : arrived at slot, dist ≤ attackRange
Reposition → Chase           : arrived at slot, dist > attackRange
Reposition → Regroup/Return  : target gone / too far from home
Regroup    → Chase           : squad target within chaseRange_
Regroup    → Return          : squadTargetId_ == 0 (squad combat ended)
Return     → Regroup         : squadTargetId_ != 0 detected on entry
Return     → Chase           : player within detectionRange (canReAggroOnReturn=true only)
Return     → Idle            : dist to spawnPos < 0.3
Dead       → (none)          : terminal
```

**Command-driven transitions (Squad → NPC via `NpcCommand`):**
```
any state → Chase/Idle    : NpcCommandType::EngageTarget / Idle
any state → Confused      : NpcCommandType::Confused (issued on leader death)
any state → Retreat       : NpcCommandType::Retreat (destination stored)
Confused  → Idle          : CONFUSION_DURATION (3.0s) elapsed
Retreat   → Idle          : arrived at retreatDestination_
```

Standalone NPCs (`squadId_ == -1`) never receive `NpcCommand` — they self-target using score-based selection and never enter Regroup or Confused states.

Every transition is logged: `[T:0042][NPC:Goblin01] Chase -> AttackWindup  (in attack range)`

---

## Squad Details

**Status lifecycle:**
```
Normal → Confused : leader NPC dies
Confused → Broken : leader dies again while in Confused state
```

**Squad::update() sub-step order (fixed each tick):**
1. `removeDeadMembers` — prune dead NPC ids
2. `checkLeaderValidity` — detect leader death, enter Confused
3. `updateConfused` / `updateBroken` — manage status timers
4. `selectTarget(dt, room)` — aggro hysteresis: acquire within `detectionRange`, retain within `chaseRange` for up to `TARGET_MEMORY_DURATION` (4s)
5. `pushCommandsToMembers` — dispatch `NpcCommand` to each live NPC

Squad disbands automatically when alive member count falls below `MIN_MEMBERS_TO_DISBAND` (2).
`Broken` status silently ignores `Attack`/`Hold` orders from Platoon; only `Retreat` is accepted.

---

## Platoon Details

Platoon has no physical NPC — it is a pure tactical coordinator with no death condition.
Removed from `Room::platoons_` only when all of its Squads have disbanded.

**`evaluateTactics()` each tick:**
- Aggregates `combatEfficiency` (= aliveRatio × avgHpRatio) across all Squad reports
- If overall efficiency < `RETREAT_EFFICIENCY_THRESHOLD` (0.25) → issues `SquadOrderType::Retreat` to all Squads
- Primary target changes are rate-limited by `TARGET_CHANGE_COOLDOWN` (1.0s)
- Target selection uses `selectPrimaryTarget()` — picks the living player visible to any Squad

---

## viz/ Module Details

### Renderer
- `Camera` struct: `worldCenterX/Z`, `scale` (px/unit)
- Default: center=(20,0), scale=12 → fits scenario world in 1280×800
- Double-buffered: `CreateCompatibleDC` → draw → `BitBlt` → `DeleteDC`
- **Always** `DeleteObject` every `HPEN` / `HBRUSH` created inline
- `#define NOMINMAX` before `<windows.h>` — prevents `min`/`max` macro conflicts
- 8-color state legend: Idle(grey) / Chase(red) / Windup(orange) / Recover(dark orange) / Return(green) / Reposition(purple) / Regroup(sky-blue) / Dead(near-black)

### Application
- `WM_TIMER` (16 ms, ~60 FPS) drives `room_.tick(DT)` → `buildSnapshot()` → `InvalidateRect`
- `WM_PAINT` triggers double-buffered `renderer_.render()`
- `GWLP_USERDATA` stores `Application*`; set at `WM_NCCREATE` (before `WM_CREATE`)
- `hwnd_` stored at `WM_NCCREATE` so all subsequent handlers can use it safely

**Simulation modes** (`SimMode` enum, default `HumanControl`):
- `AutoWaypoint` — both P1 and P2 follow scripted cyclic waypoints; set up via `setupSimulation()`
- `HumanControl` — P1 controlled via arrow keys, P2 auto-routed; set up via `setupHumanSimulation()`

**NPC presets** (configured in `setupSimulation` / `setupHumanSimulation`):
- **Goblin** — speed 5.5, detectionRange 12, windup 0.3s / recover 0.6s, separationRadius 3.5, canReAggro=true
- **Orc** — speed 3.0, detectionRange 8, attackRange 3.0, windup 0.6s / recover 1.4s, separationRadius 5.0, canReAggro=false

---

## Key Design Constraints

- **No ECS, no Behavior Tree, no GOAP**
- **No external libraries** — STL + WinAPI/GDI only
- **No DirectXMath in sim/** — use `sim::Vec3` (simple POD, no alignment requirements)
- **No pseudo-code** — all code must compile as-is
- `sim/` types must remain usable without a WinAPI environment (server portability)
- Squad is the **sole issuer** of `NpcCommand` — NPCs in a Squad never self-command

---

## Coding Style

### Naming

| Element | Convention | Example |
|---|---|---|
| Class / Struct | `PascalCase` | `Actor`, `NpcConfig`, `DebugSnapshot` |
| Member variable | `camelCase_` (trailing `_`) | `tickCount_`, `moveSpeed_`, `alive_` |
| Local variable | `camelCase` | `dist`, `nearest`, `oldPen` |
| Method / free function | `camelCase` | `update()`, `buildSnapshot()`, `npcStateStr()` |
| Namespace | lowercase | `sim`, `viz` |
| `enum class` value | `PascalCase` | `NpcState::Idle`, `NpcState::Chase` |
| `static constexpr` (class) | `ALL_CAPS` | `TIMER_ID`, `CLIENT_W` |
| Named ctor arg comment | `/*name=*/value` | `Room(/*roomId=*/1, /*dumpInterval=*/0)` |

### File structure

- **Headers** always start with `#pragma once`; no traditional include guards
- `.hpp` holds public interface + inline trivial accessors only — no non-trivial logic
- `.cpp` holds all implementations; own header is included first, then related headers, then STL headers
- Forward declarations preferred over extra includes in `.hpp`

### Section dividers

Use the `─` (U+2500) box-drawing character followed by trailing dashes to a total width of ~75 chars:

```cpp
// ─── Section Name ──────────────────────────────────────────────────────────
```

Used in `.cpp` between logical function groups, and in `.hpp` inside class bodies to separate regions (State transition / Per-state update / Helpers / Data).

### Bracing & layout

- Opening brace on the same line (K&R) for functions, classes, and namespaces
- Closing namespace brace always annotated: `} // namespace sim`
- `case` labels indented one level from `switch`; simple one-liners kept on a single line:
  ```cpp
  case NpcState::Idle:   updateIdle  (dt, room); break;
  case NpcState::Chase:  updateChase (dt, room); break;
  ```
- Scoped `{}` blocks used inside functions to limit lifetime of temporary GDI objects

### Alignment

- Align `=` in struct member default values (see `NpcConfig`)
- Align corresponding variable names in paired declarations:
  ```cpp
  Player* nearest     = nullptr;
  float   nearestDist = maxRange + 1.f;
  ```
- Align multi-line function parameter columns when parameters split across lines:
  ```cpp
  void drawNpc(HDC hdc, int w, int h,
               const sim::DebugNpcEntry&  npc,
               const sim::DebugSnapshot&  snap);
  ```

### C++ idioms

- `= default` for trivial destructors; `= delete` to suppress unwanted ops
- `override` on every virtual method override
- `nullptr` — never `NULL` or `0` for pointers
- `static_cast<>()` — never C-style casts
- `constexpr` for compile-time constants; `static constexpr` inside classes
- Default member initialization in class body: `bool alive_{ true }`, `uint32_t id_{ 0 }`
- `{}` zero-initialization for structs: `WNDCLASSEXA wc = {};`
- Structured bindings: `for (auto& [id, actor] : actors_)`
- `auto*` for pointer type deduction from `dynamic_cast` / `reinterpret_cast`
- Early returns (guard clauses) instead of deep nesting
- Constructor initializer lists: base class first, then members one per line with leading `,`

### String formatting

- Prefer `std::snprintf` + stack `char buf[]` over `std::ostringstream` for simple format strings
- `const char*` for short string literals and labels; `std::string` for names and passed-around data

### Memory & resource management

- `std::shared_ptr` for owned actors; raw `Actor*` / `Player*` for non-owning queries
- Every inline GDI object (`HPEN`, `HBRUSH`, `HFONT`, `HBITMAP`) is explicitly `DeleteObject()`'d before the scope ends
- Select → use → restore → delete pattern for GDI objects

---

## mathUtil.hpp (legacy, independent)

The original `NPCAI/mathUtil.hpp` (~3300 lines) is a C++20 header-only linear algebra
library in the `mu` namespace, backed by `DirectX::XMVECTOR`.
It is **not used** by the simulator or visualizer.
Keep it untouched unless explicitly working on the math library itself.

Types: `Radian`, `Degree`, `Vec<D>`, `NVec<D>`, `Quat`, `NQuat`, `Mat<R,C>`.
All types require 16-byte alignment (`alignas(16)` or heap allocation).
