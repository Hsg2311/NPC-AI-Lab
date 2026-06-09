#include "TacticalNpc.hpp"
#include "Room.hpp"
#include "Player.hpp"
#include "Logger.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace sim {

// ─── 파일 범위 헬퍼 ──────────────────────────────────────────────────────────

static const char* tacticalStateStr(TacticalNpcState s) {
    switch (s) {
        case TacticalNpcState::Idle:          return "Idle";
        case TacticalNpcState::Chase:         return "Chase";
        case TacticalNpcState::AttackWindup:  return "AttackWindup";
        case TacticalNpcState::AttackRecover: return "AttackRecover";
        case TacticalNpcState::Flank:         return "Flank";
        case TacticalNpcState::ChargeThrough: return "ChargeThrough";
        case TacticalNpcState::Confused:      return "Confused";
        case TacticalNpcState::Dead:          return "Dead";
        case TacticalNpcState::HoldSlot:      return "HoldSlot";
        case TacticalNpcState::PressureWait:  return "PressureWait";
    }
    return "?";
}

static float hash01(uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return static_cast<float>(v & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

// ─── 생성자 ───────────────────────────────────────────────────────────────────

TacticalNpc::TacticalNpc(const std::string& name, const Vec3& pos,
                         const TacticalNpcConfig& cfg)
    : Actor(name, pos, cfg.maxHp)
    , spawnPos_(pos)
    , moveSpeed_(cfg.moveSpeed)
    , attackRange_(cfg.attackRange)
    , attackDamage_(cfg.attackDamage)
    , attackWindupTime_(cfg.attackWindupTime)
    , attackRecoverTime_(cfg.attackRecoverTime)
    , separationRadius_(cfg.separationRadius)
    , separationWeight_(cfg.separationWeight)
    , logPrefix_("TNPC:" + name)
{
    nearbyCache_.reserve(8);
}

// ─── 접근자 ───────────────────────────────────────────────────────────────────

float TacticalNpc::getWindupProgress() const {
    return (attackWindupTime_ > 0.f)
        ? std::min(1.f, windupTimer_ / attackWindupTime_)
        : 0.f;
}

float TacticalNpc::getRecoverProgress() const {
    return (attackRecoverTime_ > 0.f)
        ? std::min(1.f, recoverTimer_ / attackRecoverTime_)
        : 0.f;
}

TacticalNpcState TacticalNpc::getDisplayState() const {
    if (state_ == TacticalNpcState::PressureWait && pressureReentering_)
        return TacticalNpcState::Chase;
    return state_;
}

// ─── receiveCommand ───────────────────────────────────────────────────────────

TacticalNpcConfig TacticalNpc::getConfig() const {
    TacticalNpcConfig cfg;
    cfg.maxHp = maxHp_;
    cfg.moveSpeed = moveSpeed_;
    cfg.attackRange = attackRange_;
    cfg.attackDamage = attackDamage_;
    cfg.attackWindupTime = attackWindupTime_;
    cfg.attackRecoverTime = attackRecoverTime_;
    cfg.separationRadius = separationRadius_;
    cfg.separationWeight = separationWeight_;
    return cfg;
}

void TacticalNpc::receiveCommand(const TacticalCommand& cmd) {
    pendingCmd_ = cmd;
}

void TacticalNpc::reviveAt(const Vec3& pos) {
    Actor::reviveAt(pos);
    state_ = TacticalNpcState::Idle;
    pendingCmd_ = {};
    targetId_ = 0;
    assignedSlot_ = {};
    slotRefTargetPos_ = {};
    abandonDist_ = 15.f;
    chargeId_ = 0;
    chargeDir_ = {};
    chargeCenter_ = {};
    impactRadius_ = 3.f;
    impactDamage_ = 0.f;
    passDistance_ = 6.f;
    chargeComplete_ = false;
    guardNearestPlayer_ = false;
    useHoldFacing_ = false;
    holdFacing_ = {};
    confusedAnchor_ = pos;
    confusedTarget_ = pos;
    confusedRetargetTimer_ = 0.f;
    confusedWanderStep_ = 0;
    speedMult_ = 1.f;
    windupTimer_ = 0.f;
    recoverTimer_ = 0.f;
    pressureWaitTimer_ = 0.f;
    resetPressureWaitTarget();
    pressureWaitAngleOffset_ = 0.f;
    pressureWaitRadiusOffset_ = 0.f;
    pressureWaitScatterSeed_ = 0;
    pressureReentering_ = false;
    reservedAttackTargetId_ = 0;
    resetAttackReservationLease();
}

// ─── transitionTo ─────────────────────────────────────────────────────────────

void TacticalNpc::transitionTo(TacticalNpcState next, const char* reason) {
    if (state_ == next) return;
    Logger::get().logTransition(name_, tacticalStateStr(state_),
                                tacticalStateStr(next), reason);
    if (next == TacticalNpcState::AttackWindup)  windupTimer_  = 0.f;
    if (next == TacticalNpcState::AttackRecover) recoverTimer_ = 0.f;
    if (next == TacticalNpcState::PressureWait) {
        pressureWaitTimer_ = 0.f;
        resetPressureWaitTarget();
        ++pressureWaitScatterSeed_;
        refreshPressureWaitScatterOffsets();
        pressureReentering_ = false;
    }
    state_ = next;
}

// ─── consumePendingCommand ────────────────────────────────────────────────────

void TacticalNpc::consumePendingCommand(Room& room) {
    if (pendingCmd_.type == TacticalCommandType::None) return;

    switch (pendingCmd_.type) {
        case TacticalCommandType::EngageTarget:
            guardNearestPlayer_ = false;
            useHoldFacing_ = false;
            confusedRetargetTimer_ = 0.f;
            speedMult_ = 1.f;
            if (targetId_ == pendingCmd_.targetId &&
                (state_ == TacticalNpcState::Chase ||
                 state_ == TacticalNpcState::PressureWait ||
                 state_ == TacticalNpcState::AttackWindup ||
                 state_ == TacticalNpcState::AttackRecover)) {
                break;
            }
            if (targetId_ != pendingCmd_.targetId) {
                releaseAttackReservation(room);
            }
            pressureReentering_ = false;
            targetId_ = pendingCmd_.targetId;
            if (state_ == TacticalNpcState::PressureWait) {
                pressureWaitTimer_ = 0.f;
                resetPressureWaitTarget();
                ++pressureWaitScatterSeed_;
                refreshPressureWaitScatterOffsets();
                break;
            }
            if (state_ == TacticalNpcState::Idle ||
                state_ == TacticalNpcState::Flank ||
                state_ == TacticalNpcState::ChargeThrough ||
                state_ == TacticalNpcState::Confused ||
                state_ == TacticalNpcState::HoldSlot) {
                transitionTo(TacticalNpcState::PressureWait,
                             "명령: EngageTarget");
                break;
            }
            transitionTo(TacticalNpcState::Chase,
                         "명령: EngageTarget 타겟 교체");
            break;
        case TacticalCommandType::FlankTarget:
            releaseAttackReservation(room);
            guardNearestPlayer_ = false;
            useHoldFacing_ = false;
            confusedRetargetTimer_ = 0.f;
            pressureReentering_ = false;
            chargeComplete_ = false;
            targetId_          = pendingCmd_.targetId;
            assignedSlot_      = pendingCmd_.slotOffset;
            slotRefTargetPos_  = pendingCmd_.slotRefTargetPos;
            abandonDist_       = pendingCmd_.abandonDist;
            speedMult_         = pendingCmd_.speedMult;
            transitionTo(TacticalNpcState::Flank, "명령: FlankTarget");
            break;
        case TacticalCommandType::ChargeThrough:
            releaseAttackReservation(room);
            guardNearestPlayer_ = false;
            useHoldFacing_ = false;
            confusedRetargetTimer_ = 0.f;
            pressureReentering_ = false;
            targetId_          = pendingCmd_.targetId;
            assignedSlot_      = pendingCmd_.slotOffset;
            chargeId_          = pendingCmd_.chargeId;
            chargeDir_         = pendingCmd_.chargeDir.normalized();
            chargeCenter_      = pendingCmd_.chargeCenter;
            impactRadius_      = pendingCmd_.impactRadius;
            impactDamage_      = pendingCmd_.impactDamage;
            passDistance_      = pendingCmd_.passDistance;
            speedMult_         = pendingCmd_.speedMult;
            chargeComplete_    = false;
            transitionTo(TacticalNpcState::ChargeThrough, "명령: ChargeThrough");
            break;
        case TacticalCommandType::HoldSlot:
            releaseAttackReservation(room);
            guardNearestPlayer_ = false;
            useHoldFacing_ = pendingCmd_.useHoldFacing;
            holdFacing_ = pendingCmd_.holdFacing;
            confusedRetargetTimer_ = 0.f;
            pressureReentering_ = false;
            speedMult_ = pendingCmd_.speedMult;
            chargeComplete_ = false;
            targetId_     = pendingCmd_.targetId;
            assignedSlot_ = pendingCmd_.slotOffset;
            transitionTo(TacticalNpcState::HoldSlot, "명령: HoldSlot");
            break;
        case TacticalCommandType::GuardSlot:
            releaseAttackReservation(room);
            guardNearestPlayer_ = true;
            useHoldFacing_ = pendingCmd_.useHoldFacing;
            holdFacing_ = pendingCmd_.holdFacing;
            confusedRetargetTimer_ = 0.f;
            pressureReentering_ = false;
            speedMult_ = pendingCmd_.speedMult;
            targetId_     = pendingCmd_.targetId;
            assignedSlot_ = pendingCmd_.slotOffset;
            transitionTo(TacticalNpcState::HoldSlot, "명령: GuardSlot");
            break;
        case TacticalCommandType::Idle:
            releaseAttackReservation(room);
            guardNearestPlayer_ = false;
            useHoldFacing_ = false;
            chargeComplete_ = false;
            confusedRetargetTimer_ = 0.f;
            pressureReentering_ = false;
            speedMult_ = 1.f;
            targetId_ = 0;
            transitionTo(TacticalNpcState::Idle, "명령: Idle");
            break;
        case TacticalCommandType::Confused:
            releaseAttackReservation(room);
            guardNearestPlayer_ = false;
            useHoldFacing_ = false;
            chargeComplete_ = false;
            pressureReentering_ = false;
            speedMult_ = 1.f;
            targetId_ = 0;
            assignedSlot_ = {};
            confusedAnchor_ = position_;
            confusedTarget_ = position_;
            confusedRetargetTimer_ = 0.f;
            confusedWanderStep_ = 0;
            transitionTo(TacticalNpcState::Confused, "명령: Confused");
            break;
        default: break;
    }
    pendingCmd_.type = TacticalCommandType::None;
}

// ─── 메인 업데이트 분기 ──────────────────────────────────────────────────────

void TacticalNpc::update(float dt, Room& room) {
    if (!alive_) {
        updateDead(room);
        return;
    }

    // 명령 소비 (매 틱 최우선)
    if (pendingCmd_.type != TacticalCommandType::None) {
        consumePendingCommand(room);
    }

    switch (state_) {
        case TacticalNpcState::Idle:          updateIdle         (dt, room); break;
        case TacticalNpcState::Chase:         updateChase        (dt, room); break;
        case TacticalNpcState::AttackWindup:  updateAttackWindup (dt, room); break;
        case TacticalNpcState::AttackRecover: updateAttackRecover(dt, room); break;
        case TacticalNpcState::Flank:    updateFlank   (dt, room); break;
        case TacticalNpcState::ChargeThrough: updateChargeThrough(dt, room); break;
        case TacticalNpcState::Confused: updateConfused(dt, room); break;
        case TacticalNpcState::HoldSlot: updateHoldSlot(dt, room); break;
        case TacticalNpcState::PressureWait: updatePressureWait(dt, room); break;
        case TacticalNpcState::Dead:          /* 종료 상태 */                 break;
    }
}

// ─── Idle ─────────────────────────────────────────────────────────────────────
// 자율 감지 없음 — 명령이 도착할 때까지 대기

void TacticalNpc::updateIdle(float /*dt*/, Room& /*room*/) {
    // 의도적으로 비워둠: 명령 구동 전용
}

// ─── Chase ────────────────────────────────────────────────────────────────────

bool TacticalNpc::hasReservedAttackSlot() const {
    return reservedAttackTargetId_ != 0 &&
        reservedAttackTargetId_ == targetId_;
}

bool TacticalNpc::isEligibleForAttackReservation(const Actor& target) const {
    float dist = Vec3::distance(position_, target.getPosition());
    if (dist > TACTICAL_ATTACK_RESERVATION_MAX_DIST)
        return false;

    if (blockedAttackReservationTargetId_ != target.getId())
        return true;

    return dist <= blockedAttackReservationDist_ -
        TACTICAL_ATTACK_RESERVATION_PROGRESS_DIST;
}

void TacticalNpc::resetAttackReservationLease() {
    reservedAttackStaleTimer_ = 0.f;
    reservedAttackProgressDist_ = 0.f;
    blockedAttackReservationTargetId_ = 0;
    blockedAttackReservationDist_ = 0.f;
}

void TacticalNpc::updateReservedAttackStaleTimer(float dt, Room& room) {
    if (!hasReservedAttackSlot()) {
        reservedAttackStaleTimer_ = 0.f;
        reservedAttackProgressDist_ = 0.f;
        return;
    }

    Actor* target = resolveTarget(room);
    if (!target) {
        reservedAttackStaleTimer_ = 0.f;
        reservedAttackProgressDist_ = 0.f;
        return;
    }

    float dist = Vec3::distance(position_, target->getPosition());
    if (state_ == TacticalNpcState::AttackWindup ||
        state_ == TacticalNpcState::AttackRecover ||
        dist <= attackRange_) {
        reservedAttackStaleTimer_ = 0.f;
        reservedAttackProgressDist_ = dist;
        return;
    }

    if (reservedAttackProgressDist_ <= 0.f ||
        dist <= reservedAttackProgressDist_ -
            TACTICAL_ATTACK_RESERVATION_PROGRESS_DIST) {
        reservedAttackStaleTimer_ = 0.f;
        reservedAttackProgressDist_ = dist;
        return;
    }

    reservedAttackStaleTimer_ += dt;
}

bool TacticalNpc::canEnterAttackSlot(Room& room) {
    if (targetId_ == 0)
        return false;

    if (hasReservedAttackSlot()) {
        Actor* target = resolveTarget(room);
        if (!target) {
            releaseAttackReservation(room);
            return false;
        }

        float dist = Vec3::distance(position_, target->getPosition());
        if (dist > TACTICAL_ATTACK_RESERVATION_MAX_DIST) {
            releaseAttackReservation(room);
            return false;
        }
        if (reservedAttackStaleTimer_ >=
            TACTICAL_ATTACK_RESERVATION_STALE_TIME) {
            releaseStaleAttackReservation(room, dist);
            return false;
        }

        return true;
    }

    Actor* target = resolveTarget(room);
    if (!target || !isEligibleForAttackReservation(*target)) {
        pressureReentering_ = false;
        return false;
    }

    if (!room.tryReserveTacticalAttackSlot(targetId_, id_))
    {
        reservedAttackTargetId_ = 0;
        reservedAttackStaleTimer_ = 0.f;
        reservedAttackProgressDist_ = 0.f;
        pressureReentering_ = false;
        return false;
    }

    reservedAttackTargetId_ = targetId_;
    reservedAttackStaleTimer_ = 0.f;
    reservedAttackProgressDist_ =
        Vec3::distance(position_, target->getPosition());
    blockedAttackReservationTargetId_ = 0;
    blockedAttackReservationDist_ = 0.f;
    return true;
}

void TacticalNpc::releaseAttackReservation(Room& room) {
    if (reservedAttackTargetId_ != 0)
        room.releaseTacticalAttackSlot(reservedAttackTargetId_, id_);
    reservedAttackTargetId_ = 0;
    resetAttackReservationLease();
    pressureReentering_ = false;
}

void TacticalNpc::releaseStaleAttackReservation(Room& room,
                                                float currentDist) {
    uint32_t targetId = reservedAttackTargetId_;
    if (targetId != 0)
        room.releaseTacticalAttackSlot(targetId, id_);

    reservedAttackTargetId_ = 0;
    reservedAttackStaleTimer_ = 0.f;
    reservedAttackProgressDist_ = 0.f;
    blockedAttackReservationTargetId_ = targetId;
    blockedAttackReservationDist_ = currentDist;
    pressureReentering_ = false;
}

void TacticalNpc::resetPressureWaitTarget() {
    pressureWaitDesired_ = {};
    pressureWaitTargetAnchor_ = {};
    pressureWaitFacingAnchor_ = { 1.f, 0.f, 0.f };
    pressureWaitRetargetTimer_ = 0.f;
    pressureWaitDesiredValid_ = false;
}

void TacticalNpc::refreshPressureWaitScatterOffsets() {
    constexpr float PI = 3.14159265f;
    constexpr float DEG_TO_RAD = PI / 180.f;
    constexpr float FULL_CIRCLE = 360.0f;
    uint32_t base = id_ * 73856093u ^
        static_cast<uint32_t>(squadId_ + 4096) * 19349663u ^
        pressureWaitScatterSeed_ * 83492791u;
    float openArc = TACTICAL_PRESSURE_FRONT_GAP_DEGREES * 2.0f;
    float angleDeg = TACTICAL_PRESSURE_FRONT_GAP_DEGREES +
        hash01(base) * (FULL_CIRCLE - openArc);
    pressureWaitAngleOffset_ = angleDeg * DEG_TO_RAD;
    pressureWaitRadiusOffset_ = TACTICAL_PRESSURE_RADIUS_OFFSET_MIN +
        hash01(base ^ 0xa511e9b3u) * TACTICAL_PRESSURE_RADIUS_OFFSET_SPAN;
}

Vec3 TacticalNpc::computePressureWaitDesired(const Actor& target) const {
    Vec3 targetPos = target.getPosition();
    float ringRadius = std::max(attackRange_ + TACTICAL_PRESSURE_EXTRA_RADIUS,
                                separationRadius_ * TACTICAL_PRESSURE_SEPARATION_MULT) +
        pressureWaitRadiusOffset_;

    Vec3 forward = target.getFacing();
    forward.y = 0.f;
    if (forward.lengthSq() <= 0.01f) {
        forward = targetPos - position_;
        forward.y = 0.f;
    }
    if (forward.lengthSq() <= 0.01f)
        forward = Vec3{ 1.f, 0.f, 0.f };
    forward = forward.normalized();

    constexpr float PI = 3.14159265f;
    float forwardAngle = std::atan2f(forward.z, forward.x);
    float angle = forwardAngle + pressureWaitAngleOffset_;
    Vec3 radial{ std::cosf(angle), 0.f, std::sinf(angle) };
    return targetPos + radial * ringRadius;
}

void TacticalNpc::moveTowardPressureWait(float dt, Room& room, const Actor& target) {
    Vec3 targetPos = target.getPosition();
    Vec3 targetFacing = target.getFacing();
    targetFacing.y = 0.f;
    if (targetFacing.lengthSq() <= 0.01f)
        targetFacing = targetPos - position_;
    if (targetFacing.lengthSq() <= 0.01f)
        targetFacing = { 1.f, 0.f, 0.f };
    targetFacing = targetFacing.normalized();

    pressureWaitRetargetTimer_ += dt;
    bool targetMoved =
        Vec3::distanceSq(targetPos, pressureWaitTargetAnchor_) >
        TACTICAL_PRESSURE_TARGET_MOVE_REFRESH_DIST *
        TACTICAL_PRESSURE_TARGET_MOVE_REFRESH_DIST;
    bool facingChanged =
        pressureWaitFacingAnchor_.lengthSq() <= 0.01f ||
        targetFacing.dot(pressureWaitFacingAnchor_) <
        TACTICAL_PRESSURE_FACING_REFRESH_DOT;

    if (!pressureWaitDesiredValid_) {
        refreshPressureWaitScatterOffsets();
        pressureWaitDesired_ = computePressureWaitDesired(target);
        pressureWaitTargetAnchor_ = targetPos;
        pressureWaitFacingAnchor_ = targetFacing;
        pressureWaitRetargetTimer_ = 0.f;
        pressureWaitDesiredValid_ = true;
    } else if (pressureWaitRetargetTimer_ >= TACTICAL_PRESSURE_RETARGET_INTERVAL &&
               (targetMoved || facingChanged)) {
        Vec3 targetDelta = targetPos - pressureWaitTargetAnchor_;
        targetDelta.y = 0.f;
        pressureWaitDesired_ += targetDelta;

        Vec3 relative = pressureWaitDesired_ - targetPos;
        relative.y = 0.f;
        float relativeLen = relative.length();
        constexpr float PI = 3.14159265f;
        constexpr float DEG_TO_RAD = PI / 180.f;
        float frontGap = TACTICAL_PRESSURE_FRONT_GAP_DEGREES * DEG_TO_RAD;
        if (relativeLen > 0.01f) {
            Vec3 dir = relative / relativeLen;
            float signedAngle = std::atan2f(
                targetFacing.x * dir.z - targetFacing.z * dir.x,
                targetFacing.dot(dir));
            if (std::fabs(signedAngle) < frontGap) {
                float side = (std::fabs(signedAngle) > 0.001f)
                    ? ((signedAngle >= 0.f) ? 1.f : -1.f)
                    : ((std::sinf(pressureWaitAngleOffset_) >= 0.f) ? 1.f : -1.f);
                float forwardAngle = std::atan2f(targetFacing.z, targetFacing.x);
                float correctedAngle = forwardAngle + side * frontGap;
                Vec3 radial{ std::cosf(correctedAngle), 0.f, std::sinf(correctedAngle) };
                pressureWaitDesired_ = targetPos + radial * relativeLen;
            }
        }

        pressureWaitTargetAnchor_ = targetPos;
        pressureWaitFacingAnchor_ = targetFacing;
        pressureWaitRetargetTimer_ = 0.f;
    }

    Vec3 toDesired = pressureWaitDesired_ - position_;
    toDesired.y = 0.f;
    float distToDesired = toDesired.length();
    Vec3 moveDir = (distToDesired > 0.1f)
        ? toDesired.normalized()
        : Vec3{ 0.f, 0.f, 0.f };

    nearbyCache_.clear();
    float pressureSeparationRadius =
        std::max(separationRadius_, separationRadius_ * TACTICAL_PRESSURE_SEPARATION_RADIUS_MULT);
    room.findNearbyNpcPositions(position_, pressureSeparationRadius, id_, nearbyCache_);
    Vec3 sep = calcSeparationForce(pressureSeparationRadius, nearbyCache_);
    if (sep.lengthSq() > 0.01f && moveDir.lengthSq() > 0.01f) {
        Vec3 sepPerp = sep - moveDir * sep.dot(moveDir);
        float sepScale = std::min(1.f, sepPerp.length());
        float nearTargetScale = std::max(TACTICAL_PRESSURE_NEAR_SEPARATION_MIN_SCALE,
            std::min(1.f, distToDesired / TACTICAL_PRESSURE_SLOW_RADIUS));
        moveDir = (moveDir + sepPerp *
            (separationWeight_ * TACTICAL_PRESSURE_SEPARATION_WEIGHT_MULT *
             sepScale * nearTargetScale)).normalized();
    }

    Vec3 lookDir = targetPos - position_;
    facing_ = (lookDir.length() > 0.1f) ? lookDir.normalized() : moveDir;
    Vec3 overlapDrift{ 0.f, 0.f, 0.f };
    nearbyCache_.clear();
    float overlapRadius = std::max(TACTICAL_PRESSURE_OVERLAP_RADIUS_MIN,
        separationRadius_ * TACTICAL_PRESSURE_OVERLAP_RADIUS_MULT);
    room.findNearbyNpcPositions(position_, overlapRadius, id_, nearbyCache_);
    Vec3 overlapSep = calcSeparationForce(overlapRadius, nearbyCache_);
    if (overlapSep.lengthSq() > 0.0001f) {
        Vec3 radial = position_ - targetPos;
        radial.y = 0.f;
        if (radial.lengthSq() <= 0.01f)
            radial = pressureWaitDesired_ - targetPos;
        radial.y = 0.f;
        if (radial.lengthSq() > 0.01f) {
            radial = radial.normalized();
            float radialDot = overlapSep.dot(radial);
            Vec3 outward = radial * std::max(0.f, radialDot);
            Vec3 tangent = overlapSep - radial * radialDot;
            overlapDrift = outward + tangent * 0.7f;
        } else {
            overlapDrift = overlapSep;
        }
    }

    if (distToDesired <= TACTICAL_PRESSURE_STOP_RADIUS ||
        moveDir.lengthSq() <= 0.01f) {
        if (overlapDrift.lengthSq() > 0.0001f)
            position_ += overlapDrift *
                (moveSpeed_ * TACTICAL_PRESSURE_OVERLAP_DRIFT_MULT * dt);
        return;
    }

    float slowT = std::min(1.f,
        (distToDesired - TACTICAL_PRESSURE_STOP_RADIUS) /
        (TACTICAL_PRESSURE_SLOW_RADIUS - TACTICAL_PRESSURE_STOP_RADIUS));
    position_ += moveDir *
        (moveSpeed_ * TACTICAL_PRESSURE_SPEED_MULT * slowT * dt);
    if (overlapDrift.lengthSq() > 0.0001f)
        position_ += overlapDrift *
            (moveSpeed_ * TACTICAL_PRESSURE_OVERLAP_DRIFT_MULT * dt);
}

void TacticalNpc::updateChase(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (!target) {
        releaseAttackReservation(room);
        targetId_ = 0;
        transitionTo(TacticalNpcState::Idle, "타겟 소실");
        return;
    }

    updateReservedAttackStaleTimer(dt, room);
    if (!canEnterAttackSlot(room)) {
        transitionTo(TacticalNpcState::PressureWait, "공격 슬롯 대기");
        return;
    }

    if (Vec3::distanceSq(position_, target->getPosition()) <= attackRange_ * attackRange_) {
        transitionTo(TacticalNpcState::AttackWindup, "사정거리 진입");
        return;
    }

    Vec3 chaseDir = (target->getPosition() - position_).normalized();
    nearbyCache_.clear();
    room.findNearbyNpcPositions(position_, separationRadius_, id_, nearbyCache_);
    Vec3 sep = calcSeparationForce(separationRadius_, nearbyCache_);

    // 추격 방향과 수직인 성분만 사용 — 역방향 이동 없이 옆으로만 밀어냄
    Vec3 sepPerp = sep - chaseDir * sep.dot(chaseDir);
    Vec3 moveDir = (chaseDir + sepPerp * separationWeight_).normalized();
    facing_   = moveDir;
    position_ += moveDir * (moveSpeed_ * dt);
}

// ─── AttackWindup ─────────────────────────────────────────────────────────────

void TacticalNpc::updateAttackWindup(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (!target) {
        releaseAttackReservation(room);
        targetId_ = 0;
        transitionTo(TacticalNpcState::Idle, "타겟 소실 (windup 중)");
        return;
    }

    windupTimer_ += dt;
    if (windupTimer_ >= attackWindupTime_) {
        float dist = Vec3::distance(position_, target->getPosition());
        if (dist <= attackRange_) {
            target->takeDamage(attackDamage_);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "hit %s for %.0f  (hp=%.1f)",
                target->getName().c_str(), attackDamage_, target->getHp());
            Logger::get().log(logPrefix_, buf);

            if (!target->isAlive()) {
                releaseAttackReservation(room);
                targetId_ = 0;
                transitionTo(TacticalNpcState::Idle, "타겟 처치");
                return;
            }
        } else {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "missed %s (dist=%.1f > range=%.1f)",
                target->getName().c_str(), dist, attackRange_);
            Logger::get().log(logPrefix_, buf);
        }
        transitionTo(TacticalNpcState::AttackRecover, "windup 완료");
    }
}

// ─── AttackRecover ────────────────────────────────────────────────────────────

void TacticalNpc::updateAttackRecover(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (!target) {
        releaseAttackReservation(room);
        targetId_ = 0;
        transitionTo(TacticalNpcState::Idle, "타겟 소실 (recover 중)");
        return;
    }

    constexpr float BODY_RADIUS = 0.8f;
    nearbyCache_.clear();
    room.findNearbyNpcPositions(position_, BODY_RADIUS * 2.f, id_, nearbyCache_);
    Vec3 push = calcSeparationForce(BODY_RADIUS * 2.f, nearbyCache_);
    if (push.lengthSq() > 0.0001f)
        position_ += push * (moveSpeed_ * TACTICAL_RECOVER_SEPARATION_DRIFT_MULT * dt);

    Vec3 lookDir = target->getPosition() - position_;
    if (lookDir.lengthSq() > 0.01f)
        facing_ = lookDir.normalized();

    recoverTimer_ += dt;
    if (recoverTimer_ >= attackRecoverTime_) {
        if (Vec3::distanceSq(position_, target->getPosition()) <= attackRange_ * attackRange_ &&
            canEnterAttackSlot(room)) {
            transitionTo(TacticalNpcState::AttackWindup, "recover 완료, 사정거리 내");
        } else {
            transitionTo(TacticalNpcState::PressureWait, "recover 완료");
        }
    }
}

void TacticalNpc::updatePressureWait(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (!target) {
        releaseAttackReservation(room);
        targetId_ = 0;
        transitionTo(TacticalNpcState::Idle, "타겟 소실 (PressureWait 중)");
        return;
    }

    pressureWaitTimer_ += dt;
    updateReservedAttackStaleTimer(dt, room);
    float reenterDelay = TACTICAL_PRESSURE_REENTER_MIN_TIME +
        static_cast<float>(id_ % 4u) * TACTICAL_PRESSURE_REENTER_STAGGER;
    bool canReenter = pressureWaitTimer_ >= reenterDelay && canEnterAttackSlot(room);

    if (canReenter) {
        pressureReentering_ = true;
        if (Vec3::distanceSq(position_, target->getPosition()) <= attackRange_ * attackRange_) {
            transitionTo(TacticalNpcState::AttackWindup, "공격 슬롯 확보");
            return;
        }

        Vec3 chaseDir = target->getPosition() - position_;
        chaseDir.y = 0.f;
        if (chaseDir.lengthSq() > 0.01f) {
            chaseDir = chaseDir.normalized();

            nearbyCache_.clear();
            room.findNearbyNpcPositions(position_, separationRadius_, id_, nearbyCache_);
            Vec3 sep = calcSeparationForce(separationRadius_, nearbyCache_);
            Vec3 sepPerp = sep - chaseDir * sep.dot(chaseDir);
            Vec3 moveDir = (chaseDir + sepPerp * separationWeight_).normalized();
            facing_ = moveDir;
            position_ += moveDir *
                (moveSpeed_ * TACTICAL_PRESSURE_REENTER_SPEED_MULT * dt);
        }
        return;
    }

    pressureReentering_ = false;
    moveTowardPressureWait(dt, room, *target);
}

// ─── Flank ────────────────────────────────────────────────────────────────────

void TacticalNpc::updateFlank(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (!target) {
        releaseAttackReservation(room);
        targetId_ = 0;
        transitionTo(TacticalNpcState::Idle, "타겟 소실 (Flank 중)");
        return;
    }

    // 타겟이 슬롯 계산 시점 위치에서 너무 멀어지면 슬롯 포기 → Chase
    float drift = Vec3::distance(target->getPosition(), slotRefTargetPos_);
    if (drift > abandonDist_) {
        transitionTo(TacticalNpcState::Chase, "타겟 이탈, 슬롯 포기");
        return;
    }

    // assignedSlot_은 이미 월드 좌표
    float distToSlot = Vec3::distance(position_, assignedSlot_);
    if (distToSlot < 0.5f) {
        // 슬롯 도착 — 공격 여부 판정
        float distToTarget = Vec3::distance(position_, target->getPosition());
        updateReservedAttackStaleTimer(dt, room);
        if (distToTarget <= attackRange_ && canEnterAttackSlot(room))
            transitionTo(TacticalNpcState::AttackWindup, "Flank 슬롯 도착, 사정거리 내");
        else
            transitionTo(canEnterAttackSlot(room)
                ? TacticalNpcState::Chase
                : TacticalNpcState::PressureWait,
                "Flank 슬롯 도착");
        return;
    }

    Vec3 slotDir = (assignedSlot_ - position_).normalized();
    nearbyCache_.clear();
    room.findNearbyNpcPositions(position_, separationRadius_, id_, nearbyCache_);
    Vec3 sep = calcSeparationForce(separationRadius_, nearbyCache_);

    // 슬롯 이동 방향과 수직인 성분만 사용
    Vec3 sepPerp = sep - slotDir * sep.dot(slotDir);
    Vec3 moveDir = (slotDir + sepPerp * separationWeight_).normalized();
    facing_   = moveDir;
    position_ += moveDir * (moveSpeed_ * TACTICAL_SPEED_MULT * speedMult_ * dt);
}

// ─── ChargeThrough ────────────────────────────────────────────────────────────
// WedgeCharge 전용 관통 돌진. 충돌 피해는 멤버별/플레이어별 1회만 적용한다.

void TacticalNpc::updateChargeThrough(float dt, Room& room) {
    Actor* target = resolveTarget(room);

    if (chargeComplete_) {
        if (target) {
            Vec3 dir = target->getPosition() - position_;
            if (dir.length() > 0.1f) facing_ = dir.normalized();
        }
        return;
    }

    if (chargeDir_.lengthSq() < 0.01f) {
        Vec3 dir = assignedSlot_ - position_;
        chargeDir_ = (dir.length() > 0.01f) ? dir.normalized() : Vec3{ 1.f, 0.f, 0.f };
    }

    float impactRadiusSq = impactRadius_ * impactRadius_;
    for (Player* p : room.getLivingPlayers()) {
        if (!p || Vec3::distanceSq(position_, p->getPosition()) > impactRadiusSq)
            continue;

        if (room.tryApplyWedgeChargeHit(chargeId_, *p, impactDamage_)) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "charge %u hit %s for %.0f  (hp=%.1f)",
                chargeId_, p->getName().c_str(), impactDamage_, p->getHp());
            Logger::get().log(logPrefix_, buf);
        }
    }

    float distToExit = Vec3::distance(position_, assignedSlot_);
    if (distToExit < 0.75f) {
        chargeComplete_ = true;
        facing_ = chargeDir_;
        return;
    }

    Vec3 toExit = assignedSlot_ - position_;
    Vec3 moveDir = (toExit.length() > 0.01f) ? toExit.normalized() : chargeDir_;
    facing_ = moveDir;
    position_ += moveDir * (moveSpeed_ * TACTICAL_SPEED_MULT * speedMult_ * dt);
}

// ─── HoldSlot ─────────────────────────────────────────────────────────────────
// 슬롯까지 이동 후 대기. 타겟이 범위 내여도 공격하지 않는다 (경계 상태).

void TacticalNpc::updateConfused(float dt, Room& room) {
    targetId_ = 0;
    confusedRetargetTimer_ -= dt;

    float distToTarget = Vec3::distance(position_, confusedTarget_);
    if (confusedRetargetTimer_ <= 0.f || distToTarget < 0.75f) {
        ++confusedWanderStep_;
        float seedA = static_cast<float>(
            (id_ * 37u + static_cast<uint32_t>(confusedWanderStep_) * 101u) %
            997u);
        float seedB = static_cast<float>(
            (id_ * 53u + static_cast<uint32_t>(confusedWanderStep_) * 193u) %
            991u);
        float r1 = std::sinf(seedA * 12.9898f) * 43758.5453f;
        float r2 = std::sinf(seedB * 78.233f) * 24634.6345f;
        r1 -= std::floorf(r1);
        r2 -= std::floorf(r2);

        constexpr float TWO_PI = 2.f * 3.14159265f;
        float angle = r1 * TWO_PI;
        float radius = CONFUSED_WANDER_RADIUS * (0.25f + 0.75f * r2);
        confusedTarget_ = confusedAnchor_ +
            Vec3{ std::cosf(angle) * radius, 0.f, std::sinf(angle) * radius };
        confusedRetargetTimer_ =
            CONFUSED_RETARGET_MIN + CONFUSED_RETARGET_SPAN * r1;
        distToTarget = Vec3::distance(position_, confusedTarget_);
    }

    Vec3 toTarget = confusedTarget_ - position_;
    Vec3 moveDir = (toTarget.length() > 0.01f)
        ? toTarget.normalized()
        : Vec3{ 1.f, 0.f, 0.f };

    float confusedSeparationRadius = std::max(separationRadius_, CONFUSED_SEPARATION_RADIUS);
    nearbyCache_.clear();
    room.findNearbyNpcPositions(position_, confusedSeparationRadius, id_, nearbyCache_);
    Vec3 sep = calcSeparationForce(confusedSeparationRadius, nearbyCache_);
    if (sep.lengthSq() > 0.01f)
        moveDir = (moveDir + sep.normalized() * CONFUSED_SEPARATION_WEIGHT).normalized();

    Vec3 fromAnchor = position_ - confusedAnchor_;
    if (fromAnchor.length() > CONFUSED_WANDER_RADIUS * 1.25f)
        moveDir = (confusedAnchor_ - position_).normalized();

    facing_ = moveDir;
    position_ += moveDir * (moveSpeed_ * CONFUSED_SPEED_MULT * dt);
}

void TacticalNpc::updateHoldSlot(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (guardNearestPlayer_) {
        target = nullptr;
        float bestDistSq = -1.f;
        for (Player* p : room.getLivingPlayers()) {
            float dSq = Vec3::distanceSq(position_, p->getPosition());
            if (bestDistSq < 0.f || dSq < bestDistSq) {
                bestDistSq = dSq;
                target = p;
            }
        }
    }

    if (!target) {
        if (useHoldFacing_) {
            float distToSlot = Vec3::distance(position_, assignedSlot_);
            if (distToSlot < separationRadius_ * 0.25f) {
                if (holdFacing_.lengthSq() > 0.01f)
                    facing_ = holdFacing_.normalized();
                return;
            }

            Vec3 slotDir = (assignedSlot_ - position_).normalized();
            facing_ = slotDir;
            position_ += slotDir * (moveSpeed_ * TACTICAL_SPEED_MULT * speedMult_ * dt);
            return;
        }

        targetId_ = 0;
        transitionTo(TacticalNpcState::Idle, "타겟 소실 (HoldSlot 중)");
        return;
    }

    float distToSlot = Vec3::distance(position_, assignedSlot_);
    if (distToSlot < separationRadius_ * 0.25f) {
        if (useHoldFacing_ && holdFacing_.lengthSq() > 0.01f) {
            facing_ = holdFacing_.normalized();
            return;
        }

        // 슬롯 도착 — 타겟 방향으로 facing 유지
        Vec3 dir = target->getPosition() - position_;
        if (dir.length() > 0.1f) facing_ = dir.normalized();
        return;
    }

    // 대형 이동 중 분리력 없음 — 슬롯 직선 접근, 일시적 겹침 허용
    Vec3 slotDir = (assignedSlot_ - position_).normalized();
    facing_   = slotDir;
    position_ += slotDir * (moveSpeed_ * TACTICAL_SPEED_MULT * speedMult_ * dt);
}

// ─── Dead ─────────────────────────────────────────────────────────────────────

void TacticalNpc::updateDead(Room& room) {
    releaseAttackReservation(room);
    targetId_ = 0;
    if (state_ != TacticalNpcState::Dead)
        transitionTo(TacticalNpcState::Dead, "hp 0");
}

// ─── isAtSlot ─────────────────────────────────────────────────────────────────
// Flank: 도착 시 상태 전환되므로 항상 이동 중.
// HoldSlot: 도착 후에도 상태 유지 → 거리로 판단.

bool TacticalNpc::isAtSlot() const {
    switch (state_) {
        case TacticalNpcState::HoldSlot:
            return Vec3::distance(position_, assignedSlot_) < separationRadius_ * 0.25f;
        case TacticalNpcState::ChargeThrough:
            return chargeComplete_;
        case TacticalNpcState::AttackWindup:
        case TacticalNpcState::AttackRecover:
        case TacticalNpcState::PressureWait:
            return true;   // 전투/압박 대기 중: 대형 완성으로 간주
        default:
            return false;  // Idle, Chase, Flank, Dead 등: 슬롯 미도달
    }
}

// ─── resolveTarget ────────────────────────────────────────────────────────────

Actor* TacticalNpc::resolveTarget(Room& room) const {
    if (targetId_ == 0) return nullptr;
    Actor* a = room.findActorById(targetId_);
    if (!a || !a->isAlive()) return nullptr;
    return a;
}

// ─── dump ─────────────────────────────────────────────────────────────────────

std::string TacticalNpc::dump() const {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "%-12s hp=%5.1f/%-5.1f pos=%s state=%s%s",
        name_.c_str(), hp_, maxHp_,
        position_.toString().c_str(),
        tacticalStateStr(state_),
        alive_ ? "" : " [DEAD]");
    return buf;
}

} // namespace sim
