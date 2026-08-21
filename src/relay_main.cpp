// relay_main.cpp
//
// System 2 Node (usrp_relay_gui):
// 1. Receives sc16 IQ + FFT stream from System 1 over TCP with TCP_NODELAY
// 2. Converts sc16 to complex float (fc32) and writes to local ring buffers
// 3. Renders the exact same 2x2 live GUI plot in real time
// 4. In parallel, converts float back to sc16 and forwards to System 3 over TCP

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
    int listen_port = 5000;
    std::string stream_target = "127.0.0.1:6000";

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
    std::cout << "  USRP B210 Relay & Real-Time Monitor (System 2)      \n";
    std::cout << "  Listening for System 1 on port: " << listen_port << "\n";
    if (!stream_target.empty()) {
        std::cout << "  Forwarding stream to System 3 at: " << stream_target << "\n";
    }
    std::cout << "=====================================================\n";

    constexpr double kSampleRateHz = 2e6;
    IqRingBuffer tx_ring(1 << 16);
    IqRingBuffer rx_ring(1 << 16);
    NetStatusProvider net_status;

    IqTcpReceiver receiver(listen_port);
    receiver.start();

    std::shared_ptr<IqTcpSender> sender;
    if (!stream_target.empty()) {
        size_t colon = stream_target.find(':');
        std::string host = (colon != std::string::npos) ? stream_target.substr(0, colon) : "127.0.0.1";
        int port = (colon != std::string::npos) ? std::stoi(stream_target.substr(colon + 1)) : 6000;
        sender = std::make_shared<IqTcpSender>(host, port);
        sender->start();
    }

    std::atomic<bool> stop_flag{false};

    // Worker thread: Receives sc16 from S1 -> converts to float -> feeds GUI -> converts float to sc16 -> sends to S3
    std::thread relay_thread([&]() {
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

            // 1. Convert sc16 from System 1 to complex float (fc32)
            net_util::sc16_to_float(tx_sc16.data(), tx_float.data(), count);
            net_util::sc16_to_float(rx_sc16.data(), rx_float.data(), count);

            // 2. Write to local ring buffers for live GUI plotting on System 2
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

            // 4. Convert float back to sc16 and forward to System 3 over TCP
            if (sender && sender->is_connected()) {
                sender->send_frame(hdr.timestamp_ns, hdr.center_freq_hz, is_bursting,
                                   tx_float.data(), rx_float.data(), count,
                                   tx_fft.empty() ? nullptr : tx_fft.data(),
                                   rx_fft.empty() ? nullptr : rx_fft.data(),
                                   hdr.fft_size);
            }
        }
    });

    // Launch identical live 2x2 GUI window on System 2
    {
        Gui gui(net_status, tx_ring, rx_ring, kSampleRateHz, "USRP B210 Relay Monitor (System 2)");
        gui.run();
    }

    std::cout << "[System 2] Shutting down...\n";
    stop_flag = true;
    if (relay_thread.joinable()) relay_thread.join();
    receiver.stop();
    if (sender) sender->stop();

    return 0;
}
