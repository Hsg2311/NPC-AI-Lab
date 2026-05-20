#include "ScenarioIsis.hpp"
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

void ScenarioIsis::setup(Room& room) {
    auto p1 = std::make_shared<Player>("P1", Vec3{ 0.f, 0.f, 0.f }, 10000.f, 15.f);
    room.addActor(p1);
    controlledPlayer_ = p1.get();

    auto p2 = std::make_shared<Player>("P2", Vec3{ -38.f, 0.f, -18.f }, 10000.f, 12.f);
    room.addActor(p2);
    room.getDummyController().addControl(p2->getId(), {
        { -38.f, 0.f, -18.f },
        { -52.f, 0.f, -28.f },
        { -34.f, 0.f, -36.f },
    }, /*loop=*/true);

    //auto p3 = std::make_shared<Player>("P3", Vec3{ 38.f, 0.f, -18.f }, 10000.f, 12.f);
    //room.addActor(p3);
    //room.getDummyController().addControl(p3->getId(), {
    //    { 38.f, 0.f, -18.f },
    //    { 52.f, 0.f, -28.f },
    //    { 34.f, 0.f, -36.f },
    //}, /*loop=*/true);

    //auto p4 = std::make_shared<Player>("P4", Vec3{ 0.f, 0.f, 42.f }, 10000.f, 12.f);
    //room.addActor(p4);
    //room.getDummyController().addControl(p4->getId(), {
    //    { 0.f, 0.f, 42.f },
    //    { -14.f, 0.f, 54.f },
    //    { 14.f, 0.f, 54.f },
    //}, /*loop=*/true);

    TacticalNpcConfig buddyCfg;
    buddyCfg.maxHp = 75.f;
    buddyCfg.moveSpeed = 17.f;
    buddyCfg.attackRange = 2.0f;
    buddyCfg.attackDamage = 10.f;
    buddyCfg.attackWindupTime = 0.35f;
    buddyCfg.attackRecoverTime = 0.7f;
    buddyCfg.separationRadius = 4.f;
    buddyCfg.separationWeight = 1.0f;

    TacticalNpcConfig bomberCfg = buddyCfg;
    bomberCfg.maxHp = 55.f;
    bomberCfg.moveSpeed = 15.f;
    bomberCfg.attackDamage = 14.f;
    bomberCfg.separationRadius = 3.5f;
    bomberCfg.separationWeight = 0.8f;

    TacticalNpcConfig leaderCfg;
    leaderCfg.maxHp = 300.f;
    leaderCfg.moveSpeed = 2.75f;
    leaderCfg.attackRange = 3.0f;
    leaderCfg.attackDamage = 18.f * 1.4f;
    leaderCfg.attackWindupTime = 0.5f;
    leaderCfg.attackRecoverTime = 1.0f;
    leaderCfg.separationRadius = 7.f;
    leaderCfg.separationWeight = 1.0f;

    auto leaderPtr = std::make_shared<PlatoonLeader>(
        "Isis", Vec3{ 56.f, 0.f, 0.f }, leaderCfg,
        std::make_unique<IsisMidBossTactic>());
    PlatoonLeader* leader = leaderPtr.get();
    room.addTacticalNpc(leaderPtr);
    room.registerPlatoonLeader(leader);

    auto makeSquad = [&](int squadId, const char* prefix, int count,
                         const Vec3& origin, const TacticalNpcConfig& cfg,
                         int columns) {
        auto squad = std::make_unique<TacticalSquad>(squadId, cfg.attackRange,
                                                     cfg.separationRadius);
        TacticalSquad* squadPtr = squad.get();
        leader->addSquad(squadPtr);

        for (int i = 0; i < count; ++i) {
            char name[16];
            std::snprintf(name, sizeof(name), "%s%d", prefix, i + 1);
            float x = origin.x + static_cast<float>(i % columns) * 2.f;
            float z = origin.z + static_cast<float>(i / columns) * 2.f;
            auto npc = std::make_shared<TacticalNpc>(name, Vec3{ x, 0.f, z }, cfg);
            npc->setSquadId(squadId);
            squadPtr->addMember(npc->getId());
            room.addTacticalNpc(npc);
        }

        room.addTacticalSquad(std::move(squad));
    };

    // Squad order is part of IsisMidBossTactic's contract:
    // 0-1 = Buddy columns, 2-3 = Bomber wedge squads.
	makeSquad(0, "BuddyL", 12, Vec3{ 62.f, 0.f, -34.f }, buddyCfg, 2);
	makeSquad(1, "BuddyR", 12, Vec3{ 62.f, 0.f,  24.f }, buddyCfg, 2);
	makeSquad(2, "BomberL", 40, Vec3{ 34.f, 0.f, -46.f }, bomberCfg, 8);
	makeSquad(3, "BomberR", 40, Vec3{ 34.f, 0.f,  26.f }, bomberCfg, 8);

    std::cout << "[Sim] ScenarioIsis: squad-loss unlock + two-stage WedgeStrike\n";
    std::cout << "Squads: Buddy x12/x12, Bomber x40/x40. Tactic unlocks when any squad drops below 80%.\n";
    std::cout << "Bombers strike first; Buddies re-evaluate clusters and strike second. Cooldown rolls 7-13 seconds.\n";
}

} // namespace sim
