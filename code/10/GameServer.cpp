#include "GameServer.h"

bool GameServer::Initialize(uint16_t port, int logicThreadCount) {
    // Winsock 초기화
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return false;
    }

    // IOCP 생성
    hCompletionPort_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (hCompletionPort_ == NULL) {
        std::cerr << "CreateIoCompletionPort failed\n";
        return false;
    }

    // 리슨 소켓 생성
    listenSocket_ = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                              NULL, 0, WSA_FLAG_OVERLAPPED);
    if (listenSocket_ == INVALID_SOCKET) {
        std::cerr << "WSASocket failed\n";
        return false;
    }

    // SO_REUSEADDR 설정
    int opt = 1;
    setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    // 바인드
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);

    if (bind(listenSocket_, reinterpret_cast<sockaddr*>(&serverAddr),
             sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "bind failed: " << WSAGetLastError() << "\n";
        return false;
    }

    // 리슨
    if (listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed: " << WSAGetLastError() << "\n";
        return false;
    }

    isRunning_ = true;

    // 로직 워커 스레드 생성
    for (int i = 0; i < logicThreadCount; ++i) {
        auto worker = std::make_unique<LogicWorker>(i, &jobQueue_);
        worker->Start();
        logicWorkers_.push_back(std::move(worker));
    }

    // I/O 워커 스레드 생성
    int ioThreadCount = static_cast<int>(std::thread::hardware_concurrency() * 2);
    if (ioThreadCount == 0) ioThreadCount = 4;

    for (int i = 0; i < ioThreadCount; ++i) {
        ioWorkerThreads_.emplace_back(&GameServer::IOWorkerThread, this);
    }

    std::cout << "GameServer initialized.\n";
    std::cout << "  Port: " << port << "\n";
    std::cout << "  I/O Threads: " << ioThreadCount << "\n";
    std::cout << "  Logic Threads: " << logicThreadCount << "\n";

    return true;
}

void GameServer::Shutdown() {
    std::cout << "Server shutting down...\n";
    isRunning_ = false;

    // 리슨 소켓 종료 (AcceptLoop 탈출)
    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }

    // 로직 워커 종료
    jobQueue_.Shutdown();
    for (auto& worker : logicWorkers_) {
        worker->Stop();

        std::cout << "LogicWorker stats - Jobs: "
                  << worker->GetProcessedJobCount()
                  << ", Avg time: "
                  << worker->GetAverageProcessingTimeMs() << "ms\n";
    }
    logicWorkers_.clear();

    // I/O 워커 종료
    for (size_t i = 0; i < ioWorkerThreads_.size(); ++i) {
        PostQueuedCompletionStatus(hCompletionPort_, 0, 0, nullptr);
    }

    for (auto& thread : ioWorkerThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    ioWorkerThreads_.clear();

    // 세션 정리
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
    // Accept를 별도 스레드에서 실행
    std::thread acceptThread(&GameServer::AcceptLoop, this);

    std::cout << "Server running. Commands: status, quit\n";

    std::string command;
    while (isRunning_) {
        std::getline(std::cin, command);

        if (command == "quit" || command == "exit") {
            break;
        }
        else if (command == "status") {
            PrintStats();
        }
    }

    isRunning_ = false;

    // Accept 스레드가 accept()에서 블록되어 있을 수 있으므로
    // 리슨 소켓을 닫아서 탈출시킴
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

        // IOCP에 소켓 연결
        CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket),
                               hCompletionPort_, 0, 0);

        auto session = std::make_shared<Session>(
            clientSocket, sessionId, &jobQueue_);

        {
            std::lock_guard<std::mutex> lock(sessionMutex_);
            sessions_[sessionId] = session;
        }

        char addrStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));
        std::cout << "[AcceptLoop] New connection: SessionId=" << sessionId
                  << " from " << addrStr << ":"
                  << ntohs(clientAddr.sin_port) << "\n";

        // 비동기 수신 시작
        session->RegisterRecv();
    }

    std::cout << "[AcceptLoop] Exiting\n";
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

        // 종료 신호
        if (completionKey == 0 && pOverlapped == nullptr) {
            break;
        }

        if (pOverlapped == nullptr) {
            continue;
        }

        // IOContext에서 세션 정보 추출
        auto ioContext = reinterpret_cast<IOContext*>(pOverlapped);

        if (!result || bytesTransferred == 0) {
            // 연결 종료 또는 에러
            if (ioContext->session) {
                uint64_t sid = ioContext->session->GetSessionId();
                ioContext->session->Disconnect();

                // 세션 맵에서 제거
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
                // 수신 완료 - 패킷 파싱 후 Job Queue에 추가
                ioContext->session->OnRecvComplete(bytesTransferred);
                delete ioContext;
                break;
            }
            case IOType::Send: {
                // 송신 완료
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

    std::cout << "\n=== Performance Stats ===\n";
    std::cout << "Connected Sessions: " << sessions_.size() << "\n";
    std::cout << "Pending Jobs: " << jobQueue_.GetPendingCount() << "\n";

    for (size_t i = 0; i < logicWorkers_.size(); ++i) {
        std::cout << "Worker " << i << " - Jobs: "
                  << logicWorkers_[i]->GetProcessedJobCount()
                  << ", Avg: "
                  << logicWorkers_[i]->GetAverageProcessingTimeMs()
                  << "ms\n";
    }
    std::cout << "========================\n\n";
}
