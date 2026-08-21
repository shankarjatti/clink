// gui.h
//
// Owns the GLFW window, OpenGL context, ImGui + ImPlot contexts, and the
// main render loop. Runs on the main thread (required for GLFW/GL). Reads
// TX/RX IQ ring buffers written by BurstController's threads and status
// atomics exposed by BurstController; never touches the USRP device
// directly.
//
// Layout: 2x2 grid
//   [ TX IQ waveform ]  [ RX IQ waveform ]
//   [ TX FFT          ]  [ RX FFT          ]

#pragma once

#include <string>
#include <vector>

#include "fft_processor.h"
#include "ring_buffer.h"
#include "status_provider.h"

struct GLFWwindow;

class Gui
{
public:
    Gui(IMonitorStatus& status, IqRingBuffer& tx_ring, IqRingBuffer& rx_ring,
        double sample_rate_hz, std::string window_title = "USRP B210 Burst Monitor");
    ~Gui();

    // Blocks, running the render loop until the window is closed. Returns
    // when the user closes the window (caller should then stop the
    // controller).
    void run();

private:
    bool init_window();
    void shutdown_window();
    void draw_frame();
    void draw_status_bar();
    void draw_waveform_plot(const char* title, IqRingBuffer& ring,
                             std::vector<std::complex<float>>& scratch,
                             std::vector<float>& i_buf, std::vector<float>& q_buf);
    void draw_fft_plot(const char* title, IqRingBuffer& ring, FftProcessor& fft,
                        std::vector<std::complex<float>>& scratch,
                        std::vector<float>& out_db, std::vector<float>& out_freq,
                        double carrier_freq_mhz, double& last_carrier_freq_mhz);

    IMonitorStatus& status_;
    IqRingBuffer& tx_ring_;
    IqRingBuffer& rx_ring_;
    double sample_rate_hz_;
    std::string window_title_;

    GLFWwindow* window_ = nullptr;

    static constexpr size_t kWaveformDisplaySamples = 2000; // ~1ms @ 2MS/s
    static constexpr size_t kFftSize = 4096;

    FftProcessor tx_fft_;
    FftProcessor rx_fft_;

    double last_tx_carrier_freq_mhz_ = 0.0;
    double last_rx_carrier_freq_mhz_ = 0.0;

    // Scratch buffers reused every frame to avoid per-frame allocation.
    std::vector<std::complex<float>> tx_wave_scratch_;
    std::vector<std::complex<float>> rx_wave_scratch_;
    std::vector<float> tx_i_buf_;
    std::vector<float> tx_q_buf_;
    std::vector<float> rx_i_buf_;
    std::vector<float> rx_q_buf_;
    std::vector<std::complex<float>> tx_fft_scratch_;
    std::vector<std::complex<float>> rx_fft_scratch_;
    std::vector<float> tx_fft_db_;
    std::vector<float> rx_fft_db_;
    std::vector<float> tx_fft_freq_;
    std::vector<float> rx_fft_freq_;
    std::vector<float> x_axis_wave_; // shared sample-index x-axis for waveform plots
};
