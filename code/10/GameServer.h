#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <mutex>
#include "JobQueue.h"
#include "LogicWorker.h"
#include "Session.h"
#include "IOContext.h"

#pragma comment(lib, "ws2_32.lib")

class GameServer {
private:
    HANDLE hCompletionPort_;
    SOCKET listenSocket_;

    // I/O 워커 스레드들
    std::vector<std::thread> ioWorkerThreads_;

    // 로직 시스템
    JobQueue jobQueue_;
    std::vector<std::unique_ptr<LogicWorker>> logicWorkers_;

    // 세션 관리
    std::unordered_map<uint64_t, SessionPtr> sessions_;
    std::mutex sessionMutex_;

    std::atomic<bool> isRunning_;
    std::atomic<uint64_t> nextSessionId_;

public:
    GameServer()
        : hCompletionPort_(NULL)
        , listenSocket_(INVALID_SOCKET)
        , isRunning_(false)
        , nextSessionId_(1) {}

    ~GameServer() {
        if (isRunning_) {
            Shutdown();
        }
    }

    bool Initialize(uint16_t port, int logicThreadCount = 4);
    void Shutdown();
    void Run();

private:
    void IOWorkerThread();
    void AcceptLoop();
    void RemoveSession(uint64_t sessionId);
    void PrintStats();
};
