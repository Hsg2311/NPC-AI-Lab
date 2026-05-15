#include "MidBossTactics.hpp"
#include "Actor.hpp"
#include "PlatoonLeader.hpp"
#include "Room.hpp"
#include "Player.hpp"
#include "TacticalSquad.hpp"
#include "Logger.hpp"
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace sim {

void MidBossTacticBase::onLeaderDead(Room& room, PlatoonLeader& leader) {
    leader.pushConfusedToSquads(room);
    Logger::get().log(leader.getName(), "dead - issued Confused to all squads");
}

std::vector<TacticalSquad*>
MidBossTacticBase::collectLiveSquads(PlatoonLeader& leader) const {
    std::vector<TacticalSquad*> liveSquads;
    for (TacticalSquad* squad : leader.getSquads()) {
        if (squad && !squad->isEmpty())
            liveSquads.push_back(squad);
    }
    return liveSquads;
}

std::vector<MidBossTacticBase::PlayerCluster>
MidBossTacticBase::buildPlayerClusters(const Room& room,
                                       float clusterRadius) const {
    const auto& players = room.getLivingPlayers();
    int count = static_cast<int>(players.size());
    std::vector<PlayerCluster> result;
    if (count <= 0)
        return result;

    std::vector<bool> visited(static_cast<size_t>(count), false);
    float clusterRadiusSq = clusterRadius * clusterRadius;

    for (int i = 0; i < count; ++i) {
        if (visited[static_cast<size_t>(i)])
            continue;

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
            if (cluster.representativeId == 0)
                cluster.representativeId = currentPlayer->getId();

            for (int j = 0; j < count; ++j) {
                if (visited[static_cast<size_t>(j)])
                    continue;
                Vec3 otherPos = players[static_cast<size_t>(j)]->getPosition();
                if (Vec3::distanceSq(currentPos, otherPos) <= clusterRadiusSq) {
                    visited[static_cast<size_t>(j)] = true;
                    stack.push_back(j);
                }
            }
        }

        if (!cluster.playerIds.empty()) {
            cluster.centroid = cluster.centroid /
                               static_cast<float>(cluster.playerIds.size());
            result.push_back(cluster);
        }
    }

    return result;
}

Vec3 MidBossTacticBase::calcPlayerCentroid(const Room& room,
                                           const Vec3& fallback) const {
    const auto& players = room.getLivingPlayers();
    if (players.empty())
        return fallback;

    Vec3 sum{};
    for (Player* p : players)
        sum += p->getPosition();
    return sum / static_cast<float>(players.size());
}

Vec3 MidBossTacticBase::calcAveragePlayerFacing(
    const Room& room, const Vec3& fallbackDir) const {
    Vec3 sum{};
    for (Player* p : room.getLivingPlayers())
        sum += p->getFacing();

    if (sum.lengthSq() > 0.01f)
        return sum.normalized();
    if (fallbackDir.lengthSq() > 0.01f)
        return fallbackDir.normalized();
    return Vec3{};
}

Player* MidBossTacticBase::selectNearestPlayer(Room& room,
                                               const Vec3& from) const {
    Player* best = nullptr;
    float bestDistSq = -1.f;
    for (Player* p : room.getLivingPlayers()) {
        float dSq = Vec3::distanceSq(from, p->getPosition());
        if (bestDistSq < 0.f || dSq < bestDistSq) {
            bestDistSq = dSq;
            best = p;
        }
    }
    return best;
}

uint32_t MidBossTacticBase::selectNearestPlayerId(Room& room,
                                                  const Vec3& from) const {
    Player* player = selectNearestPlayer(room, from);
    return player ? player->getId() : 0;
}

void MidBossTacticBase::issueEngageAll(PlatoonLeader& leader,
                                       uint32_t targetId) const {
    if (targetId == 0)
        return;

    for (TacticalSquad* squad : leader.getSquads()) {
        if (!squad || squad->isEmpty())
            continue;
        SquadOrder ord;
        ord.type = SquadOrderType::Engage;
        ord.targetId = targetId;
        squad->receiveOrder(ord);
    }
}

void MidBossTacticBase::issueIdleAll(PlatoonLeader& leader) const {
    for (TacticalSquad* squad : leader.getSquads()) {
        if (!squad || squad->isEmpty())
            continue;
        SquadOrder ord;
        ord.type = SquadOrderType::Idle;
        squad->receiveOrder(ord);
    }
}

void MidBossTacticBase::assignSquadsToPlayers(
    const Room& room, const PlatoonLeader& leader,
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
        else         centroid = leader.getPosition();

        for (int pi = 0; pi < numPlayers; ++pi) {
            float d = Vec3::distance(centroid,
                players[static_cast<size_t>(pi)]->getPosition());
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

void GoblinMidBossTactic::update(float dt, Room& room, PlatoonLeader& leader) {
    auto& squads = leader.getSquads();
    if (!initialSizesSet_) {
        initialSizesSet_ = true;
        for (auto* sq : squads)
            initialSquadSizes_.push_back(static_cast<int>(sq->getMembers().size()));
    }

    leader.removeDeadMembersFromSquads(room);

    if (leaderPhase_ == LeaderPhase::Cooldown) {
        tacticCooldown_ -= dt;
        if (tacticCooldown_ <= 0.f)
            enterPhase(tacticsUnlocked_ ? LeaderPhase::TacticalRetreat
                                         : LeaderPhase::BoxAdvance,
                       "전술 쿨타임 종료", leader);
    } else if (leaderPhase_ == LeaderPhase::Encircle) {
        if (phaseOrderIssued_ && allMembersArrived(room, leader)) {
            Player* encircleTarget = selectPrimaryTarget(room, leader);
            if (encircleTarget) {
                for (auto* sq : squads) {
                    if (sq->isEmpty()) continue;
                    SquadOrder ord;
                    ord.type = SquadOrderType::Engage;
                    ord.targetId = encircleTarget->getId();
                    sq->receiveOrder(ord);
                }
            }
            tacticCooldown_ = TACTIC_COOLDOWN_DURATION;
            enterPhase(LeaderPhase::Cooldown, "포위 완성 - Engage 후 쿨타임 진입", leader);
        }
    } else if (leaderPhase_ == LeaderPhase::DivideAndConquer) {
        updateDivideAndConquer(dt, room, leader);
    }

    Player* primary = selectPrimaryTarget(room, leader);

    if (!tacticsUnlocked_ && primary && checkTacticsConditions(leader)) {
        tacticsUnlocked_ = true;
        enterPhase(LeaderPhase::TacticalRetreat, "전술 활성화 - 공통 후퇴 시작", leader);
    }

    if (leaderPhase_ == LeaderPhase::BoxAdvance &&
        primary && allMembersArrived(room, leader)) {
        if (!tacticsUnlocked_) {
            enterPhase(LeaderPhase::Engage, "박스 대형 완성 - 일반 교전 전환", leader);
            for (auto* sq : squads) {
                if (sq->isEmpty()) continue;
                SquadOrder ord;
                ord.type = SquadOrderType::Engage;
                ord.targetId = primaryTargetId_;
                sq->receiveOrder(ord);
            }
        } else if (clusterPlayers(room, leader) == 1) {
            enterPhase(LeaderPhase::Encircle, "박스 대형 완성 - 플레이어 군집 포위", leader);
        } else {
            enterPhase(LeaderPhase::Vigilance, "박스 대형 완성 - 플레이어 분산 경계", leader);
        }
    }

    if (leaderPhase_ == LeaderPhase::Vigilance &&
        phaseOrderIssued_ && allMembersArrived(room, leader)) {
        auto clusters = buildPlayerClusters(room, leader);
        if (clusters.size() <= 1)
            enterPhase(LeaderPhase::Encircle, "경계 완료 - 플레이어 재집결 포위", leader);
        else
            enterPhase(LeaderPhase::DivideAndConquer, "경계 완료 - 각개격파 전환", leader);
    }

    bool leaderAtRetreat = Vec3::distance(leader.getPosition(), retreatTargetPos_) <= 1.5f;
    if (leaderPhase_ == LeaderPhase::TacticalRetreat &&
        phaseOrderIssued_ && allMembersArrived(room, leader) && leaderAtRetreat) {
        enterPhase(LeaderPhase::BoxAdvance, "후퇴 완료 - 박스 대형 전환", leader);
    }

    tacticTimer_ -= dt;
    if (tacticTimer_ <= 0.f) {
        tacticTimer_ = TACTIC_INTERVAL;
        evaluateTactics(room, leader);
    }

    if (leaderPhase_ == LeaderPhase::TacticalRetreat) {
        Vec3 toRetreat = retreatTargetPos_ - leader.getPosition();
        float d = toRetreat.length();
        if (d > 1.f) {
            toRetreat = toRetreat / d;
            leader.setPosition(leader.getPosition() +
                toRetreat * (leader.getLeaderMoveSpeed() * TACTICAL_SPEED_MULT) * dt);
            leader.setFacing(toRetreat);
        }
        return;
    }

    if (leaderPhase_ == LeaderPhase::BoxAdvance ||
        leaderPhase_ == LeaderPhase::Vigilance ||
        leaderPhase_ == LeaderPhase::DivideAndConquer) {
        if (primary) {
            Vec3 dir = primary->getPosition() - leader.getPosition();
            if (dir.length() > 0.1f)
                leader.setFacing(dir.normalized());
        }
        return;
    }

    if (primary) {
        float dist = Vec3::distance(leader.getPosition(), primary->getPosition());
        Vec3 toPlayer = (primary->getPosition() - leader.getPosition()).normalized();
        if (dist > BOSS_KEEP_DIST + BOSS_KEEP_TOL) {
            leader.setPosition(leader.getPosition() +
                               toPlayer * leader.getLeaderMoveSpeed() * dt);
            leader.setFacing(toPlayer);
        } else if (dist < BOSS_KEEP_DIST - BOSS_KEEP_TOL) {
            leader.setPosition(leader.getPosition() -
                               toPlayer * leader.getLeaderMoveSpeed() * dt);
            leader.setFacing(toPlayer);
        } else {
            leader.setFacing(toPlayer);
        }
    }
}

void GoblinMidBossTactic::enterPhase(LeaderPhase next, const char* reason,
                                     PlatoonLeader& leader) {
    Logger::get().log(leader.getName(), reason);
    leaderPhase_ = next;
    phaseOrderIssued_ = false;
    if (next != LeaderPhase::DivideAndConquer)
        divideTasks_.clear();
    if (next != LeaderPhase::Cooldown)
        tacticTimer_ = 0.f;
}

void GoblinMidBossTactic::enterTacticFailCooldown(Room& room,
                                                  PlatoonLeader& leader,
                                                  const char* reason) {
    leader.removeDeadMembersFromSquads(room);

    tacticCooldown_ = TACTIC_FAIL_COOLDOWN_DURATION;
    enterPhase(LeaderPhase::Cooldown, reason, leader);

    std::vector<TacticalSquad*> liveSquads;
    for (auto* sq : leader.getSquads()) {
        if (!sq->isEmpty()) liveSquads.push_back(sq);
    }

    Player* primary = selectPrimaryTarget(room, leader);
    if (!primary || liveSquads.empty()) return;

    std::vector<uint32_t> targets(liveSquads.size(), primary->getId());
    assignSquadsToPlayers(room, leader, liveSquads, targets);
    for (size_t i = 0; i < liveSquads.size(); ++i) {
        SquadOrder ord;
        ord.type = SquadOrderType::Engage;
        ord.targetId = targets[i];
        liveSquads[i]->receiveOrder(ord);
    }
}

void GoblinMidBossTactic::evaluateTactics(Room& room, PlatoonLeader& leader) {
    leader.removeDeadMembersFromSquads(room);

    std::vector<TacticalSquad*> liveSquads;
    for (auto* sq : leader.getSquads())
        if (!sq->isEmpty()) liveSquads.push_back(sq);

    Player* primary = selectPrimaryTarget(room, leader);

    if (!primary || liveSquads.empty()) {
        for (auto* sq : liveSquads) {
            SquadOrder ord; ord.type = SquadOrderType::Idle;
            sq->receiveOrder(ord);
        }
        if (leader.getState() != TacticalNpcState::Idle) {
            leader.setTacticalTarget(0);
            leader.transitionTacticalState(TacticalNpcState::Idle, "플레이어 없음");
        }
        return;
    }

    if (leader.getTargetId() != primary->getId()) {
        leader.setTacticalTarget(primary->getId());
        if (leader.getState() == TacticalNpcState::Idle) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "전술 평가: 대상 %s",
                primary->getName().c_str());
            leader.transitionTacticalState(TacticalNpcState::Chase, buf);
        }
    }

    int numSquads = static_cast<int>(liveSquads.size());
    primaryTargetId_ = primary->getId();

    if (leaderPhase_ == LeaderPhase::TacticalRetreat) {
        if (phaseOrderIssued_) return;

        Vec3 playerCent = calcPlayerCentroid(room, leader.getPosition());
        Vec3 awayDir = leader.getPosition() - playerCent;
        float awayLen = awayDir.length();
        if (awayLen > 0.01f) awayDir = awayDir / awayLen;
        else                 awayDir = Vec3{ -1.f, 0.f, 0.f };

        retreatTargetPos_ = playerCent + awayDir * REGROUP_DIST;

        for (int i = 0; i < numSquads; ++i) {
            SquadOrder ord;
            ord.type = SquadOrderType::RetreatFormUp;
            ord.targetId = primaryTargetId_;
            ord.leaderPos = leader.getPosition();
            ord.tacticCenter = retreatTargetPos_;
            ord.formationTargetPos = playerCent;
            liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);
        }
        phaseOrderIssued_ = true;
        return;
    }

    if (leaderPhase_ == LeaderPhase::BoxAdvance) {
        if (phaseOrderIssued_) return;

        boxAdvanceTargetPos_ = calcPlayerCentroid(room, leader.getPosition());

        Vec3 toTgt = boxAdvanceTargetPos_ - leader.getPosition();
        float len = toTgt.length();
        Vec3 fwd = (len > 0.01f) ? (toTgt / len) : Vec3{ 1.f, 0.f, 0.f };
        Vec3 right{ -fwd.z, 0.f, fwd.x };
        Vec3 boxCenter = leader.getPosition() + fwd * BOX_FRONT_OFFSET;

        std::vector<std::pair<float, TacticalSquad*>> sqByLat;
        sqByLat.reserve(static_cast<size_t>(numSquads));
        for (auto* sq : liveSquads) {
            Vec3 sum{}; int cnt = 0;
            for (uint32_t mid : sq->getMembers()) {
                Actor* ma = room.findActorById(mid);
                if (ma && ma->isAlive()) { sum += ma->getPosition(); ++cnt; }
            }
            Vec3 cen = (cnt > 0) ? (sum / static_cast<float>(cnt))
                                 : leader.getPosition();
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
            ord.type = SquadOrderType::BoxAdvance;
            ord.targetId = primaryTargetId_;
            ord.sectorPos = offsets[static_cast<size_t>(i)];
            ord.leaderPos = leader.getPosition();
            ord.tacticCenter = boxCenter;
            ord.formationTargetPos = boxAdvanceTargetPos_;
            sqByLat[static_cast<size_t>(i)].second->receiveOrder(ord);
        }
        phaseOrderIssued_ = true;
        return;
    }

    if (leaderPhase_ == LeaderPhase::Vigilance) {
        if (phaseOrderIssued_) return;

        Vec3 playerCent = calcPlayerCentroid(room, leader.getPosition());
        Vec3 toPlayers = playerCent - leader.getPosition();
        float len = toPlayers.length();
        Vec3 forward = (len > 0.01f) ? (toPlayers / len) : Vec3{ 1.f, 0.f, 0.f };
        float baseAngle = std::atan2f(forward.z, forward.x);
        constexpr float TWO_PI = 2.f * 3.14159265f;

        for (int i = 0; i < numSquads; ++i) {
            SquadOrder ord;
            ord.type = SquadOrderType::GuardBoss;
            ord.targetId = primaryTargetId_;
            ord.sectorAngle = baseAngle + TWO_PI * static_cast<float>(i)
                                           / static_cast<float>(numSquads);
            ord.approachRadius = VIGILANCE_GUARD_RADIUS;
            ord.tacticCenter = leader.getPosition();
            ord.formationTargetPos = playerCent;
            liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);
        }
        phaseOrderIssued_ = true;
        return;
    }

    if (leaderPhase_ == LeaderPhase::DivideAndConquer) {
        if (phaseOrderIssued_) return;

        auto clusters = buildPlayerClusters(room, leader);
        if (clusters.size() <= 1) {
            enterPhase(LeaderPhase::Encircle, "각개격파 취소 - 플레이어 재집결", leader);
            return;
        }

        issueDivideAndConquer(room, leader, liveSquads, clusters);
        phaseOrderIssued_ = true;
        return;
    }

    if (leaderPhase_ == LeaderPhase::Encircle) {
        if (phaseOrderIssued_) return;

        constexpr float TWO_PI = 2.f * 3.14159265f;
        Vec3 encircleCenter = calcPlayerCentroid(room, leader.getPosition());
        int totalMembers = 0;
        for (auto* sq : liveSquads)
            totalMembers += static_cast<int>(sq->getMembers().size());
        if (totalMembers < 1) totalMembers = 1;

        float angleAccum = 0.f;
        for (int i = 0; i < numSquads; ++i) {
            int memberCount = static_cast<int>(
                liveSquads[static_cast<size_t>(i)]->getMembers().size());
            float fraction = static_cast<float>(memberCount) /
                             static_cast<float>(totalMembers);
            float sectorSpan = TWO_PI * fraction;
            float sectorAngle = angleAccum + sectorSpan * 0.5f;

            SquadOrder ord;
            ord.type = SquadOrderType::Encircle;
            ord.targetId = primary->getId();
            ord.sectorAngle = sectorAngle;
            ord.sectorSpan = sectorSpan;
            ord.approachRadius = ENCIRCLE_RADIUS;
            ord.tacticCenter = encircleCenter;
            liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);

            angleAccum += sectorSpan;
        }
        phaseOrderIssued_ = true;
        return;
    }

    std::vector<uint32_t> targets(static_cast<size_t>(numSquads), primary->getId());
    assignSquadsToPlayers(room, leader, liveSquads, targets);
    for (int i = 0; i < numSquads; ++i) {
        SquadOrder ord;
        ord.type = SquadOrderType::Engage;
        ord.targetId = targets[static_cast<size_t>(i)];
        liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);
    }
}

bool GoblinMidBossTactic::checkTacticsConditions(const PlatoonLeader& leader) const {
    if (leader.getMaxHp() > 0.f &&
        leader.getHp() / leader.getMaxHp() <= TACTIC_HP_THRESHOLD)
        return true;

    const auto& squads = leader.getSquads();
    for (size_t i = 0; i < squads.size(); ++i) {
        int initial = (i < initialSquadSizes_.size()) ? initialSquadSizes_[i] : 0;
        int current = static_cast<int>(squads[i]->getMembers().size());
        if (initial > 0 &&
            static_cast<float>(current) / static_cast<float>(initial) <=
                TACTIC_SQUAD_RATIO)
            return true;
    }
    return false;
}

int GoblinMidBossTactic::clusterPlayers(const Room& room,
                                        const PlatoonLeader& leader) const {
    return static_cast<int>(buildPlayerClusters(room, leader).size());
}

std::vector<GoblinMidBossTactic::PlayerCluster>
GoblinMidBossTactic::buildPlayerClusters(const Room& room,
                                         const PlatoonLeader& leader) const {
    std::vector<PlayerCluster> result =
        MidBossTacticBase::buildPlayerClusters(room, CLUSTER_RADIUS);

    for (PlayerCluster& cluster : result) {
        cluster.representativeId = 0;
        cluster.score = -1.f;
        for (uint32_t id : cluster.playerIds) {
            const auto* p = dynamic_cast<const Player*>(room.findActorById(id));
            if (!p || !p->isAlive())
                continue;

            float score = evaluatePlayerScore(p, leader);
            if (cluster.representativeId == 0 || score > cluster.score) {
                cluster.representativeId = p->getId();
                cluster.score = score;
            }
        }
    }

    return result;
}

Player* GoblinMidBossTactic::selectPrimaryTarget(Room& room,
                                                 const PlatoonLeader& leader) const {
    Player* best = nullptr;
    float bestScore = -1.f;
    for (Player* p : room.getLivingPlayers()) {
        float s = evaluatePlayerScore(p, leader);
        if (s > bestScore) { bestScore = s; best = p; }
    }
    return best;
}

uint32_t GoblinMidBossTactic::selectReplacementTarget(
    Room& room, const PlatoonLeader& leader,
    const std::vector<uint32_t>& playerIds) const
{
    Player* best = nullptr;
    float bestScore = -1.f;
    for (uint32_t id : playerIds) {
        auto* p = dynamic_cast<Player*>(room.findActorById(id));
        if (!p || !p->isAlive()) continue;
        float score = evaluatePlayerScore(p, leader);
        if (score > bestScore) {
            bestScore = score;
            best = p;
        }
    }
    return best ? best->getId() : 0;
}

void GoblinMidBossTactic::issueDivideAndConquer(
    Room& room, PlatoonLeader& leader,
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
    charge.type = SquadOrderType::WedgeCharge;
    charge.targetId = chargeCluster.representativeId;
    charge.targetIds = chargeCluster.playerIds;
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
        screen.type = SquadOrderType::GuardBoss;
        screen.targetId = (supportTargetId != 0) ? supportTargetId
                                                 : chargeCluster.representativeId;
        screen.slotSpacingScale = SCREEN_SLOT_SPACING_SCALE;
        screen.slotColumnScale = SCREEN_SLOT_COLUMN_SCALE;
        screen.slotColumnCount = SCREEN_SLOT_COLUMN_COUNT;
        screen.sectorAngle = baseAngle + (sideSign < 0.f ? 3.14159265f : 0.f);
        screen.approachRadius = SCREEN_BLOCK_SPACING *
                                (1.f + 0.5f * static_cast<float>(screenIdx / 2));
        screen.tacticCenter = blockCenter;
        screen.formationTargetPos = supportCentroid;
        liveSquads[static_cast<size_t>(i)]->receiveOrder(screen);
        divideTasks_.push_back({ liveSquads[static_cast<size_t>(i)],
                                 DivideTaskType::Screen,
                                 screen.targetId,
                                 supportPlayerIds });
        ++screenIdx;
    }
}

void GoblinMidBossTactic::updateDivideAndConquer(float dt, Room& room,
                                                 PlatoonLeader& leader) {
    if (!phaseOrderIssued_) return;
    if (divideTasks_.empty()) {
        enterTacticFailCooldown(room, leader, "각개격파 실패 - 배정 없음");
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

            uint32_t targetId = selectReplacementTarget(room, leader,
                                                        task.clusterPlayerIds);
            if (targetId != 0 && task.squad && !task.squad->isEmpty()) {
                SquadOrder ord;
                ord.type = SquadOrderType::Engage;
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
                    uint32_t replacement = selectReplacementTarget(room, leader,
                        task.clusterPlayerIds);
                    task.targetId = replacement;
                    if (replacement != 0 && task.squad && !task.squad->isEmpty()) {
                        SquadOrder ord;
                        ord.type = SquadOrderType::Engage;
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
        enterPhase(LeaderPhase::Cooldown, "각개격파 완료 - 쿨타임 진입", leader);
    }
}

float GoblinMidBossTactic::evaluatePlayerScore(
    const Player* p, const PlatoonLeader& leader) const {
    float dist = Vec3::distance(leader.getPosition(), p->getPosition());
    float distScore = 1.f / (1.f + dist);
    float hpScore = 1.f - (p->getHp() / p->getMaxHp());
    return distScore * 0.5f + hpScore * 0.5f;
}

bool GoblinMidBossTactic::allMembersArrived(
    const Room& room, const PlatoonLeader& leader) const {
    for (auto* sq : leader.getSquads()) {
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

std::vector<Vec3> GoblinMidBossTactic::calcSquadBoxOffsets(int numSquads) const {
    int rows = static_cast<int>(std::max(1.f,
        std::floorf(std::sqrtf(static_cast<float>(numSquads)))));
    int cols = (numSquads + rows - 1) / rows;

    std::vector<Vec3> offsets;
    offsets.reserve(static_cast<size_t>(numSquads));
    for (int i = 0; i < numSquads; ++i) {
        int col = i % cols;
        int row = i / cols;
        float colOff = (static_cast<float>(col) -
                        static_cast<float>(cols - 1) * 0.5f) * BOX_SQUAD_SPACING;
        float rowOff = (static_cast<float>(row) -
                        static_cast<float>(rows - 1) * 0.5f) * BOX_SQUAD_SPACING;
        float halfCols = static_cast<float>(cols - 1) * 0.5f;
        float latFrac = (cols > 1)
            ? std::abs(static_cast<float>(col) - halfCols) / halfCols
            : 0.f;
        float arcZ = rowOff - BOX_ARC_DEPTH * latFrac;
        offsets.push_back(Vec3{ colOff, 0.f, arcZ });
    }
    return offsets;
}

GrandBaumMidBossTactic::GrandBaumMidBossTactic(float grandBaumA)
    : grandBaumA_(grandBaumA)
{}

void GrandBaumMidBossTactic::update(float dt, Room& room, PlatoonLeader& leader) {
    leader.removeDeadMembersFromSquads(room);

    const auto& squads = leader.getSquads();
    if (squads.empty())
        return;

    uint32_t targetId = selectNearestPlayerId(room, leader.getPosition());
    Actor* target = targetId != 0 ? room.findActorById(targetId) : nullptr;
    if (target) {
        Vec3 dir = target->getPosition() - leader.getPosition();
        if (dir.length() > 0.1f)
            leader.setFacing(dir.normalized());
    }

    float hpRatio = (leader.getMaxHp() > 0.f)
        ? leader.getHp() / leader.getMaxHp()
        : 1.f;

    if (phase_ == Phase::Cooldown) {
        tacticCooldown_ -= dt;

        engageRefreshTimer_ -= dt;
        if (engageRefreshTimer_ <= 0.f) {
            engageRefreshTimer_ = ENGAGE_REFRESH_INTERVAL;
            issueEngage(room, leader);
        }

        if (tacticCooldown_ > 0.f)
            return;

        enterPhase(Phase::Engage, "GrandBaum tactic cooldown finished", leader);
    }

    if (phase_ == Phase::Engage) {
        if (!engageOrderIssued_) {
            issueEngage(room, leader);
            engageRefreshTimer_ = ENGAGE_REFRESH_INTERVAL;
            engageOrderIssued_ = true;
            return;
        }

        engageRefreshTimer_ -= dt;
        if (engageRefreshTimer_ <= 0.f) {
            engageRefreshTimer_ = ENGAGE_REFRESH_INTERVAL;
            issueEngage(room, leader);
        }

        if (hpRatio <= grandBaumA_) {
            enterPhase(Phase::ShieldWall, "GrandBaum ShieldWall activated", leader);
            applyShieldWallProtection(room, leader, true);
            issueShieldWall(room, leader);
        }
    }

    if (phase_ == Phase::ShieldWall) {
        applyShieldWallProtection(room, leader, true);

        orderRefreshTimer_ -= dt;
        if (orderRefreshTimer_ <= 0.f) {
            orderRefreshTimer_ = ORDER_REFRESH_INTERVAL;
            issueShieldWall(room, leader);
        }

        TacticalSquad* ambushSquad = squads.size() >= 4 ? squads[3] : nullptr;
        if (isAmbushSquadAnnihilated(ambushSquad)) {
            applyShieldWallProtection(room, leader, false);
            issueEngage(room, leader);
            tacticCooldown_ = TACTIC_COOLDOWN_DURATION;
            enterPhase(Phase::Cooldown, "GrandBaum ShieldWall finished - ambush squad annihilated", leader);
            return;
        }

        updateAmbush(dt, room, leader, ambushSquad);
    }
}

void GrandBaumMidBossTactic::onLeaderDead(Room& room, PlatoonLeader& leader) {
    applyShieldWallProtection(room, leader, false);
    MidBossTacticBase::onLeaderDead(room, leader);
}

void GrandBaumMidBossTactic::enterPhase(Phase next, const char* reason,
                                        PlatoonLeader& leader) {
    Logger::get().log(leader.getName(), reason);
    phase_ = next;

    if (next == Phase::Engage) {
        engageOrderIssued_ = false;
        engageRefreshTimer_ = 0.f;
        return;
    }

    if (next == Phase::ShieldWall) {
        orderRefreshTimer_ = ORDER_REFRESH_INTERVAL;
        ambushPrepTimer_ = 0.f;
        ambushEngageIssued_ = false;
        ambushStage_ = AmbushStage::WideFlank;
        return;
    }

    if (next == Phase::Cooldown) {
        engageRefreshTimer_ = ENGAGE_REFRESH_INTERVAL;
        ambushStage_ = AmbushStage::WideFlank;
    }
}

void GrandBaumMidBossTactic::issueEngage(Room& room, PlatoonLeader& leader) {
    uint32_t targetId = selectNearestPlayerId(room, leader.getPosition());

    if (targetId == 0) {
        issueIdleAll(leader);
        leader.setTacticalTarget(0);
        leader.transitionTacticalState(TacticalNpcState::Idle, "GrandBaum no player target");
        return;
    }

    leader.setTacticalTarget(targetId);
    if (leader.getState() == TacticalNpcState::Idle)
        leader.transitionTacticalState(TacticalNpcState::Chase, "GrandBaum Engage");

    std::vector<TacticalSquad*> liveSquads = collectLiveSquads(leader);
    std::vector<uint32_t> targets(liveSquads.size(), targetId);
    assignSquadsToPlayers(room, leader, liveSquads, targets);

    for (size_t i = 0; i < liveSquads.size(); ++i) {
        SquadOrder ord;
        ord.type = SquadOrderType::Engage;
        ord.targetId = targets[i];
        liveSquads[i]->receiveOrder(ord);
    }
}

void GrandBaumMidBossTactic::issueShieldWall(Room& room, PlatoonLeader& leader) {
    const auto& squads = leader.getSquads();
    uint32_t targetId = selectNearestPlayerId(room, leader.getPosition());

    Vec3 leaderPos = leader.getPosition();
    Vec3 playerCentroid = calcPlayerCentroid(room, leaderPos);
    Vec3 forward = playerCentroid - leaderPos;
    if (forward.lengthSq() > 0.01f)
        forward = forward.normalized();
    else
        forward = Vec3{ 1.f, 0.f, 0.f };

    std::vector<TacticalSquad*> slimeSquads;
    int totalSlimeMembers = 0;
    const size_t slimeIndices[] = { 0, 1, 2 };
    for (size_t idx : slimeIndices) {
        if (idx >= squads.size())
            continue;
        TacticalSquad* squad = squads[idx];
        if (!squad || squad->isEmpty())
            continue;
        slimeSquads.push_back(squad);
        totalSlimeMembers += static_cast<int>(squad->getMembers().size());
    }

    if (!slimeSquads.empty() && totalSlimeMembers > 0) {
        constexpr float TWO_PI = 2.f * 3.14159265f;
        float angleAccum = std::atan2f(forward.z, forward.x) - 3.14159265f;
        for (TacticalSquad* squad : slimeSquads) {
            float fraction = static_cast<float>(squad->getMembers().size()) /
                             static_cast<float>(totalSlimeMembers);
            float sectorSpan = TWO_PI * fraction;

            SquadOrder ord;
            ord.type = SquadOrderType::RingGuard;
            ord.targetId = targetId;
            ord.tacticCenter = leaderPos;
            ord.sectorAngle = angleAccum + sectorSpan * 0.5f;
            ord.sectorSpan = sectorSpan;
            ord.approachRadius = SHIELD_RING_RADIUS;
            squad->receiveOrder(ord);

            angleAccum += sectorSpan;
        }
    }

    if (!ambushEngageIssued_ && squads.size() >= 4) {
        Vec3 fallbackRear = playerCentroid - leaderPos;
        Vec3 avgFacing = calcAveragePlayerFacing(room, Vec3{});
        Vec3 rearDir = (avgFacing.lengthSq() > 0.01f)
            ? avgFacing * -1.f
            : fallbackRear;
        if (rearDir.lengthSq() > 0.01f)
            rearDir = rearDir.normalized();
        else
            rearDir = forward;

        Vec3 sideDir{ -rearDir.z, 0.f, rearDir.x };
        TacticalSquad* ambushSquad = squads[3];
        Vec3 ambushCentroid = ambushSquad && !ambushSquad->isEmpty()
            ? ambushSquad->calcCentroid(room)
            : leaderPos;
        float sideSign = ((ambushCentroid - playerCentroid).dot(sideDir) >= 0.f)
            ? 1.f : -1.f;

        Vec3 ambushCenter = playerCentroid + rearDir * AMBUSH_REAR_DIST;
        if (ambushStage_ == AmbushStage::WideFlank) {
            ambushCenter = playerCentroid +
                rearDir * AMBUSH_WIDE_REAR_DIST +
                sideDir * (sideSign * AMBUSH_WIDE_SIDE_DIST);
        }

        if (ambushSquad && !ambushSquad->isEmpty()) {
            SquadOrder ord;
            ord.type = SquadOrderType::FormationHold;
            ord.targetId = targetId;
            ord.tacticCenter = ambushCenter;
            ord.formationTargetPos = playerCentroid;
            ord.slotSpacingScale = 0.75f;
            ord.slotColumnScale = 2.0f;
            ambushSquad->receiveOrder(ord);
        }
    }
}

void GrandBaumMidBossTactic::updateAmbush(float dt, Room& room,
                                          PlatoonLeader& leader,
                                          TacticalSquad* ambushSquad) {
    if (!ambushSquad || ambushSquad->isEmpty() || ambushEngageIssued_)
        return;

    ambushPrepTimer_ += dt;
    if (ambushStage_ == AmbushStage::WideFlank) {
        if (!ambushSquad->areMembersAtSlots(room) &&
            ambushPrepTimer_ < AMBUSH_WIDE_MAX_PREP_TIME)
            return;

        ambushStage_ = AmbushStage::RearApproach;
        ambushPrepTimer_ = 0.f;
        orderRefreshTimer_ = 0.f;
        Logger::get().log(leader.getName(), "GrandBaum ambush squad reached wide flank");
        return;
    }

    if (!ambushSquad->areMembersAtSlots(room) &&
        ambushPrepTimer_ < AMBUSH_MAX_PREP_TIME)
        return;

    uint32_t targetId = selectAmbushTarget(room, leader, ambushSquad);
    if (targetId == 0)
        return;

    SquadOrder ord;
    ord.type = SquadOrderType::Engage;
    ord.targetId = targetId;
    ambushSquad->receiveOrder(ord);

    ambushEngageIssued_ = true;
    ambushStage_ = AmbushStage::Engaged;
    Logger::get().log(leader.getName(), "GrandBaum ambush squad engaged");
}

bool GrandBaumMidBossTactic::isAmbushSquadAnnihilated(
    TacticalSquad* ambushSquad) const {
    return !ambushSquad || ambushSquad->isEmpty();
}

void GrandBaumMidBossTactic::applyShieldWallProtection(
    Room& room, PlatoonLeader& leader, bool enabled) {
    float multiplier = enabled ? SHIELDWALL_DAMAGE_MULT : 1.f;
    leader.setDamageTakenMultiplier(multiplier);

    const auto& squads = leader.getSquads();
    const size_t slimeIndices[] = { 0, 1, 2 };
    for (size_t idx : slimeIndices) {
        if (idx >= squads.size())
            continue;
        TacticalSquad* squad = squads[idx];
        if (!squad)
            continue;

        for (uint32_t memberId : squad->getMembers()) {
            Actor* actor = room.findActorById(memberId);
            if (actor)
                actor->setDamageTakenMultiplier(multiplier);
        }
    }
}

uint32_t GrandBaumMidBossTactic::selectAmbushTarget(
    Room& room, const PlatoonLeader& leader, TacticalSquad* ambushSquad) const {
    Vec3 ambushOrigin = leader.getPosition();
    if (ambushSquad && !ambushSquad->isEmpty())
        ambushOrigin = ambushSquad->calcCentroid(room);

    std::vector<PlayerCluster> clusters =
        buildPlayerClusters(room, AMBUSH_CLUSTER_RADIUS);
    if (clusters.empty())
        return selectNearestPlayerId(room, ambushOrigin);

    auto ratioScore = [](float value, float scale) {
        if (value <= 0.f)
            return 0.f;
        return value / (value + scale);
    };

    int bestClusterIdx = 0;
    float bestClusterScore = -1.f;
    for (int i = 0; i < static_cast<int>(clusters.size()); ++i) {
        const PlayerCluster& cluster = clusters[static_cast<size_t>(i)];

        float leaderDist = Vec3::distance(leader.getPosition(), cluster.centroid);
        float distanceFromLeaderScore = ratioScore(leaderDist, 40.f);

        float isolationScore = 0.5f;
        if (clusters.size() > 1) {
            float nearestOtherDist = -1.f;
            for (int j = 0; j < static_cast<int>(clusters.size()); ++j) {
                if (i == j)
                    continue;
                float d = Vec3::distance(cluster.centroid,
                    clusters[static_cast<size_t>(j)].centroid);
                if (nearestOtherDist < 0.f || d < nearestOtherDist)
                    nearestOtherDist = d;
            }
            isolationScore = ratioScore(nearestOtherDist, AMBUSH_CLUSTER_RADIUS);
        }

        float memberCount = static_cast<float>(
            std::max<size_t>(cluster.playerIds.size(), 1u));
        float smallClusterScore = 1.f / memberCount;

        float clusterScore =
            0.45f * distanceFromLeaderScore +
            0.35f * isolationScore +
            0.20f * smallClusterScore;

        if (clusterScore > bestClusterScore) {
            bestClusterScore = clusterScore;
            bestClusterIdx = i;
        }
    }

    const PlayerCluster& targetCluster = clusters[static_cast<size_t>(bestClusterIdx)];
    Vec3 fallbackRear = targetCluster.centroid - leader.getPosition();
    Vec3 rearDir = calcAveragePlayerFacing(room, Vec3{});
    rearDir = (rearDir.lengthSq() > 0.01f) ? rearDir * -1.f : fallbackRear;
    if (rearDir.lengthSq() > 0.01f)
        rearDir = rearDir.normalized();
    else
        rearDir = Vec3{ 1.f, 0.f, 0.f };

    uint32_t bestTargetId = 0;
    float bestTargetScore = -1.f;
    for (uint32_t id : targetCluster.playerIds) {
        auto* player = dynamic_cast<Player*>(room.findActorById(id));
        if (!player || !player->isAlive())
            continue;

        float distFromAmbush = Vec3::distance(ambushOrigin, player->getPosition());
        float distanceFromAmbushScore = 1.f / (1.f + distFromAmbush * 0.05f);

        float lowHpScore = 0.f;
        if (player->getMaxHp() > 0.f)
            lowHpScore = std::clamp(1.f - player->getHp() / player->getMaxHp(),
                                    0.f, 1.f);

        Vec3 toAmbush = ambushOrigin - player->getPosition();
        float backFacingScore = 0.5f;
        if (toAmbush.lengthSq() > 0.01f && player->getFacing().lengthSq() > 0.01f) {
            Vec3 toAmbushDir = toAmbush.normalized();
            Vec3 playerFacing = player->getFacing().normalized();
            float facingDot = std::clamp(playerFacing.dot(toAmbushDir), -1.f, 1.f);
            backFacingScore = (1.f - facingDot) * 0.5f;
        }

        float rearProjection = (player->getPosition() - targetCluster.centroid)
            .dot(rearDir);
        float rearPositionScore = ratioScore(std::max(rearProjection, 0.f), 10.f);

        float targetScore =
            0.35f * distanceFromAmbushScore +
            0.30f * lowHpScore +
            0.25f * backFacingScore +
            0.10f * rearPositionScore;

        if (targetScore > bestTargetScore) {
            bestTargetScore = targetScore;
            bestTargetId = player->getId();
        }
    }

    if (bestTargetId != 0)
        return bestTargetId;

    return selectNearestPlayerId(room, ambushOrigin);
}

} // namespace sim
