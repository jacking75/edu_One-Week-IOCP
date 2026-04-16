#include "Room.h"
#include <iostream>

int Room::GetPlayerCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(playersMutex_));
    return static_cast<int>(players_.size());
}

bool Room::CanJoin() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(playersMutex_));
    return state_.load(std::memory_order_acquire) == RoomState::Waiting &&
           players_.size() < config_.maxPlayers;
}

bool Room::AddPlayer(std::shared_ptr<Session> pSession) {
    std::lock_guard<std::mutex> lock(playersMutex_);

    if (state_.load(std::memory_order_acquire) != RoomState::Waiting) {
        return false;
    }

    if (players_.size() >= config_.maxPlayers) {
        return false;
    }

    uint64_t sessionId = pSession->GetSessionId();

    if (players_.count(sessionId) > 0) {
        return false;
    }

    players_[sessionId] = pSession;

    if (players_.size() == 1) {
        hostSessionId_ = sessionId;
    }

    std::cout << "[Room " << roomId_ << "] Player " << sessionId
              << " joined (" << players_.size() << "/"
              << config_.maxPlayers << ")\n";

    NotifyPlayerJoined(sessionId);
    SendRoomStateToPlayer(pSession);

    return true;
}

void Room::RemovePlayer(uint64_t sessionId) {
    std::lock_guard<std::mutex> lock(playersMutex_);

    auto it = players_.find(sessionId);
    if (it == players_.end()) {
        return;
    }

    players_.erase(it);

    std::cout << "[Room " << roomId_ << "] Player " << sessionId
              << " left (" << players_.size() << " remaining)\n";

    if (sessionId == hostSessionId_ && !players_.empty()) {
        hostSessionId_ = players_.begin()->first;
        std::cout << "[Room " << roomId_ << "] New host: " << hostSessionId_ << "\n";
    }

    NotifyPlayerLeft(sessionId);

    if (players_.empty()) {
        std::cout << "[Room " << roomId_ << "] Room is empty.\n";
    }
}

void Room::KickAllPlayers() {
    std::lock_guard<std::mutex> lock(playersMutex_);

    PlayerKickedPacket packet{};
    packet.header.packetId = PacketId::PlayerKicked;
    packet.header.size = sizeof(PlayerKickedPacket);
    packet.reason = KickReason::RoomClosed;

    for (auto& pair : players_) {
        auto& pSession = pair.second;
        pSession->Send(reinterpret_cast<char*>(&packet), sizeof(packet));
    }

    players_.clear();
}

void Room::BroadcastPacket(const char* pData, int size, uint64_t exceptSessionId) {
    // Note: caller must NOT hold playersMutex_ already, or this will deadlock
    std::lock_guard<std::mutex> lock(playersMutex_);

    for (const auto& pair : players_) {
        if (pair.first == exceptSessionId) {
            continue;
        }

        auto& pSession = pair.second;
        pSession->Send(pData, size);
    }
}

void Room::BroadcastPacketOptimized(const char* pData, int size, uint64_t exceptSessionId) {
    auto sharedBuffer = std::make_shared<std::vector<char>>(pData, pData + size);

    std::lock_guard<std::mutex> lock(playersMutex_);

    for (const auto& pair : players_) {
        if (pair.first == exceptSessionId) {
            continue;
        }

        auto& pSession = pair.second;
        pSession->SendShared(sharedBuffer);
    }
}

void Room::StartGame() {
    std::lock_guard<std::mutex> lock(playersMutex_);

    if (state_.load(std::memory_order_acquire) != RoomState::Waiting) {
        return;
    }

    if (players_.size() < config_.minPlayers) {
        std::cout << "[Room " << roomId_ << "] Not enough players\n";
        return;
    }

    state_.store(RoomState::Playing, std::memory_order_release);

    std::cout << "[Room " << roomId_ << "] Game started with "
              << players_.size() << " players\n";

    GameStartPacket packet{};
    packet.header.packetId = PacketId::GameStart;
    packet.header.size = sizeof(GameStartPacket);
    packet.roomId = roomId_;
    packet.playerCount = static_cast<uint32_t>(players_.size());

    // Broadcast without re-locking (already holding playersMutex_)
    for (const auto& pair : players_) {
        pair.second->Send(reinterpret_cast<char*>(&packet), sizeof(packet));
    }

    InitializeGame();
}

void Room::EndGame() {
    std::lock_guard<std::mutex> lock(playersMutex_);

    if (state_.load(std::memory_order_acquire) != RoomState::Playing) {
        return;
    }

    state_.store(RoomState::Finished, std::memory_order_release);

    GameEndPacket packet{};
    packet.header.packetId = PacketId::GameEnd;
    packet.header.size = sizeof(GameEndPacket);
    packet.roomId = roomId_;

    for (const auto& pair : players_) {
        pair.second->Send(reinterpret_cast<char*>(&packet), sizeof(packet));
    }

    std::cout << "[Room " << roomId_ << "] Game ended\n";
}

void Room::NotifyPlayerJoined(uint64_t sessionId) {
    // Called with playersMutex_ already held
    PlayerJoinedPacket packet{};
    packet.header.packetId = PacketId::PlayerJoined;
    packet.header.size = sizeof(PlayerJoinedPacket);
    packet.roomId = roomId_;
    packet.sessionId = sessionId;
    packet.currentPlayerCount = static_cast<uint32_t>(players_.size());

    for (const auto& pair : players_) {
        if (pair.first == sessionId) continue;
        pair.second->Send(reinterpret_cast<char*>(&packet), sizeof(packet));
    }
}

void Room::NotifyPlayerLeft(uint64_t sessionId) {
    // Called with playersMutex_ already held
    PlayerLeftPacket packet{};
    packet.header.packetId = PacketId::PlayerLeft;
    packet.header.size = sizeof(PlayerLeftPacket);
    packet.roomId = roomId_;
    packet.sessionId = sessionId;
    packet.currentPlayerCount = static_cast<uint32_t>(players_.size());

    for (const auto& pair : players_) {
        pair.second->Send(reinterpret_cast<char*>(&packet), sizeof(packet));
    }
}

void Room::SendRoomStateToPlayer(std::shared_ptr<Session> pSession) {
    // Called with playersMutex_ already held
    RoomStatePacket packet{};
    packet.header.packetId = PacketId::RoomState;
    packet.header.size = sizeof(RoomStatePacket);
    packet.roomId = roomId_;
    packet.state = state_.load(std::memory_order_acquire);
    packet.playerCount = static_cast<uint32_t>(players_.size());
    packet.maxPlayers = config_.maxPlayers;
    packet.hostSessionId = hostSessionId_;

    int index = 0;
    for (const auto& pair : players_) {
        if (index >= 16) break;
        packet.playerIds[index++] = pair.first;
    }

    pSession->Send(reinterpret_cast<char*>(&packet), sizeof(packet));
}

void Room::InitializeGame() {
    // Game-specific initialization
    // e.g., board initialization for Omok, deal cards for poker, etc.
}

void Room::OnPlayerAction(uint64_t sessionId, const char* pActionData, int size) {
    // Game-specific action processing
    // Will be detailed in the next chapter
}
