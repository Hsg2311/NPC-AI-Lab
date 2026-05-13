#include "PlatoonLeader.hpp"
#include "Room.hpp"
#include "Player.hpp"
#include "Logger.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

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
    if (next != LeaderPhase::DivideAndConquer)
        divideTasks_.clear();
    if (next != LeaderPhase::Cooldown)
        tacticTimer_ = 0.f;
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
    // 초기 부대 규모 기록: Squad 생존율 기반 전술 조건의 기준값이다.
    if (!initialSizesSet_) {
        initialSizesSet_ = true;
        for (auto* sq : squads_)
            initialSquadSizes_.push_back(static_cast<int>(sq->getMembers().size()));
    }

    // 리더 사망 시 소속 Squad 전체를 Confused 상태로 전환한다.
    if (!alive_) {
        if (!deathReported_) {
            deathReported_ = true;
            for (auto* sq : squads_)
                sq->pushConfusedToMembers(room);
            Logger::get().log(name_, "사망 - 부대 전체 혼란 명령 발행");
        }
        updateDead();
        return;
    }

    removeDeadMembersFromSquads(room);

    // 포위 완료 후 쿨타임이 끝나면, 전술이 이미 해금된 경우 다시 공통 후퇴부터 시작한다.
    if (leaderPhase_ == LeaderPhase::Cooldown) {
        tacticCooldown_ -= dt;
        if (tacticCooldown_ <= 0.f)
            enterPhase(tacticsUnlocked_ ? LeaderPhase::TacticalRetreat : LeaderPhase::BoxAdvance,
                       "전술 쿨타임 종료");
    } else if (leaderPhase_ == LeaderPhase::Encircle) {
        if (phaseOrderIssued_ && allMembersArrived(room)) {
            Player* encircleTarget = selectPrimaryTarget(room);
            if (encircleTarget) {
                for (auto* sq : squads_) {
                    if (sq->isEmpty()) continue;
                    SquadOrder ord;
                    ord.type     = SquadOrderType::Engage;
                    ord.targetId = encircleTarget->getId();
                    sq->receiveOrder(ord);
                }
            }
            tacticCooldown_ = TACTIC_COOLDOWN_DURATION;
            enterPhase(LeaderPhase::Cooldown, "포위 완성 - Engage 후 쿨타임 진입");
        }
    } else if (leaderPhase_ == LeaderPhase::DivideAndConquer) {
        updateDivideAndConquer(dt, room);
    }

    Player* primary = selectPrimaryTarget(room);

    // 전술 발동 조건은 HP/부대 생존율뿐이다. 플레이어 분산 여부는 여기서 보지 않는다.
    if (!tacticsUnlocked_ && primary && checkTacticsConditions()) {
        tacticsUnlocked_ = true;
        enterPhase(LeaderPhase::TacticalRetreat, "전술 활성화 - 공통 후퇴 시작");
    }

    // 박스 대형 완료 순간에만 플레이어 군집 수를 판정해 포위/경계를 선택한다.
    if (leaderPhase_ == LeaderPhase::BoxAdvance && primary && allMembersArrived(room)) {
        if (!tacticsUnlocked_) {
            enterPhase(LeaderPhase::Engage, "박스 대형 완성 - 일반 교전 전환");
            for (auto* sq : squads_) {
                if (sq->isEmpty()) continue;
                SquadOrder ord;
                ord.type     = SquadOrderType::Engage;
                ord.targetId = primaryTargetId_;
                sq->receiveOrder(ord);
            }
        } else if (clusterPlayers(room) == 1) {
            enterPhase(LeaderPhase::Encircle, "박스 대형 완성 - 플레이어 군집 포위");
        } else {
            enterPhase(LeaderPhase::Vigilance, "박스 대형 완성 - 플레이어 분산 경계");
        }
    }

    // 후퇴는 보스와 모든 Squad 멤버가 각자의 슬롯에 도착해야 완료된다.
    if (leaderPhase_ == LeaderPhase::Vigilance && phaseOrderIssued_ && allMembersArrived(room)) {
        auto clusters = buildPlayerClusters(room);
        if (clusters.size() <= 1) {
            enterPhase(LeaderPhase::Encircle, "경계 완료 - 플레이어 재집결 포위");
        } else {
            enterPhase(LeaderPhase::DivideAndConquer, "경계 완료 - 각개격파 전환");
        }
    }

    bool leaderAtRetreat = Vec3::distance(position_, retreatTargetPos_) <= 1.5f;
    if (leaderPhase_ == LeaderPhase::TacticalRetreat &&
        phaseOrderIssued_ && allMembersArrived(room) && leaderAtRetreat) {
        enterPhase(LeaderPhase::BoxAdvance, "후퇴 완료 - 박스 대형 전환");
    }

    tacticTimer_ -= dt;
    if (tacticTimer_ <= 0.f) {
        tacticTimer_ = TACTIC_INTERVAL;
        evaluateTactics(room);
    }

    pendingCmd_.type = TacticalCommandType::None;

    // TacticalRetreat 중 보스는 player centroid 반대 방향 목표점으로 직접 이동한다.
    if (leaderPhase_ == LeaderPhase::TacticalRetreat) {
        Vec3 toRetreat = retreatTargetPos_ - position_;
        float d = toRetreat.length();
        if (d > 1.f) {
            toRetreat = toRetreat / d;
            position_ += toRetreat * (moveSpeed_ * TACTICAL_SPEED_MULT) * dt;
            facing_ = toRetreat;
        }
        return;
    }

    // BoxAdvance/Vigilance 중 보스는 위치를 고정하고 플레이어만 바라본다.
    if (leaderPhase_ == LeaderPhase::BoxAdvance) {
        if (primary) {
            Vec3 dir = primary->getPosition() - position_;
            if (dir.length() > 0.1f) facing_ = dir.normalized();
        }
        return;
    }

    if (leaderPhase_ == LeaderPhase::Vigilance ||
        leaderPhase_ == LeaderPhase::DivideAndConquer) {
        if (primary) {
            Vec3 dir = primary->getPosition() - position_;
            if (dir.length() > 0.1f) facing_ = dir.normalized();
        }
        return;
    }

    // Engage/Encircle/Cooldown 중에는 기존 보스 거리 유지 로직을 사용한다.
    if (primary) {
        float dist = Vec3::distance(position_, primary->getPosition());
        Vec3 toPlayer = (primary->getPosition() - position_).normalized();
        if (dist > BOSS_KEEP_DIST + BOSS_KEEP_TOL) {
            position_ += toPlayer * moveSpeed_ * dt;
            facing_ = toPlayer;
        } else if (dist < BOSS_KEEP_DIST - BOSS_KEEP_TOL) {
            position_ -= toPlayer * moveSpeed_ * dt;
            facing_ = toPlayer;
        } else {
            facing_ = toPlayer;
        }
    }
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

    if (targetId_ != primary->getId()) {
        targetId_ = primary->getId();
        if (state_ == TacticalNpcState::Idle) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "전술 평가: 대상=%s",
                primary->getName().c_str());
            transitionTo(TacticalNpcState::Chase, buf);
        }
    }

    int numSquads = static_cast<int>(liveSquads.size());
    primaryTargetId_ = primary->getId();

    // 공통 후퇴: 대형을 새로 만들지 않고, 모든 NPC가 보스와 같은 이동량만큼 물러난다.
    if (leaderPhase_ == LeaderPhase::TacticalRetreat) {
        if (phaseOrderIssued_) return;

        Vec3 playerCent = calcPlayerCentroid(room);
        Vec3 awayDir = position_ - playerCent;
        float awayLen = awayDir.length();
        if (awayLen > 0.01f) awayDir = awayDir / awayLen;
        else                 awayDir = Vec3{ -1.f, 0.f, 0.f };

        retreatTargetPos_ = playerCent + awayDir * REGROUP_DIST;

        for (int i = 0; i < numSquads; ++i) {
            SquadOrder ord;
            ord.type               = SquadOrderType::RetreatFormUp;
            ord.targetId           = primaryTargetId_;
            ord.leaderPos          = position_;
            ord.tacticCenter       = retreatTargetPos_;
            ord.formationTargetPos = playerCent;
            liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);
        }
        phaseOrderIssued_ = true;
        return;
    }

    // 박스 대형: 보스 앞쪽에서 플레이어 방향을 바라보며 정렬한다.
    if (leaderPhase_ == LeaderPhase::BoxAdvance) {
        if (phaseOrderIssued_) return;

        boxAdvanceTargetPos_ = calcPlayerCentroid(room);

        Vec3 toTgt = boxAdvanceTargetPos_ - position_;
        float len = toTgt.length();
        Vec3 fwd = (len > 0.01f) ? (toTgt / len) : Vec3{ 1.f, 0.f, 0.f };
        Vec3 right{ -fwd.z, 0.f, fwd.x };
        Vec3 boxCenter = position_ + fwd * BOX_FRONT_OFFSET;

        std::vector<std::pair<float, TacticalSquad*>> sqByLat;
        sqByLat.reserve(static_cast<size_t>(numSquads));
        for (auto* sq : liveSquads) {
            Vec3 sum{}; int cnt = 0;
            for (uint32_t mid : sq->getMembers()) {
                Actor* ma = room.findActorById(mid);
                if (ma && ma->isAlive()) { sum += ma->getPosition(); ++cnt; }
            }
            Vec3 cen = (cnt > 0) ? (sum / static_cast<float>(cnt)) : position_;
            sqByLat.push_back({ cen.dot(right), sq });
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
            ord.tacticCenter       = boxCenter;
            ord.formationTargetPos = boxAdvanceTargetPos_;
            sqByLat[static_cast<size_t>(i)].second->receiveOrder(ord);
        }
        phaseOrderIssued_ = true;
        return;
    }

    // 경계 전술: 보스를 중심으로 3방향 GuardBoss 대형을 만든다.
    if (leaderPhase_ == LeaderPhase::Vigilance) {
        if (phaseOrderIssued_) return;

        Vec3 playerCent = calcPlayerCentroid(room);
        Vec3 toPlayers = playerCent - position_;
        float len = toPlayers.length();
        Vec3 forward = (len > 0.01f) ? (toPlayers / len) : Vec3{ 1.f, 0.f, 0.f };
        float baseAngle = std::atan2f(forward.z, forward.x);
        constexpr float TWO_PI = 2.f * 3.14159265f;

        for (int i = 0; i < numSquads; ++i) {
            SquadOrder ord;
            ord.type               = SquadOrderType::GuardBoss;
            ord.targetId           = primaryTargetId_;
            ord.sectorAngle        = baseAngle + TWO_PI * static_cast<float>(i)
                                                / static_cast<float>(numSquads);
            ord.approachRadius     = VIGILANCE_GUARD_RADIUS;
            ord.tacticCenter       = position_;
            ord.formationTargetPos = playerCent;
            liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);
        }
        phaseOrderIssued_ = true;
        return;
    }

    // 포위 전술: 박스 대형 완료 시점에 선택된 뒤에는 분산 여부를 다시 검사하지 않는다.
    if (leaderPhase_ == LeaderPhase::DivideAndConquer) {
        if (phaseOrderIssued_) return;

        auto clusters = buildPlayerClusters(room);
        if (clusters.size() <= 1) {
            enterPhase(LeaderPhase::Encircle, "각개격파 취소 - 플레이어 재집결");
            return;
        }

        issueDivideAndConquer(room, liveSquads, clusters);
        phaseOrderIssued_ = true;
        return;
    }

    if (leaderPhase_ == LeaderPhase::Encircle) {
        if (phaseOrderIssued_) return;

        constexpr float TWO_PI = 2.f * 3.14159265f;
        Vec3 encircleCenter = calcPlayerCentroid(room);
        int totalMembers = 0;
        for (auto* sq : liveSquads)
            totalMembers += static_cast<int>(sq->getMembers().size());
        if (totalMembers < 1) totalMembers = 1;

        float angleAccum = 0.f;
        for (int i = 0; i < numSquads; ++i) {
            int memberCount = static_cast<int>(liveSquads[static_cast<size_t>(i)]->getMembers().size());
            float fraction = static_cast<float>(memberCount) / static_cast<float>(totalMembers);
            float sectorSpan = TWO_PI * fraction;
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
        return;
    }

    // 일반 Engage: 각 Squad를 가장 가까운/적절한 플레이어에게 분배한다.
    std::vector<uint32_t> targets(static_cast<size_t>(numSquads), primary->getId());
    assignSquadsToPlayers(room, liveSquads, targets);
    for (int i = 0; i < numSquads; ++i) {
        SquadOrder ord;
        ord.type     = SquadOrderType::Engage;
        ord.targetId = targets[static_cast<size_t>(i)];
        liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);
    }
}

// ─── 전술 조건 ────────────────────────────────────────────────────────────────

bool PlatoonLeader::checkTacticsConditions() const {
    if (maxHp_ > 0.f && hp_ / maxHp_ <= TACTIC_HP_THRESHOLD) return true;

    for (size_t i = 0; i < squads_.size(); ++i) {
        int initial = (i < initialSquadSizes_.size()) ? initialSquadSizes_[i] : 0;
        int current = static_cast<int>(squads_[i]->getMembers().size());
        if (initial > 0 &&
            static_cast<float>(current) / static_cast<float>(initial) <= TACTIC_SQUAD_RATIO)
            return true;
    }
    return false;
}

// ─── 플레이어 군집/타겟 계산 ──────────────────────────────────────────────────

Vec3 PlatoonLeader::calcPlayerCentroid(const Room& room) const {
    const auto& players = room.getLivingPlayers();
    if (players.empty()) return position_;
    Vec3 sum{};
    for (Player* p : players) sum += p->getPosition();
    return sum / static_cast<float>(players.size());
}

int PlatoonLeader::clusterPlayers(const Room& room) const {
    return static_cast<int>(buildPlayerClusters(room).size());
}

std::vector<PlatoonLeader::PlayerCluster> PlatoonLeader::buildPlayerClusters(const Room& room) const {
    const auto& players = room.getLivingPlayers();
    int count = static_cast<int>(players.size());
    std::vector<PlayerCluster> result;
    if (count <= 0) return result;

    std::vector<bool> visited(static_cast<size_t>(count), false);
    float clusterRadiusSq = CLUSTER_RADIUS * CLUSTER_RADIUS;

    for (int i = 0; i < count; ++i) {
        if (visited[static_cast<size_t>(i)]) continue;

        PlayerCluster cluster;
        std::vector<int> stack;
        stack.push_back(i);
        visited[static_cast<size_t>(i)] = true;

        while (!stack.empty()) {
            int current = stack.back();
            stack.pop_back();

            Player* currentPlayer = players[static_cast<size_t>(current)];
            Vec3 currentPos = currentPlayer->getPosition();
            cluster.centroid += currentPos;
            cluster.playerIds.push_back(currentPlayer->getId());

            float score = evaluatePlayerScore(currentPlayer);
            if (cluster.representativeId == 0 || score > cluster.score) {
                cluster.representativeId = currentPlayer->getId();
                cluster.score = score;
            }

            for (int j = 0; j < count; ++j) {
                if (visited[static_cast<size_t>(j)]) continue;

                Vec3 otherPos = players[static_cast<size_t>(j)]->getPosition();
                if (Vec3::distanceSq(currentPos, otherPos) <= clusterRadiusSq) {
                    visited[static_cast<size_t>(j)] = true;
                    stack.push_back(j);
                }
            }
        }

        if (!cluster.playerIds.empty()) {
            cluster.centroid = cluster.centroid / static_cast<float>(cluster.playerIds.size());
            result.push_back(cluster);
        }
    }

    return result;
}

Player* PlatoonLeader::selectPrimaryTarget(Room& room) const {
    Player* best = nullptr;
    float bestScore = -1.f;
    for (Player* p : room.getLivingPlayers()) {
        float s = evaluatePlayerScore(p);
        if (s > bestScore) { bestScore = s; best = p; }
    }
    return best;
}

// ─── Squad 타겟 분배 ──────────────────────────────────────────────────────────

void PlatoonLeader::assignSquadsToPlayers(const Room& room,
    const std::vector<TacticalSquad*>& liveSquads,
    std::vector<uint32_t>& outTargetIds) const
{
    const auto& players = room.getLivingPlayers();
    int numSquads = static_cast<int>(liveSquads.size());
    int numPlayers = static_cast<int>(players.size());

    if (numPlayers <= 1) return;

    int maxPerPlayer = (numSquads + numPlayers - 1) / numPlayers;

    struct DistEntry { float dist; int squadIdx; int playerIdx; };
    std::vector<DistEntry> entries;
    entries.reserve(static_cast<size_t>(numSquads * numPlayers));

    for (int si = 0; si < numSquads; ++si) {
        Vec3 centroid{}; int cnt = 0;
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
    std::vector<int> playerCount(static_cast<size_t>(numPlayers), 0);

    for (const auto& e : entries) {
        if (squadDone[static_cast<size_t>(e.squadIdx)]) continue;
        if (playerCount[static_cast<size_t>(e.playerIdx)] >= maxPerPlayer) continue;
        outTargetIds[static_cast<size_t>(e.squadIdx)] =
            players[static_cast<size_t>(e.playerIdx)]->getId();
        squadDone[static_cast<size_t>(e.squadIdx)] = true;
        playerCount[static_cast<size_t>(e.playerIdx)]++;
    }
}

uint32_t PlatoonLeader::selectReplacementTarget(Room& room,
    const std::vector<uint32_t>& playerIds) const
{
    Player* best = nullptr;
    float bestScore = -1.f;
    for (uint32_t id : playerIds) {
        auto* p = dynamic_cast<Player*>(room.findActorById(id));
        if (!p || !p->isAlive()) continue;
        float score = evaluatePlayerScore(p);
        if (score > bestScore) {
            bestScore = score;
            best = p;
        }
    }
    return best ? best->getId() : 0;
}

void PlatoonLeader::issueDivideAndConquer(Room& room,
    const std::vector<TacticalSquad*>& liveSquads,
    const std::vector<PlayerCluster>& clusters)
{
    divideTasks_.clear();
    if (liveSquads.empty() || clusters.size() <= 1) return;

    std::vector<PlayerCluster> sorted = clusters;
    std::sort(sorted.begin(), sorted.end(),
        [](const PlayerCluster& a, const PlayerCluster& b) {
            return a.score > b.score;
        });

    const PlayerCluster& chargeCluster = sorted[0];

    int chargeSquadIdx = 0;
    float bestDist = -1.f;
    for (int i = 0; i < static_cast<int>(liveSquads.size()); ++i) {
        float d = Vec3::distance(liveSquads[static_cast<size_t>(i)]->calcCentroid(room),
                                 chargeCluster.centroid);
        if (bestDist < 0.f || d < bestDist) {
            bestDist = d;
            chargeSquadIdx = i;
        }
    }

    TacticalSquad* chargeSquad = liveSquads[static_cast<size_t>(chargeSquadIdx)];
    SquadOrder charge;
    charge.type         = SquadOrderType::WedgeCharge;
    charge.targetId     = chargeCluster.representativeId;
    charge.targetIds    = chargeCluster.playerIds;
    charge.tacticCenter = chargeCluster.centroid;
    chargeSquad->receiveOrder(charge);
    divideTasks_.push_back({ chargeSquad, DivideTaskType::Charge,
                             chargeCluster.representativeId, chargeCluster.playerIds });

    Vec3 supportCentroid{};
    int supportCount = 0;
    uint32_t supportTargetId = 0;
    std::vector<uint32_t> supportPlayerIds;
    for (size_t i = 1; i < sorted.size(); ++i) {
        const PlayerCluster& cluster = sorted[i];
        supportCentroid += cluster.centroid;
        ++supportCount;
        if (supportTargetId == 0)
            supportTargetId = cluster.representativeId;
        supportPlayerIds.insert(supportPlayerIds.end(),
                                cluster.playerIds.begin(), cluster.playerIds.end());
    }
    if (supportCount == 0) return;
    supportCentroid = supportCentroid / static_cast<float>(supportCount);

    Vec3 blockDir = supportCentroid - chargeCluster.centroid;
    float blockLen = blockDir.length();
    if (blockLen > 0.01f) blockDir = blockDir / blockLen;
    else                  blockDir = Vec3{ 1.f, 0.f, 0.f };

    Vec3 blockCenter = chargeCluster.centroid +
                       (supportCentroid - chargeCluster.centroid) *
                       SCREEN_BLOCK_CENTER_BIAS;
    Vec3 blockRight{ -blockDir.z, 0.f, blockDir.x };
    float baseAngle = std::atan2f(blockRight.z, blockRight.x);

    int screenIdx = 0;
    for (int i = 0; i < static_cast<int>(liveSquads.size()); ++i) {
        if (i == chargeSquadIdx) continue;

        float sideSign = (screenIdx % 2 == 0) ? 1.f : -1.f;
        SquadOrder screen;
        screen.type               = SquadOrderType::GuardBoss;
        screen.targetId           = (supportTargetId != 0) ? supportTargetId
                                                           : chargeCluster.representativeId;
        screen.slotSpacingScale   = SCREEN_SLOT_SPACING_SCALE;
        screen.slotColumnScale    = SCREEN_SLOT_COLUMN_SCALE;
        screen.slotColumnCount    = SCREEN_SLOT_COLUMN_COUNT;
        screen.sectorAngle        = baseAngle + (sideSign < 0.f ? 3.14159265f : 0.f);
        screen.approachRadius     = SCREEN_BLOCK_SPACING *
                                    (1.f + 0.5f * static_cast<float>(screenIdx / 2));
        screen.tacticCenter       = blockCenter;
        screen.formationTargetPos = supportCentroid;
        liveSquads[static_cast<size_t>(i)]->receiveOrder(screen);
        divideTasks_.push_back({ liveSquads[static_cast<size_t>(i)],
                                 DivideTaskType::Screen,
                                 screen.targetId,
                                 supportPlayerIds });
        ++screenIdx;
    }
}

void PlatoonLeader::updateDivideAndConquer(float dt, Room& room) {
    if (!phaseOrderIssued_) return;
    if (divideTasks_.empty()) {
        enterTacticFailCooldown(room, "각개격파 실패 - 배정 없음");
        return;
    }

    for (auto& task : divideTasks_) {
        if (!task.squad || task.squad->isEmpty()) {
            task.taskCompleted = true;
        } else if (!task.taskCompleted) {
            switch (task.type) {
                case DivideTaskType::Charge:
                    task.taskCompleted = task.squad->areChargeMembersComplete(room);
                    break;
                case DivideTaskType::Screen:
                    task.taskCompleted = task.squad->areMembersAtSlots(room);
                    break;
                default:
                    break;
            }
        }
    }

    bool allScreensCompleted = true;
    for (const auto& task : divideTasks_) {
        if (task.type == DivideTaskType::Screen && !task.taskCompleted) {
            allScreensCompleted = false;
            break;
        }
    }

    bool allProtected = true;
    for (auto& task : divideTasks_) {
        if (task.taskCompleted && !task.engageIssued) {
            if (task.type == DivideTaskType::Screen && !allScreensCompleted) {
                allProtected = false;
                continue;
            }

            uint32_t targetId = selectReplacementTarget(room, task.clusterPlayerIds);
            if (targetId != 0 && task.squad && !task.squad->isEmpty()) {
                SquadOrder ord;
                ord.type     = SquadOrderType::Engage;
                ord.targetId = targetId;
                task.squad->receiveOrder(ord);
                task.targetId = targetId;
            }
            task.engageIssued = true;
            task.engageProtectTimer = 0.f;
        }

        if (task.engageIssued) {
            if (task.targetId != 0) {
                Actor* target = room.findActorById(task.targetId);
                if (!target || !target->isAlive()) {
                    uint32_t replacement = selectReplacementTarget(room, task.clusterPlayerIds);
                    task.targetId = replacement;
                    if (replacement != 0 && task.squad && !task.squad->isEmpty()) {
                        SquadOrder ord;
                        ord.type     = SquadOrderType::Engage;
                        ord.targetId = replacement;
                        task.squad->receiveOrder(ord);
                    }
                }
            }
            task.engageProtectTimer += dt;
        }

        if (!task.engageIssued ||
            task.engageProtectTimer < DIVIDE_ENGAGE_PROTECT_DURATION)
            allProtected = false;
    }

    if (allProtected) {
        tacticCooldown_ = TACTIC_COOLDOWN_DURATION;
        enterPhase(LeaderPhase::Cooldown, "각개격파 완료 - 쿨타임 진입");
    }
}

float PlatoonLeader::evaluatePlayerScore(const Player* p) const {
    float dist = Vec3::distance(position_, p->getPosition());
    float distScore = 1.f / (1.f + dist);
    float hpScore = 1.f - (p->getHp() / p->getMaxHp());
    return distScore * 0.5f + hpScore * 0.5f;
}

// ─── 대형 완료/슬롯 계산 ─────────────────────────────────────────────────────

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

std::vector<Vec3> PlatoonLeader::calcSquadBoxOffsets(int numSquads) const {
    int rows = static_cast<int>(std::max(1.f, std::floorf(std::sqrtf(static_cast<float>(numSquads)))));
    int cols = (numSquads + rows - 1) / rows;

    std::vector<Vec3> offsets;
    offsets.reserve(static_cast<size_t>(numSquads));
    for (int i = 0; i < numSquads; ++i) {
        int col = i % cols;
        int row = i / cols;
        float colOff = (static_cast<float>(col) - static_cast<float>(cols - 1) * 0.5f) * BOX_SQUAD_SPACING;
        float rowOff = (static_cast<float>(row) - static_cast<float>(rows - 1) * 0.5f) * BOX_SQUAD_SPACING;
        float halfCols = static_cast<float>(cols - 1) * 0.5f;
        float latFrac = (cols > 1)
            ? std::abs(static_cast<float>(col) - halfCols) / halfCols
            : 0.f;
        float arcZ = rowOff - BOX_ARC_DEPTH * latFrac;
        offsets.push_back(Vec3{ colOff, 0.f, arcZ });
    }
    return offsets;
}

} // namespace sim
