#include "ScenarioTactical.hpp"
#include "Player.hpp"
#include "TacticalNpc.hpp"
#include "TacticalSquad.hpp"
#include "PlatoonLeader.hpp"
#include "Room.hpp"
#include <memory>
#include <iostream>

namespace sim {

// 배치:
//   P1 시작: (0, 0, 0) — 화살표키로 이동
//
//   PlatoonLeader "Boss": (25, 0,  0)
//   Squad A (4명):  (22, 0, -8) ~ (22, 0, -5)  — 우상단
//   Squad B (4명):  (26, 0,  0) ~ (23, 0,  0)  — 정면
//   Squad C (4명):  (22, 0,  5) ~ (22, 0,  8)  — 우하단
//
// 플레이어 1명 → 항상 포위 전술 발동
//   Boss가 P1을 감지하면 3개 부대를 120° 간격 밀집 대형으로 포위 이동 명령

void ScenarioTactical::setup(Room& room) {
    // ── 플레이어 ──────────────────────────────────────────────────────────────
    auto p1 = std::make_shared<Player>("P1", Vec3{ 0.f, 0.f, 0.f }, 300.f, 20.f);
    room.addActor(p1);
    controlledPlayer_ = p1.get();

    // ── NPC 공통 설정 ─────────────────────────────────────────────────────────
    TacticalNpcConfig cfg;
    cfg.maxHp             = 80.f;
    cfg.moveSpeed         = 9.f;
    cfg.attackRange       = 2.f;
    cfg.attackDamage      = 10.f;
    cfg.attackWindupTime  = 0.35f;
    cfg.attackRecoverTime = 0.7f;
    cfg.separationRadius  = 6.f;
    cfg.separationWeight  = 1.5f;

    TacticalNpcConfig leaderCfg = cfg;
    leaderCfg.maxHp       = 200.f;
    leaderCfg.moveSpeed   = 10.f;
    leaderCfg.attackRange = 2.5f;

    // ── PlatoonLeader ─────────────────────────────────────────────────────────
    auto leaderPtr = std::make_shared<PlatoonLeader>("Boss", Vec3{ 25.f, 0.f, 0.f }, leaderCfg);
    PlatoonLeader* leader = leaderPtr.get();
    room.addTacticalNpc(leaderPtr);
    room.registerPlatoonLeader(leader);

    // ── Squad A (우상단, 20명) ────────────────────────────────────────────────
    auto squadA = std::make_unique<TacticalSquad>(0, cfg.attackRange);
    TacticalSquad* pSquadA = squadA.get();
    leader->addSquad(pSquadA);

    for (int i = 0; i < 20; ++i) {
        char name[8]; std::snprintf(name, sizeof(name), "A%d", i + 1);
        float x = 20.f + static_cast<float>(i % 2) * 2.f;
        float z = -3.f - static_cast<float>(i / 2) * 2.f;
        auto npc = std::make_shared<TacticalNpc>(name, Vec3{ x, 0.f, z }, cfg);
        npc->setSquadId(0);
        pSquadA->addMember(npc->getId());
        room.addTacticalNpc(npc);
    }
    room.addTacticalSquad(std::move(squadA));

    // ── Squad B (정면, 20명) ──────────────────────────────────────────────────
    auto squadB = std::make_unique<TacticalSquad>(1, cfg.attackRange);
    TacticalSquad* pSquadB = squadB.get();
    leader->addSquad(pSquadB);

    for (int i = 0; i < 20; ++i) {
        char name[8]; std::snprintf(name, sizeof(name), "B%d", i + 1);
        float x = 26.f + static_cast<float>(i / 4) * 2.f;
        float z = -4.f + static_cast<float>(i % 4) * 2.f + (i % 2) * 1.f;
        auto npc = std::make_shared<TacticalNpc>(name, Vec3{ x, 0.f, z }, cfg);
        npc->setSquadId(1);
        pSquadB->addMember(npc->getId());
        room.addTacticalNpc(npc);
    }
    room.addTacticalSquad(std::move(squadB));

    // ── Squad C (우하단, 20명) ────────────────────────────────────────────────
    auto squadC = std::make_unique<TacticalSquad>(2, cfg.attackRange);
    TacticalSquad* pSquadC = squadC.get();
    leader->addSquad(pSquadC);

    for (int i = 0; i < 20; ++i) {
        char name[8]; std::snprintf(name, sizeof(name), "C%d", i + 1);
        float x = 20.f + static_cast<float>(i % 2) * 2.f;
        float z =  3.f + static_cast<float>(i / 2) * 2.f;
        auto npc = std::make_shared<TacticalNpc>(name, Vec3{ x, 0.f, z }, cfg);
        npc->setSquadId(2);
        pSquadC->addMember(npc->getId());
        room.addTacticalNpc(npc);
    }
    room.addTacticalSquad(std::move(squadC));

    std::cout << "[Sim] ScenarioTactical: P1(1명) + Boss(Leader) + Squad A/B/C 각 20명\n";
    std::cout << "화살표키로 P1 이동. Boss가 감지 후 3개 부대 포위 명령.\n";
}

} // namespace sim
