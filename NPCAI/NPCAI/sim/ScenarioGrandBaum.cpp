#include "ScenarioGrandBaum.hpp"
#include "MidBossTactics.hpp"
#include "Player.hpp"
#include "PlatoonLeader.hpp"
#include "Room.hpp"
#include "TacticalNpc.hpp"
#include "TacticalSquad.hpp"
#include <cstdio>
#include <iostream>
#include <memory>

namespace sim {

void ScenarioGrandBaum::setup(Room& room) {
    auto p1 = std::make_shared<Player>("P1", Vec3{ 0.f, 0.f, 0.f }, 10000.f, 25.f);
    room.addActor(p1);
    controlledPlayer_ = p1.get();

    auto p2 = std::make_shared<Player>("P2", Vec3{ 4.f, 0.f, 4.f }, 10000.f, 12.f);
    room.addActor(p2);
    room.getDummyController().addControl(p2->getId(), {
        {  4.f, 0.f,   4.f },
        { -10.f, 0.f, -12.f },
        {  12.f, 0.f,  -8.f },
    }, /*loop=*/true);

    TacticalNpcConfig slimeCfg;
    slimeCfg.maxHp             = 60.f;
    slimeCfg.moveSpeed         = 13.f;
    slimeCfg.attackRange       = 1.8f;
    slimeCfg.attackDamage      = 8.f;
    slimeCfg.attackWindupTime  = 0.35f;
    slimeCfg.attackRecoverTime = 0.8f;
    slimeCfg.separationRadius  = 4.f;
    slimeCfg.separationWeight  = 1.2f;

    TacticalNpcConfig snakeCfg = slimeCfg;
    snakeCfg.maxHp             = 45.f;
    snakeCfg.moveSpeed         = 18.f;
    snakeCfg.attackDamage      = 12.f;
    snakeCfg.separationRadius  = 3.f;
    snakeCfg.separationWeight  = 0.9f;

    TacticalNpcConfig leaderCfg;
    leaderCfg.maxHp             = 200.f;
    leaderCfg.moveSpeed         = 2.75f;
    leaderCfg.attackRange       = 3.f;
    leaderCfg.attackDamage      = 18.f;
    leaderCfg.attackWindupTime  = 0.5f;
    leaderCfg.attackRecoverTime = 1.0f;
    leaderCfg.separationRadius  = 7.f;
    leaderCfg.separationWeight  = 1.0f;

    auto leaderPtr = std::make_shared<PlatoonLeader>(
        "GrandBaum", Vec3{ 45.f, 0.f, 0.f }, leaderCfg,
        std::make_unique<GrandBaumMidBossTactic>(0.5f));
    PlatoonLeader* leader = leaderPtr.get();
    room.addTacticalNpc(leaderPtr);
    room.registerPlatoonLeader(leader);

    auto makeSquad = [&](int squadId, const char* prefix, int count,
                         const Vec3& origin, const TacticalNpcConfig& cfg) {
        auto squad = std::make_unique<TacticalSquad>(squadId, cfg.attackRange,
                                                     cfg.separationRadius);
        TacticalSquad* squadPtr = squad.get();
        leader->addSquad(squadPtr);

        for (int i = 0; i < count; ++i) {
            char name[16];
            std::snprintf(name, sizeof(name), "%s%d", prefix, i + 1);
            float x = origin.x + static_cast<float>(i % 4) * 2.f;
            float z = origin.z + static_cast<float>(i / 4) * 2.f;
            auto npc = std::make_shared<TacticalNpc>(name, Vec3{ x, 0.f, z }, cfg);
            npc->setSquadId(squadId);
            squadPtr->addMember(npc->getId());
            room.addTacticalNpc(npc);
        }

        room.addTacticalSquad(std::move(squad));
    };

    makeSquad(0, "A", 8,  Vec3{ 38.f, 0.f, -12.f }, slimeCfg); // (ㄱ)
    makeSquad(1, "B", 8,  Vec3{ 38.f, 0.f,  10.f }, slimeCfg); // (ㄴ)
    makeSquad(2, "C", 16, Vec3{ 34.f, 0.f,  -3.f }, slimeCfg); // (ㄷ)
    makeSquad(3, "D", 6,  Vec3{ 50.f, 0.f,  12.f }, snakeCfg); // (ㄹ)

    // Demonstration setup: start below Param.GrandBaum_A so ShieldWall is visible.
    //leader->takeDamage(430.f);

    std::cout << "[Sim] ScenarioGrandBaum: ShieldWall + rear ambush demo\n";
    std::cout << "GrandBaum starts below Param.GrandBaum_A=0.5 for immediate validation.\n";
}

} // namespace sim
