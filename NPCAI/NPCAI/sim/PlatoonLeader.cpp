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
    // 초기 부대 규모 기록 (사망 전 최초 1회)
    if (!initialSizesSet_) {
        initialSizesSet_ = true;
        for (auto* sq : squads_)
            initialSquadSizes_.push_back(static_cast<int>(sq->getMembers().size()));
    }

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

    // 경계 타이머 누적
    if (tacticalPhase_ == TacticalPhase::Vigilance)
        vigilanceElapsed_ += dt;

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
    for (auto* sq : squads_)
        sq->removeDeadMembers(room);

    std::vector<TacticalSquad*> liveSquads;
    for (auto* sq : squads_)
        if (!sq->isEmpty()) liveSquads.push_back(sq);

    Player* primary = selectPrimaryTarget(room);

    if (!primary || liveSquads.empty()) {
        for (auto* sq : liveSquads) {
            SquadOrder ord; ord.type = SquadOrderType::Idle;
            sq->receiveOrder(ord);
        }
        if (state_ != TacticalNpcState::Idle && state_ != TacticalNpcState::Return) {
            targetId_ = 0;
            transitionTo(TacticalNpcState::Idle, "플레이어 없음");
        }
        return;
    }

    // 리더 자신은 항상 primary 추격
    if (targetId_ != primary->getId()) {
        targetId_ = primary->getId();
        if (state_ == TacticalNpcState::Idle || state_ == TacticalNpcState::Return) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "전술 평가: target=%s",
                primary->getName().c_str());
            transitionTo(TacticalNpcState::Chase, buf);
        }
    }

    // 전술 발동 조건 체크 (한번 활성화되면 유지)
    if (!tacticsUnlocked_ && checkTacticsConditions()) {
        tacticsUnlocked_ = true;
        Logger::get().log(name_, "전술 활성화 — 조건 충족");
    }

    int numSquads = static_cast<int>(liveSquads.size());

    if (!tacticsUnlocked_) {
        // ── 기본: Engage ──────────────────────────────────────────────────────
        for (auto* sq : liveSquads) {
            SquadOrder ord;
            ord.type     = SquadOrderType::Engage;
            ord.targetId = primary->getId();
            sq->receiveOrder(ord);
        }
        return;
    }

    bool scattered = (clusterPlayers(room) >= 2);

    if (!scattered) {
        // ── (가) 포위 ─────────────────────────────────────────────────────────
        Vec3 centroid = calcPlayerCentroid(room);
        bool isNewPhase = (tacticalPhase_ != TacticalPhase::Encircle);
        bool centroidShifted = Vec3::distance(centroid, lastEncircleCentroid_) > ENCIRCLE_RECALC_THRESHOLD;

        if (isNewPhase) {
            Logger::get().log(name_, "전술 전환: 포위");
            tacticalPhase_    = TacticalPhase::Encircle;
            vigilanceElapsed_ = 0.f;
        }

        if (isNewPhase || centroidShifted) {
            lastEncircleCentroid_ = centroid;

            float angleStep = (numSquads > 1)
                ? (2.f * 3.14159265f / static_cast<float>(numSquads))
                : 0.f;

            for (int i = 0; i < numSquads; ++i) {
                float angle = static_cast<float>(i) * angleStep;
                Vec3  sec   = centroid + Vec3{ std::cosf(angle), 0.f, std::sinf(angle) } * ENCIRCLE_RADIUS;

                SquadOrder ord;
                ord.type           = SquadOrderType::DenseAdvance;
                ord.targetId       = primary->getId();
                ord.sectorPos      = sec;
                ord.leaderPos      = centroid;
                ord.approachRadius = ENCIRCLE_RADIUS;
                liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);
            }
        }
        // centroid 변화 없음 → 재발행 없음 (NPC 현재 상태 유지)

    } else {
        // ── 분산 상태 ─────────────────────────────────────────────────────────
        switch (tacticalPhase_) {

            case TacticalPhase::Encircle:
                Logger::get().log(name_, "전술 전환: 경계");
                tacticalPhase_    = TacticalPhase::Vigilance;
                vigilanceElapsed_ = 0.f;
                for (auto* sq : liveSquads) {
                    SquadOrder ord;
                    ord.type     = SquadOrderType::DenseHold;
                    ord.targetId = primary->getId();
                    sq->receiveOrder(ord);
                }
                break;

            case TacticalPhase::Vigilance:
                // 경계 유지 — 재발행 없음
                if (vigilanceElapsed_ >= VIGILANCE_DURATION) {
                    Logger::get().log(name_, "전술 전환: 각개격파");
                    tacticalPhase_ = TacticalPhase::DivideAndConquer;
                    if (numSquads >= 1) {
                        SquadOrder atk;
                        atk.type      = SquadOrderType::WedgeCharge;
                        atk.targetId  = primary->getId();
                        atk.leaderPos = position_;
                        liveSquads[0]->receiveOrder(atk);
                    }
                    for (int i = 1; i < numSquads; ++i) {
                        SquadOrder hold;
                        hold.type     = SquadOrderType::DenseHold;
                        hold.targetId = primary->getId();
                        liveSquads[static_cast<size_t>(i)]->receiveOrder(hold);
                    }
                }
                break;

            case TacticalPhase::DivideAndConquer:
                // 쐐기 방향만 매 evaluate마다 갱신 (매 틱은 Squad::update()에서 처리)
                if (numSquads >= 1) {
                    SquadOrder atk;
                    atk.type      = SquadOrderType::WedgeCharge;
                    atk.targetId  = primary->getId();
                    atk.leaderPos = position_;
                    liveSquads[0]->receiveOrder(atk);
                }
                break;
        }
    }
}

// ─── checkTacticsConditions ───────────────────────────────────────────────────

bool PlatoonLeader::checkTacticsConditions() const {
    // 조건 A: 리더 HP가 임계값 이하
    if (maxHp_ > 0.f && hp_ / maxHp_ < TACTIC_HP_THRESHOLD) return true;

    // 조건 B: 어느 부대든 초기 인원 대비 생존 비율이 임계값 미만
    for (size_t i = 0; i < squads_.size(); ++i) {
        int initial = (i < initialSquadSizes_.size()) ? initialSquadSizes_[i] : 0;
        int current = static_cast<int>(squads_[i]->getMembers().size());
        if (initial > 0 &&
            static_cast<float>(current) / static_cast<float>(initial) < TACTIC_SQUAD_RATIO)
            return true;
    }
    return false;
}

// ─── clusterPlayers ───────────────────────────────────────────────────────────
// union-find 없이 연결 컴포넌트 카운트 (플레이어 수 ≤ 8이므로 O(N²) 허용)

int PlatoonLeader::clusterPlayers(const Room& room) const {
    const auto& players = room.getLivingPlayers();
    int n = static_cast<int>(players.size());
    if (n <= 1) return n;

    std::vector<int> label(static_cast<size_t>(n), -1);
    int numClusters = 0;

    for (int i = 0; i < n; ++i) {
        if (label[i] != -1) continue;
        label[i] = numClusters++;
        // BFS
        for (int j = i + 1; j < n; ++j) {
            if (label[j] == -1) {
                float d = Vec3::distance(players[static_cast<size_t>(i)]->getPosition(),
                                         players[static_cast<size_t>(j)]->getPosition());
                if (d <= CLUSTER_RADIUS) label[j] = label[i];
            }
        }
    }
    return numClusters;
}

// ─── calcPlayerCentroid ───────────────────────────────────────────────────────

Vec3 PlatoonLeader::calcPlayerCentroid(const Room& room) const {
    const auto& players = room.getLivingPlayers();
    if (players.empty()) return position_;
    Vec3 sum{};
    for (Player* p : players) sum += p->getPosition();
    return sum / static_cast<float>(players.size());
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
