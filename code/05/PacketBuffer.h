
#pragma once

#include <memory>
#include <vector>
#include <cstring>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "PacketTypes.h"

class PacketBuffer {
private:
    std::vector<BYTE> buffer_;

public:
    explicit PacketBuffer(size_t size) : buffer_(size) {}

    explicit PacketBuffer(std::vector<BYTE>&& buffer)
        : buffer_(std::move(buffer)) {}

    BYTE* Data() { return buffer_.data(); }
    const BYTE* Data() const { return buffer_.data(); }
    size_t Size() const { return buffer_.size(); }

    PacketHeader* GetHeader() {
        return reinterpret_cast<PacketHeader*>(buffer_.data());
    }

    const PacketHeader* GetHeader() const {
        return reinterpret_cast<const PacketHeader*>(buffer_.data());
    }
};

using PacketBufferPtr = std::shared_ptr<PacketBuffer>;

class PacketBufferFactory {
public:
    static PacketBufferPtr Create(size_t size) {
        return std::make_shared<PacketBuffer>(size);
    }

    static PacketBufferPtr CreateFrom(const BYTE* data, size_t size) {
        auto buffer = Create(size);
        memcpy(buffer->Data(), data, size);
        return buffer;
    }

    static PacketBufferPtr CreateFrom(std::vector<BYTE>&& data) {
        return std::make_shared<PacketBuffer>(std::move(data));
    }
};
