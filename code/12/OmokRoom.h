#pragma once
#include "Room.h"
#include "OmokGame.h"
#include <memory>
#include <unordered_set>

class OmokRoom : public Room {
private:
    std::unique_ptr<OmokGame> pGame_;
    std::unordered_set<uint64_t> spectators_;
    std::mutex spectatorsMutex_;

public:
    OmokRoom(uint64_t id, const std::string& name,
            const RoomConfig& config, int threadId);

    // Room overrides
    void StartGame() override;
    void EndGame() override;

    // Omok game handling
    void OnPlaceStone(uint64_t playerId, uint8_t x, uint8_t y);
    void OnSurrender(uint64_t playerId);
    void OnPlayerDisconnected(uint64_t playerId);

    // Spectator management
    bool AddSpectator(std::shared_ptr<Session> pSession);
    void RemoveSpectator(uint64_t sessionId);
    int GetSpectatorCount() const;

    void BroadcastToAll(const char* pData, int size);

private:
    void SendGameStateToSession(std::shared_ptr<Session> pSession);
    void BroadcastTurnChanged();
    void BroadcastGameEnd();
};
