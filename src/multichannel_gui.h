// multichannel_gui.h
//
// 8-Plot (4x2 Subplots) Real-Time Monitor for System 2 and System 3:
//   - Row 1: Channel 1 Waveform (2.4 GHz x2.0)  | Channel 1 FFT (2.4 GHz)
//   - Row 2: Channel 2 Waveform (5.1 GHz x3.0)  | Channel 2 FFT (5.1 GHz)
//   - Row 3: Channel 3 Waveform (5.8 GHz x4.0)  | Channel 3 FFT (5.8 GHz)
//   - Row 4: Channel 4 Waveform (Combined Scaled)| Channel 4 FFT (Active Band)

#pragma once

#include <complex>
#include <string>
#include <vector>

#include "multichannel_demux.h"
#include "status_provider.h"

struct GLFWwindow;

class MultichannelGui
{
public:
    MultichannelGui(IMonitorStatus& status, MultichannelDemux& demux,
                    double sample_rate_hz, std::string window_title);
    ~MultichannelGui();

    void run();

private:
    bool init_window();
    void shutdown_window();
    void draw_frame();
    void draw_status_bar();

    void draw_channel_waveform(const char* title, IqRingBuffer& ring,
                               std::vector<std::complex<float>>& scratch,
                               std::vector<float>& i_buf, std::vector<float>& q_buf,
                               double y_min, double y_max);

    void draw_direct_fft_plot(const char* title, ChannelData& ch_data,
                              std::vector<float>& local_fft_db,
                              std::vector<float>& freq_axis,
                              double default_carrier_mhz,
                              double& last_carrier_mhz);

    IMonitorStatus& status_;
    MultichannelDemux& demux_;
    double sample_rate_hz_;
    std::string window_title_;

    GLFWwindow* window_ = nullptr;

    static constexpr size_t kWaveformDisplaySamples = 2000;
    static constexpr size_t kFftSize = 4096;

    // Per-channel scratch buffers
    struct ChannelScratch
    {
        std::vector<std::complex<float>> wave_scratch;
        std::vector<float> i_buf;
        std::vector<float> q_buf;
        std::vector<float> fft_db;
        std::vector<float> fft_freq;
        double last_carrier_mhz{0.0};
    };

    ChannelScratch ch_scratch_[4];
    std::vector<float> x_axis_wave_;
};
