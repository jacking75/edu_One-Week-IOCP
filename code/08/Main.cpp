
#include <iostream>
#include <string>
#include <vector>
#include "ServerGlobal.h"
#include "WorkerThread.h"
#include "AcceptThread.h"
#include "PacketDispatcher.h"
#include "SessionManager.h"
#include "Logger.h"

int main() {
    Logger::Instance().Initialize("GameServer.log");

    LOG_INFO("=== IOCP 게임 서버 Chapter 8 ===");
    LOG_INFO("Zero-Copy + Scatter-Gather I/O 적용");

    size_t handlerCount = PacketDispatcher::Instance().GetHandlerCount();
    LOG_INFO("등록된 패킷 핸들러 수: {}", handlerCount);

    if (!InitializeServer()) {
        LOG_ERROR("서버 초기화 실패");
        return -1;
    }

    if (!InitializeAccept("0.0.0.0", 7777)) {
        LOG_ERROR("Accept 초기화 실패");
        ShutdownServer();
        return -1;
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int workerThreadCount = si.dwNumberOfProcessors * 2;

    std::vector<HANDLE> workerThreads;
    for (int i = 0; i < workerThreadCount; ++i) {
        HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
        workerThreads.push_back(thread);
    }

    LOG_INFO("워커 스레드 {}개 생성됨", workerThreadCount);
    LOG_INFO("서버 실행 중... (종료: quit, 상태: status)");

    std::string command;
    while (g_running) {
        std::getline(std::cin, command);

        if (command == "quit" || command == "exit") {
            LOG_INFO("서버 종료 명령 수신");
            g_running = false;
            break;
        }
        else if (command == "status") {
            size_t sessionCount = SessionManager::Instance().GetSessionCount();
            LOG_INFO("현재 접속자 수: {}", sessionCount);
        }
    }

    LOG_INFO("서버 종료 중...");

    for (size_t i = 0; i < workerThreads.size(); ++i) {
        PostQueuedCompletionStatus(g_iocpHandle, 0, 0, nullptr);
    }

    WaitForMultipleObjects(static_cast<DWORD>(workerThreads.size()),
                          workerThreads.data(), TRUE, INFINITE);

    for (HANDLE thread : workerThreads) {
        CloseHandle(thread);
    }

    ShutdownServer();

    LOG_INFO("=== 게임 서버 종료 ===");

    return 0;
}
