
#include "PacketDispatcher.h"
#include "Logger.h"

void PacketDispatcher::RegisterHandler(std::unique_ptr<IPacketHandler> handler) {
    uint16_t packetID = handler->GetPacketID();
    if (handlers_.find(packetID) != handlers_.end()) {
        LOG_WARNING("핸들러 중복 등록: PacketID={}", packetID);
        return;
    }
    LOG_INFO("핸들러 등록: PacketID={}", packetID);
    handlers_[packetID] = std::move(handler);
}

IPacketHandler* PacketDispatcher::GetHandler(uint16_t packetID) {
    auto it = handlers_.find(packetID);
    return (it != handlers_.end()) ? it->second.get() : nullptr;
}
