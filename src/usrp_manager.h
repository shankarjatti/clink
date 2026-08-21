// usrp_manager.h
//
// Owns the single uhd::usrp::multi_usrp device and both streamers:
//   - TX on channel 0, antenna "TX/RX"
//   - RX on channel 1, antenna "RX2"
// This matches a loopback rig where TX0 is cabled through a 30 dB pad into
// RX1's RX2 port.
//
// IMPORTANT THREADING RULE:
// All device-state calls (tune, gain, antenna, sensor checks) go through
// this class and must only ever be called from ONE thread (the
// BurstController's control thread). The TX and RX worker threads only
// call send()/recv() on the streamers they already hold; they never touch
// tuning/gain themselves. This avoids concurrent access to the same
// multi_usrp object from multiple threads during a retune.

#pragma once

#include <complex>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/stream.hpp>

struct BandGainConfig
{
    double freq_hz;
    double tx_gain_db;
    double rx_gain_db;
};

class UsrpManager
{
public:
    // device_args: e.g. "" for auto-detect, or "serial=XXXXXXX" / "addr=192.168.10.2"
    UsrpManager(const std::string& device_args, double sample_rate_hz);

    // Configures channel roles, antennas, initial rates. Must be called once
    // before streaming.
    void init();

    // Retunes both TX (ch0) and RX (ch1) to freq_hz, applies the matching
    // gain from the band table, and blocks (with timeout) until both LOs
    // report locked. Returns false if lock was not achieved within the
    // timeout - caller should treat this as a fault, not transmit, and
    // retry/log.
    bool retune(double freq_hz, double tx_gain_db, double rx_gain_db,
                double lock_timeout_s = 0.005);

    uhd::tx_streamer::sptr tx_streamer() { return tx_streamer_; }
    uhd::rx_streamer::sptr rx_streamer() { return rx_streamer_; }
    uhd::usrp::multi_usrp::sptr device() { return usrp_; }

    double sample_rate() const { return sample_rate_hz_; }

    // Device time, used to schedule burst start/stop deterministically
    // instead of relying on host-side sleep() timing.
    uhd::time_spec_t now() const { return usrp_->get_time_now(); }

private:
    bool check_tx_lo_locked(double timeout_s);
    bool check_rx_lo_locked(double timeout_s);

    std::string device_args_;
    double sample_rate_hz_;

    uhd::usrp::multi_usrp::sptr usrp_;
    uhd::tx_streamer::sptr tx_streamer_;
    uhd::rx_streamer::sptr rx_streamer_;

    static constexpr size_t kTxChan = 0;
    static constexpr size_t kRxChan = 0;
};
