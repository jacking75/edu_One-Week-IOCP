
#pragma once

#include "RingBuffer.h"
#include <WinSock2.h>
#include <mutex>
#include <algorithm>
#include <cstring>

class SendBuffer : public RingBuffer {
private:
    std::mutex mutex_;

public:
    SendBuffer(int capacity) : RingBuffer(capacity) {}

    bool Write(const BYTE* data, int size) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (GetFreeSize() < size) {
            return false;
        }

        int firstSize = (std::min)(size, capacity_ - writePos_);
        memcpy(&buffer_[writePos_], data, firstSize);

        if (size > firstSize) {
            memcpy(&buffer_[0], data + firstSize, size - firstSize);
        }

        MoveWritePos(size);
        return true;
    }

    int PrepareSendBuffers(WSABUF wsaBufs[2]) {
        std::lock_guard<std::mutex> lock(mutex_);

        int dataSize = GetDataSize();
        if (dataSize == 0) {
            return 0;
        }

        int firstSize = GetContinuousDataSize();
        wsaBufs[0].buf = (char*)GetReadPtr();
        wsaBufs[0].len = firstSize;

        if (dataSize > firstSize) {
            wsaBufs[1].buf = (char*)&buffer_[0];
            wsaBufs[1].len = dataSize - firstSize;
            return 2;
        }

        return 1;
    }

    void OnSendCompleted(int bytesSent) {
        std::lock_guard<std::mutex> lock(mutex_);
        MoveReadPos(bytesSent);
    }

    int GetPendingSize() {
        std::lock_guard<std::mutex> lock(mutex_);
        return GetDataSize();
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        RingBuffer::Clear();
    }
};
