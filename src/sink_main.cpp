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
#include "multichannel_gui.h"
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

        while (!stop_flag.load()) {
            if (!receiver.recv_frame(hdr, tx_sc16, rx_sc16, tx_fft, rx_fft)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
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

    // Launch identical 8-plot GUI window on System 3
    {
        MultichannelGui gui(net_status, demux, kSampleRateHz, "USRP B210 Sink Monitor (System 3 - 8 Plots)");
        gui.run();
    }

    std::cout << "[System 3] Shutting down...\n";
    stop_flag = true;
    if (sink_thread.joinable()) sink_thread.join();
    receiver.stop();

    return 0;
}
