// gui.cpp

#include "gui.h"

#include <cstdio>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

namespace
{
void glfw_error_callback(int error, const char* description)
{
    std::fprintf(stderr, "[GLFW] error %d: %s\n", error, description);
}
} // namespace

Gui::Gui(IMonitorStatus& status, IqRingBuffer& tx_ring, IqRingBuffer& rx_ring,
         double sample_rate_hz, std::string window_title)
    : status_(status),
      tx_ring_(tx_ring),
      rx_ring_(rx_ring),
      sample_rate_hz_(sample_rate_hz),
      window_title_(std::move(window_title)),
      tx_fft_(kFftSize),
      rx_fft_(kFftSize)
{
    tx_wave_scratch_.resize(kWaveformDisplaySamples);
    rx_wave_scratch_.resize(kWaveformDisplaySamples);
    tx_i_buf_.resize(kWaveformDisplaySamples, 0.0f);
    tx_q_buf_.resize(kWaveformDisplaySamples, 0.0f);
    rx_i_buf_.resize(kWaveformDisplaySamples, 0.0f);
    rx_q_buf_.resize(kWaveformDisplaySamples, 0.0f);
    tx_fft_scratch_.resize(kFftSize);
    rx_fft_scratch_.resize(kFftSize);

    x_axis_wave_.resize(kWaveformDisplaySamples);
    for (size_t i = 0; i < kWaveformDisplaySamples; ++i) {
        x_axis_wave_[i] = static_cast<float>(i);
    }
}

Gui::~Gui() { shutdown_window(); }

bool Gui::init_window()
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "[Gui] glfwInit failed\n");
        return false;
    }

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    window_ = glfwCreateWindow(1280, 800, window_title_.c_str(), nullptr, nullptr);
    if (!window_) {
        std::fprintf(stderr, "[Gui] glfwCreateWindow failed\n");
        return false;
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    return true;
}

void Gui::shutdown_window()
{
    if (!window_) return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window_);
    glfwTerminate();
    window_ = nullptr;
}

void Gui::draw_status_bar()
{
    double freq = status_.current_freq_hz();
    bool bursting = status_.is_bursting();
    bool locked = status_.last_retune_locked();

    ImGui::Text("Band: %.3f GHz", freq / 1e9);
    ImGui::SameLine();
    ImGui::TextColored(bursting ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                                 : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                        bursting ? "[ TX BURST ]" : "[ silence/retune ]");
    ImGui::SameLine();
    if (!locked) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "LO UNLOCKED!");
        ImGui::SameLine();
    }
    ImGui::Text("| Rate: %.1f MS/s", sample_rate_hz_ / 1e6);
    ImGui::SameLine();
    ImGui::Text("| bursts: %llu",
                static_cast<unsigned long long>(status_.burst_count()));
    ImGui::SameLine();
    ImGui::Text("| TX underflow: %llu",
                static_cast<unsigned long long>(status_.tx_underflow_count()));
    ImGui::SameLine();
    ImGui::Text("| TX late: %llu",
                static_cast<unsigned long long>(status_.tx_late_count()));
    ImGui::SameLine();
    ImGui::Text("| RX overflow: %llu",
                static_cast<unsigned long long>(status_.rx_overflow_count()));
}

void Gui::draw_waveform_plot(const char* title, IqRingBuffer& ring,
                              std::vector<std::complex<float>>& scratch,
                              std::vector<float>& i_buf, std::vector<float>& q_buf)
{
    size_t n = ring.read_latest(scratch.data(), scratch.size());
    if (n < scratch.size()) {
        std::fill(scratch.begin() + n, scratch.end(), std::complex<float>(0.0f, 0.0f));
    }

    size_t count = scratch.size();
    i_buf.resize(count);
    q_buf.resize(count);
    for (size_t k = 0; k < count; ++k) {
        i_buf[k] = scratch[k].real();
        q_buf[k] = scratch[k].imag();
    }

    if (ImPlot::BeginPlot(title, ImVec2(-1, -1))) {
        ImPlot::SetupAxes("sample", "amplitude");
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, count, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -2.0, 2.0, ImGuiCond_Always);

        ImPlot::PlotLine("I", x_axis_wave_.data(), i_buf.data(),
                          static_cast<int>(count));
        ImPlot::PlotLine("Q", x_axis_wave_.data(), q_buf.data(),
                          static_cast<int>(count));
        ImPlot::EndPlot();
    }
}

void Gui::draw_fft_plot(const char* title, IqRingBuffer& ring, FftProcessor& fft,
                         std::vector<std::complex<float>>& scratch,
                         std::vector<float>& out_db, std::vector<float>& out_freq,
                         double carrier_freq_mhz, double& last_carrier_freq_mhz)
{
    size_t n = ring.read_latest(scratch.data(), scratch.size());
    if (n < scratch.size()) {
        std::fill(scratch.begin() + n, scratch.end(), std::complex<float>(0.0f, 0.0f));
    }

    fft.compute(scratch.data(), out_db, sample_rate_hz_, out_freq);

    // Convert baseband offset to absolute RF frequency in MHz
    for (size_t i = 0; i < out_freq.size(); ++i) {
        out_freq[i] += static_cast<float>(carrier_freq_mhz);
    }

    bool band_changed = (carrier_freq_mhz != last_carrier_freq_mhz);
    if (band_changed) {
        last_carrier_freq_mhz = carrier_freq_mhz;
    }

    if (ImPlot::BeginPlot(title, ImVec2(-1, -1))) {
        ImPlot::SetupAxes("RF frequency (MHz)", "magnitude (dB)",
                          ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisFormat(ImAxis_X1, "%.3f");

        // Center +/- 50 kHz span around active RF carrier frequency
        double span_mhz = 0.05;
        ImPlotCond cond = band_changed ? ImPlotCond_Always : ImPlotCond_Once;
        ImPlot::SetupAxisLimits(ImAxis_X1, carrier_freq_mhz - span_mhz,
                                carrier_freq_mhz + span_mhz, cond);

        ImPlot::PlotLine(title, out_freq.data(), out_db.data(),
                          static_cast<int>(out_db.size()));
        ImPlot::EndPlot();
    }
}

void Gui::draw_frame()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGui::Begin("USRP B210 Burst Monitor", nullptr,
                  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    draw_status_bar();
    ImGui::Separator();

    double carrier_mhz = status_.current_freq_hz() / 1e6;

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (ImPlot::BeginSubplots("##2x2", 2, 2, avail)) {
        draw_waveform_plot("TX IQ Waveform", tx_ring_, tx_wave_scratch_, tx_i_buf_, tx_q_buf_);
        draw_waveform_plot("RX IQ Waveform", rx_ring_, rx_wave_scratch_, rx_i_buf_, rx_q_buf_);
        draw_fft_plot("TX FFT", tx_ring_, tx_fft_, tx_fft_scratch_, tx_fft_db_,
                      tx_fft_freq_, carrier_mhz, last_tx_carrier_freq_mhz_);
        draw_fft_plot("RX FFT", rx_ring_, rx_fft_, rx_fft_scratch_, rx_fft_db_,
                      rx_fft_freq_, carrier_mhz, last_rx_carrier_freq_mhz_);
        ImPlot::EndSubplots();
    }

    ImGui::End();
}

void Gui::run()
{
    if (!init_window()) {
        return;
    }

    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        draw_frame();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window_, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window_);
    }
}
