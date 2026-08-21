// net_streamer.h
//
// Asynchronous, low-latency TCP client sender and TCP server receiver
// for streaming IQ and FFT frames with zero packet loss.

#pragma once

#include <atomic>
#include <complex>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net_protocol.h"

class IqTcpSender
{
public:
    IqTcpSender(std::string target_ip, int target_port);
    ~IqTcpSender();

    void start();
    void stop();

    bool is_connected() const { return is_connected_.load(); }
    uint64_t frames_sent() const { return frames_sent_.load(); }
    uint64_t bytes_sent() const { return bytes_sent_.load(); }

    bool send_frame(uint64_t timestamp_ns, double center_freq_hz, bool is_bursting,
                    const std::complex<float>* rx_iq, size_t sample_count,
                    const float* rx_fft_db = nullptr, size_t fft_size = 0,
                    float iq_multiplier = 1.0f,
                    float elevation_deg = 0.0f, float azimuth_deg = 0.0f);

    bool send_sc16_frame(uint64_t timestamp_ns, double center_freq_hz, bool is_bursting,
                         const int16_t* rx_sc16, size_t sample_count,
                         const float* rx_fft_db = nullptr, size_t fft_size = 0,
                         float iq_multiplier = 1.0f,
                         float elevation_deg = 0.0f, float azimuth_deg = 0.0f);

private:
    void connection_loop();
    bool connect_socket();
    void close_socket();

    std::string target_ip_;
    int target_port_;

    int sock_fd_ = -1;
    std::atomic<bool> is_connected_{false};
    std::atomic<bool> stop_flag_{false};
    std::thread connect_thread_;

    uint32_t seq_num_{0};
    std::atomic<uint64_t> frames_sent_{0};
    std::atomic<uint64_t> bytes_sent_{0};

    std::mutex send_mutex_;
    std::vector<int16_t> rx_sc16_buf_;
    std::vector<uint8_t> send_packet_buf_;
};

class IqTcpReceiver
{
public:
    explicit IqTcpReceiver(int listen_port);
    ~IqTcpReceiver();

    void start();
    void stop();

    bool is_connected() const { return is_client_connected_.load(); }
    uint64_t frames_received() const { return frames_received_.load(); }
    uint64_t dropped_frames() const { return dropped_frames_.load(); }
    uint64_t bytes_received() const { return bytes_received_.load(); }

    bool recv_frame(IqFrameHeader& out_header,
                    std::vector<int16_t>& out_rx_sc16,
                    std::vector<float>& out_rx_fft);

private:
    void accept_loop();
    void close_client();

    int listen_port_;
    int listen_fd_ = -1;
    int client_fd_ = -1;

    std::atomic<bool> is_client_connected_{false};
    std::atomic<bool> stop_flag_{false};
    std::thread accept_thread_;

    uint32_t expected_seq_{0};
    std::atomic<uint64_t> frames_received_{0};
    std::atomic<uint64_t> dropped_frames_{0};
    std::atomic<uint64_t> bytes_received_{0};
};
