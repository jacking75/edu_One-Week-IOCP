
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "Session.h"
#include "PacketReader.h"
#include "PacketWriter.h"

enum PacketID : uint16_t {
    PKT_NONE = 0,

    PKT_LOGIN_REQUEST = 100,
    PKT_LOGIN_RESPONSE = 101,
    PKT_LOGOUT_REQUEST = 102,

    PKT_CHAT_MESSAGE = 200,
    PKT_CHAT_BROADCAST = 201,

    PKT_MOVE_REQUEST = 300,
    PKT_MOVE_BROADCAST = 301,
    PKT_ATTACK_REQUEST = 302,
    PKT_ATTACK_BROADCAST = 303,

    PKT_ERROR_RESPONSE = 9999,
};

enum class ErrorCode : uint16_t {
    NONE = 0,
    INVALID_PACKET = 1,
    NOT_AUTHENTICATED = 2,
    INVALID_STATE = 3,
    INVALID_DATA = 4,
    RATE_LIMIT_EXCEEDED = 5,
};

class IPacketHandler {
public:
    virtual ~IPacketHandler() = default;
    virtual bool Handle(SessionPtr session, PacketReader& reader) = 0;
    virtual uint16_t GetPacketID() const = 0;
};

class AuthenticatedPacketHandler : public IPacketHandler {
public:
    bool Handle(SessionPtr session, PacketReader& reader) final {
        if (!session->IsAuthenticated()) {
            SendErrorResponse(session, ErrorCode::NOT_AUTHENTICATED,
                            "Authentication required");
            return false;
        }
        return HandleAuthenticated(session, reader);
    }

protected:
    virtual bool HandleAuthenticated(SessionPtr session, PacketReader& reader) = 0;
    void SendErrorResponse(SessionPtr session, ErrorCode errorCode,
                          const std::string& message);
};

class LoginRequestHandler : public IPacketHandler {
public:
    uint16_t GetPacketID() const override { return PKT_LOGIN_REQUEST; }
    bool Handle(SessionPtr session, PacketReader& reader) override;
};

class LogoutRequestHandler : public AuthenticatedPacketHandler {
public:
    uint16_t GetPacketID() const override { return PKT_LOGOUT_REQUEST; }
protected:
    bool HandleAuthenticated(SessionPtr session, PacketReader& reader) override;
};

class ChatMessageHandler : public AuthenticatedPacketHandler {
public:
    uint16_t GetPacketID() const override { return PKT_CHAT_MESSAGE; }
protected:
    bool HandleAuthenticated(SessionPtr session, PacketReader& reader) override;
};

class MoveRequestHandler : public AuthenticatedPacketHandler {
public:
    uint16_t GetPacketID() const override { return PKT_MOVE_REQUEST; }
protected:
    bool HandleAuthenticated(SessionPtr session, PacketReader& reader) override;
};

class AttackRequestHandler : public AuthenticatedPacketHandler {
public:
    uint16_t GetPacketID() const override { return PKT_ATTACK_REQUEST; }
protected:
    bool HandleAuthenticated(SessionPtr session, PacketReader& reader) override;
};
