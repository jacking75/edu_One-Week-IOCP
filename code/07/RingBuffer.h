
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <vector>
#include <algorithm>
#include <cstring>

class RingBuffer {
public:
    explicit RingBuffer(size_t capacity)
        : buffer_(capacity)
        , capacity_(capacity)
        , readPos_(0)
        , writePos_(0)
    {}

    void Clear() {
        readPos_ = 0;
        writePos_ = 0;
    }

    size_t GetCapacity() const { return capacity_; }

    size_t GetDataSize() const {
        if (writePos_ >= readPos_) {
            return writePos_ - readPos_;
        } else {
            return capacity_ - (readPos_ - writePos_);
        }
    }

    size_t GetFreeSize() const {
        if (writePos_ >= readPos_) {
            return capacity_ - (writePos_ - readPos_) - 1;
        } else {
            return readPos_ - writePos_ - 1;
        }
    }

    char* GetWriteBuffer() {
        return reinterpret_cast<char*>(&buffer_[writePos_]);
    }

    char* GetReadBuffer() {
        return reinterpret_cast<char*>(&buffer_[readPos_]);
    }

    void MoveWritePos(size_t size) {
        writePos_ = (writePos_ + size) % capacity_;
    }

    void MoveReadPos(size_t size) {
        readPos_ = (readPos_ + size) % capacity_;
    }

    bool Peek(char* dest, size_t size) {
        if (GetDataSize() < size) {
            return false;
        }

        size_t firstSize = (std::min)(size, capacity_ - readPos_);
        memcpy(dest, &buffer_[readPos_], firstSize);

        if (size > firstSize) {
            memcpy(dest + firstSize, &buffer_[0], size - firstSize);
        }

        return true;
    }

    size_t Read(char* dest, size_t size) {
        size_t dataSize = GetDataSize();
        size_t readSize = (std::min)(size, dataSize);

        if (readSize == 0) return 0;

        size_t firstSize = (std::min)(readSize, capacity_ - readPos_);
        memcpy(dest, &buffer_[readPos_], firstSize);

        if (readSize > firstSize) {
            memcpy(dest + firstSize, &buffer_[0], readSize - firstSize);
        }

        MoveReadPos(readSize);
        return readSize;
    }

    bool Write(const char* src, size_t size) {
        if (GetFreeSize() < size) {
            return false;
        }

        size_t firstSize = (std::min)(size, capacity_ - writePos_);
        memcpy(&buffer_[writePos_], src, firstSize);

        if (size > firstSize) {
            memcpy(&buffer_[0], src + firstSize, size - firstSize);
        }

        MoveWritePos(size);
        return true;
    }

private:
    std::vector<BYTE> buffer_;
    size_t capacity_;
    size_t readPos_;
    size_t writePos_;
};
