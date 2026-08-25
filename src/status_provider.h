// status_provider.h
//
// Interface for status bar metrics shared between the SDR hardware controller
// (System 1) and network relay/sink nodes (System 2 & 3).

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "net_protocol.h"

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
    virtual double latency_ms() const { return 0.0; }
    virtual double jitter_ms() const { return 0.0; }
    virtual double frame_rate_fps() const { return 0.0; }
    virtual bool get_domain_telemetry(ExtendedDomainTelemetry& out) const { (void)out; return false; }
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
    double latency_ms() const override { return latency_ms_.load(); }
    double jitter_ms() const override { return jitter_ms_.load(); }
    double frame_rate_fps() const override { return frame_rate_fps_.load(); }

    bool get_domain_telemetry(ExtendedDomainTelemetry& out) const override
    {
        std::lock_guard<std::mutex> lock(telem_mutex_);
        if (!has_domain_telemetry_) return false;
        out = domain_telemetry_;
        return true;
    }

    void set_freq_hz(double freq) { current_freq_hz_.store(freq); }
    void set_bursting(bool b) { is_bursting_.store(b); }
    void set_locked(bool l) { last_retune_locked_.store(l); }
    void add_burst_count(uint64_t c = 1) { burst_count_.fetch_add(c); }
    void set_tx_underflow(uint64_t c) { tx_underflow_count_.store(c); }
    void set_tx_late(uint64_t c) { tx_late_count_.store(c); }
    void set_rx_overflow(uint64_t c) { rx_overflow_count_.store(c); }
    void set_latency_ms(double l) { latency_ms_.store(l); }
    void set_jitter_ms(double j) { jitter_ms_.store(j); }
    void set_frame_rate_fps(double fps) { frame_rate_fps_.store(fps); }

    void set_domain_telemetry(const ExtendedDomainTelemetry& telem)
    {
        std::lock_guard<std::mutex> lock(telem_mutex_);
        domain_telemetry_ = telem;
        has_domain_telemetry_ = true;
    }

private:
    std::atomic<double> current_freq_hz_{2.4e9};
    std::atomic<bool> is_bursting_{false};
    std::atomic<bool> last_retune_locked_{true};
    std::atomic<uint64_t> burst_count_{0};
    std::atomic<uint64_t> tx_underflow_count_{0};
    std::atomic<uint64_t> tx_late_count_{0};
    std::atomic<uint64_t> rx_overflow_count_{0};
    std::atomic<double> latency_ms_{0.0};
    std::atomic<double> jitter_ms_{0.0};
    std::atomic<double> frame_rate_fps_{0.0};

    mutable std::mutex telem_mutex_;
    ExtendedDomainTelemetry domain_telemetry_{};
    bool has_domain_telemetry_{false};
};
