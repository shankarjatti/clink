// burst_controller.h
//
// Core state machine:
//
//   for each band in {2.4G, 5.1G, 5.8G}:
//       TX 10kHz tone burst for 10ms  (deterministically time-stamped)
//       10ms silence:
//           - retune TX+RX to next band, verify LO lock
//           - drain TX async error messages from the burst just finished
//           - run any user-supplied "other tasks" hook
//           - hold the line until exactly 10ms of silence has elapsed,
//             measured off the USRP's own clock (not host sleep()), so
//             timing does not drift over long runs
//
// Owns two threads:
//   - control thread: runs the loop above, and does the actual TX send()
//     calls (blocking send() calls naturally pace themselves against the
//     burst duration, so no separate TX thread is needed)
//   - rx thread: a free-running continuous receive loop, started once and
//     left running for the lifetime of the program, so the RX plot shows
//     both the burst and the silence/noise floor between bursts
//
// All uhd::usrp::multi_usrp calls (tune/gain/antenna) are made ONLY from
// the control thread, via UsrpManager. The RX thread only calls recv() on
// the streamer it already holds.

#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <uhd/types/time_spec.hpp>

#include "net_streamer.h"
#include "ring_buffer.h"
#include "signal_gen.h"
#include "status_provider.h"
#include "usrp_manager.h"

class BurstController : public IMonitorStatus
{
public:
    static constexpr double kBurstDurationS = 0.01;
    static constexpr double kSilenceDurationS = 0.01;
    static constexpr double kToneFreqHz = 10000.0;

    BurstController(UsrpManager& usrp, std::vector<BandGainConfig> bands,
                     IqRingBuffer& tx_ring, IqRingBuffer& rx_ring);

    // Optional hook invoked once per silence period, after retune/lock-check
    // and before waiting out the remainder of the gap. Use this to plug in
    // whatever "other tasks that might interfere with the next burst" your
    // setup needs (e.g. quiescing another radio, checking external state).
    // Runs on the control thread - keep it well under 10ms.
    void set_interference_mitigation_hook(std::function<void()> hook);

    void set_net_sender(std::shared_ptr<IqTcpSender> sender);

    void start();
    void stop(); // signals both threads and joins them

    // ---- status accessors for the GUI thread (all lock-free atomics) ----
    double current_freq_hz() const override { return current_freq_hz_.load(); }
    bool is_bursting() const override { return is_bursting_.load(); }
    bool last_retune_locked() const override { return last_retune_locked_.load(); }
    uint64_t burst_count() const override { return burst_count_.load(); }
    uint64_t tx_underflow_count() const override { return tx_underflow_count_.load(); }
    uint64_t tx_late_count() const override { return tx_late_count_.load(); }
    uint64_t rx_overflow_count() const override { return rx_overflow_count_.load(); }

private:
    void control_loop();
    void rx_loop();
    void net_stream_loop();

    // Sends kBurstDurationS worth of the tone, first packet time-stamped at
    // burst_start. Writes every transmitted chunk into tx_ring_ for the GUI.
    void run_burst(const BandGainConfig& band, const uhd::time_spec_t& burst_start);

    // Non-blocking-ish drain of queued TX async messages (underflow, late,
    // etc.) left over from the burst that just finished.
    void drain_tx_async_messages();

    UsrpManager& usrp_;
    std::vector<BandGainConfig> bands_;
    IqRingBuffer& tx_ring_;
    IqRingBuffer& rx_ring_;

    std::shared_ptr<IqTcpSender> net_sender_;

    ToneGenerator tone_gen_;
    size_t spb_ = 0; // samples per buffer, clamped to streamer's max
    std::vector<std::complex<float>> tone_chunk_;

    std::function<void()> interference_hook_;

    std::thread control_thread_;
    std::thread rx_thread_;
    std::thread net_thread_;
    std::atomic<bool> stop_flag_{false};

    std::atomic<double> current_freq_hz_{0.0};
    std::atomic<bool> is_bursting_{false};
    std::atomic<bool> last_retune_locked_{true};
    std::atomic<uint64_t> burst_count_{0};
    std::atomic<uint64_t> tx_underflow_count_{0};
    std::atomic<uint64_t> tx_late_count_{0};
    std::atomic<uint64_t> rx_overflow_count_{0};
};
