#include "TacticalSquad.hpp"
#include "Room.hpp"
#include "Logger.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace sim {

TacticalSquad::TacticalSquad(int squadId, float memberAttackRange, float memberSeparationRadius)
    : squadId_(squadId)
    , memberAttackRange_(memberAttackRange)
    , memberSeparationRadius_(memberSeparationRadius)
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

void TacticalSquad::updateBoxLeaderPos(const Vec3& pos) {
    if (currentOrder_.type == SquadOrderType::BoxAdvance)
        currentOrder_.leaderPos = pos;
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
    // FlankLeft/Right/Encircle/DenseHold/BoxAdvance: 슬롯 고정 — 재계산 없음
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

// ─── calcEncircleSlots ───────────────────────────────────────────────────────

std::vector<Vec3> TacticalSquad::calcEncircleSlots(const Vec3& targetPos,
                                                     float sectorAngle,
                                                     float sectorSpan,
                                                     float radius,
                                                     int count) const {
    std::vector<Vec3> slots;
    slots.reserve(static_cast<size_t>(count));
    // 섹터를 count 등분 후 각 구획 중앙에 배치 → 인접 Squad 경계 슬롯 겹침 방지
    float arc   = sectorSpan / static_cast<float>(count);
    float start = sectorAngle - sectorSpan * 0.5f + arc * 0.5f;
    for (int i = 0; i < count; ++i) {
        float a = start + arc * static_cast<float>(i);
        slots.push_back(targetPos + Vec3{ std::cosf(a), 0.f, std::sinf(a) } * radius);
    }
    return slots;
}

// ─── calcDenseSlots ───────────────────────────────────────────────────────────
// center 기준 직사각형 그리드. forward 방향이 앞줄.
// spacing = separationRadius * 0.5 (HoldSlot sepScale 감쇠로 수렴 가능, 밀집 외관)

std::vector<Vec3> TacticalSquad::calcDenseSlots(const Vec3& center,
                                                  const Vec3& forward,
                                                  int count) const {
    std::vector<Vec3> slots;
    slots.reserve(static_cast<size_t>(count));
    if (count <= 0) return slots;

    float spacing = memberSeparationRadius_;
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

        case SquadOrderType::Encircle: {
            Actor* targetActor = room.findActorById(ord.targetId);
            if (!targetActor || !targetActor->isAlive()) return;
            Vec3 targetPos = targetActor->getPosition();

            std::vector<Vec3> slots = calcEncircleSlots(
                targetPos, ord.sectorAngle, ord.sectorSpan, ord.approachRadius, count);

            // 각 NPC에게 가장 가까운 미사용 슬롯 배정 (경로 교차 최소화)
            std::vector<bool> slotUsed(static_cast<size_t>(count), false);
            for (int i = 0; i < count; ++i) {
                Actor* a = room.findActorById(memberIds_[static_cast<size_t>(i)]);
                if (!a || !a->isAlive()) continue;

                int   bestSlot = -1;
                float bestDist = -1.f;
                for (int j = 0; j < count; ++j) {
                    if (slotUsed[static_cast<size_t>(j)]) continue;
                    float d = Vec3::distance(a->getPosition(), slots[static_cast<size_t>(j)]);
                    if (bestDist < 0.f || d < bestDist) { bestDist = d; bestSlot = j; }
                }
                if (bestSlot < 0) continue;
                slotUsed[static_cast<size_t>(bestSlot)] = true;

                if (auto* tnpc = dynamic_cast<TacticalNpc*>(a)) {
                    TacticalCommand cmd;
                    cmd.type       = TacticalCommandType::HoldSlot;
                    cmd.targetId   = ord.targetId;
                    cmd.slotOffset = slots[static_cast<size_t>(bestSlot)];
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

        case SquadOrderType::BoxAdvance: {
            Actor* targetActor = room.findActorById(ord.targetId);
            if (!targetActor || !targetActor->isAlive()) return;
            Vec3 targetPos = ord.formationTargetPos;

            // leaderPos → targetPos 방향을 forward로, sectorPos(상대 오프셋)로 부대 중심 계산
            Vec3  toTarget = (targetPos - ord.leaderPos);
            float d        = toTarget.length();
            Vec3  forward  = (d > 0.01f) ? (toTarget / d) : Vec3{ 1.f, 0.f, 0.f };
            Vec3  right{ -forward.z, 0.f, forward.x };

            // 앞면(front face)이 approachRadius 위치에 오도록 center를 halfDepth만큼 뒤로 정렬
            int   rcols     = static_cast<int>(std::ceilf(std::sqrtf(static_cast<float>(count))));
            int   rrows     = (count + rcols - 1) / rcols;
            float halfDepth = static_cast<float>(rrows - 1) * 0.5f * memberSeparationRadius_;

            Vec3 squadCenter = (targetPos - forward * ord.approachRadius)
                               + right   * ord.sectorPos.x
                               - forward * ord.sectorPos.z
                               - forward * halfDepth;

            Vec3  faceDir = (targetPos - squadCenter);
            float fl      = faceDir.length();
            if (fl > 0.01f) faceDir = faceDir / fl; else faceDir = forward;

            std::vector<Vec3> slots = calcDenseSlots(squadCenter, faceDir, count);

            for (int i = 0; i < count; ++i) {
                Actor* a = room.findActorById(memberIds_[static_cast<size_t>(i)]);
                auto*  tnpc = dynamic_cast<TacticalNpc*>(a);
                if (!tnpc) continue;

                TacticalNpcState st = tnpc->getState();
                if (st == TacticalNpcState::AttackWindup ||
                    st == TacticalNpcState::AttackRecover)
                    continue;

                // HoldSlot 이동 중에도 슬롯 변화가 작으면 현재 목표 유지 — 매 틱 재발행 방지
                if (st == TacticalNpcState::HoldSlot) {
                    float drift = Vec3::distance(tnpc->getAssignedSlot(),
                                                 slots[static_cast<size_t>(i)]);
                    if (drift < 2.0f) continue;
                }

                TacticalCommand cmd;
                cmd.type       = TacticalCommandType::HoldSlot;
                cmd.targetId   = ord.targetId;
                cmd.slotOffset = slots[static_cast<size_t>(i)];
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
