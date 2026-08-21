// operator_console_gui.h
//
// Modern Operator Console for System 3 with:
//   - Top Status & Telemetry Header
//   - Left Sidebar Navigation Tabs (2.4 GHz, 5.1 GHz, 5.8 GHz, All/Combined)
//   - Focused High-Resolution Waveform & FFT Plot Views
//   - Real-Time Channel Diagnostics (Peak Voltage, SNR/Power, Carrier Metrics)

#pragma once

#include <complex>
#include <string>
#include <vector>

#include "multichannel_demux.h"
#include "status_provider.h"

struct GLFWwindow;

enum class ConsoleTab
{
    k24GHz = 0,
    k51GHz = 1,
    k58GHz = 2,
    kAll   = 3
};

class OperatorConsoleGui
{
public:
    OperatorConsoleGui(IMonitorStatus& status, MultichannelDemux& demux,
                       double sample_rate_hz, std::string window_title);
    ~OperatorConsoleGui();

    void run();

private:
    bool init_window();
    void shutdown_window();
    void draw_frame();

    void draw_top_status_bar();
    void draw_left_sidebar();
    void draw_main_content();

    void draw_single_channel_view(int channel_idx, const char* title_desc,
                                 double y_min, double y_max, double default_carrier_mhz);

    void draw_all_channels_view();

    void render_waveform_plot(const char* plot_id, IqRingBuffer& ring,
                             std::vector<std::complex<float>>& scratch,
                             std::vector<float>& i_buf, std::vector<float>& q_buf,
                             double y_min, double y_max, float& out_peak_v);

    void render_fft_plot(const char* plot_id, ChannelData& ch_data,
                         std::vector<float>& local_fft_db,
                         std::vector<float>& freq_axis,
                         double default_carrier_mhz,
                         double& last_carrier_mhz,
                         float& out_peak_db, double& out_peak_mhz);

    IMonitorStatus& status_;
    MultichannelDemux& demux_;
    double sample_rate_hz_;
    std::string window_title_;

    GLFWwindow* window_ = nullptr;
    ConsoleTab active_tab_ = ConsoleTab::k24GHz;

    static constexpr size_t kWaveformDisplaySamples = 2000;
    static constexpr size_t kFftSize = 4096;

    struct ChannelScratch
    {
        std::vector<std::complex<float>> wave_scratch;
        std::vector<float> i_buf;
        std::vector<float> q_buf;
        std::vector<float> fft_db;
        std::vector<float> fft_freq;
        double last_carrier_mhz{0.0};
        float peak_v{0.0f};
        float peak_db{-120.0f};
        double peak_mhz{0.0};
    };

    ChannelScratch ch_scratch_[4];
    std::vector<float> x_axis_wave_;

    bool show_all_grid_ = false; // Toggle between composite stream and 4x2 matrix in "All" tab
};
