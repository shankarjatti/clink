// multichannel_demux.h
//
// Demultiplexes incoming IQ stream and FFT vectors into 4 distinct channels:
//   - Channel 1 (2.4 GHz): Multiplier x2.0
//   - Channel 2 (5.1 GHz): Multiplier x3.0
//   - Channel 3 (5.8 GHz): Multiplier x4.0
//   - Channel 4 (Combined): Holds all incoming frames scaled by active multiplier

#pragma once

#include <cmath>
#include <complex>
#include <cstdint>
#include <mutex>
#include <vector>

#include "net_protocol.h"
#include "ring_buffer.h"

struct ChannelData
{
    IqRingBuffer tx_ring{1 << 16};
    IqRingBuffer rx_ring{1 << 16};
    std::vector<float> tx_fft;
    std::vector<float> rx_fft;
    std::mutex fft_mutex;
    double center_freq_hz{2.4e9};
    float multiplier{1.0f};
    float elevation_deg{30.0f};
    float azimuth_deg{40.0f};
    const char* label{"Channel"};
};

class MultichannelDemux
{
public:
    MultichannelDemux();

    // Ingests incoming TCP frame, decodes sc16 to float, applies band multiplier,
    // routes to dedicated band channel (Ch 1, 2, or 3) and combined channel (Ch 4),
    // and stores received original FFT spectra.
    // Returns the applied multiplier and updates out_rx_scaled.
    float process_incoming_frame(const IqFrameHeader& hdr,
                                 const int16_t* rx_sc16,
                                 const float* rx_fft_db,
                                 std::vector<std::complex<float>>& out_rx_scaled);

    ChannelData& ch1() { return ch1_; } // 2.4 GHz (x2.0)
    ChannelData& ch2() { return ch2_; } // 5.1 GHz (x3.0)
    ChannelData& ch3() { return ch3_; } // 5.8 GHz (x4.0)
    ChannelData& ch4() { return ch4_; } // Combined (dynamic scaled)

    ChannelData& channel(int idx)
    {
        switch (idx) {
            case 0: return ch1_;
            case 1: return ch2_;
            case 2: return ch3_;
            default: return ch4_;
        }
    }

private:
    int detect_band_index(double freq_hz) const;
    float get_band_multiplier(int band_idx) const;

    ChannelData ch1_; // 2.4 GHz
    ChannelData ch2_; // 5.1 GHz
    ChannelData ch3_; // 5.8 GHz
    ChannelData ch4_; // Combined

    std::vector<std::complex<float>> zero_chunk_;
    int last_active_band_ = 0;
};
