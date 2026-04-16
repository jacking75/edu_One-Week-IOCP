
#pragma once

#include "PacketTypes.h"
#include "PacketReader.h"

class Session;

class IPacketHandler {
public:
    virtual ~IPacketHandler() = default;
    virtual bool Handle(Session* session, PacketReader& reader) = 0;
};

class EchoPacketHandler : public IPacketHandler {
public:
    bool Handle(Session* session, PacketReader& reader) override;
};

class LoginPacketHandler : public IPacketHandler {
public:
    bool Handle(Session* session, PacketReader& reader) override;
};

class ChatPacketHandler : public IPacketHandler {
public:
    bool Handle(Session* session, PacketReader& reader) override;
};
