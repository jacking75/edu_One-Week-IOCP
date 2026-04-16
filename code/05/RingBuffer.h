
#pragma once

#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

class RingBuffer {
protected:
    std::vector<BYTE> buffer_;
    int capacity_;
    int readPos_;
    int writePos_;

public:
    RingBuffer(int capacity)
        : buffer_(capacity)
        , capacity_(capacity)
        , readPos_(0)
        , writePos_(0)
    {}

    void Clear() {
        readPos_ = 0;
        writePos_ = 0;
    }

    int GetCapacity() const { return capacity_; }

    int GetDataSize() const {
        if (writePos_ >= readPos_) {
            return writePos_ - readPos_;
        } else {
            return capacity_ - (readPos_ - writePos_);
        }
    }

    int GetFreeSize() const {
        if (writePos_ >= readPos_) {
            return capacity_ - (writePos_ - readPos_) - 1;
        } else {
            return readPos_ - writePos_ - 1;
        }
    }

    int GetContinuousDataSize() const {
        if (writePos_ >= readPos_) {
            return writePos_ - readPos_;
        } else {
            return capacity_ - readPos_;
        }
    }

    int GetContinuousFreeSize() const {
        if (writePos_ >= readPos_) {
            int freeSize = capacity_ - writePos_;
            if (readPos_ == 0) {
                freeSize -= 1;
            }
            return freeSize;
        } else {
            return readPos_ - writePos_ - 1;
        }
    }

    BYTE* GetReadPtr() { return &buffer_[readPos_]; }
    BYTE* GetWritePtr() { return &buffer_[writePos_]; }

    void MoveReadPos(int size) {
        readPos_ = (readPos_ + size) % capacity_;
    }

    void MoveWritePos(int size) {
        writePos_ = (writePos_ + size) % capacity_;
    }
};
