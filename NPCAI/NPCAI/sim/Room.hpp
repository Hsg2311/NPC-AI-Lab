#pragma once
#include "DummyPlayerController.hpp"
#include "DebugSnapshot.hpp"
#include "Squad.hpp"
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <vector>
#include <string>

namespace sim {

class Actor;
class Player;
class Npc;
struct NpcConfig;

class Room {
public:
    // roomId:       identifier for log output
    // dumpInterval: print snapshot every N ticks (0 = disabled)
    explicit Room(uint32_t roomId, uint32_t dumpInterval = 20);

    // Actor management
    void addActor(std::shared_ptr<Actor> actor);

    // Advance simulation by one tick
    void tick(float dt);

    // Queries (return raw non-owning pointers; Room owns lifetime)
    Actor* findActorById(uint32_t id) const;
    Player* findNearestLivingPlayer(const Vec3& from, float maxRange) const;

    // Access controller to register player routes before simulation
    DummyPlayerController& getDummyController() { return dummyCtrl_; }

    uint32_t getTickCount() const { return tickCount_; }

    // Print formatted snapshot to stdout
    void dumpSnapshot() const;

    // Build a rendering-ready snapshot (AI logic ↔ renderer boundary)
    DebugSnapshot buildSnapshot() const;

    // ── AI Query Helpers ────────────────────────────────────────────────────
    std::vector<Player*> getLivingPlayers() const;
    void findNearbyNpcPositions(const Vec3& from, float radius, uint32_t excludeId,
                                std::vector<Vec3>& out) const;
    int  countNpcsTargeting(uint32_t playerId) const;

    // ── Squad management ─────────────────────────────────────────────────────
    int    createSquad();
    void   addNpcToSquad(int squadId, uint32_t npcId, bool asLeader = false);
    void   spawnSquad(const std::string& namePrefix,
                      const std::vector<Vec3>& positions,
                      const NpcConfig& cfg);
    Squad* findSquadById(int id);
    Npc*   findNpcById(uint32_t id);

private:
    void updateSquads(float dt);

    uint32_t roomId_;
    uint32_t tickCount_{ 0 };
    uint32_t dumpInterval_;

    std::unordered_map<uint32_t, std::shared_ptr<Actor>> actors_{};
    DummyPlayerController dummyCtrl_{};

    std::vector<Squad> squads_;
    int                nextSquadId_{ 1 };
};

} // namespace sim
