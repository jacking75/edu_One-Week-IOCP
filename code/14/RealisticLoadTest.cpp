// Chapter 14.3 Scenario 4: Realistic Game Play Simulation
// Minimal IOCP Omok server + realistic dummy clients in one executable.
// Builds the matchmaking -> turn-based play -> game-end flow from the chapter.

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mswsock.h>
#include <iostream>
#include <thread>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <unordered_map>
#include <random>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

constexpr int  SERVER_PORT = 9014;
constexpr int  BOARD_SIZE  = 19;
constexpr int  MAX_TURNS   = 200;   // high cap so games rarely end mid-test
constexpr int  RECV_BUF    = 4096;

enum PacketType : uint16_t {
    C2S_LOGIN              = 1,
    C2S_PLACE_STONE        = 2,
    S2C_LOGIN_OK           = 101,
    S2C_MATCH_START        = 102,
    S2C_PLACE_STONE_RESULT = 103,
    S2C_OPPONENT_MOVE      = 104,
    S2C_GAME_OVER          = 105,
};

#pragma pack(push, 1)
struct PacketHeader { uint16_t size; uint16_t type; };
struct LoginPacket            { PacketHeader h; int32_t clientId; };
struct LoginOkPacket          { PacketHeader h; int32_t sessionId; };
struct MatchStartPacket       { PacketHeader h; int32_t gameId; int32_t firstTurn; };
struct PlaceStonePacket       { PacketHeader h; int32_t row; int32_t col; };
struct PlaceStoneResultPacket { PacketHeader h; int32_t row; int32_t col; int32_t success; };
struct OpponentMovePacket     { PacketHeader h; int32_t row; int32_t col; };
struct GameOverPacket         { PacketHeader h; int32_t winner; };
#pragma pack(pop)

//-------------------------------------------------------------
// Server
//-------------------------------------------------------------
enum IOType { IO_RECV };
struct PerIoData {
    OVERLAPPED ov;
    WSABUF     wsabuf;
    char       buffer[RECV_BUF];
    IOType     type;
};

struct Session {
    SOCKET             sock = INVALID_SOCKET;
    int                sessionId  = 0;
    int                gameId     = -1;
    int                playerIndex = -1;
    PerIoData          io{};
    std::vector<char>  recvBuf;
    std::mutex         sendMutex;
};

class OmokServer {
public:
    bool Start(int port) {
        listenSock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSock_ == INVALID_SOCKET) return false;
        BOOL reuse = TRUE;
        setsockopt(listenSock_, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons(static_cast<u_short>(port));
        if (bind(listenSock_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) return false;
        if (listen(listenSock_, SOMAXCONN) == SOCKET_ERROR) return false;

        iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (!iocp_) return false;

        SYSTEM_INFO si; GetSystemInfo(&si);
        int workerCount = static_cast<int>(si.dwNumberOfProcessors);
        if (workerCount < 2) workerCount = 2;
        for (int i = 0; i < workerCount; ++i) {
            workers_.emplace_back([this]{ WorkerLoop(); });
        }
        acceptThread_ = std::thread([this]{ AcceptLoop(); });
        return true;
    }

    void Stop() {
        running_ = false;
        closesocket(listenSock_);
        if (acceptThread_.joinable()) acceptThread_.join();

        std::vector<std::shared_ptr<Session>> all;
        {
            std::lock_guard<std::mutex> lk(sessionsMutex_);
            for (auto& kv : sessions_) all.push_back(kv.second);
            sessions_.clear();
        }
        for (auto& s : all) closesocket(s->sock);

        for (size_t i = 0; i < workers_.size(); ++i) {
            PostQueuedCompletionStatus(iocp_, 0, 0, nullptr);
        }
        for (auto& t : workers_) if (t.joinable()) t.join();
        CloseHandle(iocp_);
    }

private:
    struct Game { int p0; int p1; int currentTurn; int turnsPlayed; };

    void AcceptLoop() {
        while (running_) {
            sockaddr_in client{};
            int len = sizeof(client);
            SOCKET s = accept(listenSock_, (sockaddr*)&client, &len);
            if (s == INVALID_SOCKET) break;

            auto session = std::make_shared<Session>();
            session->sock = s;
            session->sessionId = nextSessionId_.fetch_add(1);
            {
                std::lock_guard<std::mutex> lk(sessionsMutex_);
                sessions_[session->sessionId] = session;
            }
            CreateIoCompletionPort((HANDLE)s, iocp_, (ULONG_PTR)session->sessionId, 0);
            PostRecv(session.get());
        }
    }

    void PostRecv(Session* sess) {
        ZeroMemory(&sess->io.ov, sizeof(OVERLAPPED));
        sess->io.type       = IO_RECV;
        sess->io.wsabuf.buf = sess->io.buffer;
        sess->io.wsabuf.len = RECV_BUF;
        DWORD flags = 0, bytes = 0;
        if (WSARecv(sess->sock, &sess->io.wsabuf, 1, &bytes, &flags, &sess->io.ov, nullptr) == SOCKET_ERROR) {
            if (WSAGetLastError() != WSA_IO_PENDING) {
                CloseSession(sess->sessionId);
            }
        }
    }

    void WorkerLoop() {
        while (true) {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            LPOVERLAPPED ov = nullptr;
            BOOL ok = GetQueuedCompletionStatus(iocp_, &bytes, &key, &ov, INFINITE);
            if (!ov) return; // termination signal
            auto sess = GetSession(static_cast<int>(key));
            if (!sess) continue;
            if (!ok || bytes == 0) {
                CloseSession(static_cast<int>(key));
                continue;
            }
            sess->recvBuf.insert(sess->recvBuf.end(),
                                 sess->io.buffer, sess->io.buffer + bytes);
            while (sess->recvBuf.size() >= sizeof(PacketHeader)) {
                auto* h = reinterpret_cast<PacketHeader*>(sess->recvBuf.data());
                if (h->size == 0 || sess->recvBuf.size() < h->size) break;
                HandlePacket(sess.get(), sess->recvBuf.data(), h->size);
                sess->recvBuf.erase(sess->recvBuf.begin(),
                                    sess->recvBuf.begin() + h->size);
            }
            PostRecv(sess.get());
        }
    }

    void HandlePacket(Session* sess, const char* data, int /*size*/) {
        auto* h = reinterpret_cast<const PacketHeader*>(data);
        switch (h->type) {
        case C2S_LOGIN: {
            LoginOkPacket p{};
            p.h.size = sizeof(p);
            p.h.type = S2C_LOGIN_OK;
            p.sessionId = sess->sessionId;
            SendTo(sess, &p, sizeof(p));
            EnqueueForMatching(sess->sessionId);
            break;
        }
        case C2S_PLACE_STONE: {
            auto* ps = reinterpret_cast<const PlaceStonePacket*>(data);
            HandlePlaceStone(sess, ps->row, ps->col);
            break;
        }
        default: break;
        }
    }

    void EnqueueForMatching(int sid) {
        int pair = -1;
        {
            std::lock_guard<std::mutex> lk(matchMutex_);
            if (waitingQueue_.empty()) {
                waitingQueue_.push_back(sid);
                return;
            }
            pair = waitingQueue_.front();
            waitingQueue_.pop_front();
        }
        auto s1 = GetSession(pair);
        auto s2 = GetSession(sid);
        if (!s1 || !s2) return;
        int gid = nextGameId_.fetch_add(1);
        s1->gameId = gid; s1->playerIndex = 0;
        s2->gameId = gid; s2->playerIndex = 1;
        {
            std::lock_guard<std::mutex> lk(gamesMutex_);
            games_[gid] = Game{ pair, sid, 0, 0 };
        }
        MatchStartPacket m{};
        m.h.size = sizeof(m);
        m.h.type = S2C_MATCH_START;
        m.gameId = gid;
        m.firstTurn = 1; SendTo(s1.get(), &m, sizeof(m));
        m.firstTurn = 0; SendTo(s2.get(), &m, sizeof(m));
    }

    void HandlePlaceStone(Session* sess, int row, int col) {
        int gid = sess->gameId;
        if (gid < 0) return;
        int oppSid = -1;
        bool gameOver = false;
        {
            std::lock_guard<std::mutex> lk(gamesMutex_);
            auto it = games_.find(gid);
            if (it == games_.end()) return;
            auto& g = it->second;
            if (g.currentTurn != sess->playerIndex) {
                PlaceStoneResultPacket r{};
                r.h.size = sizeof(r);
                r.h.type = S2C_PLACE_STONE_RESULT;
                r.row = row; r.col = col; r.success = 0;
                SendTo(sess, &r, sizeof(r));
                return;
            }
            g.turnsPlayed++;
            g.currentTurn = 1 - g.currentTurn;
            oppSid = (sess->playerIndex == 0) ? g.p1 : g.p0;
            if (g.turnsPlayed >= MAX_TURNS) gameOver = true;
        }
        PlaceStoneResultPacket r{};
        r.h.size = sizeof(r);
        r.h.type = S2C_PLACE_STONE_RESULT;
        r.row = row; r.col = col; r.success = 1;
        SendTo(sess, &r, sizeof(r));

        auto opp = GetSession(oppSid);
        if (opp) {
            OpponentMovePacket om{};
            om.h.size = sizeof(om);
            om.h.type = S2C_OPPONENT_MOVE;
            om.row = row; om.col = col;
            SendTo(opp.get(), &om, sizeof(om));
        }
        if (gameOver) {
            GameOverPacket go{};
            go.h.size = sizeof(go);
            go.h.type = S2C_GAME_OVER;
            go.winner = sess->playerIndex;
            SendTo(sess, &go, sizeof(go));
            if (opp) SendTo(opp.get(), &go, sizeof(go));
            std::lock_guard<std::mutex> lk(gamesMutex_);
            games_.erase(gid);
        }
    }

    void SendTo(Session* sess, const void* data, int size) {
        std::lock_guard<std::mutex> lk(sess->sendMutex);
        send(sess->sock, reinterpret_cast<const char*>(data), size, 0);
    }

    std::shared_ptr<Session> GetSession(int sid) {
        std::lock_guard<std::mutex> lk(sessionsMutex_);
        auto it = sessions_.find(sid);
        return (it == sessions_.end()) ? nullptr : it->second;
    }

    void CloseSession(int sid) {
        std::shared_ptr<Session> sess;
        {
            std::lock_guard<std::mutex> lk(sessionsMutex_);
            auto it = sessions_.find(sid);
            if (it == sessions_.end()) return;
            sess = it->second;
            sessions_.erase(it);
        }
        closesocket(sess->sock);
    }

    SOCKET listenSock_ = INVALID_SOCKET;
    HANDLE iocp_ = nullptr;
    std::atomic<bool> running_{true};
    std::atomic<int>  nextSessionId_{1};
    std::atomic<int>  nextGameId_{1};
    std::thread              acceptThread_;
    std::vector<std::thread> workers_;

    std::mutex sessionsMutex_;
    std::unordered_map<int, std::shared_ptr<Session>> sessions_;

    std::mutex        matchMutex_;
    std::deque<int>   waitingQueue_;

    std::mutex        gamesMutex_;
    std::unordered_map<int, Game> games_;
};

//-------------------------------------------------------------
// Realistic dummy client (Scenario 4)
//-------------------------------------------------------------
class RealisticDummyClient {
public:
    explicit RealisticDummyClient(int id) : id_(id), rng_(static_cast<uint32_t>(id * 2654435761u)) {}

    bool Connect(const char* ip, int port) {
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(static_cast<u_short>(port));
        inet_pton(AF_INET, ip, &addr.sin_addr);
        if (connect(socket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
            return false;
        }
        return true;
    }

    void Run(std::atomic<bool>& stop) {
        running_ = true;
        recvThread_ = std::thread([this]{ RecvLoop(); });
        GamePlayLoop(stop);
        shutdown(socket_, SD_BOTH);
        closesocket(socket_);
        if (recvThread_.joinable()) recvThread_.join();
    }

    const std::vector<int64_t>& GetLatencies() const { return latencies_; }
    int GetMatchesPlayed() const { return matchesPlayed_; }

private:
    struct InPacket { uint16_t type; std::vector<char> data; };

    // Scenario 4: match wait -> turn-based play -> game end -> re-queue
    void GamePlayLoop(std::atomic<bool>& stop) {
        while (!stop.load() && running_) {
            LoginPacket lp{};
            lp.h.size  = sizeof(lp);
            lp.h.type  = C2S_LOGIN;
            lp.clientId = id_;
            if (send(socket_, (char*)&lp, sizeof(lp), 0) == SOCKET_ERROR) return;

            InPacket p;
            if (!PopNext(p, 5000, stop) || p.type != S2C_LOGIN_OK) return;

            // 1. Wait for match
            if (!PopNext(p, 30000, stop) || p.type != S2C_MATCH_START) return;
            bool myTurn =
                reinterpret_cast<MatchStartPacket*>(p.data.data())->firstTurn == 1;
            matchesPlayed_++;

            // 2. Play turns
            bool ended = false;
            for (int turn = 0; turn < MAX_TURNS + 10 && !stop.load() && !ended; ++turn) {
                if (myTurn) {
                    // thinking time (compressed: 50~150ms instead of 1~3s for test)
                    std::uniform_int_distribution<int> think(50, 150);
                    std::this_thread::sleep_for(std::chrono::milliseconds(think(rng_)));
                    if (stop.load()) return;

                    std::uniform_int_distribution<int> pos(0, BOARD_SIZE - 1);
                    PlaceStonePacket ps{};
                    ps.h.size = sizeof(ps);
                    ps.h.type = C2S_PLACE_STONE;
                    ps.row = pos(rng_);
                    ps.col = pos(rng_);
                    auto sendTime = std::chrono::steady_clock::now();
                    if (send(socket_, (char*)&ps, sizeof(ps), 0) == SOCKET_ERROR) return;

                    if (!PopNext(p, 5000, stop)) return;
                    auto recvTime = std::chrono::steady_clock::now();
                    latencies_.push_back(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            recvTime - sendTime).count());

                    if (p.type == S2C_GAME_OVER) { ended = true; break; }
                    if (p.type != S2C_PLACE_STONE_RESULT) return;
                    // Drain possible GameOver that follows immediately.
                    if (TryPeekGameOver(stop)) { ended = true; break; }
                } else {
                    if (!PopNext(p, 30000, stop)) return;
                    if (p.type == S2C_GAME_OVER) { ended = true; break; }
                    if (p.type != S2C_OPPONENT_MOVE) return;
                    if (TryPeekGameOver(stop)) { ended = true; break; }
                }
                myTurn = !myTurn;
            }

            // 3. Short delay before re-queuing (scenario says ~5s; compressed here)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    bool TryPeekGameOver(std::atomic<bool>& stop) {
        InPacket p;
        if (PopNext(p, 5, stop)) {
            if (p.type == S2C_GAME_OVER) return true;
            // Not game over — push back at front.
            std::lock_guard<std::mutex> lk(mx_);
            inQueue_.push_front(std::move(p));
        }
        return false;
    }

    void RecvLoop() {
        std::vector<char> buf;
        char tmp[RECV_BUF];
        while (running_) {
            int n = recv(socket_, tmp, sizeof(tmp), 0);
            if (n <= 0) {
                running_ = false;
                cv_.notify_all();
                break;
            }
            buf.insert(buf.end(), tmp, tmp + n);
            while (buf.size() >= sizeof(PacketHeader)) {
                auto* h = reinterpret_cast<PacketHeader*>(buf.data());
                if (h->size == 0 || buf.size() < h->size) break;
                InPacket p;
                p.type = h->type;
                p.data.assign(buf.begin(), buf.begin() + h->size);
                {
                    std::lock_guard<std::mutex> lk(mx_);
                    inQueue_.push_back(std::move(p));
                }
                cv_.notify_all();
                buf.erase(buf.begin(), buf.begin() + h->size);
            }
        }
    }

    bool PopNext(InPacket& out, int timeoutMs, std::atomic<bool>& stop) {
        std::unique_lock<std::mutex> lk(mx_);
        if (!cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&]{
            return !running_ || stop.load() || !inQueue_.empty();
        })) return false;
        if (stop.load() || inQueue_.empty()) return false;
        out = std::move(inQueue_.front());
        inQueue_.pop_front();
        return true;
    }

    int id_;
    SOCKET socket_ = INVALID_SOCKET;
    std::atomic<bool> running_{false};
    std::thread       recvThread_;
    std::mt19937      rng_;

    std::mutex               mx_;
    std::condition_variable  cv_;
    std::deque<InPacket>     inQueue_;

    std::vector<int64_t> latencies_;
    int matchesPlayed_ = 0;
};

//-------------------------------------------------------------
// Load Test Manager
//-------------------------------------------------------------
class LoadTestManager {
public:
    void StartTest(const char* serverIP, int port, int clientCount) {
        std::cout << "Starting scenario-4 load test with "
                  << clientCount << " clients...\n";
        for (int i = 0; i < clientCount; ++i) {
            auto client = std::make_unique<RealisticDummyClient>(i);
            if (!client->Connect(serverIP, port)) {
                std::cerr << "Client " << i << " failed to connect\n";
                continue;
            }
            RealisticDummyClient* raw = client.get();
            std::thread t([raw, this]{ raw->Run(stop_); });
            clients_.push_back(std::move(client));
            threads_.push_back(std::move(t));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::cout << clients_.size() << " clients connected.\n";
    }

    void RunFor(int seconds) {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        stop_.store(true);
        for (auto& t : threads_) if (t.joinable()) t.join();
    }

    void PrintStatistics() {
        std::vector<int64_t> all;
        int totalMatches = 0;
        for (auto& c : clients_) {
            const auto& l = c->GetLatencies();
            all.insert(all.end(), l.begin(), l.end());
            totalMatches += c->GetMatchesPlayed();
        }
        std::cout << "\n=== Scenario 4 Results ===\n";
        std::cout << "Matches started:  " << totalMatches << "\n";
        std::cout << "Measured rounds:  " << all.size() << "\n";
        if (all.empty()) return;
        std::sort(all.begin(), all.end());
        double avg = static_cast<double>(
            std::accumulate(all.begin(), all.end(), 0LL)) / all.size();
        int64_t p50 = all[all.size() * 50 / 100];
        int64_t p95 = all[all.size() * 95 / 100];
        size_t p99idx = all.size() * 99 / 100;
        if (p99idx >= all.size()) p99idx = all.size() - 1;
        int64_t p99 = all[p99idx];
        std::cout << "Avg Latency:      " << avg << " ms\n";
        std::cout << "P50 Latency:      " << p50 << " ms\n";
        std::cout << "P95 Latency:      " << p95 << " ms\n";
        std::cout << "P99 Latency:      " << p99 << " ms\n";
    }

private:
    std::atomic<bool> stop_{false};
    std::vector<std::unique_ptr<RealisticDummyClient>> clients_;
    std::vector<std::thread> threads_;
};

//-------------------------------------------------------------
// main
//-------------------------------------------------------------
int main(int argc, char** argv) {
    int clientCount = 20;
    int durationSec = 10;
    if (argc > 1) clientCount = std::atoi(argv[1]);
    if (argc > 2) durationSec = std::atoi(argv[2]);
    if (clientCount < 2) clientCount = 2;
    if (clientCount % 2 == 1) clientCount++; // pair up

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    OmokServer server;
    if (!server.Start(SERVER_PORT)) {
        std::cerr << "Server failed to start on port " << SERVER_PORT << "\n";
        WSACleanup();
        return 1;
    }
    std::cout << "Server listening on 127.0.0.1:" << SERVER_PORT << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    LoadTestManager mgr;
    mgr.StartTest("127.0.0.1", SERVER_PORT, clientCount);
    std::cout << "Running for " << durationSec << " seconds...\n";
    mgr.RunFor(durationSec);
    mgr.PrintStatistics();

    server.Stop();
    WSACleanup();
    std::cout << "Done.\n";
    return 0;
}
