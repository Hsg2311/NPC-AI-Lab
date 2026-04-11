# CLAUDE.md

This file provides guidance to Claude Code when working with this repository.

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

- `sim/` — pure AI/simulation logic (no Windows headers, no rendering)
- `viz/` — WinAPI + GDI 2D visualization (depends on sim/, not the other way)
- `mathUtil.hpp` — original DirectXMath-based math library (kept, not used by sim/)

**Entry point:** `main.cpp` — launches a console window (Logger output) and a WinAPI
window (2D visualizer) side by side.

**Keys:** `Space` = pause/resume · `S` = step one tick (while paused) · `Esc` = quit

---

## Module Architecture

```
sim/                        (no #include <windows.h>)
  Vec3.hpp                  POD 3D vector, header-only, no external deps
  Logger.hpp / .cpp         Singleton console logger  [T:tick][CAT] format
  Actor.hpp / .cpp          Abstract base: id, name, position, facing, hp
  Player.hpp / .cpp         Actor subclass; waypoint-driven movement
  Npc.hpp / .cpp            Actor subclass; 5-state AI machine
  DummyPlayerController     Per-player cyclic waypoint routes
  Room.hpp / .cpp           Simulation container; tick loop; buildSnapshot()
  DebugSnapshot.hpp         Plain-data boundary struct (sim → viz handoff)

viz/
  Renderer.hpp / .cpp       GDI double-buffered 2D renderer (XZ plane)
  Application.hpp / .cpp    WinAPI window, WM_TIMER loop, key handling
```

**Separation rule:** `sim/` code must never include `viz/` headers.
`viz/` code must never read AI object internals directly — only via `DebugSnapshot`.

---

## sim/ Module Details

### Room::tick(float dt) — update order
1. `Logger::get().setTick(tickCount_)`
2. `dummyCtrl_.update(dt)` — assign waypoint targets to players
3. All living **Players** → `update(dt, room)` — execute movement
4. All **NPCs** → `update(dt, room)` — AI decision + movement
5. `tickCount_++`
6. If `tickCount_ % dumpInterval_ == 0` → `dumpSnapshot()` (console table)

### Room::buildSnapshot() — renderer handoff
Iterates `actors_`, casts to `Player*` / `Npc*`, fills `DebugSnapshot`.
Called every frame by `Application` after `tick()`.

### Actor::facing_
Updated by `Player::update()` and `Npc::updateChase()` / `Npc::updateReturn()`
whenever the actor moves. Used for the direction arrow in the renderer.

---

## NPC State Machine

```
Idle   → Chase   : nearest living Player within detectionRange
Chase  → Attack  : distance to target ≤ attackRange
Chase  → Return  : target dead/gone OR dist > chaseRange
Attack → Chase   : dist > attackRange × 1.5  (gap opened)
Attack → Return  : target dead/gone
Return → Chase   : new Player enters detectionRange during return
Return → Idle    : reached spawnPos (threshold 0.3 units)
Dead   → (none)  : terminal; respawn timer hook goes in updateDead()
```

Every transition is logged via `Logger::logTransition()`:
`[T:0042][NPC:Goblin01] Chase -> Attack  (in attack range)`

---

## viz/ Module Details

### Renderer
- `Camera` struct: `worldCenterX/Z` (world point at screen center), `scale` (px/unit)
- Default: center=(20,0), scale=12 → fits scenario world (-5…40 X, -15…15 Z) in 920×660
- Double-buffered: `CreateCompatibleDC` → draw → `BitBlt` → `DeleteDC`
- **Always** `DeleteObject` every `HPEN` / `HBRUSH` created inline
- `#define NOMINMAX` before `<windows.h>` — prevents `min`/`max` macro conflicts

### Application
- `WM_TIMER` (16 ms) drives `room_.tick(DT)` → `buildSnapshot()` → `InvalidateRect`
- `WM_PAINT` triggers double-buffered `renderer_.render()`
- `GWLP_USERDATA` stores `Application*`; set at `WM_NCCREATE` time (before `WM_CREATE`)
- `hwnd_` is stored at `WM_NCCREATE` so all subsequent handlers can use it safely

---

## Key Design Constraints

- **No ECS, no Behavior Tree, no GOAP** in v1
- **No external libraries** — STL + WinAPI/GDI only
- **No DirectXMath in sim/** — use `sim::Vec3` (simple POD, no alignment requirements)
- **No pseudo-code** — all code must compile as-is
- `sim/` types must remain usable without a WinAPI environment (server portability)

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
