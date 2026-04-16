#pragma once
#include <memory>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <vector>
#include "Room.h"

class RoomManager {
private:
    std::unordered_map<uint64_t, std::shared_ptr<Room>> rooms_;
    mutable std::shared_mutex roomsMutex_;

    std::atomic<uint64_t> nextRoomId_;
    int logicThreadCount_;

public:
    RoomManager(int logicThreadCount)
        : nextRoomId_(1)
        , logicThreadCount_(logicThreadCount) {}

    std::shared_ptr<Room> CreateRoom(const std::string& name, const RoomConfig& config);
    bool DestroyRoom(uint64_t roomId);
    std::shared_ptr<Room> FindRoom(uint64_t roomId);
    std::vector<std::shared_ptr<Room>> GetAvailableRooms();

    int GetRoomCount() const;
    int GetTotalPlayerCount() const;

    void PrintStatistics() const;
};
