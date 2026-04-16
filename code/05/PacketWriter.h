
#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <type_traits>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "PacketTypes.h"

class PacketWriter {
private:
    std::vector<BYTE> buffer_;
    size_t writePos_;

public:
    PacketWriter(size_t reserveSize = 256)
        : writePos_(0)
    {
        buffer_.reserve(reserveSize);
        buffer_.resize(sizeof(PacketHeader));
        writePos_ = sizeof(PacketHeader);
    }

    template<typename T>
    void Write(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>,
                     "T must be trivially copyable");

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

    void WriteFixedString(const char* str, size_t maxLen) {
        size_t len = strnlen(str, maxLen);
        size_t oldSize = buffer_.size();
        buffer_.resize(oldSize + maxLen);

        memcpy(&buffer_[writePos_], str, len);
        if (len < maxLen) {
            memset(&buffer_[writePos_ + len], 0, maxLen - len);
        }
        writePos_ += maxLen;
    }

    void WriteBytes(const BYTE* data, size_t size) {
        size_t oldSize = buffer_.size();
        buffer_.resize(oldSize + size);
        memcpy(&buffer_[writePos_], data, size);
        writePos_ += size;
    }

    BYTE* Finalize(PacketID packetId) {
        PacketHeader* header = reinterpret_cast<PacketHeader*>(&buffer_[0]);
        header->size = static_cast<uint16_t>(buffer_.size());
        header->packetId = static_cast<uint16_t>(packetId);
        header->reserved = 0;

        return buffer_.data();
    }

    size_t GetSize() const { return buffer_.size(); }
    const BYTE* GetData() const { return buffer_.data(); }
    std::vector<BYTE>&& MoveBuffer() { return std::move(buffer_); }
};
