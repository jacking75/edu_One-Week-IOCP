
#pragma once

#include "RingBuffer.h"
#include <WinSock2.h>
#include <algorithm>
#include <cstring>

class RecvBuffer : public RingBuffer {
public:
    RecvBuffer(int capacity) : RingBuffer(capacity) {}

    bool GetRecvBuffer(WSABUF& wsaBuf) {
        int freeSize = GetContinuousFreeSize();
        if (freeSize == 0) {
            return false;
        }

        wsaBuf.buf = (char*)GetWritePtr();
        wsaBuf.len = freeSize;
        return true;
    }

    bool OnRecvCompleted(int bytesReceived) {
        if (GetFreeSize() < bytesReceived) {
            return false;
        }

        MoveWritePos(bytesReceived);
        return true;
    }

    bool Peek(BYTE* dest, int size) {
        if (GetDataSize() < size) {
            return false;
        }

        int firstSize = (std::min)(size, capacity_ - readPos_);
        memcpy(dest, &buffer_[readPos_], firstSize);

        if (size > firstSize) {
            memcpy(dest + firstSize, &buffer_[0], size - firstSize);
        }

        return true;
    }

    void Consume(int size) {
        MoveReadPos(size);
    }
};
