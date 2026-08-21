// ring_buffer.h
//
// Single-producer / single-consumer lock-free ring buffer.
//
// Used to hand IQ sample chunks from the real-time UHD TX/RX threads to the
// GUI thread without ever blocking the UHD threads on a mutex (a stall there
// turns directly into a TX underflow or RX overflow). The GUI thread is the
// only consumer, one UHD thread is the only producer, so a classic SPSC ring
// with atomic head/tail indices is sufficient - no locks anywhere.
//
// This buffer stores complex<float> IQ samples. Capacity must be a power of
// two so index wraparound can be done with a mask instead of a modulo.

#pragma once

#include <atomic>
#include <complex>
#include <cstddef>
#include <vector>

template <typename T>
class SpscRingBuffer
{
public:
    explicit SpscRingBuffer(size_t capacity_pow2)
        : capacity_(capacity_pow2), mask_(capacity_pow2 - 1), buffer_(capacity_pow2)
    {
        // Capacity must be a power of two for the mask trick below.
        // (Not enforced with an exception here to keep this header
        // allocation-free of exceptions; callers should pass a literal
        // power of two such as 1<<20.)
    }

    // Called only from the producer thread (TX or RX worker).
    // Writes as many samples as fit; if the buffer is full, oldest data is
    // overwritten (GUI plotting is a "latest data wins" consumer, we never
    // want the producer to block or drop the newest samples).
    void write(const T* data, size_t count)
    {
        size_t head = head_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < count; ++i) {
            buffer_[head & mask_] = data[i];
            ++head;
        }
        head_.store(head, std::memory_order_release);

        // Advance tail if producer has lapped the consumer, so read_latest
        // always returns a contiguous, valid window of the most recent
        // samples rather than a torn buffer.
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (head - tail > capacity_) {
            tail_.store(head - capacity_, std::memory_order_release);
        }
    }

    // Called only from the consumer thread (GUI). Copies out up to
    // max_count of the most recent samples into `out`, returns how many
    // were copied. This is a snapshot read; it does not consume/advance the
    // buffer, since the GUI wants to repeatedly redraw "the latest N
    // samples" rather than drain a queue.
    size_t read_latest(T* out, size_t max_count) const
    {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        size_t available = head - tail;
        size_t n = std::min(available, max_count);
        size_t start = head - n;
        for (size_t i = 0; i < n; ++i) {
            out[i] = buffer_[(start + i) & mask_];
        }
        return n;
    }

    size_t capacity() const { return capacity_; }

private:
    const size_t capacity_;
    const size_t mask_;
    std::vector<T> buffer_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};

using IqRingBuffer = SpscRingBuffer<std::complex<float>>;
