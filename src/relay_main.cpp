// relay_main.cpp
//
// System 2 Node (usrp_relay_gui):
// 1. Receives unscaled sc16 IQ + FFT stream from System 1 over TCP with TCP_NODELAY
// 2. Demultiplexes into 4 channels (2.4G x2.0, 5.1G x3.0, 5.8G x4.0, Combined)
// 3. Renders 8-plot (4 Waveforms + 4 FFTs) GUI in real time
// 4. Encodes scaled float samples to sc16 and forwards to System 3 over TCP

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "multichannel_demux.h"
#include "multichannel_gui.h"
#include "net_protocol.h"
#include "net_streamer.h"
#include "status_provider.h"

int main(int argc, char* argv[])
{
    int listen_port = 5000;
    std::string stream_target = "127.0.0.1:6001";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--listen-port" && i + 1 < argc) {
            listen_port = std::stoi(argv[++i]);
        } else if (arg == "--stream-to" && i + 1 < argc) {
            stream_target = argv[++i];
        } else if (arg.rfind("--listen-port=", 0) == 0) {
            listen_port = std::stoi(arg.substr(14));
        } else if (arg.rfind("--stream-to=", 0) == 0) {
            stream_target = arg.substr(12);
        }
    }

    std::cout << "=====================================================\n";
    std::cout << "  USRP B210 Relay & 8-Plot Monitor (System 2)         \n";
    std::cout << "  Listening for System 1 on port: " << listen_port << "\n";
    if (!stream_target.empty()) {
        std::cout << "  Forwarding scaled sc16 stream to System 3 at: " << stream_target << "\n";
    }
    std::cout << "=====================================================\n";

    constexpr double kSampleRateHz = 2e6;
    NetStatusProvider net_status;
    MultichannelDemux demux;

    IqTcpReceiver receiver(listen_port);
    receiver.start();

    std::shared_ptr<IqTcpSender> sender;
    if (!stream_target.empty()) {
        size_t colon = stream_target.find(':');
        std::string host = (colon != std::string::npos) ? stream_target.substr(0, colon) : "127.0.0.1";
        int port = (colon != std::string::npos) ? std::stoi(stream_target.substr(colon + 1)) : 6001;
        sender = std::make_shared<IqTcpSender>(host, port);
        sender->start();
    }

    std::atomic<bool> stop_flag{false};

    // Worker thread: Receives sc16 from S1 -> converts & scales to float -> feeds 4-channel demux -> encodes to sc16 -> sends to S3
    std::thread relay_thread([&]() {
        IqFrameHeader hdr;
        std::vector<int16_t> rx_sc16;
        std::vector<float> rx_fft;
        std::vector<std::complex<float>> rx_scaled;

        bool prev_bursting = false;
        double ema_latency_ms = 0.0;
        double ema_jitter_ms = 0.0;
        auto last_frame_time = std::chrono::steady_clock::now();
        auto last_fps_calc_time = std::chrono::steady_clock::now();
        uint64_t frame_counter = 0;

        while (!stop_flag.load()) {
            if (!receiver.recv_frame(hdr, rx_sc16, rx_fft)) {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                continue;
            }

            auto now_steady = std::chrono::steady_clock::now();
            uint64_t now_wall_ns = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());

            // Compute one-way transit latency from System 1
            if (hdr.timestamp_ns > 0 && now_wall_ns >= hdr.timestamp_ns) {
                double raw_latency_ms = static_cast<double>(now_wall_ns - hdr.timestamp_ns) / 1e6;
                if (raw_latency_ms >= 0.0 && raw_latency_ms < 10000.0) {
                    if (ema_latency_ms == 0.0) ema_latency_ms = raw_latency_ms;
                    else ema_latency_ms = 0.95 * ema_latency_ms + 0.05 * raw_latency_ms;
                    net_status.set_latency_ms(ema_latency_ms);
                }
            }

            // Compute inter-frame arrival interval and delivery jitter (expected 1.0ms at 2MS/s with 2000 spb)
            double delta_ms = std::chrono::duration<double, std::milli>(now_steady - last_frame_time).count();
            last_frame_time = now_steady;
            if (delta_ms > 0.0 && delta_ms < 50.0) {
                double jitter = std::abs(delta_ms - 1.0);
                if (ema_jitter_ms == 0.0) ema_jitter_ms = jitter;
                else ema_jitter_ms = 0.95 * ema_jitter_ms + 0.05 * jitter;
                net_status.set_jitter_ms(ema_jitter_ms);
            }

            // Compute ingestion frame rate (FPS)
            ++frame_counter;
            if (frame_counter % 100 == 0) {
                double elapsed_s = std::chrono::duration<double>(now_steady - last_fps_calc_time).count();
                if (elapsed_s > 0.0) {
                    double fps = 100.0 / elapsed_s;
                    net_status.set_frame_rate_fps(fps);
                }
                last_fps_calc_time = now_steady;
            }

            // Process, scale IQ (x2, x3, x4), route to 4 channels and route direct FFTs
            float multiplier = demux.process_incoming_frame(hdr,
                                                           rx_sc16.data(),
                                                           rx_fft.empty() ? nullptr : rx_fft.data(),
                                                           rx_scaled);

            // Update status metrics
            net_status.set_freq_hz(hdr.center_freq_hz);
            bool is_bursting = (hdr.is_bursting != 0);
            net_status.set_bursting(is_bursting);
            if (is_bursting && !prev_bursting) {
                net_status.add_burst_count(1);
            }
            prev_bursting = is_bursting;
            net_status.set_rx_overflow(receiver.dropped_frames());

            // Forward scaled float samples encoded as sc16 + direct FFTs + angles to System 3
            if (sender && sender->is_connected()) {
                sender->send_frame(hdr.timestamp_ns,
                                   hdr.center_freq_hz,
                                   is_bursting,
                                   rx_scaled.data(),
                                   hdr.sample_count,
                                   rx_fft.empty() ? nullptr : rx_fft.data(),
                                   hdr.fft_size,
                                   multiplier,
                                   hdr.elevation_deg,
                                   hdr.azimuth_deg);
            }
        }
    });

    // Launch 4x3 Monitor GUI window on System 2
    {
        MultichannelGui gui(net_status, demux, kSampleRateHz, "USRP B210 Relay Monitor (System 2 - 12 Plots)");
        gui.run();
    }

    std::cout << "[System 2] Shutting down...\n";
    stop_flag = true;
    if (relay_thread.joinable()) relay_thread.join();
    receiver.stop();
    if (sender) sender->stop();

    return 0;
}
