#include "IOCPServer.h"
#include <csignal>

IOCPServer* g_server = nullptr;

void SignalHandler(int signal) {
    if (signal == SIGINT && g_server != nullptr) {
        LOG_INFO("Ctrl+C pressed, shutting down...");
        g_server->Shutdown();
    }
}

int main() {
    std::cout << "=== IOCP Echo Server ===\n\n";

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    int workerThreadCount = std::thread::hardware_concurrency() * 2;
    if (workerThreadCount == 0) {
        workerThreadCount = 4;
    }

    IOCPServer server;
    g_server = &server;

    std::signal(SIGINT, SignalHandler);

    if (!server.Initialize(9000, workerThreadCount)) {
        std::cerr << "Server initialization failed\n";
        WSACleanup();
        return 1;
    }

    server.Run();

    g_server = nullptr;
    WSACleanup();

    std::cout << "\nPress Enter to exit...";
    std::cin.get();

    return 0;
}
