
#include "ServerGlobal.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

HANDLE g_iocpHandle = nullptr;
SOCKET g_listenSocket = INVALID_SOCKET;
bool g_running = true;
TimerManager* g_timerManager = nullptr;

bool InitializeServer() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return false;

    g_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (g_iocpHandle == nullptr)
        return false;

    return true;
}

void ShutdownServer() {
    g_running = false;

    if (g_listenSocket != INVALID_SOCKET) {
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
    }

    if (g_iocpHandle != nullptr) {
        CloseHandle(g_iocpHandle);
        g_iocpHandle = nullptr;
    }

    WSACleanup();
}
