#include "RoomManager.h"
#include <iostream>

std::shared_ptr<Room> RoomManager::CreateRoom(const std::string& name,
                                               const RoomConfig& config) {
    uint64_t roomId = nextRoomId_.fetch_add(1, std::memory_order_relaxed);
    int threadId = static_cast<int>(roomId % logicThreadCount_);

    auto pRoom = std::make_shared<Room>(roomId, name, config, threadId);

    {
        std::unique_lock<std::shared_mutex> lock(roomsMutex_);
        rooms_[roomId] = pRoom;
    }

    std::cout << "[RoomManager] Room created: ID=" << roomId
              << ", Name=" << name
              << ", Thread=" << threadId << "\n";

    return pRoom;
}

bool RoomManager::DestroyRoom(uint64_t roomId) {
    std::shared_ptr<Room> pRoom;

    {
        std::unique_lock<std::shared_mutex> lock(roomsMutex_);
        auto it = rooms_.find(roomId);

        if (it == rooms_.end()) {
            return false;
        }

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
    if (it != rooms_.end()) {
        return it->second;
    }

    return nullptr;
}

std::vector<std::shared_ptr<Room>> RoomManager::GetAvailableRooms() {
    std::vector<std::shared_ptr<Room>> availableRooms;

    std::shared_lock<std::shared_mutex> lock(roomsMutex_);

    for (const auto& pair : rooms_) {
        auto& pRoom = pair.second;

        if (pRoom->GetState() == RoomState::Waiting && pRoom->CanJoin()) {
            availableRooms.push_back(pRoom);
        }
    }

    return availableRooms;
}

int RoomManager::GetRoomCount() const {
    std::shared_lock<std::shared_mutex> lock(roomsMutex_);
    return static_cast<int>(rooms_.size());
}

int RoomManager::GetTotalPlayerCount() const {
    std::shared_lock<std::shared_mutex> lock(roomsMutex_);

    int totalPlayers = 0;
    for (const auto& pair : rooms_) {
        totalPlayers += pair.second->GetPlayerCount();
    }

    return totalPlayers;
}

void RoomManager::PrintStatistics() const {
    std::shared_lock<std::shared_mutex> lock(roomsMutex_);

    int totalPlayers = 0;
    int waitingRooms = 0, playingRooms = 0, finishedRooms = 0;

    for (const auto& pair : rooms_) {
        totalPlayers += pair.second->GetPlayerCount();

        auto state = pair.second->GetState();
        if (state == RoomState::Waiting) waitingRooms++;
        else if (state == RoomState::Playing) playingRooms++;
        else finishedRooms++;
    }

    std::cout << "\n=== Room Statistics ===\n";
    std::cout << "Total Rooms: " << rooms_.size() << "\n";
    std::cout << "Total Players: " << totalPlayers << "\n";
    std::cout << "Waiting: " << waitingRooms
              << ", Playing: " << playingRooms
              << ", Finished: " << finishedRooms << "\n";
    std::cout << "======================\n\n";
}
