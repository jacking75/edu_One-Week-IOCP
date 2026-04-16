
#pragma once

#include "BaseContext.h"
#include "Session.h"
#include "Packet.h"
#include <vector>

struct SendContext : public BaseContext
{
    SessionPtr session;
    std::vector<PacketPtr> packets;
    std::vector<WSABUF> wsaBufs;

    SendContext() : BaseContext(ContextType::SEND) {}

    void AddPacket(PacketPtr packet) {
        WSABUF buf;
        buf.buf = packet->GetBuffer();
        buf.len = static_cast<ULONG>(packet->GetSize());
        packets.push_back(std::move(packet));
        wsaBufs.push_back(buf);
    }

    WSABUF* GetBufferArray() { return wsaBufs.data(); }
    DWORD GetBufferCount() const { return static_cast<DWORD>(wsaBufs.size()); }
};
