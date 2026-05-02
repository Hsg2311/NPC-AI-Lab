#include "PlatoonLeader.hpp"
#include "Room.hpp"
#include "Player.hpp"
#include "Logger.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace sim {

PlatoonLeader::PlatoonLeader(const std::string& name, const Vec3& pos,
                             const TacticalNpcConfig& cfg)
    : TacticalNpc(name, pos, cfg)
{}

void PlatoonLeader::addSquad(TacticalSquad* squad) {
    squads_.push_back(squad);
}

// ─── update ───────────────────────────────────────────────────────────────────

void PlatoonLeader::update(float dt, Room& room) {
    // 사망 처리: 소속 Squad에 Confused 명령 (1회만)
    if (!alive_) {
        if (!deathReported_) {
            deathReported_ = true;
            for (auto* sq : squads_)
                sq->pushConfusedToMembers(room);
            Logger::get().log(name_, "사망 — Squad 전체 Confused 명령 발행");
        }
        updateDead();
        return;
    }

    // 전술 평가 (주기적)
    tacticTimer_ -= dt;
    if (tacticTimer_ <= 0.f) {
        tacticTimer_ = TACTIC_INTERVAL;
        evaluateTactics(room);
    }

    // 자체 전투 FSM (TacticalNpc 상속 로직)
    // PlatoonLeader는 pendingCmd_를 통하지 않고 primaryTargetId_를 직접 사용
    // → base update 호출 전 pendingCmd_를 Idle로 막아서 외부 명령 간섭 차단
    pendingCmd_.type = TacticalCommandType::None;
    TacticalNpc::update(dt, room);
}

// ─── evaluateTactics ─────────────────────────────────────────────────────────

void PlatoonLeader::evaluateTactics(Room& room) {
    Player* primary = selectPrimaryTarget(room);

    // dead 멤버 먼저 정리 — isEmpty()가 올바른 값을 반환하도록
    for (auto* sq : squads_)
        sq->removeDeadMembers(room);

    // 살아있는 Squad 목록 수집
    std::vector<TacticalSquad*> liveSquads;
    for (auto* sq : squads_) {
        if (!sq->isEmpty()) liveSquads.push_back(sq);
    }

    if (!primary || liveSquads.empty()) {
        // 플레이어 없음 → 전체 Idle
        for (auto* sq : liveSquads) {
            SquadOrder ord;
            ord.type = SquadOrderType::Idle;
            sq->receiveOrder(ord);
        }
        // 리더 자신도 전투 해제
        if (state_ != TacticalNpcState::Idle && state_ != TacticalNpcState::Return) {
            targetId_ = 0;
            transitionTo(TacticalNpcState::Idle, "플레이어 없음");
        }
        return;
    }

    // 리더 자신은 항상 primary를 추격/공격
    if (targetId_ != primary->getId()) {
        targetId_ = primary->getId();
        if (state_ == TacticalNpcState::Idle || state_ == TacticalNpcState::Return) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "전술 평가: target=%s",
                primary->getName().c_str());
            transitionTo(TacticalNpcState::Chase, buf);
        }
    }

    // 모든 Squad: 정면 Engage
    for (auto* sq : liveSquads) {
        SquadOrder ord;
        ord.type     = SquadOrderType::Engage;
        ord.targetId = primary->getId();
        sq->receiveOrder(ord);
    }
}

// ─── selectPrimaryTarget ─────────────────────────────────────────────────────

Player* PlatoonLeader::selectPrimaryTarget(Room& room) const {
    Player* best      = nullptr;
    float   bestScore = -1.f;
    for (Player* p : room.getLivingPlayers()) {
        float s = evaluatePlayerScore(p);
        if (s > bestScore) { bestScore = s; best = p; }
    }
    return best;
}

float PlatoonLeader::evaluatePlayerScore(const Player* p) const {
    float dist      = Vec3::distance(position_, p->getPosition());
    float distScore = 1.f / (1.f + dist);
    float hpScore   = 1.f - (p->getHp() / p->getMaxHp());
    return distScore * 0.5f + hpScore * 0.5f;
}

} // namespace sim
