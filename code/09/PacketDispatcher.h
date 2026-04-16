
#pragma once

#include <unordered_map>
#include <memory>
#include "PacketHandler.h"
#include "Packet.h"

class PacketDispatcher {
public:
    static PacketDispatcher& Instance() {
        static PacketDispatcher instance;
        return instance;
    }

    void RegisterHandler(std::unique_ptr<IPacketHandler> handler);
    IPacketHandler* GetHandler(uint16_t packetID);
    size_t GetHandlerCount() const { return handlers_.size(); }

private:
    PacketDispatcher() = default;
    ~PacketDispatcher() = default;
    PacketDispatcher(const PacketDispatcher&) = delete;
    PacketDispatcher& operator=(const PacketDispatcher&) = delete;

    std::unordered_map<uint16_t, std::unique_ptr<IPacketHandler>> handlers_;
};

#define REGISTER_PACKET_HANDLER(HandlerClass) \
    namespace { \
        struct HandlerClass##_AutoRegister { \
            HandlerClass##_AutoRegister() { \
                PacketDispatcher::Instance().RegisterHandler( \
                    std::make_unique<HandlerClass>() \
                ); \
            } \
        }; \
        static HandlerClass##_AutoRegister g_##HandlerClass##_AutoRegister; \
    }
