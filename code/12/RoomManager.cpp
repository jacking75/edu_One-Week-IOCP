#include "RoomManager.h"
#include <iostream>

std::shared_ptr<OmokRoom> RoomManager::CreateOmokRoom(const std::string& name,
                                                        const RoomConfig& config) {
    uint64_t roomId = nextRoomId_.fetch_add(1, std::memory_order_relaxed);
    int threadId = static_cast<int>(roomId % logicThreadCount_);

    auto pRoom = std::make_shared<OmokRoom>(roomId, name, config, threadId);

    {
        std::unique_lock<std::shared_mutex> lock(roomsMutex_);
        rooms_[roomId] = pRoom;
    }

    std::cout << "[RoomManager] OmokRoom created: ID=" << roomId
              << ", Name=" << name << "\n";
    return pRoom;
}

bool RoomManager::DestroyRoom(uint64_t roomId) {
    std::shared_ptr<Room> pRoom;
    {
        std::unique_lock<std::shared_mutex> lock(roomsMutex_);
        auto it = rooms_.find(roomId);
        if (it == rooms_.end()) return false;
        pRoom = it->second;
        rooms_.erase(it);
    }
    pRoom->KickAllPlayers();
    std::cout << "[RoomManager] Room destroyed: ID=" << roomId << "\n";
    return true;
}

std::shared_ptr<Room> RoomManager::FindRoom(uint64_t roomId) {
    std::shared_lock<std::shared_mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it != rooms_.end()) return it->second;
    return nullptr;
}

std::vector<std::shared_ptr<Room>> RoomManager::GetAvailableRooms() {
    std::vector<std::shared_ptr<Room>> available;
    std::shared_lock<std::shared_mutex> lock(roomsMutex_);
    for (const auto& pair : rooms_) {
        if (pair.second->GetState() == RoomState::Waiting && pair.second->CanJoin())
            available.push_back(pair.second);
    }
    return available;
}

int RoomManager::GetRoomCount() const {
    std::shared_lock<std::shared_mutex> lock(roomsMutex_);
    return static_cast<int>(rooms_.size());
}

int RoomManager::GetTotalPlayerCount() const {
    std::shared_lock<std::shared_mutex> lock(roomsMutex_);
    int total = 0;
    for (const auto& pair : rooms_) total += pair.second->GetPlayerCount();
    return total;
}

void RoomManager::PrintStatistics() const {
    std::shared_lock<std::shared_mutex> lock(roomsMutex_);
    int totalPlayers = 0, waiting = 0, playing = 0, finished = 0;
    for (const auto& pair : rooms_) {
        totalPlayers += pair.second->GetPlayerCount();
        auto s = pair.second->GetState();
        if (s == RoomState::Waiting) waiting++;
        else if (s == RoomState::Playing) playing++;
        else finished++;
    }
    std::cout << "\n=== Room Statistics ===\n"
              << "Total Rooms: " << rooms_.size() << "\n"
              << "Total Players: " << totalPlayers << "\n"
              << "Waiting: " << waiting << ", Playing: " << playing
              << ", Finished: " << finished << "\n"
              << "======================\n\n";
}
