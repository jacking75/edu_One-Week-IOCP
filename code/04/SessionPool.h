
#pragma once

#include "Session.h"
#include <mutex>
#include <queue>
#include <unordered_map>

class SessionPool {
private:
    std::queue<Session*> freeList_;
    std::mutex freeListMutex_;

    std::unordered_map<uint64_t, Session*> activeSessions_;
    std::mutex activeSessionsMutex_;

    std::atomic<uint64_t> sessionIdGenerator_;
    const int32_t maxSessions_;

public:
    SessionPool(int32_t maxSessions);
    ~SessionPool();

    Session* Acquire(SOCKET socket);
    void Release(Session* session);

    std::vector<Session*> GetActiveSessions();
    int32_t GetActiveSessionCount();
};
