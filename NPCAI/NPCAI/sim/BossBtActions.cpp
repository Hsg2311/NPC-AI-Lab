#include "BossBtActions.hpp"
#include "FinalBoss.hpp"
#include "Player.hpp"
#include "Room.hpp"
#include "Logger.hpp"
#include <cstdio>

namespace sim {

// ─── BossChaseAction ─────────────────────────────────────────────────────────

BtStatus BossChaseAction::tick(float dt, FinalBoss& boss, Room& room) {
    Player* target = boss.resolveTarget(room);
    if (!target)
        return BtStatus::Failure;

    boss.reportActiveLeaf(name());

    const FinalBossConfig& cfg = boss.getConfig();
    float dist = Vec3::distance(boss.getPosition(), target->getPosition());

    // 사정거리 바로 안쪽까지 접근 후에는 방향만 유지 (패턴 쿨다운 대기)
    if (dist > cfg.attackRange * 0.9f)
        boss.moveToward(target->getPosition(), /*speedMult=*/1.f, dt);
    else
        boss.faceToward(target->getPosition());

    return BtStatus::Running;
}

// ─── BossIdleAction ──────────────────────────────────────────────────────────

BtStatus BossIdleAction::tick(float /*dt*/, FinalBoss& boss, Room& /*room*/) {
    boss.reportActiveLeaf(name());
    return BtStatus::Running;
}

// ─── BossMeleeComboAction ────────────────────────────────────────────────────

BtStatus BossMeleeComboAction::tick(float dt, FinalBoss& boss, Room& room) {
    Player* target = boss.resolveTarget(room);
    if (!target) {
        reset();
        return BtStatus::Failure;
    }

    boss.reportActiveLeaf(name());
    const FinalBossConfig& cfg = boss.getConfig();

    switch (phase_) {
    case Phase::Windup: {
        if (!windupStarted_) {
            // 타격 지점은 Windup 시작 시 고정 — 플레이어가 회피할 수 있게 한다
            boss.faceToward(target->getPosition());
            hitCenter_ = boss.getPosition()
                       + boss.getFacing() * (cfg.attackRange * 0.8f);
            windupStarted_ = true;
        }

        timer_ += dt;
        float frac = timer_ / cfg.comboWindupTime;

        // telegraphs는 틱마다 clear되므로 매 틱 재등록
        DebugTelegraphEntry tg;
        tg.x        = hitCenter_.x;
        tg.z        = hitCenter_.z;
        tg.radius   = cfg.comboHitRadius;
        tg.progress = frac;
        tg.kind     = 0;
        room.addDebugTelegraph(tg);

        boss.setActionProgress(
            (static_cast<float>(hitIndex_) + frac * 0.7f)
            / static_cast<float>(cfg.comboHitCount));

        if (timer_ >= cfg.comboWindupTime) {
            bool  finisher = (hitIndex_ == cfg.comboHitCount - 1);
            float damage   = cfg.comboDamage * (finisher ? cfg.comboFinisherMult : 1.f);
            int   hits     = room.applyDamageToPlayersInRange(
                                 hitCenter_, cfg.comboHitRadius, damage);

            char buf[96];
            std::snprintf(buf, sizeof(buf), "콤보 %d타%s: %d명 피격 (피해 %.0f)",
                hitIndex_ + 1, finisher ? " (마무리)" : "", hits, damage);
            Logger::get().log("BOSS:" + boss.getName(), buf);

            phase_ = Phase::Linger;
            timer_ = 0.f;
        }
        return BtStatus::Running;
    }

    case Phase::Linger:
        timer_ += dt;
        boss.setActionProgress(
            (static_cast<float>(hitIndex_) + 0.7f + (timer_ / cfg.comboLingerTime) * 0.3f)
            / static_cast<float>(cfg.comboHitCount));

        if (timer_ >= cfg.comboLingerTime) {
            ++hitIndex_;
            if (hitIndex_ >= cfg.comboHitCount) {
                reset();
                return BtStatus::Success;
            }
            phase_         = Phase::Windup;
            timer_         = 0.f;
            windupStarted_ = false;
        }
        return BtStatus::Running;
    }

    return BtStatus::Failure;   // 도달 불가
}

void BossMeleeComboAction::reset() {
    phase_         = Phase::Windup;
    hitIndex_      = 0;
    timer_         = 0.f;
    windupStarted_ = false;
}

// ─── BossChargeAction ────────────────────────────────────────────────────────

BtStatus BossChargeAction::tick(float dt, FinalBoss& boss, Room& room) {
    const FinalBossConfig& cfg = boss.getConfig();

    switch (phase_) {
    case Phase::Aim: {
        // Aim 동안에만 타겟이 필요하다 (이때는 chargeId_ == 0이라 정리 불필요)
        Player* target = boss.resolveTarget(room);
        if (!target) {
            reset();
            return BtStatus::Failure;
        }

        boss.reportActiveLeaf(name());
        boss.faceToward(target->getPosition());

        // 조준 동안 타겟을 추적하고 Dash 시작 시점의 목적지로 고정한다
        Vec3  toTarget = target->getPosition() - boss.getPosition();
        toTarget.y = 0.f;
        float dist = toTarget.length();
        Vec3  dir  = (dist > 1e-4f) ? toTarget / dist : boss.getFacing();
        chargeDest_ = boss.getPosition() + dir * (dist + cfg.chargeOvershoot);

        timer_ += dt;
        float frac = timer_ / cfg.chargeAimTime;
        addPathTelegraph(boss, room, frac);
        boss.setActionProgress(frac);

        if (timer_ >= cfg.chargeAimTime) {
            chargeId_ = room.beginWedgeCharge();
            phase_    = Phase::Dash;
            timer_    = 0.f;
            Logger::get().log("BOSS:" + boss.getName(), "돌진 시작 -> "
                + chargeDest_.toString());
        }
        return BtStatus::Running;
    }

    case Phase::Dash: {
        boss.reportActiveLeaf(name());
        boss.moveToward(chargeDest_, cfg.chargeSpeedMult, dt);

        float hitRadiusSq = cfg.chargeHitRadius * cfg.chargeHitRadius;
        for (Player* p : room.getLivingPlayers()) {
            if (Vec3::distanceSq(boss.getPosition(), p->getPosition()) > hitRadiusSq)
                continue;
            if (room.tryApplyWedgeChargeHit(chargeId_, *p, cfg.chargeDamage)) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "돌진 피격: %s (피해 %.0f)",
                    p->getName().c_str(), cfg.chargeDamage);
                Logger::get().log("BOSS:" + boss.getName(), buf);
            }
        }

        if (Vec3::distance(boss.getPosition(), chargeDest_) < 0.5f) {
            room.endWedgeCharge(chargeId_);
            chargeId_ = 0;
            phase_    = Phase::Stop;
            timer_    = 0.f;
        }
        return BtStatus::Running;
    }

    case Phase::Stop:
        boss.reportActiveLeaf(name());
        timer_ += dt;
        if (timer_ >= cfg.chargeStopTime) {
            reset();
            return BtStatus::Success;
        }
        return BtStatus::Running;
    }

    return BtStatus::Failure;   // 도달 불가
}

void BossChargeAction::reset() {
    phase_      = Phase::Aim;
    timer_      = 0.f;
    chargeDest_ = {};
    chargeId_   = 0;
}

// 보스 위치에서 목적지까지 경로를 원형 텔레그래프 연속으로 표시한다
void BossChargeAction::addPathTelegraph(FinalBoss& boss, Room& room,
                                        float progress) const {
    const FinalBossConfig& cfg = boss.getConfig();

    Vec3  from = boss.getPosition();
    Vec3  path = chargeDest_ - from;
    float len  = path.length();
    if (len < 1e-4f)
        return;

    Vec3  dir  = path / len;
    float step = cfg.chargeHitRadius * 1.5f;

    for (float d = 0.f; d <= len; d += step) {
        Vec3 p = from + dir * d;
        DebugTelegraphEntry tg;
        tg.x        = p.x;
        tg.z        = p.z;
        tg.radius   = cfg.chargeHitRadius;
        tg.progress = progress;
        tg.kind     = 1;
        room.addDebugTelegraph(tg);
    }
}

} // namespace sim
