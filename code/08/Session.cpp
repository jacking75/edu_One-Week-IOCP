
#include "Session.h"
#include "IOContext.h"
#include "SendContext.h"
#include "PacketReader.h"
#include "PacketDispatcher.h"
#include "SessionManager.h"
#include "ServerGlobal.h"
#include "Logger.h"

std::atomic<uint32_t> Session::s_nextSessionID{1};

constexpr size_t MAX_SEND_BATCH = 64;

Session::Session(SOCKET socket)
    : sessionID_(s_nextSessionID.fetch_add(1))
    , socket_(socket)
{
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
    RegisterRecv();
}

bool Session::TryDisconnect()
{
    SessionState expected = SessionState::CONNECTED;
    return state_.compare_exchange_strong(
        expected,
        SessionState::DISCONNECTING,
        std::memory_order_acq_rel
    );
}

bool Session::RegisterRecv()
{
    if (!IsConnected())
        return false;

    // 버퍼 정리 (읽은 데이터 제거)
    recvBuffer_.Compact();

    // IOContext 생성
    auto ioContext = new IOContext();
    ioContext->session = shared_from_this();

    // WSABUF 설정
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
        // Zero-Copy: 버퍼에서 직접 읽기
        const char* bufferPtr = recvBuffer_.GetReadBuffer();

        uint16_t packetSize = *reinterpret_cast<const uint16_t*>(bufferPtr);
        uint16_t packetID = *reinterpret_cast<const uint16_t*>(bufferPtr + 2);

        // 크기 검증
        if (packetSize < PACKET_HEADER_SIZE || packetSize > MAX_PACKET_SIZE)
        {
            LOG_ERROR("잘못된 패킷 크기: Size={}, SessionID={}",
                     packetSize, sessionID_);
            Disconnect();
            break;
        }

        // 완전한 패킷 확인
        if (recvBuffer_.GetDataSize() < packetSize)
            break;

        // Zero-Copy 파싱
        PacketReader reader(bufferPtr, packetSize);

        // 핸들러 실행
        auto handler = PacketDispatcher::Instance().GetHandler(packetID);
        if (handler)
        {
            try
            {
                handler->Handle(shared_from_this(), reader);
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("패킷 처리 예외: PacketID={}, SessionID={}, Error={}",
                         packetID, sessionID_, e.what());
            }
        }
        else
        {
            LOG_WARNING("등록되지 않은 패킷: PacketID={}, SessionID={}",
                       packetID, sessionID_);
        }

        // 읽은 만큼 전진
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
    // 중복 송신 방지
    bool expected = false;
    if (!isSending_.compare_exchange_strong(expected, true))
        return false;

    // SendContext 생성
    auto sendContext = new SendContext();
    sendContext->session = shared_from_this();

    // 큐에서 패킷들 가져오기 (Scatter-Gather I/O)
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

    // Gather I/O: 여러 패킷을 한 번에 전송
    DWORD bytesSent = 0;
    int result = WSASend(
        socket_,
        sendContext->GetBufferArray(),
        sendContext->GetBufferCount(),
        &bytesSent,
        0,
        &sendContext->overlapped,
        nullptr
    );

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

    // SendContext 정리 (패킷들의 shared_ptr 자동 해제)
    delete sendContext;

    // 송신 플래그 해제
    isSending_.store(false);

    // 큐에 남은 패킷이 있으면 계속 송신
    if (!sendQueue_.empty())
    {
        RegisterSend();
    }
}

void Session::Disconnect()
{
    if (!TryDisconnect())
        return;

    LOG_INFO("세션 연결 종료 시작: SessionID={}", sessionID_);

    if (socket_ != INVALID_SOCKET)
    {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    state_.store(SessionState::DISCONNECTED, std::memory_order_release);

    EnqueueJob([this]() {
        OnDisconnected();
    });

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
    if (!TryBeginProcess())
        return;

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

        try
        {
            job();
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("작업 실행 예외: SessionID={}, Error={}",
                     sessionID_, e.what());
        }
    }
}

bool Session::TryBeginProcess()
{
    int32_t expected = 0;
    return isProcessing_.compare_exchange_strong(
        expected, 1, std::memory_order_acq_rel
    );
}

void Session::EndProcess()
{
    isProcessing_.store(0, std::memory_order_release);
}
