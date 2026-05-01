#include "TacticalSquad.hpp"
#include "Room.hpp"
#include "Logger.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace sim {

TacticalSquad::TacticalSquad(int squadId, float memberAttackRange)
    : squadId_(squadId)
    , memberAttackRange_(memberAttackRange)
{}

// ─── 멤버 관리 ────────────────────────────────────────────────────────────────

void TacticalSquad::addMember(uint32_t npcId) {
    memberIds_.push_back(npcId);
}

void TacticalSquad::removeMember(uint32_t npcId) {
    memberIds_.erase(std::remove(memberIds_.begin(), memberIds_.end(), npcId),
                     memberIds_.end());
}

void TacticalSquad::receiveOrder(const SquadOrder& order) {
    currentOrder_ = order;
    orderDirty_   = true;
}

// ─── update ───────────────────────────────────────────────────────────────────

void TacticalSquad::update(float /*dt*/, Room& room) {
    removeDeadMembers(room);
    if (memberIds_.empty()) return;

    // 새 명령이 있거나 Flank/Encircle 상태에서 슬롯을 갱신
    if (orderDirty_ || currentOrder_.type == SquadOrderType::FlankLeft  ||
                       currentOrder_.type == SquadOrderType::FlankRight  ||
                       currentOrder_.type == SquadOrderType::Encircle) {
        pushCommandsToMembers(room);
        orderDirty_ = false;
    }
}

// ─── removeDeadMembers ────────────────────────────────────────────────────────

void TacticalSquad::removeDeadMembers(Room& room) {
    memberIds_.erase(
        std::remove_if(memberIds_.begin(), memberIds_.end(),
            [&room](uint32_t id) {
                Actor* a = room.findActorById(id);
                return !a || !a->isAlive();
            }),
        memberIds_.end());
}

// ─── calcFlankSlots ───────────────────────────────────────────────────────────
// 타겟 기준으로 좌/우 측면 슬롯을 계산한다.
// leftSide=true: 리더→타겟 방향의 왼쪽, false: 오른쪽
// 슬롯은 타겟에 가장 가까운 순서로 배치된다.

std::vector<Vec3> TacticalSquad::calcFlankSlots(const Vec3& targetPos,
                                                  const Vec3& leaderPos,
                                                  bool leftSide,
                                                  float radius,
                                                  int count) const {
    Vec3 toTarget = (targetPos - leaderPos).normalized();
    // XZ 평면에서 수직 방향
    Vec3 side = leftSide
        ? Vec3{  toTarget.z, 0.f, -toTarget.x }
        : Vec3{ -toTarget.z, 0.f,  toTarget.x };

    std::vector<Vec3> slots;
    slots.reserve(static_cast<size_t>(count));
    float spacing = (memberAttackRange_ + 1.5f);
    for (int i = 0; i < count; ++i) {
        // 측면 방향으로 radius, 정면 방향으로 i * spacing 간격
        Vec3 slot = targetPos
            + side    * radius
            + toTarget * (static_cast<float>(i) * spacing);
        slots.push_back(slot);
    }
    return slots;
}

// ─── calcEncircleSlots ───────────────────────────────────────────────────────

std::vector<Vec3> TacticalSquad::calcEncircleSlots(const Vec3& targetPos,
                                                     float sectorAngle,
                                                     float sectorSpan,
                                                     float radius,
                                                     int count) const {
    std::vector<Vec3> slots;
    slots.reserve(static_cast<size_t>(count));
    if (count == 1) {
        slots.push_back(targetPos + Vec3{ std::cosf(sectorAngle), 0.f,
                                          std::sinf(sectorAngle) } * radius);
        return slots;
    }
    float arc = sectorSpan / static_cast<float>(count - 1);
    float start = sectorAngle - sectorSpan * 0.5f;
    for (int i = 0; i < count; ++i) {
        float a = start + arc * static_cast<float>(i);
        slots.push_back(targetPos + Vec3{ std::cosf(a), 0.f, std::sinf(a) } * radius);
    }
    return slots;
}

// ─── pushCommandsToMembers ───────────────────────────────────────────────────

void TacticalSquad::pushCommandsToMembers(Room& room) {
    if (memberIds_.empty()) return;

    const SquadOrder& ord = currentOrder_;
    int count = static_cast<int>(memberIds_.size());

    switch (ord.type) {
        case SquadOrderType::Idle: {
            TacticalCommand cmd;
            cmd.type = TacticalCommandType::Idle;
            for (uint32_t id : memberIds_) {
                Actor* a = room.findActorById(id);
                if (auto* tnpc = dynamic_cast<TacticalNpc*>(a))
                    tnpc->receiveCommand(cmd);
            }
            break;
        }

        case SquadOrderType::Engage: {
            TacticalCommand cmd;
            cmd.type     = TacticalCommandType::EngageTarget;
            cmd.targetId = ord.targetId;
            for (uint32_t id : memberIds_) {
                Actor* a = room.findActorById(id);
                if (auto* tnpc = dynamic_cast<TacticalNpc*>(a))
                    tnpc->receiveCommand(cmd);
            }
            break;
        }

        case SquadOrderType::FlankLeft:
        case SquadOrderType::FlankRight: {
            Actor* targetActor = room.findActorById(ord.targetId);
            if (!targetActor || !targetActor->isAlive()) return;
            Vec3 targetPos = targetActor->getPosition();

            bool leftSide = (ord.type == SquadOrderType::FlankLeft);
            std::vector<Vec3> slots = calcFlankSlots(
                targetPos, ord.leaderPos, leftSide, ord.approachRadius, count);

            for (int i = 0; i < count; ++i) {
                Actor* a = room.findActorById(memberIds_[static_cast<size_t>(i)]);
                if (auto* tnpc = dynamic_cast<TacticalNpc*>(a)) {
                    TacticalCommand cmd;
                    cmd.type       = TacticalCommandType::FlankTarget;
                    cmd.targetId   = ord.targetId;
                    cmd.slotOffset = slots[static_cast<size_t>(i)];  // 월드 좌표
                    tnpc->receiveCommand(cmd);
                }
            }
            break;
        }

        case SquadOrderType::Encircle: {
            Actor* targetActor = room.findActorById(ord.targetId);
            if (!targetActor || !targetActor->isAlive()) return;
            Vec3 targetPos = targetActor->getPosition();

            std::vector<Vec3> slots = calcEncircleSlots(
                targetPos, ord.sectorAngle, ord.sectorSpan, ord.approachRadius, count);

            for (int i = 0; i < count; ++i) {
                Actor* a = room.findActorById(memberIds_[static_cast<size_t>(i)]);
                if (auto* tnpc = dynamic_cast<TacticalNpc*>(a)) {
                    TacticalCommand cmd;
                    cmd.type       = TacticalCommandType::FlankTarget;
                    cmd.targetId   = ord.targetId;
                    cmd.slotOffset = slots[static_cast<size_t>(i)];
                    tnpc->receiveCommand(cmd);
                }
            }
            break;
        }

        case SquadOrderType::AlternateAttack: {
            for (int i = 0; i < count; ++i) {
                Actor* a = room.findActorById(memberIds_[static_cast<size_t>(i)]);
                auto* tnpc = dynamic_cast<TacticalNpc*>(a);
                if (!tnpc) continue;

                TacticalCommand cmd;
                cmd.targetId = ord.targetId;
                // 이 멤버가 공격 순번인지 판단
                if (i % ord.totalTurns == ord.attackTurn) {
                    cmd.type = TacticalCommandType::EngageTarget;
                } else {
                    cmd.type = TacticalCommandType::AlternateWait;
                }
                tnpc->receiveCommand(cmd);
            }
            break;
        }

        case SquadOrderType::Retreat: {
            TacticalCommand cmd;
            cmd.type = TacticalCommandType::Retreat;
            for (uint32_t id : memberIds_) {
                Actor* a = room.findActorById(id);
                if (auto* tnpc = dynamic_cast<TacticalNpc*>(a))
                    tnpc->receiveCommand(cmd);
            }
            break;
        }
    }
}

// ─── pushConfusedToMembers ────────────────────────────────────────────────────

void TacticalSquad::pushConfusedToMembers(Room& room) {
    TacticalCommand cmd;
    cmd.type = TacticalCommandType::Confused;
    for (uint32_t id : memberIds_) {
        Actor* a = room.findActorById(id);
        if (auto* tnpc = dynamic_cast<TacticalNpc*>(a))
            tnpc->receiveCommand(cmd);
    }
}

} // namespace sim
