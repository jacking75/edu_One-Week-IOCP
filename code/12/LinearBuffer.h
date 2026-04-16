#pragma once
#include <cstdint>
#include <cstring>

class LinearBuffer {
private:
    char* buffer_;
    size_t capacity_;
    size_t readPos_;
    size_t writePos_;
public:
    explicit LinearBuffer(size_t capacity)
        : capacity_(capacity), readPos_(0), writePos_(0) {
        buffer_ = new char[capacity];
    }
    ~LinearBuffer() { delete[] buffer_; }
    LinearBuffer(const LinearBuffer&) = delete;
    LinearBuffer& operator=(const LinearBuffer&) = delete;

    char* GetWriteBuffer() { return buffer_ + writePos_; }
    const char* GetReadBuffer() const { return buffer_ + readPos_; }
    size_t GetFreeSize() const { return capacity_ - writePos_; }
    size_t GetDataSize() const { return writePos_ - readPos_; }
    void MoveWritePos(size_t bytes) { writePos_ += bytes; }
    void MoveReadPos(size_t bytes) { readPos_ += bytes; }
    void Compact() {
        if (readPos_ == 0) return;
        size_t dataSize = GetDataSize();
        if (dataSize > 0) memmove(buffer_, buffer_ + readPos_, dataSize);
        readPos_ = 0;
        writePos_ = dataSize;
    }
};
