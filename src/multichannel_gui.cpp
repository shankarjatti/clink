// multichannel_gui.cpp

#include "multichannel_gui.h"

#include <cstdio>
#include <iostream>

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

MultichannelGui::MultichannelGui(IMonitorStatus& status, MultichannelDemux& demux,
                                 double sample_rate_hz, std::string window_title)
    : status_(status),
      demux_(demux),
      sample_rate_hz_(sample_rate_hz),
      window_title_(std::move(window_title))
{
    for (int i = 0; i < 4; ++i) {
        ch_scratch_[i].wave_scratch.resize(kWaveformDisplaySamples);
        ch_scratch_[i].i_buf.resize(kWaveformDisplaySamples, 0.0f);
        ch_scratch_[i].q_buf.resize(kWaveformDisplaySamples, 0.0f);
        ch_scratch_[i].fft_db.resize(kFftSize, -120.0f);
        ch_scratch_[i].fft_freq.resize(kFftSize, 0.0f);
    }

    x_axis_wave_.resize(kWaveformDisplaySamples);
    for (size_t i = 0; i < kWaveformDisplaySamples; ++i) {
        x_axis_wave_[i] = static_cast<float>(i);
    }
}

MultichannelGui::~MultichannelGui()
{
    shutdown_window();
}

bool MultichannelGui::init_window()
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "[MultichannelGui] glfwInit failed\n");
        return false;
    }

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    window_ = glfwCreateWindow(1440, 900, window_title_.c_str(), nullptr, nullptr);
    if (!window_) {
        std::fprintf(stderr, "[MultichannelGui] glfwCreateWindow failed\n");
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

void MultichannelGui::shutdown_window()
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

void MultichannelGui::draw_status_bar()
{
    double freq = status_.current_freq_hz();
    bool bursting = status_.is_bursting();
    double lat = status_.latency_ms();
    double jit = status_.jitter_ms();
    double fps = status_.frame_rate_fps();

    ImGui::Text("Active Band: %.3f GHz", freq / 1e9);
    ImGui::SameLine();
    ImGui::TextColored(bursting ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                        bursting ? "[ BURST ACTIVE ]" : "[ silence ]");
    ImGui::SameLine();
    ImGui::Text("| Multipliers: Ch1(x2.0) | Ch2(x3.0) | Ch3(x4.0) | Ch4(Comb)");
    ImGui::SameLine();

    // Latency with color indicator
    if (lat > 0.0) {
        ImVec4 lat_color = (lat < 5.0) ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) :
                           (lat < 20.0) ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(lat_color, "| Latency (S1->S2): %.2f ms", lat);
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "| Latency (S1->S2): <1.0 ms");
    }

    ImGui::SameLine();
    ImGui::Text("| Jitter: ±%.2f ms", jit);

    ImGui::SameLine();
    ImGui::Text("| Rate: %.0f fps (%.1f MS/s)", fps > 0.0 ? fps : 1000.0, sample_rate_hz_ / 1e6);

    ImGui::SameLine();
    ImGui::Text("| Drops: %llu", static_cast<unsigned long long>(status_.rx_overflow_count()));
}

void MultichannelGui::draw_channel_waveform(const char* title, IqRingBuffer& ring,
                                            std::vector<std::complex<float>>& scratch,
                                            std::vector<float>& i_buf, std::vector<float>& q_buf,
                                            double y_min, double y_max)
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
        ImPlot::SetupAxisLimits(ImAxis_Y1, y_min, y_max, ImGuiCond_Always);

        ImPlot::PlotLine("I", x_axis_wave_.data(), i_buf.data(), static_cast<int>(count));
        ImPlot::PlotLine("Q", x_axis_wave_.data(), q_buf.data(), static_cast<int>(count));
        ImPlot::EndPlot();
    }
}

void MultichannelGui::draw_direct_fft_plot(const char* title, ChannelData& ch_data,
                                           std::vector<float>& local_fft_db,
                                           std::vector<float>& freq_axis,
                                           double default_carrier_mhz,
                                           double& last_carrier_mhz)
{
    double carrier_mhz = default_carrier_mhz;

    // Safely copy received FFT vector
    {
        std::lock_guard<std::mutex> lock(ch_data.fft_mutex);
        if (!ch_data.rx_fft.empty()) {
            local_fft_db = ch_data.rx_fft;
            if (ch_data.center_freq_hz > 0.0) {
                carrier_mhz = ch_data.center_freq_hz / 1e6;
            }
        }
    }

    size_t fft_len = local_fft_db.size();
    if (fft_len == 0) {
        fft_len = kFftSize;
        local_fft_db.resize(kFftSize, -120.0f);
    }

    freq_axis.resize(fft_len);
    float bin_mhz = static_cast<float>((sample_rate_hz_ / 1e6) / fft_len);
    for (size_t i = 0; i < fft_len; ++i) {
        freq_axis[i] = static_cast<float>(carrier_mhz) + (static_cast<float>(i) - (fft_len / 2.0f)) * bin_mhz;
    }

    bool band_changed = (carrier_mhz != last_carrier_mhz);
    if (band_changed) {
        last_carrier_mhz = carrier_mhz;
    }

    if (ImPlot::BeginPlot(title, ImVec2(-1, -1))) {
        ImPlot::SetupAxes("RF frequency (MHz)", "magnitude (dB)", ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisFormat(ImAxis_X1, "%.3f");

        double span_mhz = 0.05;
        ImPlotCond cond = band_changed ? ImPlotCond_Always : ImPlotCond_Once;
        ImPlot::SetupAxisLimits(ImAxis_X1, carrier_mhz - span_mhz, carrier_mhz + span_mhz, cond);

        ImPlot::PlotLine(title, freq_axis.data(), local_fft_db.data(), static_cast<int>(fft_len));
        ImPlot::EndPlot();
    }
}

void MultichannelGui::draw_polar_map_plot(const char* title, float elevation_deg, float azimuth_deg)
{
    if (ImPlot::BeginPlot(title, ImVec2(-1, -1), ImPlotFlags_Equal)) {
        ImPlot::SetupAxes("X (East)", "Y (North)", ImPlotAxisFlags_NoGridLines, ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxisLimits(ImAxis_X1, -95.0, 95.0, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -95.0, 95.0, ImGuiCond_Always);

        // Precompute concentric range rings (30 deg, 60 deg, 90 deg max)
        constexpr int kRingPts = 64;
        static float ring_x[3][kRingPts + 1];
        static float ring_y[3][kRingPts + 1];
        static bool s_rings_init = false;
        if (!s_rings_init) {
            float radii[3] = {30.0f, 60.0f, 90.0f};
            for (int r = 0; r < 3; ++r) {
                for (int p = 0; p <= kRingPts; ++p) {
                    float theta = static_cast<float>(p * 2.0 * M_PI / kRingPts);
                    ring_x[r][p] = radii[r] * std::cos(theta);
                    ring_y[r][p] = radii[r] * std::sin(theta);
                }
            }
            s_rings_init = true;
        }

        // Plot Range Rings
        ImPlot::SetNextLineStyle(ImVec4(0.3f, 0.4f, 0.5f, 0.35f), 1.0f);
        ImPlot::PlotLine("##R30", ring_x[0], ring_y[0], kRingPts + 1);
        ImPlot::SetNextLineStyle(ImVec4(0.3f, 0.4f, 0.5f, 0.35f), 1.0f);
        ImPlot::PlotLine("##R60", ring_x[1], ring_y[1], kRingPts + 1);
        ImPlot::SetNextLineStyle(ImVec4(0.4f, 0.6f, 0.8f, 0.6f), 1.5f);
        ImPlot::PlotLine("##R90", ring_x[2], ring_y[2], kRingPts + 1);

        // Crosshairs
        static float axis_ns_x[2] = {0.0f, 0.0f};
        static float axis_ns_y[2] = {-90.0f, 90.0f};
        static float axis_ew_x[2] = {-90.0f, 90.0f};
        static float axis_ew_y[2] = {0.0f, 0.0f};
        ImPlot::SetNextLineStyle(ImVec4(0.3f, 0.4f, 0.5f, 0.3f), 1.0f);
        ImPlot::PlotLine("##AxisNS", axis_ns_x, axis_ns_y, 2);
        ImPlot::SetNextLineStyle(ImVec4(0.3f, 0.4f, 0.5f, 0.3f), 1.0f);
        ImPlot::PlotLine("##AxisEW", axis_ew_x, axis_ew_y, 2);

        // Convert (Elevation, Azimuth) to Cartesian coordinates
        float rad = azimuth_deg * static_cast<float>(M_PI / 180.0);
        float target_r = elevation_deg;
        float target_x = target_r * std::sin(rad);
        float target_y = target_r * std::cos(rad);

        // Bearing vector line
        float line_x[2] = {0.0f, target_x};
        float line_y[2] = {0.0f, target_y};
        ImPlot::SetNextLineStyle(ImVec4(0.2f, 1.0f, 0.4f, 0.8f), 2.0f);
        ImPlot::PlotLine("Bearing", line_x, line_y, 2);

        // Target marker
        float pt_x[1] = {target_x};
        float pt_y[1] = {target_y};
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 8.0f, ImVec4(1.0f, 0.3f, 0.2f, 1.0f), 2.0f, ImVec4(1.0f, 0.9f, 0.2f, 1.0f));
        char pt_label[64];
        std::snprintf(pt_label, sizeof(pt_label), "El: %.0f°, Az: %.0f°", elevation_deg, azimuth_deg);
        ImPlot::PlotScatter(pt_label, pt_x, pt_y, 1);

        ImPlot::EndPlot();
    }
}

void MultichannelGui::draw_frame()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGui::Begin("USRP B210 Multichannel Monitor", nullptr,
                  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    draw_status_bar();
    ImGui::Separator();

    double active_carrier_mhz = status_.current_freq_hz() / 1e6;

    ImVec2 avail = ImGui::GetContentRegionAvail();
    // 4 Rows x 3 Columns = 12 Plots Total
    if (ImPlot::BeginSubplots("##12Plots", 4, 3, avail)) {
        // Row 1: Channel 1 (2.4 GHz - x2.0)
        draw_channel_waveform("Ch 1 IQ (2.4 GHz - x2.0)", demux_.ch1().rx_ring,
                              ch_scratch_[0].wave_scratch, ch_scratch_[0].i_buf, ch_scratch_[0].q_buf,
                              -4.0, 4.0);
        draw_direct_fft_plot("Ch 1 FFT (Direct 2.4 GHz)", demux_.ch1(),
                             ch_scratch_[0].fft_db, ch_scratch_[0].fft_freq,
                             2400.0, ch_scratch_[0].last_carrier_mhz);
        draw_polar_map_plot("Ch 1 Polar Map (2.4 GHz)", demux_.ch1().elevation_deg, demux_.ch1().azimuth_deg);

        // Row 2: Channel 2 (5.1 GHz - x3.0)
        draw_channel_waveform("Ch 2 IQ (5.1 GHz - x3.0)", demux_.ch2().rx_ring,
                              ch_scratch_[1].wave_scratch, ch_scratch_[1].i_buf, ch_scratch_[1].q_buf,
                              -6.0, 6.0);
        draw_direct_fft_plot("Ch 2 FFT (Direct 5.1 GHz)", demux_.ch2(),
                             ch_scratch_[1].fft_db, ch_scratch_[1].fft_freq,
                             5100.0, ch_scratch_[1].last_carrier_mhz);
        draw_polar_map_plot("Ch 2 Polar Map (5.1 GHz)", demux_.ch2().elevation_deg, demux_.ch2().azimuth_deg);

        // Row 3: Channel 3 (5.8 GHz - x4.0)
        draw_channel_waveform("Ch 3 IQ (5.8 GHz - x4.0)", demux_.ch3().rx_ring,
                              ch_scratch_[2].wave_scratch, ch_scratch_[2].i_buf, ch_scratch_[2].q_buf,
                              -8.0, 8.0);
        draw_direct_fft_plot("Ch 3 FFT (Direct 5.8 GHz)", demux_.ch3(),
                             ch_scratch_[2].fft_db, ch_scratch_[2].fft_freq,
                             5800.0, ch_scratch_[2].last_carrier_mhz);
        draw_polar_map_plot("Ch 3 Polar Map (5.8 GHz)", demux_.ch3().elevation_deg, demux_.ch3().azimuth_deg);

        // Row 4: Channel 4 (Combined Scaled)
        draw_channel_waveform("Ch 4 IQ (Combined Scaled)", demux_.ch4().rx_ring,
                              ch_scratch_[3].wave_scratch, ch_scratch_[3].i_buf, ch_scratch_[3].q_buf,
                              -8.0, 8.0);
        draw_direct_fft_plot("Ch 4 FFT (Active Band)", demux_.ch4(),
                             ch_scratch_[3].fft_db, ch_scratch_[3].fft_freq,
                             active_carrier_mhz, ch_scratch_[3].last_carrier_mhz);
        draw_polar_map_plot("Ch 4 Polar Map (Active Band)", demux_.ch4().elevation_deg, demux_.ch4().azimuth_deg);

        ImPlot::EndSubplots();
    }

    ImGui::End();
}

void MultichannelGui::run()
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
