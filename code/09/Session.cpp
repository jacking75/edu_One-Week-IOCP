
#include "Session.h"
#include "IOContext.h"
#include "SendContext.h"
#include "PacketReader.h"
#include "PacketWriter.h"
#include "PacketDispatcher.h"
#include "SessionManager.h"
#include "TimerManager.h"
#include "ServerGlobal.h"
#include "Protocol.h"
#include "Logger.h"

std::atomic<uint32_t> Session::s_nextSessionID{1};

constexpr size_t MAX_SEND_BATCH = 64;

Session::Session(SOCKET socket, TimerManager* pTimerManager)
    : sessionID_(s_nextSessionID.fetch_add(1))
    , socket_(socket)
    , pTimerManager_(pTimerManager)
{
    lastRecvTime_.store(std::chrono::steady_clock::now());
    LOG_INFO("세션 생성: SessionID={}", sessionID_);
}

Session::~Session()
{
    if (socket_ != INVALID_SOCKET)
    {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    LOG_INFO("세션 소멸: SessionID={}", sessionID_);
}

void Session::Initialize()
{
    CreateIoCompletionPort((HANDLE)socket_, g_iocpHandle,
                           (ULONG_PTR)0, 0);
    state_.store(SessionState::CONNECTED, std::memory_order_release);
    StartHeartbeat();
    RegisterRecv();
}

bool Session::TryDisconnect()
{
    SessionState expected = SessionState::CONNECTED;
    return state_.compare_exchange_strong(
        expected, SessionState::DISCONNECTING, std::memory_order_acq_rel
    );
}

bool Session::RegisterRecv()
{
    if (!IsConnected()) return false;

    recvBuffer_.Compact();

    auto ioContext = new IOContext();
    ioContext->session = shared_from_this();

    recvWsaBuf_.buf = recvBuffer_.GetWriteBuffer();
    recvWsaBuf_.len = static_cast<ULONG>(recvBuffer_.GetFreeSize());

    DWORD flags = 0;
    DWORD bytes = 0;

    int result = WSARecv(socket_, &recvWsaBuf_, 1, &bytes, &flags,
                        &ioContext->overlapped, nullptr);

    if (result == SOCKET_ERROR)
    {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            LOG_ERROR("WSARecv 실패: SessionID={}, Error={}", sessionID_, error);
            delete ioContext;
            Disconnect();
            return false;
        }
    }

    return true;
}

void Session::OnRecv(DWORD bytesTransferred)
{
    if (bytesTransferred == 0)
    {
        Disconnect();
        return;
    }

    // 수신 시간 업데이트
    UpdateLastRecvTime();

    EnqueueJob([this, bytesTransferred]() {
        recvBuffer_.MoveWritePos(bytesTransferred);
        ProcessReceivedData();
        RegisterRecv();
    });

    TryProcessJobs();
}

void Session::ProcessReceivedData()
{
    while (recvBuffer_.GetDataSize() >= PACKET_HEADER_SIZE)
    {
        const char* bufferPtr = recvBuffer_.GetReadBuffer();

        uint16_t packetSize = *reinterpret_cast<const uint16_t*>(bufferPtr);
        uint16_t packetID = *reinterpret_cast<const uint16_t*>(bufferPtr + 2);

        if (packetSize < PACKET_HEADER_SIZE || packetSize > MAX_PACKET_SIZE)
        {
            LOG_ERROR("잘못된 패킷 크기: Size={}, SessionID={}", packetSize, sessionID_);
            Disconnect();
            break;
        }

        if (recvBuffer_.GetDataSize() < packetSize)
            break;

        // Pong 패킷 특별 처리
        if (packetID == static_cast<uint16_t>(HeartbeatPacketId::Pong))
        {
            if (packetSize >= sizeof(PongPacket))
            {
                const PongPacket* pong = reinterpret_cast<const PongPacket*>(bufferPtr);
                OnPongReceived(pong->timestamp);
            }
            recvBuffer_.MoveReadPos(packetSize);
            continue;
        }

        // 일반 패킷 처리
        PacketReader reader(bufferPtr, packetSize);

        auto handler = PacketDispatcher::Instance().GetHandler(packetID);
        if (handler)
        {
            try { handler->Handle(shared_from_this(), reader); }
            catch (const std::exception& e) {
                LOG_ERROR("패킷 처리 예외: PacketID={}, SessionID={}, Error={}",
                         packetID, sessionID_, e.what());
            }
        }
        else
        {
            LOG_WARNING("등록되지 않은 패킷: PacketID={}, SessionID={}",
                       packetID, sessionID_);
        }

        recvBuffer_.MoveReadPos(packetSize);
    }
}

void Session::Send(PacketPtr packet)
{
    sendQueue_.push(std::move(packet));
    RegisterSend();
}

bool Session::RegisterSend()
{
    bool expected = false;
    if (!isSending_.compare_exchange_strong(expected, true))
        return false;

    auto sendContext = new SendContext();
    sendContext->session = shared_from_this();

    PacketPtr packet;
    while (sendQueue_.try_pop(packet) &&
           sendContext->packets.size() < MAX_SEND_BATCH)
    {
        sendContext->AddPacket(std::move(packet));
    }

    if (sendContext->packets.empty())
    {
        delete sendContext;
        isSending_.store(false);
        return false;
    }

    DWORD bytesSent = 0;
    int result = WSASend(socket_, sendContext->GetBufferArray(),
                        sendContext->GetBufferCount(), &bytesSent, 0,
                        &sendContext->overlapped, nullptr);

    if (result == SOCKET_ERROR)
    {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            LOG_ERROR("WSASend 실패: SessionID={}, Error={}", sessionID_, error);
            delete sendContext;
            isSending_.store(false);
            Disconnect();
            return false;
        }
    }

    return true;
}

void Session::OnSend(SendContext* sendContext, DWORD bytesTransferred)
{
    LOG_DEBUG("송신 완료: SessionID={}, PacketCount={}, Bytes={}",
             sessionID_, sendContext->packets.size(), bytesTransferred);

    delete sendContext;
    isSending_.store(false);

    if (!sendQueue_.empty())
        RegisterSend();
}

// 하트비트 구현

void Session::StartHeartbeat()
{
    if (!pTimerManager_) return;

    // 15초마다 Ping 전송
    heartbeatTimerId_ = pTimerManager_->AddTimer(
        std::chrono::seconds(15),
        [this]() {
            if (this->IsConnected()) {
                this->SendPing();
            }
        },
        std::chrono::seconds(15)
    );

    // 5초마다 타임아웃 체크
    timeoutCheckTimerId_ = pTimerManager_->AddTimer(
        std::chrono::seconds(5),
        [this]() {
            if (this->IsConnected()) {
                this->CheckHeartbeatTimeout();
            }
        },
        std::chrono::seconds(5)
    );

    LOG_INFO("하트비트 시작: SessionID={}", sessionID_);
}

void Session::StopHeartbeat()
{
    if (!pTimerManager_) return;

    if (heartbeatTimerId_ != 0) {
        pTimerManager_->CancelTimer(heartbeatTimerId_);
        heartbeatTimerId_ = 0;
    }

    if (timeoutCheckTimerId_ != 0) {
        pTimerManager_->CancelTimer(timeoutCheckTimerId_);
        timeoutCheckTimerId_ = 0;
    }
}

void Session::SendPing()
{
    PingPacket ping{};
    ping.header.size = sizeof(PingPacket);
    ping.header.packetId = static_cast<uint16_t>(HeartbeatPacketId::Ping);
    ping.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    auto packet = std::make_shared<Packet>(sizeof(PingPacket));
    memcpy(packet->GetBuffer(), &ping, sizeof(PingPacket));

    Send(packet);
}

void Session::OnPongReceived(uint64_t timestamp)
{
    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    uint64_t rtt = now - timestamp;
    LOG_INFO("Pong 수신: SessionID={}, RTT={}ms", sessionID_, rtt);
}

void Session::CheckHeartbeatTimeout()
{
    auto now = std::chrono::steady_clock::now();
    auto lastRecv = lastRecvTime_.load();

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastRecv);

    if (elapsed > heartbeatTimeout_)
    {
        LOG_WARNING("하트비트 타임아웃: SessionID={}, Elapsed={}s",
                   sessionID_, elapsed.count());
        ForceDisconnect(DisconnectReason::Timeout);
    }
}

void Session::UpdateLastRecvTime()
{
    lastRecvTime_.store(std::chrono::steady_clock::now());
}

void Session::ForceDisconnect(DisconnectReason reason)
{
    SessionState expected = SessionState::CONNECTED;
    if (!state_.compare_exchange_strong(expected, SessionState::DISCONNECTING))
        return;

    LOG_INFO("강제 연결 종료: SessionID={}, Reason={}",
             sessionID_, static_cast<int>(reason));

    StopHeartbeat();

    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    state_.store(SessionState::DISCONNECTED, std::memory_order_release);

    EnqueueJob([this]() { OnDisconnected(); });
    TryProcessJobs();
}

void Session::Disconnect()
{
    if (!TryDisconnect()) return;

    LOG_INFO("세션 연결 종료: SessionID={}", sessionID_);

    StopHeartbeat();

    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    state_.store(SessionState::DISCONNECTED, std::memory_order_release);

    EnqueueJob([this]() { OnDisconnected(); });
    TryProcessJobs();
}

void Session::OnDisconnected()
{
    LOG_INFO("세션 연결 종료 완료: SessionID={}, UserID={}",
             sessionID_, userID_);
    SessionManager::Instance().RemoveSession(sessionID_);
}

void Session::EnqueueJob(Job job)
{
    jobQueue_.push(std::move(job));
}

void Session::TryProcessJobs()
{
    if (!TryBeginProcess()) return;

    while (true)
    {
        Job job;
        if (!jobQueue_.try_pop(job))
        {
            EndProcess();
            if (!jobQueue_.empty() && TryBeginProcess())
                continue;
            break;
        }

        try { job(); }
        catch (const std::exception& e) {
            LOG_ERROR("작업 실행 예외: SessionID={}, Error={}",
                     sessionID_, e.what());
        }
    }
}

bool Session::TryBeginProcess()
{
    int32_t expected = 0;
    return isProcessing_.compare_exchange_strong(
        expected, 1, std::memory_order_acq_rel);
}

void Session::EndProcess()
{
    isProcessing_.store(0, std::memory_order_release);
}
