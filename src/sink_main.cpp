// sink_main.cpp
//
// System 3 Node (usrp_sink_gui):
// 1. Receives sc16 IQ + FFT stream from System 2 over TCP with TCP_NODELAY
// 2. Converts sc16 to complex float (fc32) and writes to local ring buffers
// 3. Renders the exact same 2x2 live GUI plot in real time
// 4. Verifies sequence numbers, packet continuity, and 0-loss metrics

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gui.h"
#include "net_protocol.h"
#include "net_streamer.h"
#include "ring_buffer.h"
#include "status_provider.h"

int main(int argc, char* argv[])
{
    int listen_port = 6000;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--listen-port" && i + 1 < argc) {
            listen_port = std::stoi(argv[++i]);
        } else if (arg.rfind("--listen-port=", 0) == 0) {
            listen_port = std::stoi(arg.substr(14));
        }
    }

    std::cout << "=====================================================\n";
    std::cout << "  USRP B210 Sink & Real-Time Monitor (System 3)       \n";
    std::cout << "  Listening for System 2 on port: " << listen_port << "\n";
    std::cout << "=====================================================\n";

    constexpr double kSampleRateHz = 2e6;
    IqRingBuffer tx_ring(1 << 16);
    IqRingBuffer rx_ring(1 << 16);
    NetStatusProvider net_status;

    IqTcpReceiver receiver(listen_port);
    receiver.start();

    std::atomic<bool> stop_flag{false};

    // Worker thread: Receives sc16 from S2 -> converts to float -> feeds GUI & metrics
    std::thread sink_thread([&]() {
        IqFrameHeader hdr;
        std::vector<int16_t> tx_sc16;
        std::vector<int16_t> rx_sc16;
        std::vector<float> tx_fft;
        std::vector<float> rx_fft;

        std::vector<std::complex<float>> tx_float;
        std::vector<std::complex<float>> rx_float;

        bool prev_bursting = false;

        while (!stop_flag.load()) {
            if (!receiver.recv_frame(hdr, tx_sc16, rx_sc16, tx_fft, rx_fft)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            size_t count = hdr.sample_count;
            tx_float.resize(count);
            rx_float.resize(count);

            // 1. Convert sc16 from System 2 to complex float (fc32)
            net_util::sc16_to_float(tx_sc16.data(), tx_float.data(), count);
            net_util::sc16_to_float(rx_sc16.data(), rx_float.data(), count);

            // 2. Write to local ring buffers for live GUI plotting on System 3
            tx_ring.write(tx_float.data(), count);
            rx_ring.write(rx_float.data(), count);

            // 3. Update status metrics
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

    // Launch identical live 2x2 GUI window on System 3
    {
        Gui gui(net_status, tx_ring, rx_ring, kSampleRateHz, "USRP B210 Sink Monitor (System 3)");
        gui.run();
    }

    std::cout << "[System 3] Shutting down...\n";
    stop_flag = true;
    if (sink_thread.joinable()) sink_thread.join();
    receiver.stop();

    return 0;
}
