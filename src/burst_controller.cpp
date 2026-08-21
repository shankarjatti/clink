#include "burst_controller.h"
#include "fft_processor.h"

#include <algorithm>
#include <iostream>

#include <uhd/types/metadata.hpp>

BurstController::BurstController(UsrpManager& usrp, std::vector<BandGainConfig> bands,
                                  IqRingBuffer& tx_ring, IqRingBuffer& rx_ring)
    : usrp_(usrp),
      bands_(std::move(bands)),
      tx_ring_(tx_ring),
      rx_ring_(rx_ring),
      tone_gen_(kToneFreqHz, usrp.sample_rate())
{
    // Clamp samples-per-buffer to the streamer's reported max, but keep it
    // at a sane ~1ms-scale chunk so the GUI ring buffer gets updated
    // frequently and any send() backpressure stays fine-grained.
    size_t max_spb = usrp_.tx_streamer()->get_max_num_samps();
    size_t target_spb = static_cast<size_t>(usrp_.sample_rate() * 0.001); // ~1ms
    spb_ = std::max<size_t>(1, std::min(max_spb, std::max<size_t>(target_spb, 1)));

    // kToneFreqHz / sample_rate should divide the chunk length evenly (e.g.
    // 10kHz @ 2MS/s => 200 samples/cycle) so replaying this same chunk
    // buffer every send() call stays phase-continuous with zero drift.
    tone_chunk_ = tone_gen_.generate_buffer(spb_);

    std::cout << "[BurstController] spb=" << spb_ << " samples ("
              << (spb_ / (usrp_.sample_rate() / 1e6)) << " us/chunk)\n";
}

void BurstController::set_interference_mitigation_hook(std::function<void()> hook)
{
    interference_hook_ = std::move(hook);
}

void BurstController::set_net_sender(std::shared_ptr<IqTcpSender> sender)
{
    net_sender_ = std::move(sender);
}

void BurstController::start()
{
    stop_flag_ = false;
    rx_thread_ = std::thread(&BurstController::rx_loop, this);
    control_thread_ = std::thread(&BurstController::control_loop, this);
    if (net_sender_) {
        net_sender_->start();
        net_thread_ = std::thread(&BurstController::net_stream_loop, this);
    }
}

void BurstController::stop()
{
    stop_flag_ = true;
    if (control_thread_.joinable()) control_thread_.join();
    if (rx_thread_.joinable()) rx_thread_.join();
    if (net_thread_.joinable()) net_thread_.join();
    if (net_sender_) net_sender_->stop();
}

void BurstController::net_stream_loop()
{
    std::vector<std::complex<float>> tx_buf(spb_);
    std::vector<std::complex<float>> rx_buf(spb_);

    constexpr size_t kFftLen = 4096;
    FftProcessor tx_fft(kFftLen);
    FftProcessor rx_fft(kFftLen);
    std::vector<std::complex<float>> fft_scratch(kFftLen);
    std::vector<float> tx_fft_db(kFftLen);
    std::vector<float> rx_fft_db(kFftLen);
    std::vector<float> dummy_freq(kFftLen);

    uint64_t iter = 0;

    while (!stop_flag_.load()) {
        if (!net_sender_ || !net_sender_->is_connected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        size_t n_tx = tx_ring_.read_latest(tx_buf.data(), tx_buf.size());
        size_t n_rx = rx_ring_.read_latest(rx_buf.data(), rx_buf.size());

        if (n_tx < tx_buf.size()) {
            std::fill(tx_buf.begin() + n_tx, tx_buf.end(), std::complex<float>(0.0f, 0.0f));
        }
        if (n_rx < rx_buf.size()) {
            std::fill(rx_buf.begin() + n_rx, rx_buf.end(), std::complex<float>(0.0f, 0.0f));
        }

        const float* tx_fft_ptr = nullptr;
        const float* rx_fft_ptr = nullptr;
        size_t send_fft_len = 0;

        // Compute and attach FFT spectra every ~10ms (~100 Hz)
        if (++iter % 10 == 0) {
            size_t nft = tx_ring_.read_latest(fft_scratch.data(), kFftLen);
            if (nft < kFftLen) std::fill(fft_scratch.begin() + nft, fft_scratch.end(), std::complex<float>(0.0f, 0.0f));
            tx_fft.compute(fft_scratch.data(), tx_fft_db, usrp_.sample_rate(), dummy_freq);

            size_t nfr = rx_ring_.read_latest(fft_scratch.data(), kFftLen);
            if (nfr < kFftLen) std::fill(fft_scratch.begin() + nfr, fft_scratch.end(), std::complex<float>(0.0f, 0.0f));
            rx_fft.compute(fft_scratch.data(), rx_fft_db, usrp_.sample_rate(), dummy_freq);

            tx_fft_ptr = tx_fft_db.data();
            rx_fft_ptr = rx_fft_db.data();
            send_fft_len = kFftLen;
        }

        double freq = current_freq_hz_.load();
        float elevation_deg = 30.0f;
        float azimuth_deg = 40.0f;

        if (std::abs(freq - 2.4e9) < 200e6) {
            elevation_deg = 30.0f;
            azimuth_deg = 40.0f;
        } else if (std::abs(freq - 5.1e9) < 200e6) {
            elevation_deg = 50.0f;
            azimuth_deg = 60.0f;
        } else if (std::abs(freq - 5.8e9) < 200e6) {
            elevation_deg = 60.0f;
            azimuth_deg = 70.0f;
        }

        uint64_t ts_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        net_sender_->send_frame(ts_ns, freq, is_bursting_.load(),
                                tx_buf.data(), rx_buf.data(), spb_,
                                tx_fft_ptr, rx_fft_ptr, send_fft_len, 1.0f,
                                elevation_deg, azimuth_deg);

        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>((spb_ * 1e6) / usrp_.sample_rate())));
    }
}

void BurstController::drain_tx_async_messages()
{
    uhd::async_metadata_t async_md;
    // Drain whatever is queued without blocking the gap for long; a fresh
    // burst only ever queues on the order of ~1000 packets of async status,
    // so this loop empties quickly.
    while (usrp_.tx_streamer()->recv_async_msg(async_md, 0.0)) {
        switch (async_md.event_code) {
            case uhd::async_metadata_t::EVENT_CODE_UNDERFLOW:
            case uhd::async_metadata_t::EVENT_CODE_UNDERFLOW_IN_PACKET:
                tx_underflow_count_.fetch_add(1);
                break;
            case uhd::async_metadata_t::EVENT_CODE_TIME_ERROR:
                tx_late_count_.fetch_add(1);
                break;
            default:
                break; // EVENT_CODE_BURST_ACK etc - not an error
        }
    }
}

void BurstController::run_burst(const BandGainConfig& /*band*/,
                                 const uhd::time_spec_t& burst_start)
{
    const double sample_rate = usrp_.sample_rate();
    const size_t total_samples =
        static_cast<size_t>(sample_rate * kBurstDurationS);

    uhd::tx_metadata_t md;
    md.start_of_burst = true;
    md.end_of_burst = false;
    md.has_time_spec = true;
    md.time_spec = burst_start;

    size_t samples_sent = 0;
    const std::complex<float>* buf_ptr = tone_chunk_.data();
    constexpr double kLeadTimeS = 0.01; // 10 ms lead buffer to prevent UHD underflow

    while (samples_sent < total_samples && !stop_flag_.load()) {
        uhd::time_spec_t chunk_tx_time =
            burst_start + uhd::time_spec_t(static_cast<double>(samples_sent) / sample_rate);

        // Pace transmissions so we maintain a small lead-in to prevent underflow,
        // while continuously streaming samples into tx_ring_ in real time.
        while (!stop_flag_.load() &&
               usrp_.now() < (chunk_tx_time - uhd::time_spec_t(kLeadTimeS))) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (stop_flag_.load()) break;

        size_t remaining = total_samples - samples_sent;
        size_t this_chunk = std::min(remaining, spb_);
        md.end_of_burst = (this_chunk >= remaining);

        size_t sent = usrp_.tx_streamer()->send(buf_ptr, this_chunk, md);

        if (sent > 0) {
            tx_ring_.write(buf_ptr, sent);
            samples_sent += sent;
        }

        md.start_of_burst = false;
        md.has_time_spec = false;
    }

    // Wait until the burst on hardware actually finishes transmitting
    uhd::time_spec_t burst_end = burst_start + uhd::time_spec_t(kBurstDurationS);
    while (!stop_flag_.load() && usrp_.now() < burst_end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void BurstController::rx_loop()
{
    std::vector<std::complex<float>> rx_buf(spb_);
    uhd::rx_metadata_t md;

    while (!stop_flag_.load()) {
        size_t n = usrp_.rx_streamer()->recv(rx_buf.data(), rx_buf.size(), md, 0.05);

        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            rx_overflow_count_.fetch_add(1);
            if (n > 0) {
                rx_ring_.write(rx_buf.data(), n);
            }
            continue;
        }
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_NONE && n > 0) {
            rx_ring_.write(rx_buf.data(), n);
        }
    }
}

void BurstController::control_loop()
{
    if (bands_.empty()) {
        std::cerr << "[BurstController] No bands configured, nothing to do.\n";
        return;
    }

    size_t idx = 0;

    // Initial tune before the very first burst.
    {
        const auto& band = bands_[idx];
        bool locked = usrp_.retune(band.freq_hz, band.tx_gain_db, band.rx_gain_db);
        last_retune_locked_.store(locked);
        current_freq_hz_.store(band.freq_hz);
    }

    // Pre-allocated zeros chunk for streaming flat-line during silence
    std::vector<std::complex<float>> zero_chunk(spb_, std::complex<float>(0.0f, 0.0f));

    const size_t total_samples =
        static_cast<size_t>(usrp_.sample_rate() * kBurstDurationS);

    while (!stop_flag_.load()) {
        const auto& band = bands_[idx];
        current_freq_hz_.store(band.freq_hz);

        // Schedule burst_start 20ms into the future so UHD is safely armed
        uhd::time_spec_t burst_start = usrp_.now() + uhd::time_spec_t(0.02);

        // Arm RX2 to receive exactly total_samples at the identical burst_start timestamp
        uhd::stream_cmd_t rx_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
        rx_cmd.num_samps = total_samples;
        rx_cmd.stream_now = false;
        rx_cmd.time_spec = burst_start;
        usrp_.rx_streamer()->issue_stream_cmd(rx_cmd);

        is_bursting_.store(true);
        run_burst(band, burst_start);
        is_bursting_.store(false);
        burst_count_.fetch_add(1);

        // Silence period of 100ms (0.1s)
        uhd::time_spec_t silence_end = usrp_.now() + uhd::time_spec_t(kSilenceDurationS);

        // --- silence period: retune + housekeeping ---
        size_t next_idx = (idx + 1) % bands_.size();
        const auto& next_band = bands_[next_idx];

        bool locked = usrp_.retune(next_band.freq_hz, next_band.tx_gain_db,
                                    next_band.rx_gain_db);
        last_retune_locked_.store(locked);
        if (!locked) {
            std::cerr << "[BurstController] retune to " << next_band.freq_hz
                      << " Hz did not lock in time\n";
        }

        drain_tx_async_messages();

        if (interference_hook_) {
            interference_hook_();
        }

        // Continuously stream flat-line zeros into both tx_ring_ and rx_ring_
        // during the silence period, pacing against the sample rate until silence_end.
        uhd::time_spec_t silence_start = usrp_.now();
        size_t silence_samples_pushed = 0;

        while (!stop_flag_.load()) {
            uhd::time_spec_t current_time = usrp_.now();
            if (current_time >= silence_end) {
                break;
            }

            double elapsed_silence = (current_time - silence_start).get_real_secs();
            if (elapsed_silence > 0.0) {
                size_t target_samples =
                    static_cast<size_t>(elapsed_silence * usrp_.sample_rate());
                while (silence_samples_pushed < target_samples && !stop_flag_.load()) {
                    size_t count = std::min(spb_, target_samples - silence_samples_pushed);
                    tx_ring_.write(zero_chunk.data(), count);
                    rx_ring_.write(zero_chunk.data(), count);
                    silence_samples_pushed += count;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        idx = next_idx;
    }
}
