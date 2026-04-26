#pragma once
#include "Vec3.hpp"
#include <array>
#include <vector>
#include <cstdint>

namespace sim {

static constexpr int MaxPlayerCount = 4;

// ─── SharedTargetMemory ──────────────────────────────────────────────────────
// 그룹 내 NPC 중 한 명이 보고한 플레이어 위치 기억.
// 플레이어 1명당 슬롯 1개 (playerId로 식별).
struct SharedTargetMemory {
    uint32_t playerId         = 0;   // 0 = 빈 슬롯
    uint32_t reporterNpcId    = 0;
    Vec3     lastKnownPosition{};
    uint32_t lastSeenTick     = 0;
    uint32_t expireTick       = 0;
    bool     valid            = false;
};

// ─── NpcGroup ────────────────────────────────────────────────────────────────
// 경량 시야 공유 그룹. 지휘 계층 없음, NPC에게 명령 없음.
// Room이 소유하고, NPC는 groupId를 통해 조회만 한다.
class NpcGroup {
public:
    // memoryDurationTick: 시야 기억 유지 틱 수 (기본 180 ≈ 3초 @ 60fps)
    NpcGroup(int groupId, const Vec3& center, float radius,
             uint32_t memoryDurationTick = 180);

    // ── 멤버 관리 ─────────────────────────────────────────────────────────────
    void addMember   (uint32_t npcId);
    void removeMember(uint32_t npcId);

    // ── 시야 공유: NPC가 플레이어를 감지하면 호출 ─────────────────────────────
    void reportSight(uint32_t npcId, uint32_t playerId,
                     const Vec3& pos, uint32_t currentTick);

    // ── 메모리 쿼리 ───────────────────────────────────────────────────────────
    bool                      hasValidMemory(uint32_t currentTick) const;
    const SharedTargetMemory* getBestMemory (uint32_t currentTick) const;

    // ── 활동 구역 경계 판정 ───────────────────────────────────────────────────
    bool isInsideActivityArea(const Vec3& pos) const;

    // ── 메모리 초기화 / 틱 갱신 (Room::tick에서 NPC 업데이트 전 호출) ─────────
    void clearMemory();
    void update(uint32_t currentTick);

    // ── 접근자 ────────────────────────────────────────────────────────────────
    int   getGroupId() const { return groupId_; }
    Vec3  getCenter () const { return activityCenter_; }
    float getRadius () const { return activityRadius_; }

private:
    int      groupId_;
    Vec3     activityCenter_;
    float    activityRadius_;
    uint32_t memoryDurationTick_;

    std::vector<uint32_t>                          members_;
    std::array<SharedTargetMemory, MaxPlayerCount> memories_{};
};

} // namespace sim
