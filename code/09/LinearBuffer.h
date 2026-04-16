
#pragma once

#include <vector>
#include <cstring>

class LinearBuffer
{
public:
    explicit LinearBuffer(size_t capacity)
        : buffer_(capacity)
        , capacity_(capacity)
    {
    }

    char* GetReadBuffer() { return buffer_.data() + readPos_; }
    char* GetWriteBuffer() { return buffer_.data() + writePos_; }

    size_t GetDataSize() const { return writePos_ - readPos_; }
    size_t GetFreeSize() const { return capacity_ - writePos_; }

    void MoveReadPos(size_t size) {
        readPos_ += size;
        if (readPos_ > capacity_) readPos_ = capacity_;
    }

    void MoveWritePos(size_t size) {
        writePos_ += size;
        if (writePos_ > capacity_) writePos_ = capacity_;
    }

    void Compact() {
        if (readPos_ == 0) return;
        size_t dataSize = GetDataSize();
        if (dataSize > 0) {
            memmove(buffer_.data(), buffer_.data() + readPos_, dataSize);
        }
        readPos_ = 0;
        writePos_ = dataSize;
    }

    void Clear() { readPos_ = 0; writePos_ = 0; }

private:
    std::vector<char> buffer_;
    size_t capacity_;
    size_t readPos_ = 0;
    size_t writePos_ = 0;
};
