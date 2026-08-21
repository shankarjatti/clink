// sink_main.cpp
//
// System 3 Node (usrp_sink_gui):
// 1. Receives scaled sc16 IQ + direct FFT stream from System 2 over TCP with TCP_NODELAY
// 2. Converts sc16 to scaled float (fc32) and demultiplexes into 4 channels
// 3. Renders the exact same 8-plot (4 Waveforms + 4 FFTs) GUI in real time
// 4. Verifies sequence numbers, packet continuity, and 0-loss metrics

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "multichannel_demux.h"
#include "operator_console_gui.h"
#include "net_protocol.h"
#include "net_streamer.h"
#include "status_provider.h"

int main(int argc, char* argv[])
{
    int listen_port = 6001;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--listen-port" && i + 1 < argc) {
            listen_port = std::stoi(argv[++i]);
        } else if (arg.rfind("--listen-port=", 0) == 0) {
            listen_port = std::stoi(arg.substr(14));
        }
    }

    std::cout << "=====================================================\n";
    std::cout << "  USRP B210 Sink & 8-Plot Monitor (System 3)          \n";
    std::cout << "  Listening for System 2 on port: " << listen_port << "\n";
    std::cout << "=====================================================\n";

    constexpr double kSampleRateHz = 2e6;
    NetStatusProvider net_status;
    MultichannelDemux demux;

    IqTcpReceiver receiver(listen_port);
    receiver.start();

    std::atomic<bool> stop_flag{false};

    // Worker thread: Receives scaled sc16 from S2 -> converts to float -> feeds 4-channel demux & GUI
    std::thread sink_thread([&]() {
        IqFrameHeader hdr;
        std::vector<int16_t> tx_sc16;
        std::vector<int16_t> rx_sc16;
        std::vector<float> tx_fft;
        std::vector<float> rx_fft;

        std::vector<std::complex<float>> tx_scaled;
        std::vector<std::complex<float>> rx_scaled;

        bool prev_bursting = false;
        double ema_latency_ms = 0.0;
        double ema_jitter_ms = 0.0;
        auto last_frame_time = std::chrono::steady_clock::now();
        auto last_fps_calc_time = std::chrono::steady_clock::now();
        uint64_t frame_counter = 0;

        while (!stop_flag.load()) {
            if (!receiver.recv_frame(hdr, tx_sc16, rx_sc16, tx_fft, rx_fft)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }

            auto now_steady = std::chrono::steady_clock::now();
            uint64_t now_wall_ns = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());

            // Compute true End-to-End transit latency (System 1 -> System 2 -> System 3)
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

            // Decode scaled sc16 to float, route to 4 channels and route direct FFTs
            demux.process_incoming_frame(hdr,
                                         tx_sc16.data(),
                                         rx_sc16.data(),
                                         tx_fft.empty() ? nullptr : tx_fft.data(),
                                         rx_fft.empty() ? nullptr : rx_fft.data(),
                                         tx_scaled,
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
        }
    });

    // Launch Operator Console GUI window on System 3
    {
        OperatorConsoleGui console(net_status, demux, kSampleRateHz, "USRP B210 Operator Console (System 3)");
        console.run();
    }

    std::cout << "[System 3] Shutting down...\n";
    stop_flag = true;
    if (sink_thread.joinable()) sink_thread.join();
    receiver.stop();

    return 0;
}
