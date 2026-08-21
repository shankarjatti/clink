// multichannel_demux.cpp

#include "multichannel_demux.h"

#include <algorithm>
#include <cmath>
#include <iostream>

MultichannelDemux::MultichannelDemux()
{
    ch1_.label = "Channel 1 (2.4 GHz - x2.0)";
    ch1_.center_freq_hz = 2.4e9;
    ch1_.multiplier = 2.0f;
    ch1_.elevation_deg = 30.0f;
    ch1_.azimuth_deg = 40.0f;

    ch2_.label = "Channel 2 (5.1 GHz - x3.0)";
    ch2_.center_freq_hz = 5.1e9;
    ch2_.multiplier = 3.0f;
    ch2_.elevation_deg = 50.0f;
    ch2_.azimuth_deg = 60.0f;

    ch3_.label = "Channel 3 (5.8 GHz - x4.0)";
    ch3_.center_freq_hz = 5.8e9;
    ch3_.multiplier = 4.0f;
    ch3_.elevation_deg = 60.0f;
    ch3_.azimuth_deg = 70.0f;

    ch4_.label = "Channel 4 (Combined Scaled)";
    ch4_.center_freq_hz = 2.4e9;
    ch4_.multiplier = 1.0f;
    ch4_.elevation_deg = 30.0f;
    ch4_.azimuth_deg = 40.0f;

    zero_chunk_.resize(4096, std::complex<float>(0.0f, 0.0f));
}

int MultichannelDemux::detect_band_index(double freq_hz) const
{
    if (std::abs(freq_hz - 2.4e9) < 200e6) return 0; // 2.4 GHz -> Ch 1
    if (std::abs(freq_hz - 5.1e9) < 200e6) return 1; // 5.1 GHz -> Ch 2
    if (std::abs(freq_hz - 5.8e9) < 200e6) return 2; // 5.8 GHz -> Ch 3
    return 0;
}

float MultichannelDemux::get_band_multiplier(int band_idx) const
{
    switch (band_idx) {
        case 0: return 2.0f; // 2.4 GHz -> x2.0
        case 1: return 3.0f; // 5.1 GHz -> x3.0
        case 2: return 4.0f; // 5.8 GHz -> x4.0
        default: return 1.0f;
    }
}

float MultichannelDemux::process_incoming_frame(const IqFrameHeader& hdr,
                                               const int16_t* tx_sc16,
                                               const int16_t* rx_sc16,
                                               const float* tx_fft_db,
                                               const float* rx_fft_db,
                                               std::vector<std::complex<float>>& out_tx_scaled,
                                               std::vector<std::complex<float>>& out_rx_scaled)
{
    size_t count = hdr.sample_count;
    out_tx_scaled.resize(count);
    out_rx_scaled.resize(count);

    int band_idx = detect_band_index(hdr.center_freq_hz);
    float multiplier = get_band_multiplier(band_idx);

    // 1. Decode sc16 to float and apply band multiplier
    if (hdr.iq_multiplier > 1.0f) {
        // Frame received from System 2 with pre-scaled multiplier metadata
        multiplier = hdr.iq_multiplier;
        net_util::sc16_to_float_scaled(tx_sc16, out_tx_scaled.data(), count, multiplier);
        net_util::sc16_to_float_scaled(rx_sc16, out_rx_scaled.data(), count, multiplier);
    } else {
        // Frame received from System 1: convert unscaled sc16 to float and multiply by M
        net_util::sc16_to_float(tx_sc16, out_tx_scaled.data(), count);
        net_util::sc16_to_float(rx_sc16, out_rx_scaled.data(), count);
        for (size_t i = 0; i < count; ++i) {
            out_tx_scaled[i] *= multiplier;
            out_rx_scaled[i] *= multiplier;
        }
    }

    if (zero_chunk_.size() < count) {
        zero_chunk_.resize(count, std::complex<float>(0.0f, 0.0f));
    }

    // 2. Route scaled samples and angles to the active channel's ring buffers
    channel(band_idx).tx_ring.write(out_tx_scaled.data(), count);
    channel(band_idx).rx_ring.write(out_rx_scaled.data(), count);
    channel(band_idx).center_freq_hz = hdr.center_freq_hz;
    channel(band_idx).multiplier = multiplier;
    channel(band_idx).elevation_deg = hdr.elevation_deg;
    channel(band_idx).azimuth_deg = hdr.azimuth_deg;

    // 3. Feed flat-line zeros to the other 2 inactive band channels
    for (int b = 0; b < 3; ++b) {
        if (b != band_idx) {
            channel(b).tx_ring.write(zero_chunk_.data(), count);
            channel(b).rx_ring.write(zero_chunk_.data(), count);
        }
    }

    // 4. Route scaled samples and angles continuously into Channel 4 (Combined)
    ch4_.tx_ring.write(out_tx_scaled.data(), count);
    ch4_.rx_ring.write(out_rx_scaled.data(), count);
    ch4_.center_freq_hz = hdr.center_freq_hz;
    ch4_.multiplier = multiplier;
    ch4_.elevation_deg = hdr.elevation_deg;
    ch4_.azimuth_deg = hdr.azimuth_deg;

    // 5. Direct FFT routing (no multiplier, zero recomputation)
    if (hdr.fft_size > 0 && tx_fft_db && rx_fft_db) {
        // Store into active band FFT buffer
        {
            std::lock_guard<std::mutex> lock(channel(band_idx).fft_mutex);
            channel(band_idx).tx_fft.assign(tx_fft_db, tx_fft_db + hdr.fft_size);
            channel(band_idx).rx_fft.assign(rx_fft_db, rx_fft_db + hdr.fft_size);
        }
        // Store into combined FFT buffer
        {
            std::lock_guard<std::mutex> lock(ch4_.fft_mutex);
            ch4_.tx_fft.assign(tx_fft_db, tx_fft_db + hdr.fft_size);
            ch4_.rx_fft.assign(rx_fft_db, rx_fft_db + hdr.fft_size);
        }
    }

    last_active_band_ = band_idx;
    return multiplier;
}
