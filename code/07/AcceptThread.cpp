
#include "AcceptThread.h"
#include "ServerGlobal.h"
#include "SessionManager.h"
#include "Logger.h"
#include <WS2tcpip.h>

static LPFN_ACCEPTEX g_AcceptEx = nullptr;
static LPFN_GETACCEPTEXSOCKADDRS g_GetAcceptExSockaddrs = nullptr;

bool InitializeAccept(const char* ip, uint16_t port) {
    g_listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                               nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (g_listenSocket == INVALID_SOCKET) {
        LOG_ERROR("Listen 소켓 생성 실패");
        return false;
    }

    BOOL reuseAddr = TRUE;
    setsockopt(g_listenSocket, SOL_SOCKET, SO_REUSEADDR,
               (char*)&reuseAddr, sizeof(reuseAddr));

    SOCKADDR_IN serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serverAddr.sin_addr);

    if (bind(g_listenSocket, (SOCKADDR*)&serverAddr,
             sizeof(serverAddr)) == SOCKET_ERROR) {
        LOG_ERROR("Bind 실패");
        return false;
    }

    if (listen(g_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        LOG_ERROR("Listen 실패");
        return false;
    }

    CreateIoCompletionPort((HANDLE)g_listenSocket, g_iocpHandle,
                           COMPLETION_KEY_ACCEPT, 0);

    GUID guidAcceptEx = WSAID_ACCEPTEX;
    DWORD bytes = 0;
    WSAIoctl(g_listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
             &guidAcceptEx, sizeof(guidAcceptEx),
             &g_AcceptEx, sizeof(g_AcceptEx), &bytes, nullptr, nullptr);

    if (g_AcceptEx == nullptr) {
        LOG_ERROR("AcceptEx 함수 포인터 획득 실패");
        return false;
    }

    GUID guidGetSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
    WSAIoctl(g_listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
             &guidGetSockaddrs, sizeof(guidGetSockaddrs),
             &g_GetAcceptExSockaddrs, sizeof(g_GetAcceptExSockaddrs),
             &bytes, nullptr, nullptr);

    if (g_GetAcceptExSockaddrs == nullptr) {
        LOG_ERROR("GetAcceptExSockaddrs 함수 포인터 획득 실패");
        return false;
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int prePostedAcceptCount = si.dwNumberOfProcessors * 2;
    for (int i = 0; i < prePostedAcceptCount; ++i) {
        PostAccept();
    }

    LOG_INFO("서버 시작: {}:{}", ip, port);
    return true;
}

void PostAccept() {
    AcceptOverlapped* acceptOv = new AcceptOverlapped{};
    ZeroMemory(&acceptOv->overlapped, sizeof(OVERLAPPED));

    acceptOv->acceptSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                                       nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (acceptOv->acceptSocket == INVALID_SOCKET) {
        LOG_ERROR("Accept 소켓 생성 실패");
        delete acceptOv;
        return;
    }

    DWORD bytes = 0;
    BOOL result = g_AcceptEx(g_listenSocket, acceptOv->acceptSocket,
        acceptOv->buffer, 0,
        sizeof(SOCKADDR_STORAGE) + 16, sizeof(SOCKADDR_STORAGE) + 16,
        &bytes, &acceptOv->overlapped);

    if (!result && WSAGetLastError() != ERROR_IO_PENDING) {
        LOG_ERROR("AcceptEx 실패: {}", WSAGetLastError());
        closesocket(acceptOv->acceptSocket);
        delete acceptOv;
    }
}

void ProcessAccept(AcceptOverlapped* acceptOv, DWORD bytesTransferred) {
    setsockopt(acceptOv->acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
               (char*)&g_listenSocket, sizeof(g_listenSocket));

    SOCKADDR* localAddr = nullptr;
    SOCKADDR* remoteAddr = nullptr;
    int localAddrLen = 0, remoteAddrLen = 0;

    g_GetAcceptExSockaddrs(acceptOv->buffer, 0,
        sizeof(SOCKADDR_STORAGE) + 16, sizeof(SOCKADDR_STORAGE) + 16,
        &localAddr, &localAddrLen, &remoteAddr, &remoteAddrLen);

    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((SOCKADDR_IN*)remoteAddr)->sin_addr,
              clientIP, sizeof(clientIP));
    LOG_INFO("새 연결: {}", clientIP);

    auto session = std::make_shared<Session>(acceptOv->acceptSocket);

    SessionManager::Instance().AddSession(session);
    session->Initialize();

    PostAccept();
    delete acceptOv;
}
