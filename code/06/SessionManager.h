
#pragma once

#include "Session.h"
#include <unordered_map>
#include <mutex>

class SessionManager {
public:
    static SessionManager& Instance() {
        static SessionManager instance;
        return instance;
    }

    void AddSession(SessionPtr session);
    void RemoveSession(uint32_t sessionID);
    SessionPtr GetSession(uint32_t sessionID);
    size_t GetSessionCount() const;

    void BroadcastPacket(PacketPtr packet);
    void BroadcastPacket(PacketPtr packet, uint32_t excludeSessionID);

private:
    SessionManager() = default;
    ~SessionManager() = default;

    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, SessionPtr> sessions_;
};
