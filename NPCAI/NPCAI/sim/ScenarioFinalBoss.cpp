#include "ScenarioFinalBoss.hpp"
#include "FinalBoss.hpp"
#include "Player.hpp"
#include "Room.hpp"
#include <iostream>
#include <memory>

namespace sim {

void ScenarioFinalBoss::setup(Room& room) {
    auto p1 = std::make_shared<Player>("P1", Vec3{ 0.f, 0.f, 0.f },
                                       /*maxHp=*/1000.f, /*moveSpeed=*/12.f);
    room.addActor(p1);
    controlledPlayer_ = p1.get();

    FinalBossConfig cfg;
    auto boss = std::make_shared<FinalBoss>("Demon", Vec3{ 24.f, 0.f, 0.f }, cfg);
    room.addFinalBoss(boss);

    std::cout << "[Sim] ScenarioFinalBoss: 1v1 boss fight (BT-driven)\n";
    std::cout << "Boss: HP " << cfg.maxHp
              << ", melee combo x" << cfg.comboHitCount
              << " (cd " << cfg.comboCooldown << "s)"
              << ", charge (cd " << cfg.chargeCooldown << "s)\n";
    std::cout << "Player: Arrows = move, Z = attack\n";
}

} // namespace sim
