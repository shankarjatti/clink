// status_provider.h
//
// Interface for status bar metrics shared between the SDR hardware controller
// (System 1) and network relay/sink nodes (System 2 & 3).

#pragma once

#include <atomic>
#include <cstdint>

struct IMonitorStatus
{
    virtual ~IMonitorStatus() = default;
    virtual double current_freq_hz() const = 0;
    virtual bool is_bursting() const = 0;
    virtual bool last_retune_locked() const = 0;
    virtual uint64_t burst_count() const = 0;
    virtual uint64_t tx_underflow_count() const = 0;
    virtual uint64_t tx_late_count() const = 0;
    virtual uint64_t rx_overflow_count() const = 0;
};

class NetStatusProvider : public IMonitorStatus
{
public:
    double current_freq_hz() const override { return current_freq_hz_.load(); }
    bool is_bursting() const override { return is_bursting_.load(); }
    bool last_retune_locked() const override { return last_retune_locked_.load(); }
    uint64_t burst_count() const override { return burst_count_.load(); }
    uint64_t tx_underflow_count() const override { return tx_underflow_count_.load(); }
    uint64_t tx_late_count() const override { return tx_late_count_.load(); }
    uint64_t rx_overflow_count() const override { return rx_overflow_count_.load(); }

    void set_freq_hz(double freq) { current_freq_hz_.store(freq); }
    void set_bursting(bool b) { is_bursting_.store(b); }
    void set_locked(bool l) { last_retune_locked_.store(l); }
    void add_burst_count(uint64_t c = 1) { burst_count_.fetch_add(c); }
    void set_tx_underflow(uint64_t c) { tx_underflow_count_.store(c); }
    void set_tx_late(uint64_t c) { tx_late_count_.store(c); }
    void set_rx_overflow(uint64_t c) { rx_overflow_count_.store(c); }

private:
    std::atomic<double> current_freq_hz_{2.4e9};
    std::atomic<bool> is_bursting_{false};
    std::atomic<bool> last_retune_locked_{true};
    std::atomic<uint64_t> burst_count_{0};
    std::atomic<uint64_t> tx_underflow_count_{0};
    std::atomic<uint64_t> tx_late_count_{0};
    std::atomic<uint64_t> rx_overflow_count_{0};
};
