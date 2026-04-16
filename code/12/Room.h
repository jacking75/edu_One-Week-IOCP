#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include <mutex>
#include <atomic>
#include <vector>
#include "Session.h"
#include "Protocol.h"

struct RoomConfig {
    uint32_t maxPlayers = 2;
    uint32_t minPlayers = 2;
    bool isPrivate = false;
    std::string password;
};

class Room : public std::enable_shared_from_this<Room> {
protected:
    uint64_t roomId_;
    std::string roomName_;
    std::atomic<RoomState> state_;
    RoomConfig config_;

    std::unordered_map<uint64_t, std::shared_ptr<Session>> players_;
    std::mutex playersMutex_;

    uint64_t hostSessionId_;
    int assignedThreadId_;

public:
    Room(uint64_t id, const std::string& name, const RoomConfig& config, int threadId)
        : roomId_(id), roomName_(name), state_(RoomState::Waiting)
        , config_(config), hostSessionId_(0), assignedThreadId_(threadId) {}

    virtual ~Room() = default;

    uint64_t GetRoomId() const { return roomId_; }
    std::string GetRoomName() const { return roomName_; }
    RoomState GetState() const { return state_.load(std::memory_order_acquire); }
    int GetPlayerCount() const;
    bool CanJoin() const;

    bool AddPlayer(std::shared_ptr<Session> pSession);
    void RemovePlayer(uint64_t sessionId);
    void KickAllPlayers();

    void BroadcastPacket(const char* pData, int size, uint64_t exceptSessionId = 0);

    virtual void StartGame();
    virtual void EndGame();

    // Helper: get player list as vector
    std::vector<std::shared_ptr<Session>> GetPlayers();
    std::shared_ptr<Session> FindPlayerSession(uint64_t sessionId);

private:
    void NotifyPlayerJoined(uint64_t sessionId);
    void NotifyPlayerLeft(uint64_t sessionId);
    void SendRoomStateToPlayer(std::shared_ptr<Session> pSession);
};
