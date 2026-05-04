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

    if (orderDirty_) {
        // 새 명령 수신 시 1회 계산 (FlankLeft/Right/Encircle/DenseHold/DenseAdvance)
        pushCommandsToMembers(room);
        orderDirty_ = false;
    } else if (currentOrder_.type == SquadOrderType::WedgeCharge) {
        // 쐐기 돌진: 타겟이 움직이므로 매 틱 슬롯 갱신
        pushCommandsToMembers(room);
    }
    // FlankLeft/Right/Encircle/DenseHold: 슬롯 고정 — 재계산 없음
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

// ─── calcDenseSlots ───────────────────────────────────────────────────────────
// center 기준 직사각형 그리드. forward 방향이 앞줄.
// cols = ceil(sqrt(count)), spacing = attackRange * 0.8

std::vector<Vec3> TacticalSquad::calcDenseSlots(const Vec3& center,
                                                  const Vec3& forward,
                                                  int count) const {
    std::vector<Vec3> slots;
    slots.reserve(static_cast<size_t>(count));
    if (count <= 0) return slots;

    float spacing = memberAttackRange_ * 0.8f;
    if (spacing < 1.2f) spacing = 1.2f;

    // XZ 평면 우방향 벡터
    Vec3 right{ -forward.z, 0.f, forward.x };

    int cols = static_cast<int>(std::ceilf(std::sqrtf(static_cast<float>(count))));
    if (cols < 1) cols = 1;
    int rows = (count + cols - 1) / cols;

    for (int i = 0; i < count; ++i) {
        int col = i % cols;
        int row = i / cols;
        float colOff = (static_cast<float>(col) - static_cast<float>(cols - 1) * 0.5f) * spacing;
        float rowOff = (static_cast<float>(row) - static_cast<float>(rows - 1) * 0.5f) * spacing;
        slots.push_back(center + right * colOff + forward * rowOff);
    }
    return slots;
}

// ─── calcWedgeSlots ───────────────────────────────────────────────────────────
// V자 화살표 대형. 첨단이 타겟 방향. 행 i에 (i+1)명.

std::vector<Vec3> TacticalSquad::calcWedgeSlots(const Vec3& targetPos,
                                                  const Vec3& fromPos,
                                                  int count) const {
    std::vector<Vec3> slots;
    slots.reserve(static_cast<size_t>(count));
    if (count <= 0) return slots;

    float spacing = memberAttackRange_ * 1.2f;
    if (spacing < 1.5f) spacing = 1.5f;

    Vec3 toTarget = (targetPos - fromPos);
    float dist = toTarget.length();
    Vec3 forward = (dist > 0.01f) ? (toTarget / dist) : Vec3{ 1.f, 0.f, 0.f };
    Vec3 right{ -forward.z, 0.f, forward.x };

    // 첨단: 타겟에서 attackRange 거리
    Vec3 tip = targetPos - forward * memberAttackRange_;

    int idx = 0;
    for (int row = 0; idx < count; ++row) {
        int rowCount = row + 1;
        float rowDist = static_cast<float>(row) * spacing * 1.5f;
        Vec3  rowCenter = tip - forward * rowDist;  // 첨단에서 뒤로
        for (int col = 0; col < rowCount && idx < count; ++col, ++idx) {
            float colOff = (static_cast<float>(col) - static_cast<float>(rowCount - 1) * 0.5f) * spacing;
            slots.push_back(rowCenter + right * colOff);
        }
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
                    cmd.type             = TacticalCommandType::FlankTarget;
                    cmd.targetId         = ord.targetId;
                    cmd.slotOffset       = slots[static_cast<size_t>(i)];
                    cmd.slotRefTargetPos = targetPos;
                    cmd.abandonDist      = ord.approachRadius * 2.f;
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
                    cmd.type             = TacticalCommandType::FlankTarget;
                    cmd.targetId         = ord.targetId;
                    cmd.slotOffset       = slots[static_cast<size_t>(i)];
                    cmd.slotRefTargetPos = targetPos;
                    cmd.abandonDist      = ord.approachRadius * 2.f;
                    tnpc->receiveCommand(cmd);
                }
            }
            break;
        }

        case SquadOrderType::DenseHold: {
            Actor* targetActor = room.findActorById(ord.targetId);
            if (!targetActor || !targetActor->isAlive()) return;
            Vec3 targetPos = targetActor->getPosition();

            // 현재 멤버 centroid 계산
            Vec3 centroid{};
            int liveCount = 0;
            for (uint32_t id : memberIds_) {
                Actor* a = room.findActorById(id);
                if (a && a->isAlive()) { centroid += a->getPosition(); ++liveCount; }
            }
            if (liveCount == 0) return;
            centroid = centroid / static_cast<float>(liveCount);

            Vec3 fwd = (targetPos - centroid);
            float flen = fwd.length();
            if (flen > 0.01f) fwd = fwd / flen; else fwd = Vec3{ 1.f, 0.f, 0.f };

            std::vector<Vec3> slots = calcDenseSlots(centroid, fwd, count);

            for (int i = 0; i < count; ++i) {
                Actor* a = room.findActorById(memberIds_[static_cast<size_t>(i)]);
                if (auto* tnpc = dynamic_cast<TacticalNpc*>(a)) {
                    TacticalCommand cmd;
                    cmd.type       = TacticalCommandType::HoldSlot;
                    cmd.targetId   = ord.targetId;
                    cmd.slotOffset = slots[static_cast<size_t>(i)];
                    tnpc->receiveCommand(cmd);
                }
            }
            break;
        }

        case SquadOrderType::DenseAdvance: {
            Actor* targetActor = room.findActorById(ord.targetId);
            if (!targetActor || !targetActor->isAlive()) return;
            Vec3 targetPos = targetActor->getPosition();

            // sectorPos에서 플레이어 centroid(leaderPos) 방향으로 정렬
            Vec3 fwd = (ord.leaderPos - ord.sectorPos);
            float flen = fwd.length();
            if (flen > 0.01f) fwd = fwd / flen; else fwd = Vec3{ 1.f, 0.f, 0.f };

            std::vector<Vec3> slots = calcDenseSlots(ord.sectorPos, fwd, count);

            for (int i = 0; i < count; ++i) {
                Actor* a = room.findActorById(memberIds_[static_cast<size_t>(i)]);
                if (auto* tnpc = dynamic_cast<TacticalNpc*>(a)) {
                    TacticalCommand cmd;
                    cmd.type             = TacticalCommandType::FlankTarget;
                    cmd.targetId         = ord.targetId;
                    cmd.slotOffset       = slots[static_cast<size_t>(i)];
                    cmd.slotRefTargetPos = targetPos;
                    cmd.abandonDist      = ord.approachRadius * 2.f;
                    tnpc->receiveCommand(cmd);
                }
            }
            break;
        }

        case SquadOrderType::WedgeCharge: {
            Actor* targetActor = room.findActorById(ord.targetId);
            if (!targetActor || !targetActor->isAlive()) return;
            Vec3 targetPos = targetActor->getPosition();

            std::vector<Vec3> slots = calcWedgeSlots(targetPos, ord.leaderPos, count);

            for (int i = 0; i < count; ++i) {
                Actor* a = room.findActorById(memberIds_[static_cast<size_t>(i)]);
                if (auto* tnpc = dynamic_cast<TacticalNpc*>(a)) {
                    TacticalCommand cmd;
                    cmd.type             = TacticalCommandType::FlankTarget;
                    cmd.targetId         = ord.targetId;
                    cmd.slotOffset       = slots[static_cast<size_t>(i)];
                    cmd.slotRefTargetPos = targetPos;
                    cmd.abandonDist      = ord.approachRadius * 3.f;
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
