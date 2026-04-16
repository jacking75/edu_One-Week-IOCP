
#include "PacketHandler.h"
#include "Session.h"
#include "PacketWriter.h"
#include <iostream>

bool EchoPacketHandler::Handle(Session* session, PacketReader& reader) {
    std::string message;
    if (!reader.ReadString(message)) {
        std::cout << "[Echo] 패킷 파싱 실패" << std::endl;
        return false;
    }

    std::cout << "[Echo] 수신: " << message << std::endl;

    PacketWriter writer;
    writer.WriteString(message);

    BYTE* data = writer.Finalize(PacketID::ECHO_RES);
    session->Send(data, static_cast<int>(writer.GetSize()));

    return true;
}

bool LoginPacketHandler::Handle(Session* session, PacketReader& reader) {
    std::string accountId, password;
    if (!reader.ReadString(accountId) || !reader.ReadString(password)) {
        std::cout << "[Login] 패킷 파싱 실패" << std::endl;
        return false;
    }

    std::cout << "[Login] 계정: " << accountId << std::endl;

    bool success = (accountId.length() > 0 && password.length() > 0);

    PacketWriter writer;
    writer.Write<uint8_t>(success ? 1 : 0);
    writer.Write<uint64_t>(session->GetSessionId());
    writer.WriteFixedString(accountId.c_str(), 20);

    BYTE* data = writer.Finalize(PacketID::LOGIN_RES);
    session->Send(data, static_cast<int>(writer.GetSize()));

    return true;
}

bool ChatPacketHandler::Handle(Session* session, PacketReader& reader) {
    std::string message;
    if (!reader.ReadString(message)) {
        std::cout << "[Chat] 패킷 파싱 실패" << std::endl;
        return false;
    }

    std::cout << "[Chat] 세션 " << session->GetSessionId()
              << ": " << message << std::endl;

    PacketWriter writer;
    writer.Write<uint64_t>(session->GetSessionId());
    writer.WriteString(message);

    BYTE* data = writer.Finalize(PacketID::CHAT_BROADCAST);
    session->Send(data, static_cast<int>(writer.GetSize()));

    return true;
}
