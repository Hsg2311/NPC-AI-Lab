#include "ScenarioSoloNpc.hpp"
#include "Player.hpp"
#include "Npc.hpp"
#include <memory>
#include <cstdio>

namespace sim {

void ScenarioSoloNpc::setup(Room& room) {
    // ── Player (인간 조작) ────────────────────────────────────────────────────
    auto p1 = std::make_shared<Player>("P1", Vec3{ 0.f, 0.f, 20.f }, 100.f, 20.f);
    room.addActor(p1);
    controlledPlayer_ = p1.get();

    // ── Goblin 설정 ───────────────────────────────────────────────────────────
    NpcConfig goblin;
    goblin.maxHp              = 60.f;
    goblin.moveSpeed          = 5.5f;
    goblin.detectionRange     = 12.f;
    goblin.attackRange        = 1.8f;
    goblin.chaseRange         = 20.f;
    goblin.maxChaseDistance   = 24.f;
    goblin.attackDamage       = 8.f;
    goblin.attackWindupTime   = 0.30f;
    goblin.attackRecoverTime  = 0.60f;
    goblin.separationRadius   = 3.5f;
    goblin.separationWeight   = 0.7f;
    goblin.canReAggroOnReturn = true;
    goblin.overlapThreshold   = 2;

    // ── Orc 설정 ──────────────────────────────────────────────────────────────
    NpcConfig orc;
    orc.maxHp              = 120.f;
    orc.moveSpeed          = 3.0f;
    orc.detectionRange     = 8.f;
    orc.attackRange        = 3.0f;
    orc.chaseRange         = 18.f;
    orc.maxChaseDistance   = 22.f;
    orc.attackDamage       = 22.f;
    orc.attackWindupTime   = 0.60f;
    orc.attackRecoverTime  = 1.40f;
    orc.separationRadius   = 5.0f;
    orc.separationWeight   = 0.5f;
    orc.canReAggroOnReturn = false;
    orc.overlapThreshold   = 1;

    // ── 독립 Goblin 10마리 ───────────────────────────────────────────────────
    room.addActor(std::make_shared<Npc>("Goblin01", Vec3{ 10.f, 0.f,  5.f }, goblin));
    room.addActor(std::make_shared<Npc>("Goblin02", Vec3{ 15.f, 0.f, -3.f }, goblin));
    room.addActor(std::make_shared<Npc>("Goblin03", Vec3{  8.f, 0.f, -8.f }, goblin));
    room.addActor(std::make_shared<Npc>("Goblin04", Vec3{ 20.f, 0.f,  8.f }, goblin));
    room.addActor(std::make_shared<Npc>("Goblin05", Vec3{ 22.f, 0.f, -2.f }, goblin));
    room.addActor(std::make_shared<Npc>("Goblin06", Vec3{ 18.f, 0.f, -10.f}, goblin));
    room.addActor(std::make_shared<Npc>("Goblin07", Vec3{ 28.f, 0.f,  3.f }, goblin));
    room.addActor(std::make_shared<Npc>("Goblin08", Vec3{ 25.f, 0.f, -8.f }, goblin));
    room.addActor(std::make_shared<Npc>("Goblin09", Vec3{ 12.f, 0.f, 12.f }, goblin));
    room.addActor(std::make_shared<Npc>("Goblin10", Vec3{ 30.f, 0.f, -4.f }, goblin));

    // ── 독립 Orc 2마리 ────────────────────────────────────────────────────────
    //room.addActor(std::make_shared<Npc>("Orc01", Vec3{ 25.f, 0.f,  0.f }, orc));
    //room.addActor(std::make_shared<Npc>("Orc02", Vec3{ 28.f, 0.f,  8.f }, orc));

    //printf("[Sim] ScenarioSoloNpc: 1 player, 3 goblins, 2 orcs (all standalone)\n");
    printf("[Sim] ScenarioSoloNpc: 1 player, 10 goblins (all standalone)\n");
}

} // namespace sim
