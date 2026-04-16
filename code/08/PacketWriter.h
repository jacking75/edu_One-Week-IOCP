
#pragma once

#include "Packet.h"
#include <string>
#include <vector>
#include <cstring>

class PacketWriter {
public:
    explicit PacketWriter(uint16_t packetID) {
        Write<uint16_t>(0);
        Write<uint16_t>(packetID);
    }

    template<typename T>
    void Write(const T& value) {
        const char* ptr = reinterpret_cast<const char*>(&value);
        buffer_.insert(buffer_.end(), ptr, ptr + sizeof(T));
    }

    void WriteString(const std::string& str) {
        Write<uint16_t>(static_cast<uint16_t>(str.size()));
        buffer_.insert(buffer_.end(), str.begin(), str.end());
    }

    PacketPtr GetPacket() {
        uint16_t size = static_cast<uint16_t>(buffer_.size());
        memcpy(buffer_.data(), &size, sizeof(size));

        auto packet = std::make_shared<Packet>(buffer_.size());
        memcpy(packet->GetBuffer(), buffer_.data(), buffer_.size());
        return packet;
    }

private:
    std::vector<char> buffer_;
};
