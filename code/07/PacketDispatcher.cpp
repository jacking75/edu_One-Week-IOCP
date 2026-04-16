
#include "PacketDispatcher.h"
#include "Logger.h"

void PacketDispatcher::RegisterHandler(std::unique_ptr<IPacketHandler> handler) {
    uint16_t packetID = handler->GetPacketID();

    if (handlers_.find(packetID) != handlers_.end()) {
        LOG_WARNING("핸들러 중복 등록: PacketID={}", packetID);
        return;
    }

    handlers_[packetID] = std::move(handler);
    LOG_INFO("핸들러 등록: PacketID={}", packetID);
}

bool PacketDispatcher::Dispatch(SessionPtr session, PacketPtr packet) {
    if (!session || !packet) {
        LOG_ERROR("유효하지 않은 세션 또는 패킷");
        return false;
    }

    PacketReader reader(packet);

    uint16_t packetSize = reader.Read<uint16_t>();
    uint16_t packetID = reader.Read<uint16_t>();

    if (reader.HasError()) {
        LOG_ERROR("패킷 헤더 읽기 실패: SessionID={}", session->GetID());
        return false;
    }

    auto it = handlers_.find(packetID);
    if (it == handlers_.end()) {
        LOG_WARNING("등록되지 않은 패킷 ID: {} (SessionID={})",
                   packetID, session->GetID());
        return false;
    }

    try {
        bool result = it->second->Handle(session, reader);

        if (!result) {
            LOG_WARNING("패킷 처리 실패: PacketID={}, SessionID={}",
                       packetID, session->GetID());
        }

        return result;
    } catch (const std::exception& e) {
        LOG_ERROR("패킷 핸들러 예외: PacketID={}, SessionID={}, Error={}",
                 packetID, session->GetID(), e.what());
        return false;
    }
}
