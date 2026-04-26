#pragma once
#include "DummyPlayerController.hpp"
#include "DebugSnapshot.hpp"
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
    std::vector<Player*> getLivingPlayers() const;
    void findNearbyNpcPositions(const Vec3& from, float radius, uint32_t excludeId,
                                std::vector<Vec3>& out) const;
    int  countNpcsTargeting(uint32_t playerId) const;

private:
    uint32_t roomId_;
    uint32_t tickCount_{ 0 };
    uint32_t dumpInterval_;

    std::unordered_map<uint32_t, std::shared_ptr<Actor>> actors_{};
    DummyPlayerController dummyCtrl_{};
};

} // namespace sim
