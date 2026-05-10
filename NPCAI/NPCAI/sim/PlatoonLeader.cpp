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

// ─── enterPhase ───────────────────────────────────────────────────────────────

void PlatoonLeader::enterPhase(LeaderPhase next, const char* reason) {
    Logger::get().log(name_, reason);
    leaderPhase_      = next;
    phaseOrderIssued_ = false;
}

void PlatoonLeader::enterTacticFailCooldown(Room& room, const char* reason) {
    removeDeadMembersFromSquads(room);

    tacticCooldown_ = TACTIC_FAIL_COOLDOWN_DURATION;
    enterPhase(LeaderPhase::Cooldown, reason);

    std::vector<TacticalSquad*> liveSquads;
    for (auto* sq : squads_) {
        if (!sq->isEmpty()) liveSquads.push_back(sq);
    }

    Player* primary = selectPrimaryTarget(room);
    if (!primary || liveSquads.empty()) return;

    std::vector<uint32_t> targets(liveSquads.size(), primary->getId());
    assignSquadsToPlayers(room, liveSquads, targets);
    for (size_t i = 0; i < liveSquads.size(); ++i) {
        SquadOrder ord;
        ord.type     = SquadOrderType::Engage;
        ord.targetId = targets[i];
        liveSquads[i]->receiveOrder(ord);
    }
}

void PlatoonLeader::removeDeadMembersFromSquads(Room& room) {
    for (auto* sq : squads_)
        sq->removeDeadMembers(room);
}

// ─── update ───────────────────────────────────────────────────────────────────

void PlatoonLeader::update(float dt, Room& room) {
    // 초기 부대 규모 기록 (최초 1회)
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

    removeDeadMembersFromSquads(room);

    // 쿨타임 관리
    if (leaderPhase_ == LeaderPhase::Cooldown) {
        tacticCooldown_ -= dt;
        if (tacticCooldown_ <= 0.f)
            enterPhase(LeaderPhase::BoxAdvance, "전술 쿨타임 종료 — 박스 대형 재개");
    }
    else if (leaderPhase_ == LeaderPhase::Encircle) {
        if (phaseOrderIssued_ && allMembersArrived(room)) {
            tacticCooldown_ = TACTIC_COOLDOWN_DURATION;
            enterPhase(LeaderPhase::Cooldown, "포위 완성 — 쿨타임 진입");
        }
    }

    // ── 박스 대형 완성 감지 ───────────────────────────────────────────────────
    Player* primary = selectPrimaryTarget(room);

    if (!tacticsUnlocked_ && primary && checkTacticsConditions()) {
        tacticsUnlocked_ = true;
        Logger::get().log(name_, "전술 활성화 — 조건 충족");
        if (leaderPhase_ == LeaderPhase::Engage && clusterPlayers(room) == 1)
            enterPhase(LeaderPhase::TacticRegroup, "전술 활성화 — 플레이어 군집, 집결 후 포위 전환");
    }

    if (leaderPhase_ == LeaderPhase::BoxAdvance && primary && allMembersArrived(room)) {
        if (tacticsUnlocked_ && clusterPlayers(room) != 1) {
            enterTacticFailCooldown(room, "Tactic failed: players scattered after BoxAdvance");
            return;
        }

        if (!tacticsUnlocked_) {
            enterPhase(LeaderPhase::Engage, "박스 대형 완성 — Engage 전환");
            for (auto* sq : squads_) {
                if (sq->isEmpty()) continue;
                SquadOrder ord;
                ord.type     = SquadOrderType::Engage;
                ord.targetId = primaryTargetId_;
                sq->receiveOrder(ord);
            }
        }
        else if (clusterPlayers(room) == 1) {
            enterPhase(LeaderPhase::Encircle, "박스 대형 완성 — 플레이어 군집 포위");
        }
        else {
            enterPhase(LeaderPhase::Engage, "박스 대형 완성 — 플레이어 분산, Engage 유지");
            for (auto* sq : squads_) {
                if (sq->isEmpty()) continue;
                SquadOrder ord;
                ord.type     = SquadOrderType::Engage;
                ord.targetId = primaryTargetId_;
                sq->receiveOrder(ord);
            }
        }
    }

    // TacticRegroup: 전 부대 슬롯 도착 → Encircle 진입
    if (leaderPhase_ == LeaderPhase::TacticRegroup && phaseOrderIssued_ && allMembersArrived(room)) {
        if (clusterPlayers(room) != 1) {
            enterTacticFailCooldown(room, "Tactic failed: players scattered after regroup");
            return;
        }

        if (clusterPlayers(room) == 1) {
            enterPhase(LeaderPhase::Encircle, "집결 완료 — 플레이어 군집 포위 전술 발동");
        } else {
            enterPhase(LeaderPhase::Engage, "집결 완료 — 플레이어 분산, Engage 유지");
            for (auto* sq : squads_) {
                if (sq->isEmpty()) continue;
                SquadOrder ord;
                ord.type     = SquadOrderType::Engage;
                ord.targetId = primaryTargetId_;
                sq->receiveOrder(ord);
            }
        }
    }

    // 전술 평가 (주기적)
    tacticTimer_ -= dt;
    if (tacticTimer_ <= 0.f) {
        tacticTimer_ = TACTIC_INTERVAL;
        evaluateTactics(room);
    }

    // ── 보스 이동 ─────────────────────────────────────────────────────────────
    pendingCmd_.type = TacticalCommandType::None;

    if (leaderPhase_ == LeaderPhase::BoxAdvance) {
        // BoxAdvance 중: 보스 이동 없음, 플레이어 응시
        if (primary)
            facing_ = (primary->getPosition() - position_).normalized();
        return;
    }

    if (leaderPhase_ == LeaderPhase::TacticRegroup) {
        // 집결지로 이동
        Vec3  toRally = regroupTargetPos_ - position_;
        float d       = toRally.length();
        if (d > 1.f) {
            toRally = toRally / d;
            position_ += toRally * moveSpeed_ * dt;
            facing_    = toRally;
        }
        return;
    }

    // Engage / Encircle / Cooldown: 플레이어와 거리 유지
    if (primary) {
        float dist    = Vec3::distance(position_, primary->getPosition());
        Vec3 toPlayer = (primary->getPosition() - position_).normalized();
        if (dist > BOSS_KEEP_DIST + BOSS_KEEP_TOL) {
            position_ += toPlayer * moveSpeed_ * dt;
            facing_    = toPlayer;
        } else if (dist < BOSS_KEEP_DIST - BOSS_KEEP_TOL) {
            position_ -= toPlayer * moveSpeed_ * dt;
            facing_    = toPlayer;
        } else {
            facing_ = toPlayer;
        }
    }
    // TacticalNpc::update() 호출하지 않음 — 보스는 직접 공격 안 함
}

// ─── evaluateTactics ─────────────────────────────────────────────────────────

void PlatoonLeader::evaluateTactics(Room& room) {
    removeDeadMembersFromSquads(room);

    std::vector<TacticalSquad*> liveSquads;
    for (auto* sq : squads_)
        if (!sq->isEmpty()) liveSquads.push_back(sq);

    Player* primary = selectPrimaryTarget(room);

    if (!primary || liveSquads.empty()) {
        for (auto* sq : liveSquads) {
            SquadOrder ord; ord.type = SquadOrderType::Idle;
            sq->receiveOrder(ord);
        }
        if (state_ != TacticalNpcState::Idle) {
            targetId_ = 0;
            transitionTo(TacticalNpcState::Idle, "플레이어 없음");
        }
        return;
    }

    // 리더 자신은 항상 primary 추격
    if (targetId_ != primary->getId()) {
        targetId_ = primary->getId();
        if (state_ == TacticalNpcState::Idle) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "전술 평가: target=%s",
                primary->getName().c_str());
            transitionTo(TacticalNpcState::Chase, buf);
        }
    }

    if (tacticsUnlocked_ && leaderPhase_ == LeaderPhase::Engage && clusterPlayers(room) == 1) {
        enterPhase(LeaderPhase::TacticRegroup, "플레이어 재군집 — 집결 후 포위 전환");
    }

    int numSquads = static_cast<int>(liveSquads.size());

    if (leaderPhase_ != LeaderPhase::Encircle) {
        primaryTargetId_ = primary->getId();

        if (leaderPhase_ == LeaderPhase::BoxAdvance) {
            if (phaseOrderIssued_) return;

            boxAdvanceTargetPos_ = primary->getPosition();

            // right 방향 투영값 기준으로 부대 정렬 → 반대편 이동 방지
            Vec3 toTgt2  = boxAdvanceTargetPos_ - position_;
            float tLen2  = toTgt2.length();
            Vec3  fwd2   = (tLen2 > 0.01f) ? (toTgt2 / tLen2) : Vec3{ 1.f, 0.f, 0.f };
            Vec3  rgt2{ -fwd2.z, 0.f, fwd2.x };

            std::vector<std::pair<float, TacticalSquad*>> sqByLat;
            sqByLat.reserve(static_cast<size_t>(numSquads));
            for (auto* sq : liveSquads) {
                Vec3 sum{}; int cnt = 0;
                for (uint32_t mid : sq->getMembers()) {
                    Actor* ma = room.findActorById(mid);
                    if (ma && ma->isAlive()) { sum += ma->getPosition(); ++cnt; }
                }
                Vec3 cen = (cnt > 0) ? (sum / static_cast<float>(cnt)) : position_;
                sqByLat.push_back({ cen.dot(rgt2), sq });
            }
            std::sort(sqByLat.begin(), sqByLat.end(),
                [](const std::pair<float, TacticalSquad*>& a,
                   const std::pair<float, TacticalSquad*>& b) {
                    return a.first < b.first;
                });

            auto offsets = calcSquadBoxOffsets(numSquads);
            for (int i = 0; i < numSquads; ++i) {
                SquadOrder ord;
                ord.type               = SquadOrderType::BoxAdvance;
                ord.targetId           = primaryTargetId_;
                ord.sectorPos          = offsets[static_cast<size_t>(i)];
                ord.leaderPos          = position_;
                ord.formationTargetPos = boxAdvanceTargetPos_;
                ord.approachRadius     = BOX_APPROACH_DIST;
                sqByLat[static_cast<size_t>(i)].second->receiveOrder(ord);
            }
            phaseOrderIssued_ = true;
        } else if (leaderPhase_ == LeaderPhase::TacticRegroup) {
            // 집결 명령 최초 1회만 발행
            if (!phaseOrderIssued_) {
                // 집결 위치: 보스 위치에서 플레이어 반대 방향으로 REGROUP_DIST
                Vec3 playerCent = calcPlayerCentroid(room);
                Vec3 toPlayer   = playerCent - position_;
                float tLen      = toPlayer.length();
                Vec3  awayDir   = (tLen > 0.01f) ? (toPlayer / tLen * -1.f) : Vec3{ -1.f, 0.f, 0.f };
                regroupTargetPos_ = position_ + awayDir * REGROUP_DIST;

                // 집결지 기준 박스 대형 — 플레이어 방향을 front로
                auto offsets = calcSquadBoxOffsets(numSquads);
                for (int i = 0; i < numSquads; ++i) {
                    SquadOrder ord;
                    ord.type               = SquadOrderType::BoxAdvance;
                    ord.targetId           = primaryTargetId_;
                    ord.sectorPos          = offsets[static_cast<size_t>(i)];
                    ord.leaderPos          = regroupTargetPos_;
                    ord.formationTargetPos = playerCent;
                    ord.approachRadius     = BOX_APPROACH_DIST;
                    liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);
                }
                phaseOrderIssued_ = true;
            }
        } else {
            // Engage 또는 Cooldown: 부대별 가장 가까운 플레이어를 독립 타겟팅
            std::vector<uint32_t> targets(static_cast<size_t>(numSquads), primary->getId());
            assignSquadsToPlayers(room, liveSquads, targets);
            for (int i = 0; i < numSquads; ++i) {
                SquadOrder ord;
                ord.type     = SquadOrderType::Engage;
                ord.targetId = targets[static_cast<size_t>(i)];
                liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);
            }
        }
        return;
    }

    // ── LeaderPhase::Encircle ─────────────────────────────────────────────────
    constexpr float TWO_PI = 2.f * 3.14159265f;

    if (!phaseOrderIssued_) {
        if (clusterPlayers(room) != 1) {
            enterTacticFailCooldown(room, "Tactic failed: players scattered before Encircle");
            return;
        }

        if (clusterPlayers(room) != 1) {
            enterPhase(LeaderPhase::Engage, "포위 보류 — 플레이어 분산, Engage 유지");
            std::vector<uint32_t> targets(static_cast<size_t>(numSquads), primaryTargetId_);
            assignSquadsToPlayers(room, liveSquads, targets);
            for (int i = 0; i < numSquads; ++i) {
                SquadOrder ord;
                ord.type     = SquadOrderType::Engage;
                ord.targetId = targets[static_cast<size_t>(i)];
                liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);
            }
            return;
        }

        Vec3 encircleCenter = calcPlayerCentroid(room);
        int totalMembers = 0;
        for (auto* sq : liveSquads)
            totalMembers += static_cast<int>(sq->getMembers().size());
        if (totalMembers < 1) totalMembers = 1;

        float angleAccum = 0.f;
        for (int i = 0; i < numSquads; ++i) {
            int   memberCount = static_cast<int>(liveSquads[static_cast<size_t>(i)]->getMembers().size());
            float fraction    = static_cast<float>(memberCount) / static_cast<float>(totalMembers);
            float sectorSpan  = TWO_PI * fraction;
            float sectorAngle = angleAccum + sectorSpan * 0.5f;

            SquadOrder ord;
            ord.type           = SquadOrderType::Encircle;
            ord.targetId       = primary->getId();
            ord.sectorAngle    = sectorAngle;
            ord.sectorSpan     = sectorSpan;
            ord.approachRadius = ENCIRCLE_RADIUS;
            ord.tacticCenter   = encircleCenter;
            liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);

            angleAccum += sectorSpan;
        }
        phaseOrderIssued_ = true;
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

// ─── calcPlayerCentroid ───────────────────────────────────────────────────────

Vec3 PlatoonLeader::calcPlayerCentroid(const Room& room) const {
    const auto& players = room.getLivingPlayers();
    if (players.empty()) return position_;
    Vec3 sum{};
    for (Player* p : players) sum += p->getPosition();
    return sum / static_cast<float>(players.size());
}

int PlatoonLeader::clusterPlayers(const Room& room) const {
    const auto& players = room.getLivingPlayers();
    int count = static_cast<int>(players.size());
    if (count <= 1) return count;

    std::vector<bool> visited(static_cast<size_t>(count), false);
    int clusters = 0;
    float clusterRadiusSq = CLUSTER_RADIUS * CLUSTER_RADIUS;

    for (int i = 0; i < count; ++i) {
        if (visited[static_cast<size_t>(i)]) continue;

        ++clusters;
        std::vector<int> stack;
        stack.push_back(i);
        visited[static_cast<size_t>(i)] = true;

        while (!stack.empty()) {
            int current = stack.back();
            stack.pop_back();

            Vec3 currentPos = players[static_cast<size_t>(current)]->getPosition();
            for (int j = 0; j < count; ++j) {
                if (visited[static_cast<size_t>(j)]) continue;

                Vec3 otherPos = players[static_cast<size_t>(j)]->getPosition();
                if (Vec3::distanceSq(currentPos, otherPos) <= clusterRadiusSq) {
                    visited[static_cast<size_t>(j)] = true;
                    stack.push_back(j);
                }
            }
        }
    }

    return clusters;
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

// ─── assignSquadsToPlayers ───────────────────────────────────────────────────
// 부대-플레이어 거리 행렬 기반 균등 greedy 배정.
// maxPerPlayer = ceil(N/P) 제한으로 어느 플레이어도 완전히 무시되지 않도록 보장.

void PlatoonLeader::assignSquadsToPlayers(const Room& room,
    const std::vector<TacticalSquad*>& liveSquads,
    std::vector<uint32_t>& outTargetIds) const
{
    const auto& players = room.getLivingPlayers();
    int numSquads  = static_cast<int>(liveSquads.size());
    int numPlayers = static_cast<int>(players.size());

    // 플레이어가 없거나 1명이면 기존 값(primaryTargetId_) 유지
    if (numPlayers <= 1) return;

    // ceil(numSquads / numPlayers) — 플레이어당 최대 배정 부대 수
    int maxPerPlayer = (numSquads + numPlayers - 1) / numPlayers;

    // 각 부대의 중심점 계산 (살아있는 멤버 위치 평균)
    struct DistEntry { float dist; int squadIdx; int playerIdx; };
    std::vector<DistEntry> entries;
    entries.reserve(static_cast<size_t>(numSquads * numPlayers));

    for (int si = 0; si < numSquads; ++si) {
        Vec3 centroid{};
        int  cnt = 0;
        for (uint32_t mid : liveSquads[static_cast<size_t>(si)]->getMembers()) {
            Actor* a = room.findActorById(mid);
            if (a && a->isAlive()) { centroid += a->getPosition(); ++cnt; }
        }
        if (cnt > 0) centroid = centroid / static_cast<float>(cnt);
        else         centroid = position_;

        for (int pi = 0; pi < numPlayers; ++pi) {
            float d = Vec3::distance(centroid, players[static_cast<size_t>(pi)]->getPosition());
            entries.push_back({ d, si, pi });
        }
    }

    std::sort(entries.begin(), entries.end(),
        [](const DistEntry& a, const DistEntry& b) { return a.dist < b.dist; });

    std::vector<bool> squadDone(static_cast<size_t>(numSquads), false);
    std::vector<int>  playerCount(static_cast<size_t>(numPlayers), 0);

    for (const auto& e : entries) {
        if (squadDone[static_cast<size_t>(e.squadIdx)]) continue;
        if (playerCount[static_cast<size_t>(e.playerIdx)] >= maxPerPlayer) continue;
        outTargetIds[static_cast<size_t>(e.squadIdx)] =
            players[static_cast<size_t>(e.playerIdx)]->getId();
        squadDone[static_cast<size_t>(e.squadIdx)]   = true;
        playerCount[static_cast<size_t>(e.playerIdx)]++;
    }
}

float PlatoonLeader::evaluatePlayerScore(const Player* p) const {
    float dist      = Vec3::distance(position_, p->getPosition());
    float distScore = 1.f / (1.f + dist);
    float hpScore   = 1.f - (p->getHp() / p->getMaxHp());
    return distScore * 0.5f + hpScore * 0.5f;
}

// ─── allMembersArrived ────────────────────────────────────────────────────────
// 모든 생존 멤버가 슬롯에 도착했는지 확인.

bool PlatoonLeader::allMembersArrived(const Room& room) const {
    for (auto* sq : squads_) {
        for (uint32_t id : sq->getMembers()) {
            Actor* a = room.findActorById(id);
            if (!a || !a->isAlive()) continue;
            auto* tnpc = dynamic_cast<TacticalNpc*>(a);
            if (!tnpc) continue;
            if (!tnpc->isAtSlot()) return false;
        }
    }
    return true;
}

// ─── calcSquadBoxOffsets ──────────────────────────────────────────────────────
// numSquads개 부대의 상대 오프셋 반환.
// x=우방향, z=깊이방향 — TacticalSquad가 매 틱 절대 좌표로 변환.

std::vector<Vec3> PlatoonLeader::calcSquadBoxOffsets(int numSquads) const {
    int rows = static_cast<int>(std::max(1.f, std::floorf(std::sqrtf(static_cast<float>(numSquads)))));
    int cols = (numSquads + rows - 1) / rows;

    std::vector<Vec3> offsets;
    offsets.reserve(static_cast<size_t>(numSquads));
    for (int i = 0; i < numSquads; ++i) {
        int   col        = i % cols;
        int   row        = i / cols;
        float colOff     = (static_cast<float>(col) - static_cast<float>(cols - 1) * 0.5f) * BOX_SQUAD_SPACING;
        float rowOff     = (static_cast<float>(row) - static_cast<float>(rows - 1) * 0.5f) * BOX_SQUAD_SPACING;
        float halfCols   = static_cast<float>(cols - 1) * 0.5f;
        float latFrac    = (cols > 1)
            ? std::abs(static_cast<float>(col) - halfCols) / halfCols
            : 0.f;
        float arcZ       = rowOff - BOX_ARC_DEPTH * latFrac;  // 측면 부대를 플레이어 방향으로 당김
        offsets.push_back(Vec3{ colOff, 0.f, arcZ });
    }
    return offsets;
}

} // namespace sim
