// usrp_manager.cpp

#include "usrp_manager.h"

#include <iostream>
#include <stdexcept>

#include <uhd/utils/thread.hpp>
#include <uhd/types/tune_request.hpp>

UsrpManager::UsrpManager(const std::string& device_args, double sample_rate_hz)
    : device_args_(device_args), sample_rate_hz_(sample_rate_hz)
{
}

void UsrpManager::init()
{
    usrp_ = uhd::usrp::multi_usrp::make(device_args_);

    // Subdev spec: TX uses A:A (RF A TX/RX frontend), RX uses A:B (RF B RX2 frontend).
    // This allows simultaneous full-duplex TX/RX loopback without channel crossbar conflicts.
    usrp_->set_tx_subdev_spec(uhd::usrp::subdev_spec_t("A:A"));
    usrp_->set_rx_subdev_spec(uhd::usrp::subdev_spec_t("A:B"));

    // Antennas: TX uses "TX/RX" on chain A, RX uses "RX2" on chain B.
    usrp_->set_tx_antenna("TX/RX", kTxChan);
    usrp_->set_rx_antenna("RX2", kRxChan);

    // Sample rates.
    usrp_->set_tx_rate(sample_rate_hz_, kTxChan);
    usrp_->set_rx_rate(sample_rate_hz_, kRxChan);

    double actual_tx_rate = usrp_->get_tx_rate(kTxChan);
    double actual_rx_rate = usrp_->get_rx_rate(kRxChan);
    std::cout << "[UsrpManager] TX rate: " << actual_tx_rate
              << " Hz, RX rate: " << actual_rx_rate << " Hz\n";

    // Lock mboard time reference to the device's internal clock (no
    // external ref/PPS in this single-device loopback setup) and reset the
    // time counter to zero so time_spec_t scheduling starts from a known
    // point.
    usrp_->set_clock_source("internal");
    usrp_->set_time_source("internal");
    usrp_->set_time_now(uhd::time_spec_t(0.0));

    // Stream args: complex<float> host format, sc16 wire format (standard
    // for B210 over USB3).
    uhd::stream_args_t tx_stream_args("fc32", "sc16");
    tx_stream_args.channels = {kTxChan};
    tx_streamer_ = usrp_->get_tx_stream(tx_stream_args);

    uhd::stream_args_t rx_stream_args("fc32", "sc16");
    rx_stream_args.channels = {kRxChan};
    rx_streamer_ = usrp_->get_rx_stream(rx_stream_args);

    std::cout << "[UsrpManager] Device initialized: "
              << usrp_->get_pp_string() << "\n";
}

bool UsrpManager::check_tx_lo_locked(double timeout_s)
{
    const auto deadline = std::chrono::steady_clock::now() +
                           std::chrono::duration<double>(timeout_s);
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            if (usrp_->get_tx_sensor("lo_locked", kTxChan).to_bool()) {
                return true;
            }
        } catch (const uhd::lookup_error&) {
            // Some daughterboard/firmware combos don't expose lo_locked;
            // treat absence of the sensor as "can't verify, proceed".
            return true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return false;
}

bool UsrpManager::check_rx_lo_locked(double timeout_s)
{
    const auto deadline = std::chrono::steady_clock::now() +
                           std::chrono::duration<double>(timeout_s);
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            if (usrp_->get_rx_sensor("lo_locked", kRxChan).to_bool()) {
                return true;
            }
        } catch (const uhd::lookup_error&) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return false;
}

bool UsrpManager::retune(double freq_hz, double tx_gain_db, double rx_gain_db,
                          double lock_timeout_s)
{
    uhd::tune_request_t tx_tune(freq_hz);
    uhd::tune_request_t rx_tune(freq_hz);

    usrp_->set_tx_freq(tx_tune, kTxChan);
    usrp_->set_rx_freq(rx_tune, kRxChan);

    usrp_->set_tx_gain(tx_gain_db, kTxChan);
    usrp_->set_rx_gain(rx_gain_db, kRxChan);

    bool tx_locked = check_tx_lo_locked(lock_timeout_s);
    bool rx_locked = check_rx_lo_locked(lock_timeout_s);

    if (!tx_locked || !rx_locked) {
        std::cerr << "[UsrpManager] WARNING: LO lock failed at "
                  << (freq_hz / 1e9) << " GHz (tx_locked=" << tx_locked
                  << " rx_locked=" << rx_locked << ")\n";
        return false;
    }
    return true;
}
