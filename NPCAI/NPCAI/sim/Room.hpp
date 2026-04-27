#pragma once
#include "DummyPlayerController.hpp"
#include "DebugSnapshot.hpp"
#include "NpcGroup.hpp"
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <vector>
#include <string>

namespace sim {

class Actor;
class Player;
class Npc;

class Room {
public:
    // roomId:       로그 출력용 식별자
    // dumpInterval: N 틱마다 스냅샷 출력 (0 = 비활성화)
    explicit Room(uint32_t roomId, uint32_t dumpInterval = 20);

    // Actor 관리
    void addActor(std::shared_ptr<Actor> actor);

    // 시뮬레이션을 한 틱 진행
    void tick(float dt);

    // 쿼리 (비소유 원시 포인터 반환; Room이 생존 기간 소유)
    Actor*  findActorById(uint32_t id) const;
    Player* findNearestLivingPlayer(const Vec3& from, float maxRange) const;

    // 시뮬레이션 전 플레이어 경로를 등록하는 컨트롤러 접근
    DummyPlayerController& getDummyController() { return dummyCtrl_; }

    uint32_t getTickCount() const { return tickCount_; }

    // 스냅샷을 포맷하여 stdout에 출력
    void dumpSnapshot() const;

    // 렌더링 준비 스냅샷 생성 (AI 로직 - 렌더러 경계)
    DebugSnapshot buildSnapshot() const;

    // ── AI 쿼리 헬퍼 ─────────────────────────────────────────────────────────
    const std::vector<Player*>& getLivingPlayers() const;
    void findNearbyNpcPositions(const Vec3& from, float radius, uint32_t excludeId,
                                std::vector<Vec3>& out) const;
    int  countNpcsTargeting(uint32_t playerId) const;

    // ── NpcGroup 관리 ─────────────────────────────────────────────────────────
    // 그룹 생성 후 Room이 소유; 반환된 포인터는 Room 생존 기간 동안 유효
    NpcGroup* createNpcGroup(const Vec3& center, float radius,
                              uint32_t memoryDurationTick = 180);
    NpcGroup* getNpcGroup(int groupId);

private:
    // ── 틱별 캐시 재구성 ──────────────────────────────────────────────────────
    void rebuildLivingPlayersCache(); // F: getLivingPlayers 반복 할당 제거
    void rebuildAggroCount();         // B: countNpcsTargeting O(1)화
    void rebuildSpatialGrid();        // C: findNearbyNpcPositions O(N²) → O(1) 수렴

    // 공간 분할 그리드 셀 키 — cx/cz 좌표를 int64_t 하나로 인코딩
    static int64_t gridKey(int cx, int cz);

    // ── 데이터 ────────────────────────────────────────────────────────────────
    uint32_t roomId_;
    uint32_t tickCount_{ 0 };
    uint32_t dumpInterval_;

    // A: actors_ 단일 맵 → Player/NPC 분리 (dynamic_cast 제거)
    std::unordered_map<uint32_t, std::shared_ptr<Player>> players_{};
    std::unordered_map<uint32_t, std::shared_ptr<Npc>>    npcs_{};

    // F: getLivingPlayers 틱당 1회 재구성 캐시
    std::vector<Player*> livingPlayersCache_{};

    // B: aggroCount_ 캐시 — playerId → 어그로 중인 NPC 수
    std::unordered_map<uint32_t, int> aggroCount_{};

    // C: 공간 분할 그리드 — 셀 키 → NPC id 목록
    static constexpr float GRID_CELL_SIZE = 6.f;
    std::unordered_map<int64_t, std::vector<uint32_t>> spatialGrid_{};

    DummyPlayerController dummyCtrl_{};
    std::vector<std::unique_ptr<NpcGroup>> npcGroups_{};
};

} // namespace sim
