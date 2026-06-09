#include "TacticalSquad.hpp"
#include "Room.hpp"
#include "Player.hpp"
#include "Logger.hpp"
#include <cassert>
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

void TacticalSquad::addMember(TacticalNpc* npc) {
    assert(npc != nullptr);
    assert(memberIds_.size() == memberCache_.size());
    memberIds_.push_back(npc->getId());
    memberCache_.push_back(npc);
}

void TacticalSquad::removeMember(uint32_t npcId) {
    for (size_t i = 0; i < memberIds_.size(); ++i) {
        if (memberIds_[i] == npcId) {
            memberIds_.erase(memberIds_.begin() + static_cast<ptrdiff_t>(i));
            memberCache_.erase(memberCache_.begin() + static_cast<ptrdiff_t>(i));
            return;
        }
    }
}

void TacticalSquad::receiveOrder(const SquadOrder& order) {
    if (!leaderlessBrawlEnabled_ &&
        order.type == SquadOrderType::Engage &&
        currentOrder_.type == SquadOrderType::Engage &&
        currentOrder_.targetId == order.targetId) {
        return;
    }

    leaderlessBrawlEnabled_ = false;
    leaderlessBrawlTimer_ = 0.f;
    leaderlessRetargetTimer_ = 0.f;
    boxRefreshTimer_ = 0.f;
    currentOrder_ = order;
    orderDirty_   = true;
    wedgePrepared_ = false;
    wedgeMemberIds_.clear();
    wedgeMemberCache_.clear();
    wedgePrepareSlots_.clear();
    wedgeExitSlots_.clear();
    activeWedgeChargeId_ = 0;
    wedgeChargeReleased_ = !order.waitForChargeRelease;
}

void TacticalSquad::updateBoxLeaderPos(const Vec3& pos) {
    if (currentOrder_.type == SquadOrderType::BoxAdvance)
        currentOrder_.leaderPos = pos;
}

// ─── update ───────────────────────────────────────────────────────────────────

void TacticalSquad::update(float dt, Room& room) {
    if (memberIds_.empty()) return;

    if (leaderlessBrawlEnabled_) {
        updateLeaderlessBrawl(dt, room);
        return;
    }

    if (orderDirty_) {
        // 새 명령 수신 시 1회 계산 (FlankLeft/Right/Encircle/DenseHold/DenseAdvance)
        pushCommandsToMembers(room);
        orderDirty_ = false;
        return;
    } else if (currentOrder_.type == SquadOrderType::BoxAdvance) {
        boxRefreshTimer_ -= dt;
        if (boxRefreshTimer_ <= 0.f) {
            pushCommandsToMembers(room);
            boxRefreshTimer_ = 0.1f;  // 10Hz
        }
        return;
    }
    if (currentOrder_.type == SquadOrderType::WedgeCharge &&
        !wedgePrepared_ && areMembersAtSlots()) {
        wedgePrepared_ = true;
        if (wedgeChargeReleased_)
            pushCommandsToMembers(room);
    }
    if (currentOrder_.type == SquadOrderType::WedgeCharge &&
        wedgePrepared_ && activeWedgeChargeId_ != 0 &&
        areChargeMembersComplete()) {
        room.endWedgeCharge(activeWedgeChargeId_);
        activeWedgeChargeId_ = 0;
    }
    // Encircle/DenseHold/WedgeCharge: 슬롯/돌진 목표 고정 — 재계산 없음
}

// ─── leaderless brawl ─────────────────────────────────────────────────────────

void TacticalSquad::updateLeaderlessBrawl(float dt, Room& room) {
    if (leaderlessBrawlTimer_ > 0.f) {
        leaderlessBrawlTimer_ -= dt;
        if (leaderlessBrawlTimer_ > 0.f)
            return;
        leaderlessRetargetTimer_ = 0.f;
    }

    leaderlessRetargetTimer_ -= dt;
    if (leaderlessRetargetTimer_ > 0.f)
        return;
    leaderlessRetargetTimer_ = LEADERLESS_RETARGET_INTERVAL;

    currentOrder_ = {};
    currentOrder_.targetId = selectNearestPlayerToSquad(room);
    currentOrder_.type = (currentOrder_.targetId != 0)
        ? SquadOrderType::Engage
        : SquadOrderType::Idle;
    orderDirty_ = false;
    wedgePrepared_ = false;
    wedgeMemberIds_.clear();
    wedgePrepareSlots_.clear();
    wedgeExitSlots_.clear();
    activeWedgeChargeId_ = 0;
    wedgeChargeReleased_ = true;

    pushCommandsToMembers(room);
}

uint32_t TacticalSquad::selectNearestPlayerToSquad(Room& room) const {
    Vec3 center = calcCentroid();
    uint32_t bestId = 0;
    float bestDistSq = -1.f;

    for (Player* player : room.getLivingPlayers()) {
        if (!player || !player->isAlive())
            continue;

        float distSq = Vec3::distanceSq(center, player->getPosition());
        if (bestId == 0 ||
            distSq < bestDistSq ||
            (distSq == bestDistSq && player->getId() < bestId)) {
            bestId = player->getId();
            bestDistSq = distSq;
        }
    }

    return bestId;
}

// ─── removeDeadMembers ────────────────────────────────────────────────────────

void TacticalSquad::removeDeadMembers() {
    size_t w = 0;
    for (size_t r = 0; r < memberIds_.size(); ++r) {
        if (memberCache_[r] && memberCache_[r]->isAlive()) {
            memberIds_[w]   = memberIds_[r];
            memberCache_[w] = memberCache_[r];
            ++w;
        }
    }
    memberIds_.resize(w);
    memberCache_.resize(w);
}

Vec3 TacticalSquad::calcCentroid() const {
    Vec3 sum{};
    int count = 0;
    for (TacticalNpc* tnpc : memberCache_) {
        if (tnpc && tnpc->isAlive()) {
            sum += tnpc->getPosition();
            ++count;
        }
    }
    return (count > 0) ? (sum / static_cast<float>(count)) : Vec3{};
}

bool TacticalSquad::areMembersAtSlots() const {
    bool anyAlive = false;
    for (TacticalNpc* tnpc : memberCache_) {
        if (!tnpc || !tnpc->isAlive()) continue;
        anyAlive = true;
        if (!tnpc->isAtSlot()) return false;
    }
    return anyAlive;
}

bool TacticalSquad::areChargeMembersComplete() const {
    bool anyAlive = false;
    for (TacticalNpc* tnpc : memberCache_) {
        if (!tnpc || !tnpc->isAlive()) continue;
        anyAlive = true;
        TacticalNpcState st = tnpc->getState();
        if (st == TacticalNpcState::AttackWindup ||
            st == TacticalNpcState::AttackRecover)
            continue;
        if (!tnpc->isChargeComplete()) return false;
    }
    return anyAlive;
}

// ─── calcEncircleSlots ───────────────────────────────────────────────────────

void TacticalSquad::endActiveWedgeCharge(Room& room) {
    if (activeWedgeChargeId_ == 0)
        return;
    room.endWedgeCharge(activeWedgeChargeId_);
    activeWedgeChargeId_ = 0;
}

void TacticalSquad::releaseWedgeCharge() {
    if (currentOrder_.type != SquadOrderType::WedgeCharge ||
        wedgeChargeReleased_)
        return;

    wedgeChargeReleased_ = true;
    if (wedgePrepared_)
        orderDirty_ = true;
}

float TacticalSquad::estimateWedgeHalfWidth(float spacingMult,
                                            bool reserveApex) const {
    int count = static_cast<int>(memberIds_.size()) + (reserveApex ? 1 : 0);
    if (count <= 1)
        return 0.f;

    if (spacingMult <= 0.f)
        spacingMult = 1.f;
    float spacing = std::max(memberSeparationRadius_ * 0.75f, 1.5f) *
                    spacingMult;

    int placed = 0;
    int row = 0;
    int widestRow = 1;
    while (placed < count) {
        int rowCount = std::min(row + 1, count - placed);
        widestRow = std::max(widestRow, rowCount);
        placed += rowCount;
        ++row;
    }

    return static_cast<float>(widestRow - 1) * spacing * 0.5f;
}

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
                                                  int count,
                                                  float spacingScale,
                                                  float columnScale,
                                                  int fixedColumnCount) const {
    std::vector<Vec3> slots;
    slots.reserve(static_cast<size_t>(count));
    if (count <= 0) return slots;

    float spacing = memberSeparationRadius_ * spacingScale;
    if (spacing < 1.2f) spacing = 1.2f;

    // XZ 평면 우방향 벡터
    Vec3 right{ -forward.z, 0.f, forward.x };

    int cols = fixedColumnCount;
    if (cols <= 0) {
        if (columnScale < 1.f) columnScale = 1.f;
        cols = static_cast<int>(std::ceilf(std::sqrtf(static_cast<float>(count)) *
                                           columnScale));
    }
    if (cols < 1) cols = 1;
    if (cols > count) cols = count;
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

std::vector<Vec3> TacticalSquad::calcWedgeSlots(const Vec3& apex,
                                                const Vec3& forward,
                                                int count,
                                                float spacingMult) const {
    std::vector<Vec3> slots;
    slots.reserve(static_cast<size_t>(count));
    if (count <= 0) return slots;

    Vec3 fwd = (forward.lengthSq() > 0.01f) ? forward.normalized() : Vec3{ 1.f, 0.f, 0.f };
    Vec3 right{ -fwd.z, 0.f, fwd.x };
    if (spacingMult <= 0.f)
        spacingMult = 1.f;
    float spacing = std::max(memberSeparationRadius_ * 0.75f, 1.5f) *
                    spacingMult;
    float rowSpacing = std::max(memberSeparationRadius_ * 0.55f, 1.25f) *
                       spacingMult;

    int placed = 0;
    int row = 0;
    while (placed < count) {
        int rowCount = std::min(row + 1, count - placed);
        float backOffset = static_cast<float>(row) * rowSpacing;

        for (int col = 0; col < rowCount; ++col) {
            float lateral = (static_cast<float>(col) -
                             static_cast<float>(rowCount - 1) * 0.5f) * spacing;
            slots.push_back(apex - fwd * backOffset + right * lateral);
            ++placed;
        }
        ++row;
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
            for (TacticalNpc* tnpc : memberCache_) {
                if (tnpc && tnpc->isAlive()) tnpc->receiveCommand(cmd);
            }
            break;
        }

        case SquadOrderType::Engage: {
            TacticalCommand cmd;
            cmd.type     = TacticalCommandType::EngageTarget;
            cmd.targetId = ord.targetId;
            for (TacticalNpc* tnpc : memberCache_) {
                if (tnpc && tnpc->isAlive()) tnpc->receiveCommand(cmd);
            }
            break;
        }

        case SquadOrderType::DistributedEngage: {
            if (ord.targetIds.empty())
                return;

            for (int i = 0; i < count; ++i) {
                TacticalNpc* tnpc = memberCache_[static_cast<size_t>(i)];
                if (!tnpc || !tnpc->isAlive()) continue;
                TacticalCommand cmd;
                cmd.type     = TacticalCommandType::EngageTarget;
                cmd.targetId = ord.targetIds[static_cast<size_t>(i) % ord.targetIds.size()];
                tnpc->receiveCommand(cmd);
            }
            break;
        }

        case SquadOrderType::Encircle: {
            Actor* targetActor = room.findActorById(ord.targetId);
            if (!targetActor || !targetActor->isAlive()) return;
            Vec3 targetPos = ord.tacticCenter;

            std::vector<Vec3> slots = calcEncircleSlots(
                targetPos, ord.sectorAngle, ord.sectorSpan, ord.approachRadius, count);

            // 각 NPC에게 가장 가까운 미사용 슬롯 배정 (경로 교차 최소화)
            std::vector<bool> slotUsed(static_cast<size_t>(count), false);
            for (int i = 0; i < count; ++i) {
                TacticalNpc* tnpc = memberCache_[static_cast<size_t>(i)];
                if (!tnpc || !tnpc->isAlive()) continue;

                int   bestSlot   = -1;
                float bestDistSq = -1.f;
                for (int j = 0; j < count; ++j) {
                    if (slotUsed[static_cast<size_t>(j)]) continue;
                    float dSq = Vec3::distanceSq(tnpc->getPosition(), slots[static_cast<size_t>(j)]);
                    if (bestDistSq < 0.f || dSq < bestDistSq) { bestDistSq = dSq; bestSlot = j; }
                }
                if (bestSlot < 0) continue;
                slotUsed[static_cast<size_t>(bestSlot)] = true;

                TacticalCommand cmd;
                cmd.type       = TacticalCommandType::HoldSlot;
                cmd.targetId   = ord.targetId;
                cmd.slotOffset = slots[static_cast<size_t>(bestSlot)];
                tnpc->receiveCommand(cmd);
            }
            break;
        }

        case SquadOrderType::WedgeCharge: {
            Actor* targetActor = room.findActorById(ord.targetId);
            bool hasLiveClusterTarget =
                targetActor && targetActor->isAlive();
            if (!hasLiveClusterTarget) {
                for (uint32_t targetId : ord.targetIds) {
                    Actor* candidate = room.findActorById(targetId);
                    if (candidate && candidate->isAlive()) {
                        hasLiveClusterTarget = true;
                        break;
                    }
                }
            }
            if (!hasLiveClusterTarget)
                return;

            Vec3 centroid = calcCentroid();
            Vec3 targetCenter = ord.tacticCenter;
            Vec3 forward = targetCenter - centroid;
            float flen = forward.length();
            if (flen > 0.01f) forward = forward / flen;
            else              forward = Vec3{ 1.f, 0.f, 0.f };

            if (!wedgePrepared_) {
                Vec3 prepareApex = centroid + forward * WEDGE_PREP_APEX_DISTANCE;
                int slotCount = count + (ord.reserveWedgeApex ? 1 : 0);
                std::vector<Vec3> slots =
                    calcWedgeSlots(prepareApex, forward, slotCount,
                                   ord.wedgeSpacingMult);
                Vec3 exitApex = targetCenter + forward * WEDGE_EXIT_DISTANCE;

                wedgeMemberIds_.clear();
                wedgePrepareSlots_.clear();
                wedgeExitSlots_.clear();

                std::vector<bool> slotUsed(slots.size(), false);
                if (ord.reserveWedgeApex && !slotUsed.empty())
                    slotUsed[0] = true;
                for (int i = 0; i < count; ++i) {
                    TacticalNpc* tnpc = memberCache_[static_cast<size_t>(i)];
                    if (!tnpc || !tnpc->isAlive()) continue;

                    int   bestSlot   = -1;
                    float bestDistSq = -1.f;
                    for (int j = 0; j < static_cast<int>(slots.size()); ++j) {
                        if (slotUsed[static_cast<size_t>(j)]) continue;
                        float dSq = Vec3::distanceSq(tnpc->getPosition(), slots[static_cast<size_t>(j)]);
                        if (bestDistSq < 0.f || dSq < bestDistSq) {
                            bestDistSq = dSq;
                            bestSlot   = j;
                        }
                    }
                    if (bestSlot < 0) continue;
                    slotUsed[static_cast<size_t>(bestSlot)] = true;

                    Vec3 prepareSlot = slots[static_cast<size_t>(bestSlot)];
                    Vec3 rel = prepareSlot - prepareApex;
                    Vec3 exitSlot = exitApex + rel;

                    wedgeMemberIds_.push_back(tnpc->getId());
                    wedgeMemberCache_.push_back(tnpc);
                    wedgePrepareSlots_.push_back(prepareSlot);
                    wedgeExitSlots_.push_back(exitSlot);

                    TacticalCommand cmd;
                    cmd.type       = TacticalCommandType::HoldSlot;
                    cmd.targetId   = ord.targetId;
                    cmd.slotOffset = prepareSlot;
                    cmd.useHoldFacing = true;
                    cmd.holdFacing = forward;
                    tnpc->receiveCommand(cmd);
                }
                return;
            }

            if (activeWedgeChargeId_ == 0)
                activeWedgeChargeId_ = room.beginWedgeCharge();

            for (size_t i = 0; i < wedgeMemberCache_.size(); ++i) {
                TacticalNpc* tnpc = wedgeMemberCache_[i];
                if (!tnpc || !tnpc->isAlive()) continue;

                TacticalNpcState st = tnpc->getState();
                if (st == TacticalNpcState::AttackWindup ||
                    st == TacticalNpcState::AttackRecover ||
                    st == TacticalNpcState::ChargeThrough)
                    continue;

                TacticalCommand cmd;
                cmd.type         = TacticalCommandType::ChargeThrough;
                cmd.targetId     = ord.targetId;
                cmd.targetIds    = ord.targetIds;
                cmd.chargeId     = activeWedgeChargeId_;
                cmd.slotOffset   = wedgeExitSlots_[i];
                cmd.chargeDir    = forward;
                cmd.chargeCenter = targetCenter;
                cmd.impactRadius = std::max(WEDGE_IMPACT_RADIUS, memberAttackRange_);
                float damageMult = (ord.wedgeDamageMult > 0.f)
                    ? ord.wedgeDamageMult
                    : 1.f;
                cmd.impactDamage = WEDGE_CHARGE_DAMAGE * damageMult;
                cmd.passDistance = WEDGE_PASS_DISTANCE;
                cmd.speedMult    = (ord.chargeSpeedMult > 0.f)
                    ? ord.chargeSpeedMult
                    : WEDGE_SPEED_MULT;
                tnpc->receiveCommand(cmd);
            }
            break;
        }

        case SquadOrderType::DenseHold: {
            Actor* targetActor = room.findActorById(ord.targetId);
            if (!targetActor || !targetActor->isAlive()) return;
            Vec3 targetPos = targetActor->getPosition();

            Vec3 centroid = calcCentroid();
            if (memberIds_.empty()) return;

            Vec3 fwd = (targetPos - centroid);
            float flen = fwd.length();
            if (flen > 0.01f) fwd = fwd / flen; else fwd = Vec3{ 1.f, 0.f, 0.f };

            std::vector<Vec3> slots = calcDenseSlots(centroid, fwd, count);

            for (int i = 0; i < count; ++i) {
                TacticalNpc* tnpc = memberCache_[static_cast<size_t>(i)];
                if (!tnpc || !tnpc->isAlive()) continue;
                TacticalCommand cmd;
                cmd.type       = TacticalCommandType::HoldSlot;
                cmd.targetId   = ord.targetId;
                cmd.slotOffset = slots[static_cast<size_t>(i)];
                tnpc->receiveCommand(cmd);
            }
            break;
        }

        case SquadOrderType::GuardBoss: {
            Vec3 guardDir{ std::cosf(ord.sectorAngle), 0.f, std::sinf(ord.sectorAngle) };
            Vec3 squadCenter = ord.tacticCenter + guardDir * ord.approachRadius;

            Vec3 faceDir = ord.formationTargetPos - squadCenter;
            float fl = faceDir.length();
            if (fl > 0.01f) faceDir = faceDir / fl;
            else            faceDir = guardDir * -1.f;

            std::vector<Vec3> slots = calcDenseSlots(squadCenter, faceDir, count,
                                                     ord.slotSpacingScale,
                                                     ord.slotColumnScale,
                                                     ord.slotColumnCount);

            for (int i = 0; i < count; ++i) {
                TacticalNpc* tnpc = memberCache_[static_cast<size_t>(i)];
                if (!tnpc || !tnpc->isAlive()) continue;
                TacticalCommand cmd;
                cmd.type       = TacticalCommandType::GuardSlot;
                cmd.targetId   = ord.targetId;
                cmd.slotOffset = slots[static_cast<size_t>(i)];
                tnpc->receiveCommand(cmd);
            }
            break;
        }

        case SquadOrderType::FormationHold:
        case SquadOrderType::FormationGuard: {
            Actor* targetActor = room.findActorById(ord.targetId);
            if (!targetActor || !targetActor->isAlive()) return;

            Vec3 center = ord.tacticCenter;
            Vec3 faceDir = ord.formationTargetPos - center;
            float fl = faceDir.length();
            if (fl > 0.01f) faceDir = faceDir / fl;
            else            faceDir = Vec3{ 1.f, 0.f, 0.f };

            std::vector<Vec3> slots = calcDenseSlots(center, faceDir, count,
                                                     ord.slotSpacingScale,
                                                     ord.slotColumnScale,
                                                     ord.slotColumnCount);

            for (int i = 0; i < count; ++i) {
                TacticalNpc* tnpc = memberCache_[static_cast<size_t>(i)];
                if (!tnpc || !tnpc->isAlive()) continue;
                TacticalCommand cmd;
                cmd.type = (ord.type == SquadOrderType::FormationGuard)
                    ? TacticalCommandType::GuardSlot
                    : TacticalCommandType::HoldSlot;
                cmd.targetId   = ord.targetId;
                cmd.slotOffset = slots[static_cast<size_t>(i)];
                cmd.speedMult  = ord.speedMult;
                tnpc->receiveCommand(cmd);
            }
            break;
        }

        case SquadOrderType::RingGuard: {
            std::vector<TacticalNpc*> liveMembers;
            liveMembers.reserve(memberCache_.size());
            for (TacticalNpc* tnpc : memberCache_) {
                if (tnpc && tnpc->isAlive())
                    liveMembers.push_back(tnpc);
            }

            int liveCount = static_cast<int>(liveMembers.size());
            if (liveCount <= 0)
                return;

            std::vector<Vec3> slots;
            slots.reserve(liveMembers.size());
            if (ord.slotSpacingScale > 1.f && ord.approachRadius > 0.01f &&
                ord.sectorSpan > 0.01f) {
                float minArcSpacing = ord.slotSpacingScale;
                int maxPerLane = static_cast<int>(std::floorf(
                    (ord.sectorSpan * ord.approachRadius) / minArcSpacing));
                maxPerLane = std::clamp(maxPerLane, 1, liveCount);

                int laneCount = (liveCount + maxPerLane - 1) / maxPerLane;
                float laneSpacing = std::max(ord.slotColumnScale, 0.1f);
                if (laneCount > 1) {
                    float innerFloor = std::max(memberSeparationRadius_ * 2.f,
                                                ord.approachRadius * 0.5f);
                    float usableDepth = std::max(ord.approachRadius - innerFloor, 0.f);
                    laneSpacing = std::min(laneSpacing,
                                           usableDepth / static_cast<float>(laneCount - 1));
                }

                for (int lane = 0; lane < laneCount; ++lane) {
                    int laneStart = lane * maxPerLane;
                    int laneMembers = std::min(maxPerLane, liveCount - laneStart);
                    if (laneMembers <= 0)
                        continue;

                    float radius = ord.approachRadius -
                        laneSpacing * static_cast<float>(lane);
                    float arc = ord.sectorSpan / static_cast<float>(laneMembers);
                    float start = ord.sectorAngle - ord.sectorSpan * 0.5f + arc * 0.5f;
                    for (int i = 0; i < laneMembers; ++i) {
                        float a = start + arc * static_cast<float>(i);
                        slots.push_back(ord.tacticCenter +
                            Vec3{ std::cosf(a), 0.f, std::sinf(a) } * radius);
                    }
                }
            } else {
                slots = calcEncircleSlots(
                    ord.tacticCenter, ord.sectorAngle, ord.sectorSpan,
                    ord.approachRadius, liveCount);
            }

            std::vector<bool> slotUsed(slots.size(), false);
            for (int i = 0; i < liveCount; ++i) {
                TacticalNpc* tnpc = liveMembers[static_cast<size_t>(i)];
                if (!tnpc || !tnpc->isAlive()) continue;

                int   bestSlot   = -1;
                float bestDistSq = -1.f;
                for (int j = 0; j < static_cast<int>(slots.size()); ++j) {
                    if (slotUsed[static_cast<size_t>(j)]) continue;
                    float dSq = Vec3::distanceSq(tnpc->getPosition(), slots[static_cast<size_t>(j)]);
                    if (bestDistSq < 0.f || dSq < bestDistSq) {
                        bestDistSq = dSq;
                        bestSlot   = j;
                    }
                }
                if (bestSlot < 0) continue;
                slotUsed[static_cast<size_t>(bestSlot)] = true;

                Vec3 slot = slots[static_cast<size_t>(bestSlot)];
                Vec3 outward = slot - ord.tacticCenter;
                if (outward.lengthSq() > 0.01f)
                    outward = outward.normalized();
                else
                    outward = Vec3{ 1.f, 0.f, 0.f };

                TacticalCommand cmd;
                cmd.type = TacticalCommandType::HoldSlot;
                cmd.targetId = ord.targetId;
                cmd.slotOffset = slot;
                cmd.useHoldFacing = true;
                cmd.holdFacing = outward;
                tnpc->receiveCommand(cmd);
            }
            break;
        }

        case SquadOrderType::RetreatFormUp: {
            // 박스/밀집 대형을 만들지 않고, 현재 배치를 유지한 채 공통 이동량으로 후퇴한다.
            Vec3 retreatDelta = ord.tacticCenter - ord.leaderPos;

            for (int i = 0; i < count; ++i) {
                TacticalNpc* tnpc = memberCache_[static_cast<size_t>(i)];
                if (!tnpc || !tnpc->isAlive()) continue;
                TacticalCommand cmd;
                cmd.type       = TacticalCommandType::HoldSlot;
                cmd.targetId   = ord.targetId;
                cmd.slotOffset = tnpc->getPosition() + retreatDelta;
                cmd.speedMult  = ord.speedMult;
                tnpc->receiveCommand(cmd);
            }
            break;
        }

        case SquadOrderType::BoxAdvance: {
            Actor* targetActor = room.findActorById(ord.targetId);
            if (!targetActor || !targetActor->isAlive()) return;
            Vec3 boxCenter = ord.tacticCenter;
            Vec3 faceTargetPos = ord.formationTargetPos;

            // 보스 중심 박스 대형: 위치 기준은 tacticCenter, 방향만 플레이어 centroid를 바라본다.
            Vec3  toTarget = (faceTargetPos - boxCenter);
            float d        = toTarget.length();
            Vec3  forward  = (d > 0.01f) ? (toTarget / d) : Vec3{ 1.f, 0.f, 0.f };
            Vec3  right{ -forward.z, 0.f, forward.x };

            Vec3 squadCenter = boxCenter
                               + right   * ord.sectorPos.x
                               - forward * ord.sectorPos.z;

            Vec3  faceDir = (faceTargetPos - squadCenter);
            float fl      = faceDir.length();
            if (fl > 0.01f) faceDir = faceDir / fl; else faceDir = forward;

            std::vector<Vec3> slots = calcDenseSlots(squadCenter, faceDir, count);

            for (int i = 0; i < count; ++i) {
                TacticalNpc* tnpc = memberCache_[static_cast<size_t>(i)];
                if (!tnpc || !tnpc->isAlive()) continue;

                TacticalNpcState st = tnpc->getState();

                // HoldSlot 이동 중에도 슬롯 변화가 작으면 현재 목표 유지 — 매 틱 재발행 방지
                if (st == TacticalNpcState::HoldSlot) {
                    float driftSq = Vec3::distanceSq(tnpc->getAssignedSlot(),
                                                     slots[static_cast<size_t>(i)]);
                    if (driftSq < 4.0f) continue;
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
    endActiveWedgeCharge(room);
    currentOrder_ = {};
    currentOrder_.type = SquadOrderType::Idle;
    orderDirty_ = false;
    wedgePrepared_ = false;
    wedgeMemberIds_.clear();
    wedgeMemberCache_.clear();
    wedgePrepareSlots_.clear();
    wedgeExitSlots_.clear();
    leaderlessBrawlEnabled_ = true;
    leaderlessBrawlTimer_ = LEADERLESS_CONFUSED_DURATION;
    leaderlessRetargetTimer_ = 0.f;

    TacticalCommand cmd;
    cmd.type = TacticalCommandType::Confused;
    for (TacticalNpc* tnpc : memberCache_) {
        if (tnpc && tnpc->isAlive()) tnpc->receiveCommand(cmd);
    }
}

} // namespace sim
