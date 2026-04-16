#include "GameServer.h"

bool GameServer::Initialize(uint16_t port, int logicThreadCount) {
    // Winsock init
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return false;
    }

    // IOCP
    hCompletionPort_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (hCompletionPort_ == NULL) {
        std::cerr << "CreateIoCompletionPort failed\n";
        return false;
    }

    // Listen socket
    listenSocket_ = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                              NULL, 0, WSA_FLAG_OVERLAPPED);
    if (listenSocket_ == INVALID_SOCKET) {
        std::cerr << "WSASocket failed\n";
        return false;
    }

    int opt = 1;
    setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);

    if (bind(listenSocket_, reinterpret_cast<sockaddr*>(&serverAddr),
             sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "bind failed: " << WSAGetLastError() << "\n";
        return false;
    }

    if (listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed: " << WSAGetLastError() << "\n";
        return false;
    }

    isRunning_ = true;

    // Room manager
    pRoomManager_ = std::make_unique<RoomManager>(logicThreadCount);

    // Logic workers
    for (int i = 0; i < logicThreadCount; ++i) {
        auto worker = std::make_unique<LogicWorker>(i, &jobQueue_);
        worker->Start();
        logicWorkers_.push_back(std::move(worker));
    }

    // I/O workers
    int ioThreadCount = static_cast<int>(std::thread::hardware_concurrency() * 2);
    if (ioThreadCount == 0) ioThreadCount = 4;

    for (int i = 0; i < ioThreadCount; ++i) {
        ioWorkerThreads_.emplace_back(&GameServer::IOWorkerThread, this);
    }

    std::cout << "GameServer initialized with Room system.\n";
    std::cout << "  Port: " << port << "\n";
    std::cout << "  I/O Threads: " << ioThreadCount << "\n";
    std::cout << "  Logic Threads: " << logicThreadCount << "\n";

    return true;
}

void GameServer::Shutdown() {
    std::cout << "Server shutting down...\n";
    isRunning_ = false;

    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }

    // Logic workers
    jobQueue_.Shutdown();
    for (auto& worker : logicWorkers_) {
        worker->Stop();
        std::cout << "LogicWorker stats - Jobs: "
                  << worker->GetProcessedJobCount()
                  << ", Avg time: "
                  << worker->GetAverageProcessingTimeMs() << "ms\n";
    }
    logicWorkers_.clear();

    // I/O workers
    for (size_t i = 0; i < ioWorkerThreads_.size(); ++i) {
        PostQueuedCompletionStatus(hCompletionPort_, 0, 0, nullptr);
    }

    for (auto& thread : ioWorkerThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    ioWorkerThreads_.clear();

    // Sessions
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        for (auto& [id, session] : sessions_) {
            session->Disconnect();
        }
        sessions_.clear();
    }

    if (hCompletionPort_ != NULL) {
        CloseHandle(hCompletionPort_);
        hCompletionPort_ = NULL;
    }

    WSACleanup();
    std::cout << "Server shut down complete.\n";
}

void GameServer::Run() {
    std::thread acceptThread(&GameServer::AcceptLoop, this);

    std::cout << "Server running. Commands: status, rooms, quit\n";

    std::string command;
    while (isRunning_) {
        std::getline(std::cin, command);

        if (command == "quit" || command == "exit") {
            break;
        }
        else if (command == "status") {
            PrintStats();
        }
        else if (command == "rooms") {
            pRoomManager_->PrintStatistics();
        }
    }

    isRunning_ = false;

    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }

    if (acceptThread.joinable()) {
        acceptThread.join();
    }

    Shutdown();
}

void GameServer::AcceptLoop() {
    std::cout << "[AcceptLoop] Started\n";

    while (isRunning_) {
        sockaddr_in clientAddr{};
        int addrLen = sizeof(clientAddr);

        SOCKET clientSocket = accept(listenSocket_,
            reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);

        if (clientSocket == INVALID_SOCKET) {
            if (!isRunning_) break;
            std::cerr << "[AcceptLoop] accept failed: "
                      << WSAGetLastError() << "\n";
            continue;
        }

        uint64_t sessionId = nextSessionId_.fetch_add(1);

        CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket),
                               hCompletionPort_, 0, 0);

        auto session = std::make_shared<Session>(
            clientSocket, sessionId, &jobQueue_);

        // Set up room packet handler callbacks
        SetupSessionCallbacks(session);

        {
            std::lock_guard<std::mutex> lock(sessionMutex_);
            sessions_[sessionId] = session;
        }

        char addrStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));
        std::cout << "[AcceptLoop] New connection: SessionId=" << sessionId
                  << " from " << addrStr << ":"
                  << ntohs(clientAddr.sin_port) << "\n";

        session->RegisterRecv();
    }

    std::cout << "[AcceptLoop] Exiting\n";
}

void GameServer::SetupSessionCallbacks(SessionPtr pSession) {
    pSession->SetCreateRoomCallback(
        [this](SessionPtr session, const CreateRoomPacket* packet) {
            HandleCreateRoom(session, packet);
        });

    pSession->SetJoinRoomCallback(
        [this](SessionPtr session, const JoinRoomPacket* packet) {
            HandleJoinRoom(session, packet);
        });
}

void GameServer::HandleCreateRoom(SessionPtr pSession,
                                   const CreateRoomPacket* pPacket) {
    RoomConfig config;
    config.maxPlayers = pPacket->maxPlayers;
    config.minPlayers = pPacket->minPlayers;
    config.isPrivate = pPacket->isPrivate;
    config.password = pPacket->password;

    auto pRoom = pRoomManager_->CreateRoom(pPacket->roomName, config);

    CreateRoomResponsePacket response{};
    response.header.packetId = PacketId::CreateRoomResponse;
    response.header.size = sizeof(CreateRoomResponsePacket);
    response.success = (pRoom != nullptr);
    response.roomId = pRoom ? pRoom->GetRoomId() : 0;

    pSession->Send(reinterpret_cast<char*>(&response), sizeof(response));

    // Auto-join the created room
    if (pRoom) {
        pRoom->AddPlayer(pSession);
        pSession->SetCurrentRoom(pRoom);
    }
}

void GameServer::HandleJoinRoom(SessionPtr pSession,
                                 const JoinRoomPacket* pPacket) {
    auto pRoom = pRoomManager_->FindRoom(pPacket->roomId);

    JoinRoomResponsePacket response{};
    response.header.packetId = PacketId::JoinRoomResponse;
    response.header.size = sizeof(JoinRoomResponsePacket);

    if (!pRoom) {
        response.success = false;
        response.roomId = pPacket->roomId;
        pSession->Send(reinterpret_cast<char*>(&response), sizeof(response));
        return;
    }

    bool joined = pRoom->AddPlayer(pSession);
    response.success = joined;
    response.roomId = pPacket->roomId;

    pSession->Send(reinterpret_cast<char*>(&response), sizeof(response));

    if (joined) {
        pSession->SetCurrentRoom(pRoom);
    }
}

void GameServer::IOWorkerThread() {
    while (isRunning_) {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        OVERLAPPED* pOverlapped = nullptr;

        BOOL result = GetQueuedCompletionStatus(
            hCompletionPort_,
            &bytesTransferred,
            &completionKey,
            &pOverlapped,
            INFINITE
        );

        // Shutdown signal
        if (completionKey == 0 && pOverlapped == nullptr) {
            break;
        }

        if (pOverlapped == nullptr) {
            continue;
        }

        auto ioContext = reinterpret_cast<IOContext*>(pOverlapped);

        if (!result || bytesTransferred == 0) {
            if (ioContext->session) {
                uint64_t sid = ioContext->session->GetSessionId();
                ioContext->session->Disconnect();
                RemoveSession(sid);
            }

            if (ioContext->ioType == IOType::Send) {
                delete reinterpret_cast<SendContext*>(pOverlapped);
            } else {
                delete ioContext;
            }
            continue;
        }

        switch (ioContext->ioType) {
            case IOType::Recv: {
                ioContext->session->OnRecvComplete(bytesTransferred);
                delete ioContext;
                break;
            }
            case IOType::Send: {
                auto sendCtx = reinterpret_cast<SendContext*>(pOverlapped);
                delete sendCtx;
                break;
            }
            default:
                delete ioContext;
                break;
        }
    }
}

void GameServer::RemoveSession(uint64_t sessionId) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    sessions_.erase(sessionId);
}

void GameServer::PrintStats() {
    std::lock_guard<std::mutex> lock(sessionMutex_);

    std::cout << "\n=== Server Stats ===\n";
    std::cout << "Connected Sessions: " << sessions_.size() << "\n";
    std::cout << "Pending Jobs: " << jobQueue_.GetPendingCount() << "\n";
    std::cout << "Rooms: " << pRoomManager_->GetRoomCount() << "\n";
    std::cout << "Players in Rooms: " << pRoomManager_->GetTotalPlayerCount() << "\n";

    for (size_t i = 0; i < logicWorkers_.size(); ++i) {
        std::cout << "Worker " << i << " - Jobs: "
                  << logicWorkers_[i]->GetProcessedJobCount()
                  << ", Avg: "
                  << logicWorkers_[i]->GetAverageProcessingTimeMs()
                  << "ms\n";
    }
    std::cout << "====================\n\n";
}
