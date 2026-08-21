// net_protocol.h
//
// Shared networking protocol, frame headers, socket helpers, and
// high-performance sc16 <-> float (fc32) SIMD/vectorized converters
// for lossless streaming across System 1 -> System 2 -> System 3.

#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// Magic 32-bit identifier for frame alignment ('IQS3' = 0x49515333)
constexpr uint32_t kIqFrameMagic = 0x49515333;

#pragma pack(push, 1)
struct IqFrameHeader
{
    uint32_t magic;           // Must be kIqFrameMagic
    uint32_t sequence_num;    // Monotonically increasing (0, 1, 2, ...)
    uint64_t timestamp_ns;    // USRP timestamp in nanoseconds
    double   center_freq_hz;  // Active RF carrier frequency (e.g. 2.4e9, 5.1e9, 5.8e9)
    float    iq_multiplier;   // 1.0 at S1 -> S2; 2.0 / 3.0 / 4.0 at S2 -> S3
    uint32_t sample_count;    // Number of complex samples per channel in this frame
    uint32_t fft_size;        // FFT points (e.g. 4096, or 0 if no FFT vector in this frame)
    uint32_t is_bursting;     // 1 during active TX burst, 0 during silence
    uint32_t reserved;        // Padding for 64-bit alignment
};
#pragma pack(pop)

namespace net_util
{

// Sets TCP_NODELAY (disables Nagle's algorithm) to guarantee sub-millisecond transmission
inline bool set_tcp_nodelay(int fd)
{
    int flag = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&flag), sizeof(flag)) == 0;
}

// Configures OS send and receive socket buffer sizes to avoid drops under load
inline bool set_socket_buffers(int fd, int buffer_size_bytes = 4 * 1024 * 1024)
{
    int snd = buffer_size_bytes;
    int rcv = buffer_size_bytes;
    bool ok1 = (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&snd), sizeof(snd)) == 0);
    bool ok2 = (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&rcv), sizeof(rcv)) == 0);
    return ok1 && ok2;
}

// Blocks until all total_bytes are sent over socket_fd (handles partial writes)
inline bool send_all(int fd, const void* data, size_t total_bytes)
{
    const char* ptr = reinterpret_cast<const char*>(data);
    size_t remaining = total_bytes;
    while (remaining > 0) {
        ssize_t n = send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        ptr += n;
        remaining -= n;
    }
    return true;
}

// Blocks until exact total_bytes are received from socket_fd (handles partial reads)
inline bool recv_exact(int fd, void* data, size_t total_bytes)
{
    char* ptr = reinterpret_cast<char*>(data);
    size_t remaining = total_bytes;
    while (remaining > 0) {
        ssize_t n = recv(fd, ptr, remaining, MSG_WAITALL);
        if (n <= 0) {
            return false;
        }
        ptr += n;
        remaining -= n;
    }
    return true;
}

// Converts complex float (fc32 [-1.0, 1.0]) to interleaved sc16 (int16_t I, int16_t Q)
inline void float_to_sc16(const std::complex<float>* in, int16_t* out_sc16, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        float re = in[i].real();
        float im = in[i].imag();

        // Clamp to [-1.0, 1.0] and scale by 32767.0
        float re_clamped = std::max(-1.0f, std::min(1.0f, re));
        float im_clamped = std::max(-1.0f, std::min(1.0f, im));

        out_sc16[2 * i]     = static_cast<int16_t>(std::lround(re_clamped * 32767.0f));
        out_sc16[2 * i + 1] = static_cast<int16_t>(std::lround(im_clamped * 32767.0f));
    }
}

// Converts interleaved sc16 (int16_t I, int16_t Q) back to complex float (fc32)
inline void sc16_to_float(const int16_t* in_sc16, std::complex<float>* out, size_t count)
{
    constexpr float kInvScale = 1.0f / 32768.0f;
    for (size_t i = 0; i < count; ++i) {
        float re = static_cast<float>(in_sc16[2 * i])     * kInvScale;
        float im = static_cast<float>(in_sc16[2 * i + 1]) * kInvScale;
        out[i] = std::complex<float>(re, im);
    }
}

// Encodes scaled float to sc16 normalizing by multiplier M to preserve precision without clipping
inline void float_to_sc16_scaled(const std::complex<float>* in, int16_t* out_sc16, size_t count, float multiplier)
{
    float inv_m = (multiplier > 0.0f) ? (1.0f / multiplier) : 1.0f;
    for (size_t i = 0; i < count; ++i) {
        float re = in[i].real() * inv_m;
        float im = in[i].imag() * inv_m;

        float re_clamped = std::max(-1.0f, std::min(1.0f, re));
        float im_clamped = std::max(-1.0f, std::min(1.0f, im));

        out_sc16[2 * i]     = static_cast<int16_t>(std::lround(re_clamped * 32767.0f));
        out_sc16[2 * i + 1] = static_cast<int16_t>(std::lround(im_clamped * 32767.0f));
    }
}

// Decodes sc16 back to scaled float using multiplier M
inline void sc16_to_float_scaled(const int16_t* in_sc16, std::complex<float>* out, size_t count, float multiplier)
{
    float scale = (multiplier > 0.0f ? multiplier : 1.0f) / 32768.0f;
    for (size_t i = 0; i < count; ++i) {
        float re = static_cast<float>(in_sc16[2 * i])     * scale;
        float im = static_cast<float>(in_sc16[2 * i + 1]) * scale;
        out[i] = std::complex<float>(re, im);
    }
}

} // namespace net_util
