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
//   P1 시작: (-10, 0, 0)  — 화살표키로 이동
//
//   PlatoonLeader "Boss":  (15, 0,  0)
//   Squad A 멤버 2명:      (12, 0, -3),  (12, 0, -6)   ← 좌측 배치
//   Squad B 멤버 2명:      (12, 0,  3),  (12, 0,  6)   ← 우측 배치
//
// 플레이어가 Boss 접근 시:
//   Squad A → FlankLeft  (플레이어 왼쪽 측면)
//   Squad B → FlankRight (플레이어 오른쪽 측면)

void ScenarioTactical::setup(Room& room) {
    // ── 플레이어 ──────────────────────────────────────────────────────────────
    auto p1 = std::make_shared<Player>("P1", Vec3{ -10.f, 0.f, 0.f }, 100.f, 20.f);
    room.addActor(p1);
    controlledPlayer_ = p1.get();

    // ── 전술 NPC 설정 ─────────────────────────────────────────────────────────
    TacticalNpcConfig cfg;
    cfg.maxHp             = 120.f;
    cfg.moveSpeed         = 4.5f;
    cfg.attackRange       = 2.f;
    cfg.attackDamage      = 12.f;
    cfg.attackWindupTime  = 0.35f;
    cfg.attackRecoverTime = 0.75f;
    cfg.separationRadius  = 3.f;
    cfg.separationWeight  = 0.5f;

    TacticalNpcConfig leaderCfg = cfg;
    leaderCfg.maxHp       = 200.f;
    leaderCfg.attackRange = 2.5f;
    leaderCfg.moveSpeed   = 3.5f;

    // ── PlatoonLeader ─────────────────────────────────────────────────────────
    auto leaderPtr = std::make_shared<PlatoonLeader>("Boss", Vec3{ 15.f, 0.f, 0.f }, leaderCfg);
    PlatoonLeader* leader = leaderPtr.get();
    room.addTacticalNpc(leaderPtr);
    room.registerPlatoonLeader(leader);

    // ── Squad A (좌측) ────────────────────────────────────────────────────────
    auto squadA = std::make_unique<TacticalSquad>(0, cfg.attackRange);
    TacticalSquad* pSquadA = squadA.get();
    leader->addSquad(pSquadA);

    auto a1 = std::make_shared<TacticalNpc>("SoldierA1", Vec3{ 12.f, 0.f, -3.f }, cfg);
    auto a2 = std::make_shared<TacticalNpc>("SoldierA2", Vec3{ 12.f, 0.f, -6.f }, cfg);
    a1->setSquadId(0);
    a2->setSquadId(0);
    pSquadA->addMember(a1->getId());
    pSquadA->addMember(a2->getId());
    room.addTacticalNpc(a1);
    room.addTacticalNpc(a2);
    room.addTacticalSquad(std::move(squadA));

    // ── Squad B (우측) ────────────────────────────────────────────────────────
    auto squadB = std::make_unique<TacticalSquad>(1, cfg.attackRange);
    TacticalSquad* pSquadB = squadB.get();
    leader->addSquad(pSquadB);

    auto b1 = std::make_shared<TacticalNpc>("SoldierB1", Vec3{ 12.f, 0.f, 3.f }, cfg);
    auto b2 = std::make_shared<TacticalNpc>("SoldierB2", Vec3{ 12.f, 0.f, 6.f }, cfg);
    b1->setSquadId(1);
    b2->setSquadId(1);
    pSquadB->addMember(b1->getId());
    pSquadB->addMember(b2->getId());
    room.addTacticalNpc(b1);
    room.addTacticalNpc(b2);
    room.addTacticalSquad(std::move(squadB));

    std::cout << "[Sim] ScenarioTactical: P1, Boss(Leader) + Squad A 2명 + Squad B 2명\n";
    std::cout << "화살표키로 P1 이동. Boss가 플레이어를 인식하면 좌우 협공 명령.\n";
}

} // namespace sim
