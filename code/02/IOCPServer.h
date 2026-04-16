#pragma once
#include "Common.h"
#include "Session.h"

class IOCPServer {
private:
    HANDLE hCompletionPort_;
    SOCKET listenSocket_;
    std::vector<std::thread> workerThreads_;
    std::atomic<bool> isRunning_;
    std::unordered_map<SessionID, std::shared_ptr<Session>> sessions_;
    std::mutex sessionsMutex_;

public:
    IOCPServer()
        : hCompletionPort_(nullptr),
          listenSocket_(INVALID_SOCKET),
          isRunning_(false) {
    }

    ~IOCPServer() {
        Shutdown();
    }

    bool Initialize(int port, int workerThreadCount) {
        hCompletionPort_ = CreateIoCompletionPort(
            INVALID_HANDLE_VALUE, nullptr, 0, 0);

        if (hCompletionPort_ == nullptr) {
            LOG_ERROR("Failed to create completion port");
            return false;
        }

        listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket_ == INVALID_SOCKET) {
            LOG_ERROR("Failed to create listen socket");
            return false;
        }

        int reuseAddr = 1;
        setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR,
                   (char*)&reuseAddr, sizeof(reuseAddr));

        sockaddr_in serverAddr = {};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(port);

        if (bind(listenSocket_, (sockaddr*)&serverAddr,
                 sizeof(serverAddr)) == SOCKET_ERROR) {
            LOG_ERROR("Bind failed");
            return false;
        }

        if (listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR) {
            LOG_ERROR("Listen failed");
            return false;
        }

        LOG_INFO("Server listening on port {}", port);

        isRunning_ = true;
        for (int i = 0; i < workerThreadCount; ++i) {
            workerThreads_.emplace_back([this, i]() {
                WorkerThread(i);
            });
        }

        LOG_INFO("Created {} worker threads", workerThreadCount);
        return true;
    }

    void Run() {
        LOG_INFO("Server started");

        while (isRunning_) {
            sockaddr_in clientAddr = {};
            int addrLen = sizeof(clientAddr);

            SOCKET clientSocket = accept(
                listenSocket_,
                (sockaddr*)&clientAddr,
                &addrLen
            );

            if (clientSocket == INVALID_SOCKET) {
                if (isRunning_) {
                    LOG_ERROR("Accept failed");
                }
                continue;
            }

            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
            LOG_INFO("New connection from {}:{}", clientIP, ntohs(clientAddr.sin_port));

            auto session = std::make_shared<Session>(clientSocket);

            HANDLE result = CreateIoCompletionPort(
                (HANDLE)clientSocket,
                hCompletionPort_,
                (ULONG_PTR)session.get(),
                0
            );

            if (result != hCompletionPort_) {
                LOG_ERROR("Failed to associate socket with IOCP");
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(sessionsMutex_);
                sessions_[session->GetId()] = session;
            }

            if (!session->StartReceive()) {
                LOG_ERROR("Failed to start receive");
                RemoveSession(session->GetId());
            }
        }
    }

    void Shutdown() {
        if (!isRunning_.exchange(false)) {
            return;
        }

        LOG_INFO("Shutting down server");

        if (listenSocket_ != INVALID_SOCKET) {
            closesocket(listenSocket_);
            listenSocket_ = INVALID_SOCKET;
        }

        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            for (auto& pair : sessions_) {
                pair.second->Close();
            }
            sessions_.clear();
        }

        for (size_t i = 0; i < workerThreads_.size(); ++i) {
            PostQueuedCompletionStatus(hCompletionPort_, 0, 0, nullptr);
        }

        for (auto& thread : workerThreads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        if (hCompletionPort_ != nullptr) {
            CloseHandle(hCompletionPort_);
            hCompletionPort_ = nullptr;
        }

        LOG_INFO("Server shut down complete");
    }

private:
    void WorkerThread(int threadId) {
        LOG_INFO("Worker thread {} started", threadId);

        while (isRunning_) {
            DWORD bytesTransferred = 0;
            ULONG_PTR completionKey = 0;
            LPOVERLAPPED overlapped = nullptr;

            BOOL result = GetQueuedCompletionStatus(
                hCompletionPort_,
                &bytesTransferred,
                &completionKey,
                &overlapped,
                INFINITE
            );

            if (completionKey == 0 && overlapped == nullptr) {
                break;
            }

            if (overlapped == nullptr) {
                continue;
            }

            IOContext* ioContext = (IOContext*)overlapped;
            Session* session = (Session*)completionKey;

            if (result == FALSE || bytesTransferred == 0) {
                RemoveSession(session->GetId());
                continue;
            }

            if (ioContext->operation == IOOperation::RECV) {
                HandleReceive(session, ioContext, bytesTransferred);
            } else if (ioContext->operation == IOOperation::SEND) {
                HandleSend(session, ioContext, bytesTransferred);
            }
        }

        LOG_INFO("Worker thread {} terminated", threadId);
    }

    void HandleReceive(Session* session, IOContext* ioContext, DWORD bytesTransferred) {
        LOG_INFO("Received {} bytes from session {}", bytesTransferred, session->GetId());

        if (!session->Send(ioContext->buffer, bytesTransferred)) {
            RemoveSession(session->GetId());
            return;
        }
    }

    void HandleSend(Session* session, IOContext* ioContext, DWORD bytesTransferred) {
        LOG_INFO("Sent {} bytes to session {}", bytesTransferred, session->GetId());

        if (!session->StartReceive()) {
            RemoveSession(session->GetId());
        }
    }

    void RemoveSession(SessionID sessionId) {
        std::shared_ptr<Session> session;

        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            auto it = sessions_.find(sessionId);
            if (it != sessions_.end()) {
                session = it->second;
                sessions_.erase(it);
            }
        }

        if (session) {
            session->Close();
            LOG_INFO("Removed session {}", sessionId);
        }
    }
};
