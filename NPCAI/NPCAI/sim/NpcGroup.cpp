#include "NpcGroup.hpp"
#include <algorithm>

namespace sim {

// ─── 생성자 ───────────────────────────────────────────────────────────────────

NpcGroup::NpcGroup(int groupId, const Vec3& center, float radius,
                   uint32_t memoryDurationTick)
    : groupId_(groupId)
    , activityCenter_(center)
    , activityRadius_(radius)
    , memoryDurationTick_(memoryDurationTick)
{}

// ─── 멤버 관리 ────────────────────────────────────────────────────────────────

void NpcGroup::addMember(uint32_t npcId) {
    members_.push_back(npcId);
}

void NpcGroup::removeMember(uint32_t npcId) {
    auto it = std::find(members_.begin(), members_.end(), npcId);
    if (it != members_.end()) members_.erase(it);
}

// ─── reportSight ──────────────────────────────────────────────────────────────

void NpcGroup::reportSight(uint32_t npcId, uint32_t playerId,
                            const Vec3& pos, uint32_t currentTick) {
    // 해당 플레이어의 기존 슬롯 탐색 (유효/만료 무관)
    int slot = -1;
    for (int i = 0; i < MaxPlayerCount; ++i) {
        if (memories_[i].valid && memories_[i].playerId == playerId) {
            slot = i;
            break;
        }
    }
    // 빈 슬롯 확보
    if (slot == -1) {
        for (int i = 0; i < MaxPlayerCount; ++i) {
            if (!memories_[i].valid) { slot = i; break; }
        }
    }
    if (slot == -1) return;  // 슬롯 부족 (플레이어 4명 초과 시 발생하지 않음)

    auto& m             = memories_[slot];
    m.playerId          = playerId;
    m.reporterNpcId     = npcId;
    m.lastKnownPosition = pos;
    m.lastSeenTick      = currentTick;
    m.expireTick        = currentTick + memoryDurationTick_;
    m.valid             = true;
}

// ─── hasValidMemory ───────────────────────────────────────────────────────────

bool NpcGroup::hasValidMemory(uint32_t currentTick) const {
    for (const auto& m : memories_) {
        if (m.valid && currentTick <= m.expireTick) return true;
    }
    return false;
}

// ─── getBestMemory ────────────────────────────────────────────────────────────

const SharedTargetMemory* NpcGroup::getBestMemory(uint32_t currentTick) const {
    const SharedTargetMemory* best = nullptr;
    for (const auto& m : memories_) {
        if (!m.valid || currentTick > m.expireTick) continue;
        if (!best || m.lastSeenTick > best->lastSeenTick)
            best = &m;
    }
    return best;
}

// ─── isInsideActivityArea ─────────────────────────────────────────────────────

bool NpcGroup::isInsideActivityArea(const Vec3& pos) const {
    return Vec3::distance(pos, activityCenter_) <= activityRadius_;
}

// ─── clearMemory ─────────────────────────────────────────────────────────────

void NpcGroup::clearMemory() {
    for (auto& m : memories_) m = SharedTargetMemory{};
}

// ─── update ──────────────────────────────────────────────────────────────────

void NpcGroup::update(uint32_t currentTick) {
    for (auto& m : memories_) {
        if (m.valid && currentTick > m.expireTick)
            m = SharedTargetMemory{};
    }
}

} // namespace sim
