
#include "Session.h"
#include "PacketDispatcher.h"
#include "SessionManager.h"
#include "ServerGlobal.h"
#include "Logger.h"
#include <MSWSock.h>

std::atomic<uint32_t> Session::s_nextSessionID{1};

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

bool Session::RegisterRecv()
{
    if (!IsConnected())
        return false;

    // IOContext 생성 (shared_ptr 참조 포함)
    auto ioContext = new IOContext{};
    ioContext->session = shared_from_this();
    ioContext->ioType = IOType::RECV;

    // 수신 버퍼 준비
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

    // 작업을 큐에 추가하고 직렬 처리
    EnqueueJob([this, bytesTransferred]() {
        // 수신 버퍼 업데이트
        recvBuffer_.MoveWritePos(bytesTransferred);

        // 패킷 처리
        ProcessPackets();

        // 다음 수신 등록
        RegisterRecv();
    });

    TryProcessJobs();
}

void Session::ProcessPackets()
{
    // 완전한 패킷들 추출
    auto packets = ExtractPackets();

    // 각 패킷을 디스패처로 전달
    for (auto& packet : packets)
    {
        PacketDispatcher::Instance().Dispatch(shared_from_this(), packet);
    }
}

std::vector<PacketPtr> Session::ExtractPackets()
{
    std::vector<PacketPtr> packets;

    while (recvBuffer_.GetDataSize() >= PACKET_HEADER_SIZE)
    {
        // 패킷 크기 읽기 (Peek)
        uint16_t packetSize = 0;
        recvBuffer_.Peek(reinterpret_cast<char*>(&packetSize), sizeof(packetSize));

        // 패킷 크기 검증
        if (packetSize < PACKET_HEADER_SIZE || packetSize > MAX_PACKET_SIZE)
        {
            LOG_ERROR("잘못된 패킷 크기: Size={}, SessionID={}",
                     packetSize, sessionID_);
            Disconnect();
            break;
        }

        // 완전한 패킷이 도착했는지 확인
        if (recvBuffer_.GetDataSize() < packetSize)
            break;

        // 완전한 패킷 추출
        auto packet = std::make_shared<Packet>(packetSize);
        size_t readSize = recvBuffer_.Read(packet->GetBuffer(), packetSize);

        if (readSize != packetSize)
        {
            LOG_ERROR("패킷 읽기 실패: Expected={}, Actual={}, SessionID={}",
                     packetSize, readSize, sessionID_);
            Disconnect();
            break;
        }

        packets.push_back(packet);
    }

    return packets;
}

void Session::Send(PacketPtr packet)
{
    // 송신 큐에 추가 (thread-safe)
    sendQueue_.push(packet);

    // 송신 시작 시도
    RegisterSend();
}

bool Session::RegisterSend()
{
    // 이미 송신 중이면 중복 등록 방지
    bool expected = false;
    if (!isSending_.compare_exchange_strong(expected, true))
        return false;

    // 송신 큐에서 패킷들 가져오기
    std::vector<PacketPtr> packets;
    PacketPtr packet;

    while (sendQueue_.try_pop(packet))
    {
        packets.push_back(packet);

        // 한 번에 최대 64개까지
        if (packets.size() >= 64)
            break;
    }

    if (packets.empty())
    {
        isSending_.store(false);
        return false;
    }

    // 송신 버퍼에 패킷들 복사
    sendBuffer_.Clear();
    for (auto& pkt : packets)
    {
        sendBuffer_.Write(pkt->GetBuffer(), pkt->GetSize());
    }

    // IOContext 생성
    auto ioContext = new IOContext{};
    ioContext->session = shared_from_this();
    ioContext->ioType = IOType::SEND;

    // WSABUF 설정
    sendWsaBuf_.buf = sendBuffer_.GetReadBuffer();
    sendWsaBuf_.len = static_cast<ULONG>(sendBuffer_.GetDataSize());

    DWORD bytes = 0;

    int result = WSASend(socket_, &sendWsaBuf_, 1, &bytes, 0,
                        &ioContext->overlapped, nullptr);

    if (result == SOCKET_ERROR)
    {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            LOG_ERROR("WSASend 실패: SessionID={}, Error={}", sessionID_, error);
            delete ioContext;
            isSending_.store(false);
            Disconnect();
            return false;
        }
    }

    return true;
}

void Session::OnSend(DWORD bytesTransferred)
{
    // 송신 완료
    sendBuffer_.MoveReadPos(bytesTransferred);

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
    // 이미 연결 종료 중이면 무시
    if (!TryDisconnect())
        return;

    LOG_INFO("세션 연결 종료 시작: SessionID={}", sessionID_);

    // 소켓 종료
    if (socket_ != INVALID_SOCKET)
    {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    // 상태 변경
    state_.store(SessionState::DISCONNECTED, std::memory_order_release);

    // 정리 작업
    EnqueueJob([this]() {
        OnDisconnected();
    });

    TryProcessJobs();
}

void Session::OnDisconnected()
{
    LOG_INFO("세션 연결 종료 완료: SessionID={}, UserID={}",
             sessionID_, userID_);

    // SessionManager에서 제거
    SessionManager::Instance().RemoveSession(sessionID_);
}

void Session::EnqueueJob(Job job)
{
    jobQueue_.push(std::move(job));
}

void Session::TryProcessJobs()
{
    // 처리 중이 아니면 처리 시작
    if (!TryBeginProcess())
        return;

    // 큐가 빌 때까지 처리
    while (true)
    {
        Job job;
        if (!jobQueue_.try_pop(job))
        {
            // 큐가 비었으면 종료
            EndProcess();

            // Double-Check: 끝내는 순간 추가된 작업 확인
            if (!jobQueue_.empty() && TryBeginProcess())
                continue;

            break;
        }

        try
        {
            job();  // 작업 실행
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
