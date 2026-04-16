
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h>
#include <concurrent_unordered_map.h>
#include <shared_mutex>
#include "Session.h"

class SessionManager
{
public:
    static SessionManager& Instance()
    {
        static SessionManager instance;
        return instance;
    }

    void AddSession(SessionPtr session);
    void RemoveSession(uint32_t sessionID);
    SessionPtr GetSession(uint32_t sessionID);
    size_t GetSessionCount() const;

    void BroadcastPacket(PacketPtr packet);
    void BroadcastPacket(PacketPtr packet, uint32_t excludeSessionID);
    void BroadcastToAuthenticated(PacketPtr packet);

    template<typename Func>
    void ForEachSession(Func func);

private:
    SessionManager() = default;
    ~SessionManager() = default;

    concurrency::concurrent_unordered_map<uint32_t, SessionPtr> sessions_;
};

template<typename Func>
void SessionManager::ForEachSession(Func func)
{
    for (auto& pair : sessions_)
    {
        if (pair.second)
        {
            func(pair.second);
        }
    }
}
