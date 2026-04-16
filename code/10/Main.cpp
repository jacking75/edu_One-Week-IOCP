#include "GameServer.h"

int main() {
    std::cout << "=== IOCP Game Server Chapter 10 ===\n";
    std::cout << "I/O Thread and Logic Thread Separation\n\n";

    GameServer server;

    // 로직 스레드 4개로 서버 초기화, 포트 7777
    if (!server.Initialize(7777, 4)) {
        std::cerr << "Server initialization failed\n";
        return 1;
    }

    server.Run();

    return 0;
}
