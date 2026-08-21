// main.cpp
//
// USRP B210 burst tester: TX0 -> 30dB pad -> RX1/RX2 loopback.
// Cycles 2.4 / 5.1 / 5.8 GHz, 1s burst of a 10kHz tone + 1s silence/retune
// per band, with a live 2x2 TX/RX IQ + FFT view.
//
// Usage:
//   ./usrp_burst_gui [device_args]
//
// device_args examples: "" (auto-detect single device), "serial=3195657",
// "addr=192.168.10.2"

#include <csignal>
#include <iostream>
#include <string>
#include <vector>

#include "burst_controller.h"
#include "gui.h"
#include "ring_buffer.h"
#include "usrp_manager.h"

#include <uhd/utils/safe_main.hpp>

namespace
{
constexpr double kSampleRateHz = 2e6;

// Per-band TX/RX gain (dB). These are starting points, not calibrated
// values - see the README calibration step. B210 TX gain range is roughly
// 0-89.8 dB, RX gain range roughly 0-76 dB depending on frequency; with a
// fixed 30 dB pad in the loopback path these moderate values are a
// reasonable starting point to avoid RX ADC clipping while keeping enough
// TX gain to be well above the noise floor.
std::vector<BandGainConfig> default_bands()
{
    return {
        {2.4e9, 65.0, 51.0},
        {5.1e9, 65.0, 61.0},
        {5.8e9, 65.0, 63.0},
    };
}
} // namespace

int UHD_SAFE_MAIN(int argc, char* argv[])
{
    std::string device_args = "";
    std::string stream_target = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--stream-to" && i + 1 < argc) {
            stream_target = argv[++i];
        } else if (arg.rfind("--args=", 0) == 0) {
            device_args = arg.substr(7);
        } else if (arg.rfind("--stream-to=", 0) == 0) {
            stream_target = arg.substr(12);
        } else if (device_args.empty() && arg[0] != '-') {
            device_args = arg;
        }
    }

    std::cout << "Opening USRP B210 (device_args=\"" << device_args << "\")...\n";
    UsrpManager usrp(device_args, kSampleRateHz);
    usrp.init();

    // Ring buffer capacity: power of two, large enough to comfortably hold
    // the GUI's FFT window (4096 samples) plus margin. 1<<16 = 65536
    // samples = ~32.8ms of history at 2MS/s.
    IqRingBuffer tx_ring(1 << 16);
    IqRingBuffer rx_ring(1 << 16);

    BurstController controller(usrp, default_bands(), tx_ring, rx_ring);

    // Optional network streaming to System 2
    std::shared_ptr<IqTcpSender> net_sender;
    if (!stream_target.empty()) {
        size_t colon = stream_target.find(':');
        std::string host = (colon != std::string::npos) ? stream_target.substr(0, colon) : "127.0.0.1";
        int port = (colon != std::string::npos) ? std::stoi(stream_target.substr(colon + 1)) : 5000;

        std::cout << "[System 1] Configured TCP streaming to System 2 at " << host << ":" << port << "\n";
        net_sender = std::make_shared<IqTcpSender>(host, port);
        controller.set_net_sender(net_sender);
    }

    // Extension point for "other tasks that might interfere with the next
    // burst" - wire up whatever your setup needs here (e.g. quiescing
    // another radio, checking external interlocks). No-op by default.
    controller.set_interference_mitigation_hook([]() {
        // TODO: hook up site-specific housekeeping tasks here.
    });

    controller.start();

    {
        Gui gui(controller, tx_ring, rx_ring, kSampleRateHz, "USRP B210 Burst Monitor (System 1 - Source)");
        gui.run(); // blocks until the window is closed
    }

    std::cout << "Shutting down...\n";
    controller.stop();

    return EXIT_SUCCESS;
}
