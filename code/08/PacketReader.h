
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

class PacketReader
{
public:
    PacketReader(const char* buffer, size_t size)
        : buffer_(buffer)
        , size_(size)
        , pos_(4)  // 헤더(Size + ID) 건너뜀
    {
    }

    template<typename T>
    T Read()
    {
        if (pos_ + sizeof(T) > size_)
        {
            hasError_ = true;
            return T{};
        }

        T value = *reinterpret_cast<const T*>(buffer_ + pos_);
        pos_ += sizeof(T);
        return value;
    }

    // 복사 버전 (필요시)
    std::string ReadString()
    {
        uint16_t length = Read<uint16_t>();
        if (hasError_ || pos_ + length > size_)
        {
            hasError_ = true;
            return "";
        }

        std::string str(buffer_ + pos_, length);
        pos_ += length;
        return str;
    }

    // Zero-Copy 버전 (C++17)
    std::string_view ReadStringView()
    {
        uint16_t length = Read<uint16_t>();
        if (hasError_ || pos_ + length > size_)
        {
            hasError_ = true;
            return "";
        }

        std::string_view view(buffer_ + pos_, length);
        pos_ += length;
        return view;
    }

    bool HasError() const { return hasError_; }
    size_t GetRemainingSize() const { return size_ - pos_; }

private:
    const char* buffer_;
    size_t size_;
    size_t pos_;
    bool hasError_ = false;
};
