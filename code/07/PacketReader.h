
#pragma once

#include "Packet.h"
#include <string>

class PacketReader {
public:
    explicit PacketReader(PacketPtr packet)
        : packet_(packet)
        , offset_(0)
    {}

    template<typename T>
    T Read() {
        if (offset_ + sizeof(T) > packet_->GetSize()) {
            hasError_ = true;
            return T{};
        }

        T value;
        memcpy(&value, packet_->GetBuffer() + offset_, sizeof(T));
        offset_ += sizeof(T);
        return value;
    }

    std::string ReadString() {
        uint16_t length = Read<uint16_t>();
        if (hasError_) return "";

        if (offset_ + length > packet_->GetSize()) {
            hasError_ = true;
            return "";
        }

        std::string str(packet_->GetBuffer() + offset_, length);
        offset_ += length;
        return str;
    }

    bool HasError() const { return hasError_; }

private:
    PacketPtr packet_;
    size_t offset_;
    bool hasError_ = false;
};
