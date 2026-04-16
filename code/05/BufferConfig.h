
#pragma once

class BufferConfig {
public:
    static constexpr int DEFAULT_RECV_BUFFER = 64 * 1024;  // 64KB
    static constexpr int DEFAULT_SEND_BUFFER = 32 * 1024;  // 32KB

    static constexpr int SMALL_RECV_BUFFER = 16 * 1024;    // 16KB
    static constexpr int SMALL_SEND_BUFFER = 8 * 1024;     // 8KB

    static constexpr int LARGE_RECV_BUFFER = 128 * 1024;   // 128KB
    static constexpr int LARGE_SEND_BUFFER = 64 * 1024;    // 64KB
};
