#include "AcceptThread.h"
#include "ServerGlobal.h"
#include "SessionPool.h"
#include "Session.h"
#include <WS2tcpip.h>
#include <iostream>

LPFN_ACCEPTEX g_AcceptEx = nullptr;
LPFN_GETACCEPTEXSOCKADDRS g_GetAcceptExSockaddrs = nullptr;

bool InitializeAccept(const char* ip, uint16_t port) {
    // Listen 소켓 생성
    g_listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                               nullptr, 0, WSA_FLAG_OVERLAPPED);

    if (g_listenSocket == INVALID_SOCKET) {
        std::cout << "[Accept] Listen 소켓 생성 실패" << std::endl;
        return false;
    }

    // SO_REUSEADDR 설정
    BOOL reuseAddr = TRUE;
    setsockopt(g_listenSocket, SOL_SOCKET, SO_REUSEADDR,
               (char*)&reuseAddr, sizeof(reuseAddr));

    // Bind
    SOCKADDR_IN serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serverAddr.sin_addr);

    if (bind(g_listenSocket, (SOCKADDR*)&serverAddr,
             sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cout << "[Accept] Bind 실패" << std::endl;
        return false;
    }

    // Listen
    if (listen(g_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cout << "[Accept] Listen 실패" << std::endl;
        return false;
    }

    // Listen 소켓을 IOCP에 연결
    CreateIoCompletionPort((HANDLE)g_listenSocket, g_iocpHandle,
                           COMPLETION_KEY_ACCEPT, 0);

    // AcceptEx 함수 포인터 얻기
    GUID guidAcceptEx = WSAID_ACCEPTEX;
    DWORD bytes = 0;

    WSAIoctl(g_listenSocket,
             SIO_GET_EXTENSION_FUNCTION_POINTER,
             &guidAcceptEx, sizeof(guidAcceptEx),
             &g_AcceptEx, sizeof(g_AcceptEx),
             &bytes, nullptr, nullptr);

    if (g_AcceptEx == nullptr) {
        std::cout << "[Accept] AcceptEx 함수 포인터 획득 실패" << std::endl;
        return false;
    }

    // GetAcceptExSockaddrs 함수 포인터 얻기
    GUID guidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;

    WSAIoctl(g_listenSocket,
             SIO_GET_EXTENSION_FUNCTION_POINTER,
             &guidGetAcceptExSockaddrs, sizeof(guidGetAcceptExSockaddrs),
             &g_GetAcceptExSockaddrs, sizeof(g_GetAcceptExSockaddrs),
             &bytes, nullptr, nullptr);

    if (g_GetAcceptExSockaddrs == nullptr) {
        std::cout << "[Accept] GetAcceptExSockaddrs 함수 포인터 획득 실패"
                  << std::endl;
        return false;
    }

    // 여러 개의 Accept 작업 대기
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int prePostedAcceptCount = si.dwNumberOfProcessors * 2;

    for (int i = 0; i < prePostedAcceptCount; ++i) {
        PostAccept();
    }

    std::cout << "[Accept] 서버 시작: " << ip << ":" << port << std::endl;

    return true;
}

void PostAccept() {
    AcceptOverlapped* acceptOv = new AcceptOverlapped{};
    ZeroMemory(&acceptOv->overlapped, sizeof(OVERLAPPED));

    acceptOv->acceptSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                                       nullptr, 0, WSA_FLAG_OVERLAPPED);

    if (acceptOv->acceptSocket == INVALID_SOCKET) {
        std::cout << "[Accept] Accept 소켓 생성 실패" << std::endl;
        delete acceptOv;
        return;
    }

    DWORD bytes = 0;
    BOOL result = g_AcceptEx(
        g_listenSocket,
        acceptOv->acceptSocket,
        acceptOv->buffer,
        0,  // 첫 데이터를 받지 않음
        sizeof(SOCKADDR_STORAGE) + 16,
        sizeof(SOCKADDR_STORAGE) + 16,
        &bytes,
        &acceptOv->overlapped
    );

    if (!result && WSAGetLastError() != ERROR_IO_PENDING) {
        std::cout << "[Accept] AcceptEx 실패: " << WSAGetLastError()
                  << std::endl;
        closesocket(acceptOv->acceptSocket);
        delete acceptOv;
    }
}

void ProcessAccept(AcceptOverlapped* acceptOv, DWORD bytesTransferred) {
    // Accept 컨텍스트 업데이트
    setsockopt(acceptOv->acceptSocket, SOL_SOCKET,
               SO_UPDATE_ACCEPT_CONTEXT,
               (char*)&g_listenSocket, sizeof(g_listenSocket));

    // 클라이언트 주소 정보 추출
    SOCKADDR* localAddr = nullptr;
    SOCKADDR* remoteAddr = nullptr;
    int localAddrLen = 0;
    int remoteAddrLen = 0;

    g_GetAcceptExSockaddrs(
        acceptOv->buffer,
        0,
        sizeof(SOCKADDR_STORAGE) + 16,
        sizeof(SOCKADDR_STORAGE) + 16,
        &localAddr, &localAddrLen,
        &remoteAddr, &remoteAddrLen
    );

    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((SOCKADDR_IN*)remoteAddr)->sin_addr,
              clientIP, sizeof(clientIP));

    std::cout << "[Accept] 새 연결: " << clientIP << std::endl;

    // 세션 풀에서 세션 획득
    Session* session = g_sessionPool->Acquire(acceptOv->acceptSocket);

    if (session != nullptr) {
        session->Initialize();
    } else {
        std::cout << "[Accept] 세션 풀 고갈, 연결 거부" << std::endl;
        closesocket(acceptOv->acceptSocket);
    }

    // 새로운 Accept 대기
    PostAccept();

    delete acceptOv;
}
