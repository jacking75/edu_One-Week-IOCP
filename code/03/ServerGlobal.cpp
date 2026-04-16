#include "ServerGlobal.h"
#include "SessionPool.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

HANDLE g_iocpHandle = nullptr;
SessionPool* g_sessionPool = nullptr;
SOCKET g_listenSocket = INVALID_SOCKET;
bool g_running = true;

bool InitializeServer() {
    // Winsock 초기화
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }

    // IOCP 생성
    g_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
                                          nullptr, 0, 0);
    if (g_iocpHandle == nullptr) {
        return false;
    }

    // 세션 풀 생성
    g_sessionPool = new SessionPool(1000);

    return true;
}

void ShutdownServer() {
    g_running = false;

    if (g_listenSocket != INVALID_SOCKET) {
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
    }

    if (g_sessionPool != nullptr) {
        delete g_sessionPool;
        g_sessionPool = nullptr;
    }

    if (g_iocpHandle != nullptr) {
        CloseHandle(g_iocpHandle);
        g_iocpHandle = nullptr;
    }

    WSACleanup();
}
