
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <vector>
#include <memory>
#include <cstdint>

constexpr size_t PACKET_HEADER_SIZE = 4;
constexpr size_t MAX_PACKET_SIZE = 8192;

class Packet {
public:
    explicit Packet(size_t size) : buffer_(size) {}

    char* GetBuffer() { return buffer_.data(); }
    const char* GetBuffer() const { return buffer_.data(); }
    size_t GetSize() const { return buffer_.size(); }

    uint16_t GetPacketSize() const {
        if (buffer_.size() < 2) return 0;
        return *reinterpret_cast<const uint16_t*>(buffer_.data());
    }

    uint16_t GetPacketID() const {
        if (buffer_.size() < 4) return 0;
        return *reinterpret_cast<const uint16_t*>(buffer_.data() + 2);
    }

private:
    std::vector<char> buffer_;
};

using PacketPtr = std::shared_ptr<Packet>;
