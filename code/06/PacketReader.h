
#pragma once

#include "Packet.h"
#include <string>
#include <cstring>
#include <type_traits>

class PacketReader {
public:
    explicit PacketReader(PacketPtr packet)
        : buffer_(packet->GetBuffer())
        , bufferSize_(packet->GetSize())
        , readPos_(PACKET_HEADER_SIZE)
        , error_(false)
    {
        if (bufferSize_ < PACKET_HEADER_SIZE) {
            error_ = true;
        }
    }

    template<typename T>
    T Read() {
        static_assert(std::is_trivially_copyable_v<T>);
        T value{};
        if (error_ || readPos_ + sizeof(T) > bufferSize_) {
            error_ = true;
            return value;
        }
        memcpy(&value, buffer_ + readPos_, sizeof(T));
        readPos_ += sizeof(T);
        return value;
    }

    std::string ReadString() {
        uint16_t len = Read<uint16_t>();
        if (error_ || readPos_ + len > bufferSize_) {
            error_ = true;
            return "";
        }
        std::string result(buffer_ + readPos_, len);
        readPos_ += len;
        return result;
    }

    bool HasError() const { return error_; }

    size_t GetRemainSize() const {
        return error_ ? 0 : bufferSize_ - readPos_;
    }

private:
    const char* buffer_;
    size_t bufferSize_;
    size_t readPos_;
    bool error_;
};
