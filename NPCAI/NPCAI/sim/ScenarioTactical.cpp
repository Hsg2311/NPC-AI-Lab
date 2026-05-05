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
    auto p1 = std::make_shared<Player>("P1", Vec3{ 0.f, 0.f, 0.f }, 10000.f, 20.f);
    room.addActor(p1);
    controlledPlayer_ = p1.get();

    // ── NPC 공통 설정 ─────────────────────────────────────────────────────────
    TacticalNpcConfig cfg;
    cfg.maxHp             = 80.f;
    cfg.moveSpeed         = 15.f;
    cfg.attackRange       = 2.f;
    cfg.attackDamage      = 10.f;
    cfg.attackWindupTime  = 0.35f;
    cfg.attackRecoverTime = 0.7f;
    cfg.separationRadius  = 6.f;
    cfg.separationWeight  = 1.5f;

    TacticalNpcConfig leaderCfg = cfg;
    leaderCfg.maxHp       = 200.f;
    leaderCfg.moveSpeed   = 18.f;
    leaderCfg.attackRange = 2.5f;

    // ── PlatoonLeader ─────────────────────────────────────────────────────────
    auto leaderPtr = std::make_shared<PlatoonLeader>("Boss", Vec3{ 25.f, 0.f, 0.f }, leaderCfg);
    PlatoonLeader* leader = leaderPtr.get();
    room.addTacticalNpc(leaderPtr);
    room.registerPlatoonLeader(leader);

    // ── Squad A (우상단) ──────────────────────────────────────────────────────
    auto squadA = std::make_unique<TacticalSquad>(0, cfg.attackRange);
    TacticalSquad* pSquadA = squadA.get();
    leader->addSquad(pSquadA);

    const char* namesA[] = { "A1", "A2", "A3", "A4" };
    float posAz[] = { -8.f, -6.f, -10.f, -12.f };
    for (int i = 0; i < 4; ++i) {
        auto npc = std::make_shared<TacticalNpc>(namesA[i], Vec3{ 22.f, 0.f, posAz[i] }, cfg);
        npc->setSquadId(0);
        pSquadA->addMember(npc->getId());
        room.addTacticalNpc(npc);
    }
    room.addTacticalSquad(std::move(squadA));

    // ── Squad B (정면) ────────────────────────────────────────────────────────
    auto squadB = std::make_unique<TacticalSquad>(1, cfg.attackRange);
    TacticalSquad* pSquadB = squadB.get();
    leader->addSquad(pSquadB);

    const char* namesB[] = { "B1", "B2", "B3", "B4" };
    float posBx[] = { 26.f, 28.f, 26.f, 28.f };
    float posBz[] = { -2.f, -2.f,  2.f,  2.f };
    for (int i = 0; i < 4; ++i) {
        auto npc = std::make_shared<TacticalNpc>(namesB[i], Vec3{ posBx[i], 0.f, posBz[i] }, cfg);
        npc->setSquadId(1);
        pSquadB->addMember(npc->getId());
        room.addTacticalNpc(npc);
    }
    room.addTacticalSquad(std::move(squadB));

    // ── Squad C (우하단) ──────────────────────────────────────────────────────
    auto squadC = std::make_unique<TacticalSquad>(2, cfg.attackRange);
    TacticalSquad* pSquadC = squadC.get();
    leader->addSquad(pSquadC);

    const char* namesC[] = { "C1", "C2", "C3", "C4" };
    float posCz[] = { 8.f, 6.f, 10.f, 12.f };
    for (int i = 0; i < 4; ++i) {
        auto npc = std::make_shared<TacticalNpc>(namesC[i], Vec3{ 22.f, 0.f, posCz[i] }, cfg);
        npc->setSquadId(2);
        pSquadC->addMember(npc->getId());
        room.addTacticalNpc(npc);
    }
    room.addTacticalSquad(std::move(squadC));

    std::cout << "[Sim] ScenarioTactical: P1(1명) + Boss(Leader) + Squad A/B/C 각 4명\n";
    std::cout << "화살표키로 P1 이동. Boss가 감지 후 3개 부대 밀집 포위 명령.\n";
}

} // namespace sim
