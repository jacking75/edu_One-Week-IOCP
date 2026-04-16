
#include "SessionPool.h"
#include "BufferConfig.h"
#include <iostream>

SessionPool::SessionPool(int32_t maxSessions)
    : sessionIdGenerator_(1)
    , maxSessions_(maxSessions)
{
    for (int32_t i = 0; i < maxSessions_; ++i) {
        Session* session = new Session();
        freeList_.push(session);
    }

    int totalMemoryMB = maxSessions_ *
        (BufferConfig::DEFAULT_RECV_BUFFER + BufferConfig::DEFAULT_SEND_BUFFER)
        / (1024 * 1024);

    std::cout << "[SessionPool] 세션 풀 생성: " << maxSessions_
              << "개" << std::endl;
    std::cout << "사전 할당 메모리: " << totalMemoryMB
              << " MB" << std::endl;
}

SessionPool::~SessionPool() {
    while (!freeList_.empty()) {
        Session* session = freeList_.front();
        freeList_.pop();
        delete session;
    }

    for (auto& pair : activeSessions_) {
        delete pair.second;
    }
}

Session* SessionPool::Acquire(SOCKET socket) {
    Session* session = nullptr;

    {
        std::lock_guard<std::mutex> lock(freeListMutex_);

        if (freeList_.empty()) {
            std::cout << "[SessionPool] 세션 풀 고갈" << std::endl;
            return nullptr;
        }

        session = freeList_.front();
        freeList_.pop();
    }

    uint64_t sessionId = sessionIdGenerator_.fetch_add(1);
    session->Reset(socket, sessionId);

    {
        std::lock_guard<std::mutex> lock(activeSessionsMutex_);
        activeSessions_[sessionId] = session;
    }

    return session;
}

void SessionPool::Release(Session* session) {
    uint64_t sessionId = session->GetSessionId();

    {
        std::lock_guard<std::mutex> lock(activeSessionsMutex_);
        activeSessions_.erase(sessionId);
    }

    {
        std::lock_guard<std::mutex> lock(freeListMutex_);
        freeList_.push(session);
    }
}

std::vector<Session*> SessionPool::GetActiveSessions() {
    std::lock_guard<std::mutex> lock(activeSessionsMutex_);

    std::vector<Session*> sessions;
    sessions.reserve(activeSessions_.size());

    for (auto& pair : activeSessions_) {
        sessions.push_back(pair.second);
    }

    return sessions;
}

int32_t SessionPool::GetActiveSessionCount() {
    std::lock_guard<std::mutex> lock(activeSessionsMutex_);
    return static_cast<int32_t>(activeSessions_.size());
}
