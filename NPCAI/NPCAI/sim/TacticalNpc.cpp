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
        case TacticalNpcState::Dead:          return "Dead";
        case TacticalNpcState::HoldSlot:      return "HoldSlot";
    }
    return "?";
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

// ─── receiveCommand ───────────────────────────────────────────────────────────

void TacticalNpc::receiveCommand(const TacticalCommand& cmd) {
    pendingCmd_ = cmd;
}

// ─── transitionTo ─────────────────────────────────────────────────────────────

void TacticalNpc::transitionTo(TacticalNpcState next, const char* reason) {
    if (state_ == next) return;
    Logger::get().logTransition(name_, tacticalStateStr(state_),
                                tacticalStateStr(next), reason);
    if (next == TacticalNpcState::AttackWindup)  windupTimer_  = 0.f;
    if (next == TacticalNpcState::AttackRecover) recoverTimer_ = 0.f;
    state_ = next;
}

// ─── consumePendingCommand ────────────────────────────────────────────────────

void TacticalNpc::consumePendingCommand() {
    if (pendingCmd_.type == TacticalCommandType::None) return;

    switch (pendingCmd_.type) {
        case TacticalCommandType::EngageTarget:
            targetId_ = pendingCmd_.targetId;
            transitionTo(TacticalNpcState::Chase, "명령: EngageTarget");
            break;
        case TacticalCommandType::FlankTarget:
            targetId_          = pendingCmd_.targetId;
            assignedSlot_      = pendingCmd_.slotOffset;
            slotRefTargetPos_  = pendingCmd_.slotRefTargetPos;
            abandonDist_       = pendingCmd_.abandonDist;
            speedMult_         = pendingCmd_.speedMult;
            transitionTo(TacticalNpcState::Flank, "명령: FlankTarget");
            break;
        case TacticalCommandType::HoldSlot:
            targetId_     = pendingCmd_.targetId;
            assignedSlot_ = pendingCmd_.slotOffset;
            transitionTo(TacticalNpcState::HoldSlot, "명령: HoldSlot");
            break;
        case TacticalCommandType::Idle:
            targetId_ = 0;
            transitionTo(TacticalNpcState::Idle, "명령: Idle");
            break;
        case TacticalCommandType::Confused:
            targetId_ = 0;
            transitionTo(TacticalNpcState::Idle, "명령: Confused");
            break;
        default: break;
    }
    pendingCmd_.type = TacticalCommandType::None;
}

// ─── 메인 업데이트 분기 ──────────────────────────────────────────────────────

void TacticalNpc::update(float dt, Room& room) {
    if (!alive_) {
        updateDead();
        return;
    }

    // 명령 소비 (매 틱 최우선)
    if (pendingCmd_.type != TacticalCommandType::None) {
        consumePendingCommand();
    }

    switch (state_) {
        case TacticalNpcState::Idle:          updateIdle         (dt, room); break;
        case TacticalNpcState::Chase:         updateChase        (dt, room); break;
        case TacticalNpcState::AttackWindup:  updateAttackWindup (dt, room); break;
        case TacticalNpcState::AttackRecover: updateAttackRecover(dt, room); break;
        case TacticalNpcState::Flank:    updateFlank   (dt, room); break;
        case TacticalNpcState::HoldSlot: updateHoldSlot(dt, room); break;
        case TacticalNpcState::Dead:          /* 종료 상태 */                 break;
    }
}

// ─── Idle ─────────────────────────────────────────────────────────────────────
// 자율 감지 없음 — 명령이 도착할 때까지 대기

void TacticalNpc::updateIdle(float /*dt*/, Room& /*room*/) {
    // 의도적으로 비워둠: 명령 구동 전용
}

// ─── Chase ────────────────────────────────────────────────────────────────────

void TacticalNpc::updateChase(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (!target) {
        targetId_ = 0;
        transitionTo(TacticalNpcState::Idle, "타겟 소실");
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
        targetId_ = 0;
        transitionTo(TacticalNpcState::Idle, "타겟 소실 (recover 중)");
        return;
    }

    constexpr float BODY_RADIUS = 0.8f;
    nearbyCache_.clear();
    room.findNearbyNpcPositions(position_, BODY_RADIUS * 2.f, id_, nearbyCache_);
    Vec3 push = calcSeparationForce(BODY_RADIUS * 2.f, nearbyCache_);
    if (push.length() > 0.1f)
        position_ += push.normalized() * (moveSpeed_ * 0.15f * dt);

    recoverTimer_ += dt;
    if (recoverTimer_ >= attackRecoverTime_) {
        if (Vec3::distanceSq(position_, target->getPosition()) <= attackRange_ * attackRange_)
            transitionTo(TacticalNpcState::AttackWindup, "recover 완료, 사정거리 내");
        else
            transitionTo(TacticalNpcState::Chase, "recover 완료, 사정거리 이탈");
    }
}

// ─── Flank ────────────────────────────────────────────────────────────────────

void TacticalNpc::updateFlank(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (!target) {
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
        if (distToTarget <= attackRange_)
            transitionTo(TacticalNpcState::AttackWindup, "Flank 슬롯 도착, 사정거리 내");
        else
            transitionTo(TacticalNpcState::Chase, "Flank 슬롯 도착, 사정거리 이탈");
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

// ─── HoldSlot ─────────────────────────────────────────────────────────────────
// 슬롯까지 이동 후 대기. 타겟이 범위 내여도 공격하지 않는다 (경계 상태).

void TacticalNpc::updateHoldSlot(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (!target) {
        targetId_ = 0;
        transitionTo(TacticalNpcState::Idle, "타겟 소실 (HoldSlot 중)");
        return;
    }

    float distToSlot = Vec3::distance(position_, assignedSlot_);
    if (distToSlot < separationRadius_ * 0.25f) {
        // 슬롯 도착 — 타겟 방향으로 facing 유지
        Vec3 dir = target->getPosition() - position_;
        if (dir.length() > 0.1f) facing_ = dir.normalized();
        return;
    }

    // 대형 이동 중 분리력 없음 — 슬롯 직선 접근, 일시적 겹침 허용
    Vec3 slotDir = (assignedSlot_ - position_).normalized();
    facing_   = slotDir;
    position_ += slotDir * (moveSpeed_ * TACTICAL_SPEED_MULT * dt);
}

// ─── Dead ─────────────────────────────────────────────────────────────────────

void TacticalNpc::updateDead() {
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
        case TacticalNpcState::AttackWindup:
        case TacticalNpcState::AttackRecover:
            return true;   // 전투 중: 대형 완성으로 간주
        default:
            return false;  // Idle, Chase, Flank, Return, Dead 등: 슬롯 미도달
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
