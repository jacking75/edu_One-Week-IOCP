#include "GameServer.h"

int main() {
    std::cout << "=== IOCP Game Server Chapter 11 ===\n";
    std::cout << "Room-based Game Server\n\n";

    GameServer server;

    if (!server.Initialize(7777, 4)) {
        std::cerr << "Server initialization failed\n";
        return 1;
    }

    server.Run();

    return 0;
}
