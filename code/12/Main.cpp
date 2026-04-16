#include "GameServer.h"

int main() {
    std::cout << "=== IOCP Game Server Chapter 12 ===\n";
    std::cout << "Omok (Five-in-a-Row) Game Server\n\n";

    GameServer server;

    if (!server.Initialize(7777, 4)) {
        std::cerr << "Server initialization failed\n";
        return 1;
    }

    server.Run();

    return 0;
}
