
#pragma once

#include "Packet.h"
#include <string>
#include <cstring>
#include <algorithm>
#include <type_traits>

class PacketWriter {
public:
    explicit PacketWriter(uint16_t packetID, size_t reserveSize = 256)
        : packetID_(packetID)
    {
        buffer_.reserve(reserveSize);
        buffer_.resize(PACKET_HEADER_SIZE);
        writePos_ = PACKET_HEADER_SIZE;
    }

    template<typename T>
    void Write(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        size_t oldSize = buffer_.size();
        buffer_.resize(oldSize + sizeof(T));
        memcpy(&buffer_[writePos_], &value, sizeof(T));
        writePos_ += sizeof(T);
    }

    void WriteString(const std::string& str) {
        uint16_t len = static_cast<uint16_t>(
            (std::min)(str.length(), size_t(1024)));
        Write(len);
        if (len > 0) {
            size_t oldSize = buffer_.size();
            buffer_.resize(oldSize + len);
            memcpy(&buffer_[writePos_], str.c_str(), len);
            writePos_ += len;
        }
    }

    PacketPtr GetPacket() {
        // 헤더 기록
        uint16_t packetSize = static_cast<uint16_t>(buffer_.size());
        memcpy(&buffer_[0], &packetSize, sizeof(uint16_t));
        memcpy(&buffer_[2], &packetID_, sizeof(uint16_t));

        auto packet = std::make_shared<Packet>(buffer_.size());
        memcpy(packet->GetBuffer(), buffer_.data(), buffer_.size());
        return packet;
    }

    size_t GetSize() const { return buffer_.size(); }

private:
    uint16_t packetID_;
    std::vector<char> buffer_;
    size_t writePos_ = 0;
};
