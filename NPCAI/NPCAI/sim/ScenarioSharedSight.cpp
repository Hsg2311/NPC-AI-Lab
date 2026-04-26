#include "ScenarioSharedSight.hpp"
#include "Player.hpp"
#include "Npc.hpp"
#include "NpcGroup.hpp"
#include <memory>
#include <iostream>

namespace sim {

// 공유 시야 데모 배치:
//
//   플레이어 P1 시작: (0, 0, 5)
//
//   그룹 A (청록, groupId=0): 중심 (13, 0, 2), 반경 16
//     GoblinA1  (10, 0,  2)  ← 플레이어 경로에서 가장 가까움
//     GoblinA2  (15, 0,  8)  ← 멀리 배치 (A1 감지 후 조사로 반응)
//     GoblinA3  (18, 0, -3)  ← 멀리 배치 (A1 감지 후 조사로 반응)
//
//   그룹 B (황금, groupId=1): 중심 (32, 0, -4), 반경 14
//     GoblinB1  (28, 0, -2)
//     GoblinB2  (34, 0, -6)
//     GoblinB3  (31, 0,  4)

void ScenarioSharedSight::setup(Room& room) {
    // ── 플레이어 ──────────────────────────────────────────────────────────────
    auto p1 = std::make_shared<Player>("P1", Vec3{ 0.f, 0.f, 5.f }, 100.f, 20.f);
    room.addActor(p1);
    controlledPlayer_ = p1.get();

    // ── 고블린 설정 ───────────────────────────────────────────────────────────
    // 개별 감지 범위를 좁게 설정 → 공유 시야가 없으면 일부 NPC는 반응 불가
    NpcConfig goblin;
    goblin.maxHp              = 60.f;
    goblin.moveSpeed          = 5.5f;
    goblin.detectionRange     = 7.f;   // 좁은 감지 범위 — 공유 시야의 효과를 부각
    goblin.attackRange        = 1.8f;
    goblin.activityZoneRadius = 16.f;
    goblin.attackDamage       = 8.f;
    goblin.attackWindupTime   = 0.30f;
    goblin.attackRecoverTime  = 0.60f;
    goblin.separationRadius   = 3.5f;
    goblin.separationWeight   = 0.7f;
    goblin.canReAggroOnReturn = true;
    goblin.overlapThreshold   = 2;

    // ── 그룹 A ────────────────────────────────────────────────────────────────
    Vec3  centerA{ 13.f, 0.f, 2.f };
    float radA = 16.f;
    NpcGroup* groupA = room.createNpcGroup(centerA, radA);

    auto ga1 = std::make_shared<Npc>("GoblinA1", Vec3{ 10.f, 0.f,  2.f }, goblin);
    auto ga2 = std::make_shared<Npc>("GoblinA2", Vec3{ 15.f, 0.f,  8.f }, goblin);
    auto ga3 = std::make_shared<Npc>("GoblinA3", Vec3{ 18.f, 0.f, -3.f }, goblin);

    for (auto& npc : { ga1.get(), ga2.get(), ga3.get() }) {
        npc->setActivityZone(centerA, radA);
        npc->setGroupId(groupA->getGroupId());
        groupA->addMember(npc->getId());
    }
    room.addActor(ga1);
    room.addActor(ga2);
    room.addActor(ga3);

    // ── 그룹 B ────────────────────────────────────────────────────────────────
    Vec3  centerB{ 32.f, 0.f, -4.f };
    float radB = 14.f;
    NpcGroup* groupB = room.createNpcGroup(centerB, radB);

    auto gb1 = std::make_shared<Npc>("GoblinB1", Vec3{ 28.f, 0.f, -2.f }, goblin);
    auto gb2 = std::make_shared<Npc>("GoblinB2", Vec3{ 34.f, 0.f, -6.f }, goblin);
    auto gb3 = std::make_shared<Npc>("GoblinB3", Vec3{ 31.f, 0.f,  4.f }, goblin);

    for (auto& npc : { gb1.get(), gb2.get(), gb3.get() }) {
        npc->setActivityZone(centerB, radB);
        npc->setGroupId(groupB->getGroupId());
        groupB->addMember(npc->getId());
    }
    room.addActor(gb1);
    room.addActor(gb2);
    room.addActor(gb3);

	std::cout << "[Sim] ScenarioSharedSight: P1, 그룹A(G0) 고블린 3마리, 그룹B(G1) 고블린 3마리\n";
    std::cout << "화살표키로 P1 이동. GoblinA1 감지 범위에 들어오면 그룹 전체가 반응한다.\n";
}

} // namespace sim
