
#pragma once

#include <string>
#include <cstring>
#include <type_traits>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "PacketTypes.h"

class PacketReader {
private:
    const BYTE* buffer_;
    size_t bufferSize_;
    size_t readPos_;
    bool error_;

public:
    PacketReader(const BYTE* buffer, size_t size)
        : buffer_(buffer)
        , bufferSize_(size)
        , readPos_(sizeof(PacketHeader))
        , error_(false)
    {
        if (size < sizeof(PacketHeader)) {
            error_ = true;
        }
    }

    const PacketHeader* GetHeader() const {
        if (bufferSize_ < sizeof(PacketHeader)) {
            return nullptr;
        }
        return reinterpret_cast<const PacketHeader*>(buffer_);
    }

    template<typename T>
    bool Read(T& outValue) {
        static_assert(std::is_trivially_copyable_v<T>,
                     "T must be trivially copyable");

        if (error_ || readPos_ + sizeof(T) > bufferSize_) {
            error_ = true;
            return false;
        }

        memcpy(&outValue, &buffer_[readPos_], sizeof(T));
        readPos_ += sizeof(T);
        return true;
    }

    bool ReadString(std::string& outStr) {
        uint16_t len;
        if (!Read(len)) {
            return false;
        }

        if (error_ || readPos_ + len > bufferSize_) {
            error_ = true;
            return false;
        }

        outStr.assign(reinterpret_cast<const char*>(&buffer_[readPos_]), len);
        readPos_ += len;
        return true;
    }

    bool ReadFixedString(char* outStr, size_t maxLen) {
        if (error_ || readPos_ + maxLen > bufferSize_) {
            error_ = true;
            return false;
        }

        memcpy(outStr, &buffer_[readPos_], maxLen);
        readPos_ += maxLen;
        return true;
    }

    bool ReadBytes(BYTE* outData, size_t size) {
        if (error_ || readPos_ + size > bufferSize_) {
            error_ = true;
            return false;
        }

        memcpy(outData, &buffer_[readPos_], size);
        readPos_ += size;
        return true;
    }

    size_t GetRemainSize() const {
        return bufferSize_ - readPos_;
    }

    bool IsEnd() const {
        return readPos_ >= bufferSize_;
    }

    bool HasError() const {
        return error_;
    }
};
