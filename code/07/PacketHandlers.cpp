
#include "PacketHandler.h"
#include "PacketDispatcher.h"
#include "SessionManager.h"
#include "Logger.h"

constexpr size_t MAX_USER_ID_LENGTH = 20;
constexpr size_t MAX_PASSWORD_LENGTH = 20;
constexpr size_t MAX_CHAT_MESSAGE_LENGTH = 256;
constexpr int32_t MAP_WIDTH = 1000;
constexpr int32_t MAP_HEIGHT = 1000;

void AuthenticatedPacketHandler::SendErrorResponse(SessionPtr session,
                                                   ErrorCode errorCode,
                                                   const std::string& message) {
    PacketWriter writer(PKT_ERROR_RESPONSE);
    writer.Write<uint16_t>(static_cast<uint16_t>(errorCode));
    writer.WriteString(message);
    session->Send(writer.GetPacket());

    LOG_INFO("에러 응답 전송: SessionID={}, ErrorCode={}, Message={}",
             session->GetID(), static_cast<int>(errorCode), message);
}

bool LoginRequestHandler::Handle(SessionPtr session, PacketReader& reader) {
    if (session->IsAuthenticated()) {
        LOG_WARNING("이미 로그인된 세션: SessionID={}", session->GetID());
        return false;
    }

    std::string userId = reader.ReadString();
    std::string password = reader.ReadString();

    if (reader.HasError()) {
        LOG_ERROR("로그인 패킷 파싱 실패: SessionID={}", session->GetID());
        return false;
    }

    if (userId.empty() || userId.length() > MAX_USER_ID_LENGTH ||
        password.empty() || password.length() > MAX_PASSWORD_LENGTH) {
        LOG_WARNING("잘못된 로그인 데이터: SessionID={}, UserID={}",
                   session->GetID(), userId);

        PacketWriter writer(PKT_LOGIN_RESPONSE);
        writer.Write<uint8_t>(0);
        writer.WriteString("Invalid credentials");
        session->Send(writer.GetPacket());
        return true;
    }

    bool loginSuccess = (userId == "test" && password == "1234");

    if (loginSuccess) {
        session->SetAuthenticated(true);
        session->SetUserID(userId);
        LOG_INFO("로그인 성공: SessionID={}, UserID={}", session->GetID(), userId);
    } else {
        LOG_INFO("로그인 실패: SessionID={}, UserID={}", session->GetID(), userId);
    }

    PacketWriter writer(PKT_LOGIN_RESPONSE);
    writer.Write<uint8_t>(loginSuccess ? 1 : 0);
    writer.WriteString(loginSuccess ? "Welcome!" : "Login failed");
    session->Send(writer.GetPacket());

    return true;
}

bool LogoutRequestHandler::HandleAuthenticated(SessionPtr session, PacketReader& reader) {
    LOG_INFO("로그아웃 요청: SessionID={}, UserID={}",
             session->GetID(), session->GetUserID());

    session->SetAuthenticated(false);
    session->Disconnect();

    return true;
}

bool ChatMessageHandler::HandleAuthenticated(SessionPtr session, PacketReader& reader) {
    std::string message = reader.ReadString();

    if (reader.HasError()) {
        LOG_ERROR("채팅 패킷 파싱 실패: SessionID={}", session->GetID());
        return false;
    }

    if (message.empty() || message.length() > MAX_CHAT_MESSAGE_LENGTH) {
        LOG_WARNING("잘못된 채팅 메시지 길이: SessionID={}, Length={}",
                   session->GetID(), message.length());
        return false;
    }

    LOG_INFO("채팅 메시지: UserID={}, Message={}", session->GetUserID(), message);

    PacketWriter writer(PKT_CHAT_BROADCAST);
    writer.WriteString(session->GetUserID());
    writer.WriteString(message);

    auto packet = writer.GetPacket();
    SessionManager::Instance().BroadcastPacket(packet);

    return true;
}

bool MoveRequestHandler::HandleAuthenticated(SessionPtr session, PacketReader& reader) {
    int32_t x = reader.Read<int32_t>();
    int32_t y = reader.Read<int32_t>();

    if (reader.HasError()) {
        LOG_ERROR("이동 패킷 파싱 실패: SessionID={}", session->GetID());
        return false;
    }

    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) {
        LOG_WARNING("유효하지 않은 좌표: SessionID={}, ({}, {})",
                   session->GetID(), x, y);
        SendErrorResponse(session, ErrorCode::INVALID_DATA, "Invalid coordinates");
        return false;
    }

    session->SetPosition(x, y);
    LOG_INFO("플레이어 이동: UserID={}, Pos=({}, {})",
             session->GetUserID(), x, y);

    PacketWriter writer(PKT_MOVE_BROADCAST);
    writer.WriteString(session->GetUserID());
    writer.Write<int32_t>(x);
    writer.Write<int32_t>(y);

    auto packet = writer.GetPacket();
    SessionManager::Instance().BroadcastPacket(packet);

    return true;
}

bool AttackRequestHandler::HandleAuthenticated(SessionPtr session, PacketReader& reader) {
    std::string targetUserID = reader.ReadString();

    if (reader.HasError()) {
        LOG_ERROR("공격 패킷 파싱 실패: SessionID={}", session->GetID());
        return false;
    }

    if (targetUserID.empty() || targetUserID.length() > MAX_USER_ID_LENGTH) {
        LOG_WARNING("잘못된 공격 대상: SessionID={}, Target={}",
                   session->GetID(), targetUserID);
        return false;
    }

    LOG_INFO("플레이어 공격: UserID={}, Target={}",
             session->GetUserID(), targetUserID);

    PacketWriter writer(PKT_ATTACK_BROADCAST);
    writer.WriteString(session->GetUserID());
    writer.WriteString(targetUserID);

    auto packet = writer.GetPacket();
    SessionManager::Instance().BroadcastPacket(packet);

    return true;
}

// 핸들러 자동 등록
REGISTER_PACKET_HANDLER(LoginRequestHandler)
REGISTER_PACKET_HANDLER(LogoutRequestHandler)
REGISTER_PACKET_HANDLER(ChatMessageHandler)
REGISTER_PACKET_HANDLER(MoveRequestHandler)
REGISTER_PACKET_HANDLER(AttackRequestHandler)
