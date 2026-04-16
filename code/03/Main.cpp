#include <iostream>
#include <vector>
#include "ServerGlobal.h"
#include "SessionPool.h"
#include "WorkerThread.h"
#include "AcceptThread.h"

int main() {
    std::cout << "=== IOCP 게임 서버 Chapter 3 ===" << std::endl;

    // 서버 초기화
    if (!InitializeServer()) {
        std::cout << "서버 초기화 실패" << std::endl;
        return -1;
    }

    // Accept 초기화
    if (!InitializeAccept("0.0.0.0", 9000)) {
        std::cout << "Accept 초기화 실패" << std::endl;
        ShutdownServer();
        return -1;
    }

    // 워커 스레드 생성
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int workerThreadCount = si.dwNumberOfProcessors * 2;

    std::vector<HANDLE> workerThreads;
    for (int i = 0; i < workerThreadCount; ++i) {
        HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
        workerThreads.push_back(thread);
    }

    std::cout << "워커 스레드 " << workerThreadCount << "개 생성됨" << std::endl;
    std::cout << "서버 실행 중... (종료하려면 'q' 입력)" << std::endl;

    // 메인 루프
    while (g_running) {
        char input;
        std::cin >> input;

        if (input == 'q' || input == 'Q') {
            g_running = false;
            break;
        }

        if (input == 's' || input == 'S') {
            int activeCount = g_sessionPool->GetActiveSessionCount();
            std::cout << "현재 활성 세션: " << activeCount << "개" << std::endl;
        }
    }

    std::cout << "서버 종료 중..." << std::endl;

    // 워커 스레드 종료 신호
    for (size_t i = 0; i < workerThreads.size(); ++i) {
        PostQueuedCompletionStatus(g_iocpHandle, 0, 0, nullptr);
    }

    // 워커 스레드 종료 대기
    WaitForMultipleObjects(static_cast<DWORD>(workerThreads.size()),
                          workerThreads.data(), TRUE, INFINITE);

    for (HANDLE thread : workerThreads) {
        CloseHandle(thread);
    }

    // 서버 종료
    ShutdownServer();

    std::cout << "서버 종료 완료" << std::endl;

    return 0;
}
