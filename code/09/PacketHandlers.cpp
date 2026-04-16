
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
}

bool LoginRequestHandler::Handle(SessionPtr session, PacketReader& reader) {
    if (session->IsAuthenticated()) {
        LOG_WARNING("이미 로그인된 세션: SessionID={}", session->GetID());
        return false;
    }

    std::string userId = reader.ReadString();
    std::string password = reader.ReadString();

    if (reader.HasError()) return false;

    if (userId.empty() || userId.length() > MAX_USER_ID_LENGTH ||
        password.empty() || password.length() > MAX_PASSWORD_LENGTH) {
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
    LOG_INFO("로그아웃: SessionID={}, UserID={}",
             session->GetID(), session->GetUserID());
    session->SetAuthenticated(false);
    session->Disconnect();
    return true;
}

bool ChatMessageHandler::HandleAuthenticated(SessionPtr session, PacketReader& reader) {
    std::string message = reader.ReadString();
    if (reader.HasError() || message.empty() || message.length() > MAX_CHAT_MESSAGE_LENGTH)
        return false;

    LOG_INFO("채팅: UserID={}, Message={}", session->GetUserID(), message);

    PacketWriter writer(PKT_CHAT_BROADCAST);
    writer.WriteString(session->GetUserID());
    writer.WriteString(message);
    SessionManager::Instance().BroadcastPacket(writer.GetPacket());
    return true;
}

bool MoveRequestHandler::HandleAuthenticated(SessionPtr session, PacketReader& reader) {
    int32_t x = reader.Read<int32_t>();
    int32_t y = reader.Read<int32_t>();
    if (reader.HasError()) return false;

    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) {
        SendErrorResponse(session, ErrorCode::INVALID_DATA, "Invalid coordinates");
        return false;
    }

    session->SetPosition(x, y);

    PacketWriter writer(PKT_MOVE_BROADCAST);
    writer.WriteString(session->GetUserID());
    writer.Write<int32_t>(x);
    writer.Write<int32_t>(y);
    SessionManager::Instance().BroadcastPacket(writer.GetPacket());
    return true;
}

bool AttackRequestHandler::HandleAuthenticated(SessionPtr session, PacketReader& reader) {
    std::string targetUserID = reader.ReadString();
    if (reader.HasError() || targetUserID.empty() || targetUserID.length() > MAX_USER_ID_LENGTH)
        return false;

    PacketWriter writer(PKT_ATTACK_BROADCAST);
    writer.WriteString(session->GetUserID());
    writer.WriteString(targetUserID);
    SessionManager::Instance().BroadcastPacket(writer.GetPacket());
    return true;
}

REGISTER_PACKET_HANDLER(LoginRequestHandler)
REGISTER_PACKET_HANDLER(LogoutRequestHandler)
REGISTER_PACKET_HANDLER(ChatMessageHandler)
REGISTER_PACKET_HANDLER(MoveRequestHandler)
REGISTER_PACKET_HANDLER(AttackRequestHandler)
