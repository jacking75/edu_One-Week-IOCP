
#include "SessionManager.h"
#include "Logger.h"

void SessionManager::AddSession(SessionPtr session) {
    if (!session) return;
    uint32_t sessionID = session->GetID();
    sessions_[sessionID] = session;
    LOG_INFO("세션 추가: SessionID={}, Total={}", sessionID, sessions_.size());
}

void SessionManager::RemoveSession(uint32_t sessionID) {
    auto it = sessions_.find(sessionID);
    if (it != sessions_.end()) {
        sessions_.unsafe_erase(it);
        LOG_INFO("세션 제거: SessionID={}, Total={}", sessionID, sessions_.size());
    }
}

SessionPtr SessionManager::GetSession(uint32_t sessionID) {
    auto it = sessions_.find(sessionID);
    return (it != sessions_.end()) ? it->second : nullptr;
}

size_t SessionManager::GetSessionCount() const {
    return sessions_.size();
}

void SessionManager::BroadcastPacket(PacketPtr packet) {
    for (auto& pair : sessions_) {
        if (pair.second && pair.second->IsConnected())
            pair.second->Send(packet);
    }
}

void SessionManager::BroadcastPacket(PacketPtr packet, uint32_t excludeSessionID) {
    for (auto& pair : sessions_) {
        if (pair.first != excludeSessionID && pair.second && pair.second->IsConnected())
            pair.second->Send(packet);
    }
}

void SessionManager::BroadcastToAuthenticated(PacketPtr packet) {
    for (auto& pair : sessions_) {
        if (pair.second && pair.second->IsConnected() && pair.second->IsAuthenticated())
            pair.second->Send(packet);
    }
}
