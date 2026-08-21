// net_streamer.cpp

#include "net_streamer.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

IqTcpSender::IqTcpSender(std::string target_ip, int target_port)
    : target_ip_(std::move(target_ip)), target_port_(target_port)
{
}

IqTcpSender::~IqTcpSender()
{
    stop();
}

void IqTcpSender::start()
{
    stop_flag_ = false;
    connect_thread_ = std::thread(&IqTcpSender::connection_loop, this);
}

void IqTcpSender::stop()
{
    stop_flag_ = true;
    close_socket();
    if (connect_thread_.joinable()) {
        connect_thread_.join();
    }
}

void IqTcpSender::close_socket()
{
    std::lock_guard<std::mutex> lock(send_mutex_);
    is_connected_ = false;
    if (sock_fd_ >= 0) {
        close(sock_fd_);
        sock_fd_ = -1;
    }
}

bool IqTcpSender::connect_socket()
{
    std::lock_guard<std::mutex> lock(send_mutex_);
    if (sock_fd_ >= 0) {
        close(sock_fd_);
        sock_fd_ = -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    net_util::set_tcp_nodelay(fd);
    net_util::set_socket_buffers(fd, 4 * 1024 * 1024);

    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(static_cast<uint16_t>(target_port_));

    if (inet_pton(AF_INET, target_ip_.c_str(), &serv_addr.sin_addr) <= 0) {
        close(fd);
        return false;
    }

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
        close(fd);
        return false;
    }

    sock_fd_ = fd;
    is_connected_ = true;
    std::cout << "[IqTcpSender] Connected to " << target_ip_ << ":" << target_port_ << "\n";
    return true;
}

void IqTcpSender::connection_loop()
{
    while (!stop_flag_.load()) {
        if (!is_connected_.load()) {
            connect_socket();
        }
        if (!is_connected_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

bool IqTcpSender::send_frame(uint64_t timestamp_ns, double center_freq_hz, bool is_bursting,
                             const std::complex<float>* tx_iq, const std::complex<float>* rx_iq,
                             size_t sample_count,
                             const float* tx_fft_db, const float* rx_fft_db,
                             size_t fft_size, float iq_multiplier)
{
    if (!is_connected_.load()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(send_mutex_);
    if (!is_connected_.load() || sock_fd_ < 0) {
        return false;
    }

    // Convert tx and rx float samples to sc16 (using scaled encoder if multiplier != 1.0)
    tx_sc16_buf_.resize(sample_count * 2);
    rx_sc16_buf_.resize(sample_count * 2);

    if (std::abs(iq_multiplier - 1.0f) > 1e-4f) {
        net_util::float_to_sc16_scaled(tx_iq, tx_sc16_buf_.data(), sample_count, iq_multiplier);
        net_util::float_to_sc16_scaled(rx_iq, rx_sc16_buf_.data(), sample_count, iq_multiplier);
    } else {
        net_util::float_to_sc16(tx_iq, tx_sc16_buf_.data(), sample_count);
        net_util::float_to_sc16(rx_iq, rx_sc16_buf_.data(), sample_count);
    }

    IqFrameHeader hdr;
    hdr.magic = kIqFrameMagic;
    hdr.sequence_num = seq_num_++;
    hdr.timestamp_ns = timestamp_ns;
    hdr.center_freq_hz = center_freq_hz;
    hdr.iq_multiplier = iq_multiplier;
    hdr.sample_count = static_cast<uint32_t>(sample_count);
    hdr.fft_size = (tx_fft_db && rx_fft_db) ? static_cast<uint32_t>(fft_size) : 0;
    hdr.is_bursting = is_bursting ? 1 : 0;
    hdr.reserved = 0;

    size_t iq_bytes_per_chan = sample_count * sizeof(int16_t) * 2;
    size_t fft_bytes_per_chan = hdr.fft_size * sizeof(float);
    size_t total_payload_bytes = sizeof(IqFrameHeader) + (iq_bytes_per_chan * 2) + (fft_bytes_per_chan * 2);

    send_packet_buf_.resize(total_payload_bytes);
    uint8_t* dst = send_packet_buf_.data();

    // 1. Copy Header
    std::memcpy(dst, &hdr, sizeof(IqFrameHeader));
    dst += sizeof(IqFrameHeader);

    // 2. Copy TX sc16
    std::memcpy(dst, tx_sc16_buf_.data(), iq_bytes_per_chan);
    dst += iq_bytes_per_chan;

    // 3. Copy RX sc16
    std::memcpy(dst, rx_sc16_buf_.data(), iq_bytes_per_chan);
    dst += iq_bytes_per_chan;

    // 4. Copy Optional FFT vectors
    if (hdr.fft_size > 0) {
        std::memcpy(dst, tx_fft_db, fft_bytes_per_chan);
        dst += fft_bytes_per_chan;
        std::memcpy(dst, rx_fft_db, fft_bytes_per_chan);
        dst += fft_bytes_per_chan;
    }

    if (!net_util::send_all(sock_fd_, send_packet_buf_.data(), total_payload_bytes)) {
        std::cerr << "[IqTcpSender] send failed, closing connection\n";
        is_connected_ = false;
        close(sock_fd_);
        sock_fd_ = -1;
        return false;
    }

    frames_sent_.fetch_add(1);
    bytes_sent_.fetch_add(total_payload_bytes);
    return true;
}

bool IqTcpSender::send_sc16_frame(uint64_t timestamp_ns, double center_freq_hz, bool is_bursting,
                                  const int16_t* tx_sc16, const int16_t* rx_sc16,
                                  size_t sample_count,
                                  const float* tx_fft_db, const float* rx_fft_db,
                                  size_t fft_size, float iq_multiplier)
{
    if (!is_connected_.load()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(send_mutex_);
    if (!is_connected_.load() || sock_fd_ < 0) {
        return false;
    }

    IqFrameHeader hdr;
    hdr.magic = kIqFrameMagic;
    hdr.sequence_num = seq_num_++;
    hdr.timestamp_ns = timestamp_ns;
    hdr.center_freq_hz = center_freq_hz;
    hdr.iq_multiplier = iq_multiplier;
    hdr.sample_count = static_cast<uint32_t>(sample_count);
    hdr.fft_size = (tx_fft_db && rx_fft_db) ? static_cast<uint32_t>(fft_size) : 0;
    hdr.is_bursting = is_bursting ? 1 : 0;
    hdr.reserved = 0;

    size_t iq_bytes_per_chan = sample_count * sizeof(int16_t) * 2;
    size_t fft_bytes_per_chan = hdr.fft_size * sizeof(float);
    size_t total_payload_bytes = sizeof(IqFrameHeader) + (iq_bytes_per_chan * 2) + (fft_bytes_per_chan * 2);

    send_packet_buf_.resize(total_payload_bytes);
    uint8_t* dst = send_packet_buf_.data();

    // 1. Copy Header
    std::memcpy(dst, &hdr, sizeof(IqFrameHeader));
    dst += sizeof(IqFrameHeader);

    // 2. Copy TX sc16
    std::memcpy(dst, tx_sc16, iq_bytes_per_chan);
    dst += iq_bytes_per_chan;

    // 3. Copy RX sc16
    std::memcpy(dst, rx_sc16, iq_bytes_per_chan);
    dst += iq_bytes_per_chan;

    // 4. Copy Optional FFT vectors
    if (hdr.fft_size > 0) {
        std::memcpy(dst, tx_fft_db, fft_bytes_per_chan);
        dst += fft_bytes_per_chan;
        std::memcpy(dst, rx_fft_db, fft_bytes_per_chan);
        dst += fft_bytes_per_chan;
    }

    if (!net_util::send_all(sock_fd_, send_packet_buf_.data(), total_payload_bytes)) {
        std::cerr << "[IqTcpSender] send failed, closing connection\n";
        is_connected_ = false;
        close(sock_fd_);
        sock_fd_ = -1;
        return false;
    }

    frames_sent_.fetch_add(1);
    bytes_sent_.fetch_add(total_payload_bytes);
    return true;
}

// -----------------------------------------------------------------------------
// IqTcpReceiver implementation
// -----------------------------------------------------------------------------

IqTcpReceiver::IqTcpReceiver(int listen_port) : listen_port_(listen_port)
{
}

IqTcpReceiver::~IqTcpReceiver()
{
    stop();
}

void IqTcpReceiver::start()
{
    stop_flag_ = false;

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "[IqTcpReceiver] Failed to create socket\n";
        return;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(listen_port_));

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[IqTcpReceiver] Failed to bind to port " << listen_port_ << "\n";
        close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    if (listen(listen_fd_, 5) < 0) {
        std::cerr << "[IqTcpReceiver] Failed to listen on port " << listen_port_ << "\n";
        close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    std::cout << "[IqTcpReceiver] Listening on 0.0.0.0:" << listen_port_ << "\n";
    accept_thread_ = std::thread(&IqTcpReceiver::accept_loop, this);
}

void IqTcpReceiver::stop()
{
    stop_flag_ = true;
    close_client();
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void IqTcpReceiver::close_client()
{
    is_client_connected_ = false;
    if (client_fd_ >= 0) {
        close(client_fd_);
        client_fd_ = -1;
    }
}

void IqTcpReceiver::accept_loop()
{
    while (!stop_flag_.load()) {
        if (client_fd_ < 0 && listen_fd_ >= 0) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int new_fd = accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);

            if (new_fd >= 0) {
                if (stop_flag_.load()) {
                    close(new_fd);
                    break;
                }
                net_util::set_tcp_nodelay(new_fd);
                net_util::set_socket_buffers(new_fd, 4 * 1024 * 1024);

                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
                std::cout << "[IqTcpReceiver] Client connected from " << client_ip << "\n";

                client_fd_ = new_fd;
                expected_seq_ = 0;
                is_client_connected_ = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool IqTcpReceiver::recv_frame(IqFrameHeader& out_header,
                              std::vector<int16_t>& out_tx_sc16,
                              std::vector<int16_t>& out_rx_sc16,
                              std::vector<float>& out_tx_fft,
                              std::vector<float>& out_rx_fft)
{
    if (!is_client_connected_.load() || client_fd_ < 0) {
        return false;
    }

    // 1. Read Header
    if (!net_util::recv_exact(client_fd_, &out_header, sizeof(IqFrameHeader))) {
        std::cerr << "[IqTcpReceiver] Client disconnected (header read failed)\n";
        close_client();
        return false;
    }

    if (out_header.magic != kIqFrameMagic) {
        std::cerr << "[IqTcpReceiver] Corrupt frame magic: 0x" << std::hex << out_header.magic << "\n";
        close_client();
        return false;
    }

    // Check Sequence Continuity for 0-loss verification
    if (frames_received_.load() > 0 && out_header.sequence_num != expected_seq_) {
        uint32_t lost = (out_header.sequence_num > expected_seq_) ? (out_header.sequence_num - expected_seq_) : 1;
        dropped_frames_.fetch_add(lost);
        std::cerr << "[IqTcpReceiver] Sequence gap detected: expected " << expected_seq_
                  << ", got " << out_header.sequence_num << " (lost " << lost << " frames)\n";
    }
    expected_seq_ = out_header.sequence_num + 1;

    // 2. Read TX sc16
    size_t iq_samples_per_chan = out_header.sample_count;
    out_tx_sc16.resize(iq_samples_per_chan * 2);
    if (!net_util::recv_exact(client_fd_, out_tx_sc16.data(), iq_samples_per_chan * sizeof(int16_t) * 2)) {
        close_client();
        return false;
    }

    // 3. Read RX sc16
    out_rx_sc16.resize(iq_samples_per_chan * 2);
    if (!net_util::recv_exact(client_fd_, out_rx_sc16.data(), iq_samples_per_chan * sizeof(int16_t) * 2)) {
        close_client();
        return false;
    }

    // 4. Read Optional FFTs
    if (out_header.fft_size > 0) {
        out_tx_fft.resize(out_header.fft_size);
        out_rx_fft.resize(out_header.fft_size);
        if (!net_util::recv_exact(client_fd_, out_tx_fft.data(), out_header.fft_size * sizeof(float))) {
            close_client();
            return false;
        }
        if (!net_util::recv_exact(client_fd_, out_rx_fft.data(), out_header.fft_size * sizeof(float))) {
            close_client();
            return false;
        }
    } else {
        out_tx_fft.clear();
        out_rx_fft.clear();
    }

    size_t total_frame_bytes = sizeof(IqFrameHeader) + (iq_samples_per_chan * sizeof(int16_t) * 4)
                             + (out_header.fft_size * sizeof(float) * 2);
    frames_received_.fetch_add(1);
    bytes_received_.fetch_add(total_frame_bytes);
    return true;
}
