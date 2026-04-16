
#include "SessionManager.h"
#include "Logger.h"

void SessionManager::AddSession(SessionPtr session) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[static_cast<uint32_t>(session->GetID())] = session;
    LOG_INFO("세션 매니저: 세션 {} 추가 (총 {}개)", session->GetID(), sessions_.size());
}

void SessionManager::RemoveSession(uint32_t sessionID) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(sessionID);
    LOG_INFO("세션 매니저: 세션 {} 제거 (총 {}개)", sessionID, sessions_.size());
}

SessionPtr SessionManager::GetSession(uint32_t sessionID) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sessionID);
    if (it != sessions_.end()) {
        return it->second;
    }
    return nullptr;
}

size_t SessionManager::GetSessionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

void SessionManager::BroadcastPacket(PacketPtr packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& pair : sessions_) {
        pair.second->Send(packet);
    }
    LOG_DEBUG("패킷 브로드캐스트: SessionCount={}", sessions_.size());
}

void SessionManager::BroadcastPacket(PacketPtr packet, uint32_t excludeSessionID) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& pair : sessions_) {
        if (pair.first != excludeSessionID) {
            pair.second->Send(packet);
        }
    }
}
