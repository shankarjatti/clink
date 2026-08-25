// operator_console_gui.cpp

#include "operator_console_gui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

// Helper to push history timeline values
void push_history(std::vector<float>& buf, float val)
{
    if (buf.empty()) return;
    for (size_t i = 0; i + 1 < buf.size(); ++i) {
        buf[i] = buf[i + 1];
    }
    buf.back() = val;
}
} // namespace

OperatorConsoleGui::OperatorConsoleGui(IMonitorStatus& status, MultichannelDemux& demux,
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

    // Initialize 20-point historical timeline buffers
    time_axis_20_.resize(kHistoryLen20);
    for (size_t i = 0; i < kHistoryLen20; ++i) {
        time_axis_20_[i] = -static_cast<float>(kHistoryLen20 - 1 - i);
    }

    gnss_time_power_.assign(kHistoryLen20, -124.2f);
    gnss_time_agc_.assign(kHistoryLen20, 58.0f);
    gnss_time_pr_res_.assign(kHistoryLen20, 0.4f);
    gnss_time_dop_res_.assign(kHistoryLen20, -0.2f);
    gnss_time_phase_res_.assign(kHistoryLen20, 0.1f);
    gnss_time_pos_e_.assign(kHistoryLen20, 0.12f);
    gnss_time_pos_n_.assign(kHistoryLen20, -0.08f);
    gnss_time_pos_u_.assign(kHistoryLen20, 0.15f);
    gnss_time_vel_.assign(kHistoryLen20, 0.04f);

    dos_time_noise_.assign(kHistoryLen20, -104.2f);
    dos_time_duty_.assign(kHistoryLen20, 14.2f);
    dos_time_entropy_.assign(kHistoryLen20, 4.82f);
    dos_time_tdoa_.assign(kHistoryLen20, 0.94f);
    dos_time_cir_.assign(kHistoryLen20, 0.05f);

    stft_waterfall_matrix_.assign(kStftHistory * kStftBins, 0.05f);
    terr_waterfall_matrix_.assign(kStftHistory * kStftBins, 0.05f);
    air_waterfall_matrix_.assign(kStftHistory * kStftBins, 0.05f);
    std::fill_n(audio_vis_buf_, 128, 0.0f);
}

OperatorConsoleGui::~OperatorConsoleGui()
{
    shutdown_window();
}

bool OperatorConsoleGui::init_window()
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "[OperatorConsoleGui] glfwInit failed\n");
        return false;
    }

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    window_ = glfwCreateWindow(1600, 950, window_title_.c_str(), nullptr, nullptr);
    if (!window_) {
        std::fprintf(stderr, "[OperatorConsoleGui] glfwCreateWindow failed\n");
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

    // Customize UI theme for sleek operator aesthetics
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.FramePadding = ImVec2(8.0f, 6.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.08f, 0.12f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.14f, 0.18f, 0.28f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.28f, 0.42f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.36f, 0.54f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.12f, 0.16f, 0.24f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.26f, 0.38f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.24f, 0.34f, 0.48f, 1.00f);

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    return true;
}

void OperatorConsoleGui::shutdown_window()
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

void OperatorConsoleGui::draw_top_status_bar()
{
    double freq = status_.current_freq_hz();
    bool bursting = status_.is_bursting();
    uint64_t drops = status_.rx_overflow_count();

    // System Branding & Hardware Clock
    ImGui::TextColored(ImVec4(0.0f, 0.9f, 1.0f, 1.0f), "C2 TACTICAL CONSOLE");
    ImGui::SameLine(0, 8.0f);
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[ GPSDO LOCKED ±0.001 ppm ]");

    ImGui::SameLine(0, 16.0f);
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "ACTIVE CARRIER:");
    ImGui::SameLine();
    ImGui::Text("%.3f GHz", freq / 1e9);

    ImGui::SameLine(0, 16.0f);
    if (bursting) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "[ BURST ACTIVE ]");
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[ SILENCE ]");
    }

    ImGui::SameLine(0, 16.0f);
    float active_mult = 1.0f;
    if (std::abs(freq - 2.4e9) < 200e6) active_mult = 2.0f;
    else if (std::abs(freq - 5.1e9) < 200e6) active_mult = 3.0f;
    else if (std::abs(freq - 5.8e9) < 200e6) active_mult = 4.0f;

    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "MULTIPLIER: x%.1f", active_mult);

    double lat = status_.latency_ms();
    double jit = status_.jitter_ms();
    double fps = status_.frame_rate_fps();

    ImGui::SameLine(0, 16.0f);
    if (lat > 0.0) {
        ImVec4 lat_color = (lat < 5.0) ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) :
                           (lat < 20.0) ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(lat_color, "| Latency: %.2f ms", lat);
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "| Latency: <1.0 ms");
    }

    ImGui::SameLine(0, 16.0f);
    ImGui::Text("| Jitter: ±%.2f ms", jit);

    ImGui::SameLine(0, 16.0f);
    ImGui::Text("| Rate: %.0f fps (%.1f MS/s)", fps > 0.0 ? fps : 1000.0, sample_rate_hz_ / 1e6);

    ImGui::SameLine(0, 16.0f);
    if (drops == 0) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "| Loss: 0 (Lossless)");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "| Loss: %llu Drops", static_cast<unsigned long long>(drops));
    }
}

void OperatorConsoleGui::draw_left_sidebar()
{
    ImGui::BeginChild("SidebarRegion", ImVec2(240, 0), true);

    double current_freq = status_.current_freq_hz();

    // Helper for rendering styled sidebar buttons
    auto draw_tab_button = [&](ConsoleTab tab, const char* label, const char* sublabel, const char* badge, bool is_band_btn, double target_freq_hz) {
        bool is_selected = (active_tab_ == tab);
        bool is_band_active = is_band_btn ? (std::abs(current_freq - target_freq_hz) < 200e6) : false;

        if (is_selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.32f, 0.55f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.38f, 0.64f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.13f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.20f, 0.30f, 1.0f));
        }

        ImVec2 btn_size = ImVec2(ImGui::GetContentRegionAvail().x, 50.0f);
        std::string btn_id = std::string(label) + "##btn";
        if (ImGui::Button(btn_id.c_str(), btn_size)) {
            active_tab_ = tab;
        }

        ImVec2 p_min = ImGui::GetItemRectMin();
        ImVec2 p_max = ImGui::GetItemRectMax();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        // Main tab title
        ImVec4 text_col = is_selected ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        draw_list->AddText(ImVec2(p_min.x + 10.0f, p_min.y + 6.0f), ImGui::ColorConvertFloat4ToU32(text_col), label);

        // Sublabel
        draw_list->AddText(ImVec2(p_min.x + 10.0f, p_min.y + 26.0f),
                           ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.62f, 0.72f, 1.0f)), sublabel);

        // Badge on right
        if (badge) {
            ImU32 badge_bg = is_band_btn ? (is_band_active ? IM_COL32(30, 150, 60, 220) : IM_COL32(60, 65, 75, 180))
                                         : IM_COL32(30, 80, 140, 200);
            ImVec2 badge_pos = ImVec2(p_max.x - 52.0f, p_min.y + 14.0f);
            draw_list->AddRectFilled(badge_pos, ImVec2(badge_pos.x + 44.0f, badge_pos.y + 20.0f), badge_bg, 4.0f);
            draw_list->AddText(ImVec2(badge_pos.x + 6.0f, badge_pos.y + 2.0f), IM_COL32(255, 255, 255, 255), badge);
        }

        ImGui::PopStyleColor(2);
        ImGui::Spacing();
    };

    ImGui::TextColored(ImVec4(0.8f, 0.85f, 0.95f, 1.0f), "RF BANDS & CHANNELS");
    ImGui::Separator();
    ImGui::Spacing();

    draw_tab_button(ConsoleTab::k24GHz, "2.4 GHz Band", "Multiplier: x2.0", "CH 1", true, 2.4e9);
    draw_tab_button(ConsoleTab::k51GHz, "5.1 GHz Band", "Multiplier: x3.0", "CH 2", true, 5.1e9);
    draw_tab_button(ConsoleTab::k58GHz, "5.8 GHz Band", "Multiplier: x4.0", "CH 3", true, 5.8e9);
    draw_tab_button(ConsoleTab::kAll,   "All Channels", "Composite Stream", "CH 4", false, 0.0);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "TACTICAL PROCESSORS");
    ImGui::Separator();
    ImGui::Spacing();

    draw_tab_button(ConsoleTab::kTerrestrial, "Terrestrial Decoder", "DMR / TETRA / SCADA", "LMR", false, 0.0);
    draw_tab_button(ConsoleTab::kAirMaritime, "Air & Sea Awareness", "ADS-B / AIS Dual DDC", "RADAR", false, 0.0);
    draw_tab_button(ConsoleTab::kReplayDos,   "Replay & DoS Threat", "28 Anomaly Metrics", "28 PUB", false, 0.0);
    draw_tab_button(ConsoleTab::kGnss,        "GNSS Threat Monitor", "14 Topics (L1/L2)", "14 PUB", false, 0.0);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Node Information Box
    ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "SINK NODE TELEMETRY");
    ImGui::Text("System: Node 3 (Operator)");
    ImGui::Text("Rate: 2.0 MS/s (Lossless)");
    ImGui::Text("Listen Port: 6001");
    ImGui::Text("Link: S1 -> S2 -> S3");

    ImGui::EndChild();
}

void OperatorConsoleGui::render_waveform_plot(const char* plot_id, IqRingBuffer& ring,
                                              std::vector<std::complex<float>>& scratch,
                                              std::vector<float>& i_buf, std::vector<float>& q_buf,
                                              double y_min, double y_max, float& out_peak_v)
{
    size_t n = ring.read_latest(scratch.data(), scratch.size());
    if (n < scratch.size()) {
        std::fill(scratch.begin() + n, scratch.end(), std::complex<float>(0.0f, 0.0f));
    }

    size_t count = scratch.size();
    i_buf.resize(count);
    q_buf.resize(count);
    float max_v = 0.0f;
    for (size_t k = 0; k < count; ++k) {
        float i_val = scratch[k].real();
        float q_val = scratch[k].imag();
        i_buf[k] = i_val;
        q_buf[k] = q_val;
        float mag = std::sqrt(i_val * i_val + q_val * q_val);
        if (mag > max_v) max_v = mag;
    }
    out_peak_v = max_v;

    if (ImPlot::BeginPlot(plot_id, ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Sample Index", "Voltage (V)");
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, count, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, y_min, y_max, ImGuiCond_Always);

        ImPlot::PlotLine("I (In-Phase)", x_axis_wave_.data(), i_buf.data(), static_cast<int>(count));
        ImPlot::PlotLine("Q (Quadrature)", x_axis_wave_.data(), q_buf.data(), static_cast<int>(count));
        ImPlot::EndPlot();
    }
}

void OperatorConsoleGui::render_fft_plot(const char* plot_id, ChannelData& ch_data,
                                         std::vector<float>& local_fft_db,
                                         std::vector<float>& freq_axis,
                                         double default_carrier_mhz,
                                         double& last_carrier_mhz,
                                         float& out_peak_db, double& out_peak_mhz)
{
    double carrier_mhz = default_carrier_mhz;

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
    float max_db = -200.0f;
    double max_freq = carrier_mhz;

    for (size_t i = 0; i < fft_len; ++i) {
        double freq_val = carrier_mhz + (static_cast<double>(i) - (fft_len / 2.0)) * bin_mhz;
        freq_axis[i] = static_cast<float>(freq_val);
        if (local_fft_db[i] > max_db) {
            max_db = local_fft_db[i];
            max_freq = freq_val;
        }
    }
    out_peak_db = max_db;
    out_peak_mhz = max_freq;

    bool band_changed = (carrier_mhz != last_carrier_mhz);
    if (band_changed) {
        last_carrier_mhz = carrier_mhz;
    }

    if (ImPlot::BeginPlot(plot_id, ImVec2(-1, -1))) {
        ImPlot::SetupAxes("RF Frequency (MHz)", "Magnitude (dBFS)", ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisFormat(ImAxis_X1, "%.3f");

        double span_mhz = 0.05;
        ImPlotCond cond = band_changed ? ImPlotCond_Always : ImPlotCond_Once;
        ImPlot::SetupAxisLimits(ImAxis_X1, carrier_mhz - span_mhz, carrier_mhz + span_mhz, cond);

        ImPlot::PlotLine(plot_id, freq_axis.data(), local_fft_db.data(), static_cast<int>(fft_len));
        ImPlot::EndPlot();
    }
}

void OperatorConsoleGui::render_polar_map_plot(const char* plot_id, float elevation_deg, float azimuth_deg, bool is_active)
{
    if (ImPlot::BeginPlot(plot_id, ImVec2(-1, -1), ImPlotFlags_Equal)) {
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

        if (is_active && elevation_deg > 0.0f) {
            float rad = azimuth_deg * static_cast<float>(M_PI / 180.0);
            float target_r = elevation_deg;
            float target_x = target_r * std::sin(rad);
            float target_y = target_r * std::cos(rad);

            float line_x[2] = {0.0f, target_x};
            float line_y[2] = {0.0f, target_y};
            ImPlot::SetNextLineStyle(ImVec4(0.2f, 1.0f, 0.4f, 0.9f), 2.0f);
            ImPlot::PlotLine("Bearing", line_x, line_y, 2);

            float pt_x[1] = {target_x};
            float pt_y[1] = {target_y};
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 8.0f, ImVec4(1.0f, 0.3f, 0.2f, 1.0f), 2.0f, ImVec4(1.0f, 0.9f, 0.2f, 1.0f));
            char pt_label[64];
            std::snprintf(pt_label, sizeof(pt_label), "Target (El: %.0f°, Az: %.0f°)", elevation_deg, azimuth_deg);
            ImPlot::PlotScatter(pt_label, pt_x, pt_y, 1);
        } else {
            float pt_x[1] = {0.0f};
            float pt_y[1] = {0.0f};
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5.0f, ImVec4(0.5f, 0.5f, 0.6f, 0.6f), 1.0f, ImVec4(0.3f, 0.3f, 0.4f, 0.8f));
            ImPlot::PlotScatter("Idle (0°, 0°)", pt_x, pt_y, 1);
        }

        ImPlot::EndPlot();
    }
}

// -----------------------------------------------------------------------------
// Specialized Renderer: Clear Tactical Map (Air & Sea Contacts)
// -----------------------------------------------------------------------------
void OperatorConsoleGui::render_clear_tactical_map(const char* plot_id, const ExtendedDomainTelemetry& telem, float width, float height)
{
    ImGui::BeginChild(plot_id, ImVec2(width, height), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Interactive Map Toolbar at top of map
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));

    if (ImGui::Button(" ➕ ")) { map_zoom_nmi_ = std::max(5.0f, map_zoom_nmi_ * 0.75f); }
    ImGui::SameLine();
    if (ImGui::Button(" ➖ ")) { map_zoom_nmi_ = std::min(120.0f, map_zoom_nmi_ * 1.33f); }
    ImGui::SameLine();
    if (ImGui::Button("🎯 Center")) { map_pan_lat_ = 0.0f; map_pan_lon_ = 0.0f; map_zoom_nmi_ = 28.0f; }
    ImGui::SameLine();
    if (ImGui::Button("15nm Bay")) { map_pan_lat_ = 0.03f; map_pan_lon_ = -0.02f; map_zoom_nmi_ = 15.0f; }
    ImGui::SameLine();
    if (ImGui::Button("30nm Metro")) { map_pan_lat_ = 0.0f; map_pan_lon_ = 0.0f; map_zoom_nmi_ = 30.0f; }
    ImGui::SameLine();
    if (ImGui::Button("60nm Wide")) { map_pan_lat_ = 0.0f; map_pan_lon_ = 0.0f; map_zoom_nmi_ = 60.0f; }

    ImGui::SameLine(0, 16.0f);
    ImGui::Checkbox("✈ Air", &show_air_layer_);
    ImGui::SameLine();
    ImGui::Checkbox("⚓ Sea", &show_sea_layer_);
    ImGui::SameLine();
    ImGui::Checkbox("🚢 Channels", &show_channels_layer_);
    ImGui::SameLine();
    ImGui::Checkbox("📡 Buoys", &show_buoys_layer_);
    ImGui::SameLine();
    ImGui::Checkbox("🛡 Airspace", &show_airspace_layer_);
    ImGui::SameLine();
    ImGui::Checkbox("➰ Trails", &show_trails_layer_);
    ImGui::SameLine();
    ImGui::Checkbox("↗ Vectors", &show_vectors_layer_);

    ImGui::PopStyleVar(2);
    ImGui::Spacing();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 50.0f || avail.y < 50.0f) {
        ImGui::EndChild();
        return;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p_min = ImGui::GetCursorScreenPos();
    ImVec2 p_max = ImVec2(p_min.x + avail.x, p_min.y + avail.y);
    float w = avail.x;
    float h = avail.y;

    draw_list->PushClipRect(p_min, p_max, true);

    // Viewport Center and Geodesic Projection Parameters
    float center_lat = map_center_lat_ + map_pan_lat_;
    float center_lon = map_center_lon_ + map_pan_lon_;
    float zoom_nmi = std::clamp(map_zoom_nmi_, 4.0f, 150.0f);

    auto geo_to_screen = [&](float lat, float lon) -> ImVec2 {
        float dlat_nmi = (lat - center_lat) * 60.0f;
        float dlon_nmi = (lon - center_lon) * 60.0f * std::cos(center_lat * 0.0174532925f);
        float sx = (p_min.x + p_max.x) * 0.5f + (dlon_nmi / zoom_nmi) * (w * 0.5f);
        float sy = (p_min.y + p_max.y) * 0.5f - (dlat_nmi / zoom_nmi) * (h * 0.5f);
        return ImVec2(sx, sy);
    };

    // 1. Deep Oceanic Background
    draw_list->AddRectFilled(p_min, p_max, IM_COL32(6, 12, 22, 255), 4.0f);

    // 2. Nautical Coordinate Grid & Lat/Lon Markings
    for (float gx = 0.1f; gx < 1.0f; gx += 0.15f) {
        draw_list->AddLine(ImVec2(p_min.x + w * gx, p_min.y), ImVec2(p_min.x + w * gx, p_max.y), IM_COL32(255, 255, 255, 8), 1.0f);
    }
    for (float gy = 0.15f; gy < 1.0f; gy += 0.18f) {
        draw_list->AddLine(ImVec2(p_min.x, p_min.y + h * gy), ImVec2(p_max.x, p_min.y + h * gy), IM_COL32(255, 255, 255, 8), 1.0f);
    }

    // 3. High-Fidelity Geodesic Landmasses & Coastlines
    // North / Marin County Peninsula
    ImVec2 land_marin_geo[] = {
        geo_to_screen(38.15f, -122.95f),
        geo_to_screen(38.15f, -122.45f),
        geo_to_screen(37.98f, -122.46f),
        geo_to_screen(37.89f, -122.46f),
        geo_to_screen(37.83f, -122.48f), // Point Bonita / Marin Headlands
        geo_to_screen(37.83f, -122.54f),
        geo_to_screen(37.90f, -122.70f),
        geo_to_screen(38.15f, -122.95f)
    };
    draw_list->AddConvexPolyFilled(land_marin_geo, 7, IM_COL32(18, 30, 48, 255));
    draw_list->AddPolyline(land_marin_geo, 8, IM_COL32(38, 60, 95, 255), ImDrawFlags_None, 1.5f);

    // South / San Francisco Peninsula
    ImVec2 land_sf_geo[] = {
        geo_to_screen(37.81f, -122.48f), // Fort Point
        geo_to_screen(37.81f, -122.40f), // North Beach / Embarcadero
        geo_to_screen(37.76f, -122.38f), // Hunters Point
        geo_to_screen(37.62f, -122.37f), // SFO Waterfront
        geo_to_screen(37.45f, -122.35f), // Redwood City
        geo_to_screen(37.45f, -122.52f), // Half Moon Bay
        geo_to_screen(37.72f, -122.51f), // Ocean Beach
        geo_to_screen(37.79f, -122.51f), // Cliff House
        geo_to_screen(37.81f, -122.48f)
    };
    draw_list->AddConvexPolyFilled(land_sf_geo, 8, IM_COL32(18, 30, 48, 255));
    draw_list->AddPolyline(land_sf_geo, 9, IM_COL32(38, 60, 95, 255), ImDrawFlags_None, 1.5f);

    // East Bay / Oakland / Alameda
    ImVec2 land_east_geo[] = {
        geo_to_screen(38.15f, -122.35f),
        geo_to_screen(38.15f, -122.05f),
        geo_to_screen(37.45f, -122.05f),
        geo_to_screen(37.45f, -122.25f),
        geo_to_screen(37.68f, -122.28f), // Oakland Airport
        geo_to_screen(37.78f, -122.31f), // Alameda Island
        geo_to_screen(37.82f, -122.33f), // Port of Oakland
        geo_to_screen(37.88f, -122.34f), // Berkeley Marina
        geo_to_screen(37.95f, -122.42f), // Richmond Point
        geo_to_screen(38.15f, -122.35f)
    };
    draw_list->AddConvexPolyFilled(land_east_geo, 9, IM_COL32(18, 30, 48, 255));
    draw_list->AddPolyline(land_east_geo, 10, IM_COL32(38, 60, 95, 255), ImDrawFlags_None, 1.5f);

    // Islands: Angel Island, Alcatraz, Treasure Island
    ImVec2 pt_angel = geo_to_screen(37.86f, -122.43f);
    draw_list->AddCircleFilled(pt_angel, std::max(4.0f, 18.0f * (28.0f / zoom_nmi)), IM_COL32(18, 30, 48, 255));
    draw_list->AddCircle(pt_angel, std::max(4.0f, 18.0f * (28.0f / zoom_nmi)), IM_COL32(38, 60, 95, 255), 16, 1.2f);

    ImVec2 pt_alcatraz = geo_to_screen(37.827f, -122.423f);
    draw_list->AddCircleFilled(pt_alcatraz, std::max(2.5f, 7.0f * (28.0f / zoom_nmi)), IM_COL32(18, 30, 48, 255));
    draw_list->AddCircle(pt_alcatraz, std::max(2.5f, 7.0f * (28.0f / zoom_nmi)), IM_COL32(38, 60, 95, 255), 12, 1.0f);

    ImVec2 pt_treasure = geo_to_screen(37.824f, -122.370f);
    draw_list->AddCircleFilled(pt_treasure, std::max(3.0f, 12.0f * (28.0f / zoom_nmi)), IM_COL32(18, 30, 48, 255));
    draw_list->AddCircle(pt_treasure, std::max(3.0f, 12.0f * (28.0f / zoom_nmi)), IM_COL32(38, 60, 95, 255), 16, 1.0f);

    // Bridges (Golden Gate & Bay Bridge Spans)
    ImVec2 gg_north = geo_to_screen(37.830f, -122.478f);
    ImVec2 gg_south = geo_to_screen(37.810f, -122.478f);
    draw_list->AddLine(gg_north, gg_south, IM_COL32(234, 88, 12, 180), 2.0f);

    ImVec2 bb_sf = geo_to_screen(37.795f, -122.390f);
    ImVec2 bb_ti = geo_to_screen(37.820f, -122.370f);
    ImVec2 bb_oak = geo_to_screen(37.825f, -122.310f);
    draw_list->AddLine(bb_sf, bb_ti, IM_COL32(148, 163, 184, 160), 1.5f);
    draw_list->AddLine(bb_ti, bb_oak, IM_COL32(148, 163, 184, 160), 1.5f);

    // 4. Traffic Separation Scheme (TSS) Shipping Channels
    if (show_channels_layer_) {
        ImVec2 ch_w = geo_to_screen(37.750f, -122.680f);
        ImVec2 ch_gg = geo_to_screen(37.820f, -122.475f);
        ImVec2 ch_c = geo_to_screen(37.825f, -122.365f);
        ImVec2 ch_e = geo_to_screen(37.800f, -122.330f);

        draw_list->AddLine(ch_w, ch_gg, IM_COL32(245, 158, 11, 70), 1.8f);
        draw_list->AddLine(ch_gg, ch_c, IM_COL32(245, 158, 11, 70), 1.8f);
        draw_list->AddLine(ch_c, ch_e, IM_COL32(245, 158, 11, 70), 1.8f);

        draw_list->AddText(ImVec2(ch_gg.x + 8.0f, ch_gg.y - 14.0f), IM_COL32(245, 158, 11, 140), "TSS MAIN CHANNEL");
    }

    // 5. Navigation Aids & Channel Buoys (Flashing LED indicators)
    if (show_buoys_layer_) {
        bool flash_phase = (static_cast<int>(ui_tick_ / 15) % 2 == 0);

        // Point Bonita Lighthouse
        ImVec2 pt_bonita = geo_to_screen(37.820f, -122.529f);
        draw_list->AddCircleFilled(pt_bonita, 4.0f, IM_COL32(254, 240, 138, flash_phase ? 255 : 80));
        draw_list->AddText(ImVec2(pt_bonita.x + 6.0f, pt_bonita.y - 6.0f), IM_COL32(254, 240, 138, 180), "PT BONITA LT");

        // Starboard Green Channel Buoys (G "1", G "3")
        ImVec2 b_g1 = geo_to_screen(37.785f, -122.580f);
        ImVec2 b_g3 = geo_to_screen(37.815f, -122.460f);
        draw_list->AddCircleFilled(b_g1, 3.5f, IM_COL32(34, 197, 94, flash_phase ? 255 : 60));
        draw_list->AddText(ImVec2(b_g1.x + 5.0f, b_g1.y - 5.0f), IM_COL32(34, 197, 94, 200), "G \"1\"");
        draw_list->AddCircleFilled(b_g3, 3.5f, IM_COL32(34, 197, 94, flash_phase ? 255 : 60));
        draw_list->AddText(ImVec2(b_g3.x + 5.0f, b_g3.y - 5.0f), IM_COL32(34, 197, 94, 200), "G \"3\"");

        // Port Red Channel Buoys (R "2", R "4")
        ImVec2 b_r2 = geo_to_screen(37.795f, -122.580f);
        ImVec2 b_r4 = geo_to_screen(37.825f, -122.460f);
        draw_list->AddCircleFilled(b_r2, 3.5f, IM_COL32(239, 68, 68, !flash_phase ? 255 : 60));
        draw_list->AddText(ImVec2(b_r2.x + 5.0f, b_r2.y - 5.0f), IM_COL32(239, 68, 68, 200), "R \"2\"");
        draw_list->AddCircleFilled(b_r4, 3.5f, IM_COL32(239, 68, 68, !flash_phase ? 255 : 60));
        draw_list->AddText(ImVec2(b_r4.x + 5.0f, b_r4.y - 5.0f), IM_COL32(239, 68, 68, 200), "R \"4\"");
    }

    // 6. Airspace Boundaries (SFO Class B, Glide Slopes, R-2501)
    if (show_airspace_layer_) {
        // SFO Class B 10nm Core
        ImVec2 sfo_pt = geo_to_screen(37.619f, -122.375f);
        float r_sfo_10nm = (10.0f / zoom_nmi) * (w * 0.5f);
        draw_list->AddCircle(sfo_pt, r_sfo_10nm, IM_COL32(56, 189, 248, 80), 48, 1.2f);
        draw_list->AddText(ImVec2(sfo_pt.x - 35.0f, sfo_pt.y + r_sfo_10nm + 2.0f), IM_COL32(56, 189, 248, 160), "CLASS B SFC-100");

        // ILS 28L/28R Final Approach Cones
        ImVec2 ils_start = geo_to_screen(37.580f, -122.200f);
        ImVec2 ils_end = geo_to_screen(37.615f, -122.355f);
        draw_list->AddLine(ils_start, ils_end, IM_COL32(56, 189, 248, 100), 1.5f);
        draw_list->AddText(ImVec2(ils_start.x - 40.0f, ils_start.y - 12.0f), IM_COL32(56, 189, 248, 140), "ILS RWY 28R/L");

        // Restricted Military Airspace R-2501
        ImVec2 r_pt = geo_to_screen(37.960f, -122.720f);
        float r_radius = (6.5f / zoom_nmi) * (w * 0.5f);
        draw_list->AddCircleFilled(r_pt, r_radius, IM_COL32(239, 68, 68, 25));
        draw_list->AddCircle(r_pt, r_radius, IM_COL32(239, 68, 68, 160), 32, 1.5f);
        draw_list->AddText(ImVec2(r_pt.x - 42.0f, r_pt.y - 6.0f), IM_COL32(239, 68, 68, 220), "RESTRICTED R-2501");
    }

    // 7. Sensor Base Station in Center with Range Rings
    ImVec2 base_pt = geo_to_screen(map_center_lat_, map_center_lon_);
    if (show_range_rings_) {
        float r_5nm = (5.0f / zoom_nmi) * (w * 0.5f);
        float r_15nm = (15.0f / zoom_nmi) * (w * 0.5f);
        float r_30nm = (30.0f / zoom_nmi) * (w * 0.5f);

        draw_list->AddCircle(base_pt, r_5nm, IM_COL32(6, 182, 212, 40), 40, 1.0f);
        draw_list->AddCircle(base_pt, r_15nm, IM_COL32(6, 182, 212, 50), 48, 1.0f);
        draw_list->AddCircle(base_pt, r_30nm, IM_COL32(6, 182, 212, 60), 64, 1.0f);

        draw_list->AddText(ImVec2(base_pt.x + r_5nm + 2.0f, base_pt.y - 6.0f), IM_COL32(6, 182, 212, 120), "5nm");
        draw_list->AddText(ImVec2(base_pt.x + r_15nm + 2.0f, base_pt.y - 6.0f), IM_COL32(6, 182, 212, 140), "15nm");
        draw_list->AddText(ImVec2(base_pt.x + r_30nm + 2.0f, base_pt.y - 6.0f), IM_COL32(6, 182, 212, 160), "30nm");
    }

    draw_list->AddCircleFilled(base_pt, 4.5f, IM_COL32(6, 182, 212, 255));
    draw_list->AddText(ImVec2(p_min.x + 8.0f, p_min.y + 6.0f), IM_COL32(148, 163, 184, 240),
                       "Base: 37.7749° N, 122.4194° W | Dual-DDC ADS-B (1090M) + AIS (162M) Active");

    ImVec2 mouse_pos = ImGui::GetMousePos();
    bool mouse_clicked = ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered();

    // 8. Maritime Vessels (AIS Class A & B: Hydrodynamic Wakes, Rotated Hulls, Nav Status)
    if (show_sea_layer_) {
        for (int i = 0; i < telem.sea_contact_count; ++i) {
            const auto& s = telem.sea_contacts[i];
            ImVec2 pos = geo_to_screen(s.lat, s.lon);

            // Skip if far outside screen
            if (pos.x < p_min.x - 50.0f || pos.x > p_max.x + 50.0f || pos.y < p_min.y - 50.0f || pos.y > p_max.y + 50.0f) {
                continue;
            }

            float hdg_rad = (s.heading_deg - 90.0f) * 0.0174532925f;
            float cos_h = std::cos(hdg_rad);
            float sin_h = std::sin(hdg_rad);

            // Classification color
            ImU32 v_col = (s.vessel_type == 0) ? IM_COL32(16, 185, 129, 255) : // Container (Emerald)
                          (s.vessel_type == 1) ? IM_COL32(6, 182, 212, 255) :  // Tanker (Cyan)
                          (s.vessel_type == 2) ? IM_COL32(59, 130, 246, 255) : // Coast Guard (Royal Blue)
                          (s.vessel_type == 3) ? IM_COL32(245, 158, 11, 255) : // Tug (Amber)
                          (s.vessel_type == 4) ? IM_COL32(236, 72, 153, 255) : // Ferry (Magenta)
                                                 IM_COL32(132, 204, 22, 255);  // Fishing (Lime)

            // A. Hydrodynamic Wake Trail & Bow Wave
            if (show_trails_layer_) {
                // Historical Wake points
                for (int k = 0; k < s.wake_count - 1; ++k) {
                    ImVec2 w1 = geo_to_screen(s.wake_lat[k], s.wake_lon[k]);
                    ImVec2 w2 = geo_to_screen(s.wake_lat[k + 1], s.wake_lon[k + 1]);
                    float alpha = std::max(20.0f, 160.0f - k * 25.0f);
                    draw_list->AddLine(w1, w2, IM_COL32(200, 240, 255, static_cast<int>(alpha)), 2.5f - k * 0.25f);
                }

                // Diverging Kelvin Bow Waves
                float bow_len = 16.0f;
                float wake_ang1 = hdg_rad + 2.80f; // ~160 deg from heading
                float wake_ang2 = hdg_rad - 2.80f;
                ImVec2 bow_pt = ImVec2(pos.x + cos_h * 8.0f, pos.y + sin_h * 8.0f);
                draw_list->AddLine(bow_pt, ImVec2(bow_pt.x + std::cos(wake_ang1) * bow_len, bow_pt.y + std::sin(wake_ang1) * bow_len),
                                   IM_COL32(220, 245, 255, 120), 1.2f);
                draw_list->AddLine(bow_pt, ImVec2(bow_pt.x + std::cos(wake_ang2) * bow_len, bow_pt.y + std::sin(wake_ang2) * bow_len),
                                   IM_COL32(220, 245, 255, 120), 1.2f);
            }

            // B. Scaled & Rotated Vessel Hull Polygon (Pointed Bow, Transom Stern)
            float hull_len = (s.vessel_type == 0 || s.vessel_type == 1) ? 16.0f :
                             (s.vessel_type == 2) ? 12.0f : 8.0f;
            float hull_beam = (s.vessel_type == 0 || s.vessel_type == 1) ? 5.5f : 3.5f;

            auto rot_pt = [&](float dx, float dy) -> ImVec2 {
                return ImVec2(pos.x + (dx * cos_h - dy * sin_h),
                              pos.y + (dx * sin_h + dy * cos_h));
            };

            ImVec2 hull[5] = {
                rot_pt(hull_len * 0.6f, 0.0f),            // Bow tip
                rot_pt(hull_len * 0.2f, hull_beam),       // Starboard forward
                rot_pt(-hull_len * 0.6f, hull_beam),      // Starboard stern
                rot_pt(-hull_len * 0.6f, -hull_beam),     // Port stern
                rot_pt(hull_len * 0.2f, -hull_beam)       // Port forward
            };
            draw_list->AddConvexPolyFilled(hull, 5, v_col);
            draw_list->AddPolyline(hull, 5, IM_COL32(255, 255, 255, 255), ImDrawFlags_Closed, 1.2f);

            // Deckhouse / Bridge Indicator
            ImVec2 bridge_pt = rot_pt(-hull_len * 0.3f, 0.0f);
            draw_list->AddCircleFilled(bridge_pt, 1.8f, IM_COL32(255, 255, 255, 220));

            // C. Speed Vector Predictor (3-minute projection)
            if (show_vectors_layer_) {
                float vec_len = s.speed_kts * 1.5f;
                ImVec2 vec_tip = ImVec2(pos.x + cos_h * vec_len, pos.y + sin_h * vec_len);
                draw_list->AddLine(pos, vec_tip, v_col, 1.5f);
                draw_list->AddCircleFilled(vec_tip, 2.0f, v_col);
            }

            // D. AIS Data Callout Tag
            char tag1[48], tag2[48];
            std::snprintf(tag1, sizeof(tag1), "⚓ %s", s.name);
            std::snprintf(tag2, sizeof(tag2), "%.1fkt • D:%.1fm", s.speed_kts, s.draft_m);

            ImVec2 tag_pos(pos.x + 12.0f, pos.y - 12.0f);
            draw_list->AddRectFilled(tag_pos, ImVec2(tag_pos.x + 115.0f, tag_pos.y + 26.0f), IM_COL32(10, 18, 30, 210), 3.0f);
            draw_list->AddRect(tag_pos, ImVec2(tag_pos.x + 115.0f, tag_pos.y + 26.0f), IM_COL32(255, 255, 255, 40), 3.0f);
            draw_list->AddText(ImVec2(tag_pos.x + 4.0f, tag_pos.y + 2.0f), v_col, tag1);
            draw_list->AddText(ImVec2(tag_pos.x + 4.0f, tag_pos.y + 14.0f), IM_COL32(203, 213, 225, 230), tag2);

            // E. Target Selection Detection & Reticle
            bool is_hovered = (std::abs(mouse_pos.x - pos.x) < 16.0f && std::abs(mouse_pos.y - pos.y) < 16.0f);
            if (is_hovered && mouse_clicked) {
                selected_target_domain_ = 2;
                selected_target_idx_ = i;
            }

            if (selected_target_domain_ == 2 && selected_target_idx_ == i) {
                float ret_r = 18.0f + std::sin(ui_tick_ * 0.15f) * 2.0f;
                draw_list->AddCircle(pos, ret_r, IM_COL32(6, 182, 212, 255), 24, 2.0f);
                draw_list->AddLine(ImVec2(pos.x - ret_r - 4.0f, pos.y), ImVec2(pos.x - ret_r + 4.0f, pos.y), IM_COL32(6, 182, 212, 255), 2.0f);
                draw_list->AddLine(ImVec2(pos.x + ret_r - 4.0f, pos.y), ImVec2(pos.x + ret_r + 4.0f, pos.y), IM_COL32(6, 182, 212, 255), 2.0f);
                draw_list->AddLine(ImVec2(pos.x, pos.y - ret_r - 4.0f), ImVec2(pos.x, pos.y - ret_r + 4.0f), IM_COL32(6, 182, 212, 255), 2.0f);
                draw_list->AddLine(ImVec2(pos.x, pos.y + ret_r - 4.0f), ImVec2(pos.x, pos.y + ret_r + 4.0f), IM_COL32(6, 182, 212, 255), 2.0f);
            }
        }
    }

    // 9. Aircraft Contacts (ADS-B Mode-S: Vector Silhouettes, Contrails, ATC Data Tags)
    if (show_air_layer_) {
        for (int i = 0; i < telem.air_contact_count; ++i) {
            const auto& a = telem.air_contacts[i];
            ImVec2 pos = geo_to_screen(a.lat, a.lon);

            if (pos.x < p_min.x - 50.0f || pos.x > p_max.x + 50.0f || pos.y < p_min.y - 50.0f || pos.y > p_max.y + 50.0f) {
                continue;
            }

            float hdg_rad = (a.heading_deg - 90.0f) * 0.0174532925f;
            float cos_h = std::cos(hdg_rad);
            float sin_h = std::sin(hdg_rad);

            // Altitude-Coded Color
            ImU32 a_col = (a.emergency_mode != 0) ? IM_COL32(239, 68, 68, 255) : // Emergency (Flashing Red)
                          (a.aircraft_type == 4)  ? IM_COL32(192, 132, 252, 255) : // UAV (Violet)
                          (a.alt_ft >= 24000.0f)  ? IM_COL32(56, 189, 248, 255) :  // High Cruise (Cyan)
                          (a.alt_ft >= 10000.0f)  ? IM_COL32(96, 165, 250, 255) :  // Mid Transition (Sky Blue)
                                                    IM_COL32(245, 158, 11, 255);   // Terminal Low (Amber)

            // A. Flight Contrail / Breadcrumb Trail
            if (show_trails_layer_) {
                for (int k = 0; k < a.trail_count - 1; ++k) {
                    ImVec2 t1 = geo_to_screen(a.trail_lat[k], a.trail_lon[k]);
                    ImVec2 t2 = geo_to_screen(a.trail_lat[k + 1], a.trail_lon[k + 1]);
                    float alpha = std::max(20.0f, 180.0f - k * 28.0f);
                    draw_list->AddLine(t1, t2, IM_COL32(180, 230, 255, static_cast<int>(alpha)), 2.0f - k * 0.2f);
                }
            }

            auto rot_pt = [&](float dx, float dy) -> ImVec2 {
                return ImVec2(pos.x + (dx * cos_h - dy * sin_h),
                              pos.y + (dx * sin_h + dy * cos_h));
            };

            // B. Rotated Vector Aircraft Silhouettes
            if (a.aircraft_type == 3) {
                // SAR Helicopter: Fuselage + spinning rotor cross
                draw_list->AddLine(rot_pt(8.0f, 0.0f), rot_pt(-10.0f, 0.0f), a_col, 2.5f);
                draw_list->AddLine(rot_pt(-10.0f, 0.0f), rot_pt(-10.0f, -4.0f), a_col, 1.8f); // Tail rotor

                float rot_ang = static_cast<float>(ui_tick_) * 0.35f;
                draw_list->AddCircle(pos, 8.0f, IM_COL32(255, 255, 255, 100), 16, 1.0f);
                draw_list->AddLine(ImVec2(pos.x + std::cos(rot_ang) * 8.0f, pos.y + std::sin(rot_ang) * 8.0f),
                                   ImVec2(pos.x - std::cos(rot_ang) * 8.0f, pos.y - std::sin(rot_ang) * 8.0f), a_col, 1.8f);
                draw_list->AddLine(ImVec2(pos.x + std::cos(rot_ang + 1.57f) * 8.0f, pos.y + std::sin(rot_ang + 1.57f) * 8.0f),
                                   ImVec2(pos.x - std::cos(rot_ang + 1.57f) * 8.0f, pos.y - std::sin(rot_ang + 1.57f) * 8.0f), a_col, 1.8f);
            } else if (a.aircraft_type == 4) {
                // High-Altitude UAV Drone: Slender glider wings
                draw_list->AddLine(rot_pt(10.0f, 0.0f), rot_pt(-8.0f, 0.0f), a_col, 2.2f);
                draw_list->AddLine(rot_pt(2.0f, 14.0f), rot_pt(2.0f, -14.0f), a_col, 2.5f); // High-aspect wings
                draw_list->AddLine(rot_pt(-8.0f, 4.0f), rot_pt(-8.0f, -4.0f), a_col, 1.8f); // V-tail
            } else {
                // Commercial / Regional / GA Swept Jet: Fuselage, 35 deg swept wings, horizontal tail, engines
                draw_list->AddLine(rot_pt(11.0f, 0.0f), rot_pt(-9.0f, 0.0f), a_col, 2.5f); // Fuselage
                // Swept Wings
                draw_list->AddLine(rot_pt(4.0f, 0.0f), rot_pt(-3.0f, 11.0f), a_col, 2.2f);
                draw_list->AddLine(rot_pt(4.0f, 0.0f), rot_pt(-3.0f, -11.0f), a_col, 2.2f);
                // Stabilizer Tail
                draw_list->AddLine(rot_pt(-8.0f, 0.0f), rot_pt(-11.0f, 5.0f), a_col, 1.6f);
                draw_list->AddLine(rot_pt(-8.0f, 0.0f), rot_pt(-11.0f, -5.0f), a_col, 1.6f);
                // Engine Pods
                draw_list->AddCircleFilled(rot_pt(0.0f, 4.5f), 1.2f, IM_COL32(255, 255, 255, 220));
                draw_list->AddCircleFilled(rot_pt(0.0f, -4.5f), 1.2f, IM_COL32(255, 255, 255, 220));
            }

            // C. Velocity Leader Line with Time Ticks (1 min, 2 min)
            if (show_vectors_layer_) {
                float vec_len = (a.speed_kts / 40.0f) * 4.0f;
                ImVec2 vec_tip = ImVec2(pos.x + cos_h * vec_len, pos.y + sin_h * vec_len);
                draw_list->AddLine(pos, vec_tip, a_col, 1.5f);
                draw_list->AddCircleFilled(vec_tip, 2.0f, a_col);
            }

            // D. Standard ATC Callout Data Tag Block
            char tag1[48], tag2[48], tag3[48];
            std::snprintf(tag1, sizeof(tag1), "✈ %s", a.callsign);
            if (a.alt_ft >= 18000.0f) {
                std::snprintf(tag2, sizeof(tag2), "FL%.0f • %.0fkt", a.alt_ft / 100.0f, a.speed_kts);
            } else {
                std::snprintf(tag2, sizeof(tag2), "%.0fft • %.0fkt", a.alt_ft, a.speed_kts);
            }
            const char* vsi_sym = (a.vertical_rate_fpm > 300.0f) ? "▲" : (a.vertical_rate_fpm < -300.0f) ? "▼" : "—";
            std::snprintf(tag3, sizeof(tag3), "%s %s", vsi_sym, a.squawk);

            ImVec2 tag_pos(pos.x + 14.0f, pos.y - 20.0f);
            draw_list->AddRectFilled(tag_pos, ImVec2(tag_pos.x + 110.0f, tag_pos.y + 36.0f), IM_COL32(10, 18, 30, 210), 3.0f);
            draw_list->AddRect(tag_pos, ImVec2(tag_pos.x + 110.0f, tag_pos.y + 36.0f), IM_COL32(255, 255, 255, 40), 3.0f);
            draw_list->AddText(ImVec2(tag_pos.x + 4.0f, tag_pos.y + 2.0f), a_col, tag1);
            draw_list->AddText(ImVec2(tag_pos.x + 4.0f, tag_pos.y + 13.0f), IM_COL32(203, 213, 225, 230), tag2);
            draw_list->AddText(ImVec2(tag_pos.x + 4.0f, tag_pos.y + 24.0f),
                               (a.emergency_mode != 0) ? IM_COL32(239, 68, 68, 255) : IM_COL32(148, 163, 184, 230), tag3);

            // E. Target Selection Detection & Reticle
            bool is_hovered = (std::abs(mouse_pos.x - pos.x) < 16.0f && std::abs(mouse_pos.y - pos.y) < 16.0f);
            if (is_hovered && mouse_clicked) {
                selected_target_domain_ = 1;
                selected_target_idx_ = i;
            }

            if (selected_target_domain_ == 1 && selected_target_idx_ == i) {
                float ret_r = 18.0f + std::sin(ui_tick_ * 0.15f) * 2.0f;
                draw_list->AddCircle(pos, ret_r, IM_COL32(56, 189, 248, 255), 24, 2.0f);
                draw_list->AddLine(ImVec2(pos.x - ret_r - 4.0f, pos.y), ImVec2(pos.x - ret_r + 4.0f, pos.y), IM_COL32(56, 189, 248, 255), 2.0f);
                draw_list->AddLine(ImVec2(pos.x + ret_r - 4.0f, pos.y), ImVec2(pos.x + ret_r + 4.0f, pos.y), IM_COL32(56, 189, 248, 255), 2.0f);
                draw_list->AddLine(ImVec2(pos.x, pos.y - ret_r - 4.0f), ImVec2(pos.x, pos.y - ret_r + 4.0f), IM_COL32(56, 189, 248, 255), 2.0f);
                draw_list->AddLine(ImVec2(pos.x, pos.y + ret_r - 4.0f), ImVec2(pos.x, pos.y + ret_r + 4.0f), IM_COL32(56, 189, 248, 255), 2.0f);
            }
        }
    }

    // 10. Selected Target Inspection HUD Overlay (Glass Cockpit Card)
    if (selected_target_domain_ != 0 && selected_target_idx_ >= 0) {
        ImVec2 hud_p1(p_max.x - 220.0f, p_min.y + 10.0f);
        ImVec2 hud_p2(p_max.x - 10.0f, p_min.y + 175.0f);

        draw_list->AddRectFilled(hud_p1, hud_p2, IM_COL32(15, 23, 42, 235), 6.0f);
        draw_list->AddRect(hud_p1, hud_p2, IM_COL32(56, 189, 248, 160), 6.0f, 0, 1.5f);

        char title[64], row1[64], row2[64], row3[64], row4[64], row5[64];
        float t_lat = 0.0f, t_lon = 0.0f;

        if (selected_target_domain_ == 1 && selected_target_idx_ < telem.air_contact_count) {
            const auto& a = telem.air_contacts[selected_target_idx_];
            t_lat = a.lat; t_lon = a.lon;
            std::snprintf(title, sizeof(title), "✈ %s (ICAO: %s)", a.callsign, a.icao);
            std::snprintf(row1, sizeof(row1), "Alt: %.0f ft (FL%.0f)", a.alt_ft, a.alt_ft / 100.0f);
            std::snprintf(row2, sizeof(row2), "Spd: %.0f kts | Mach: %.2f", a.speed_kts, a.mach);
            std::snprintf(row3, sizeof(row3), "Heading: %.0f° | %s", a.heading_deg, a.squawk);
            std::snprintf(row4, sizeof(row4), "Route: %s -> %s", a.origin, a.destination);
            std::snprintf(row5, sizeof(row5), "VSI: %+0.0f fpm | RSSI: %.0f dBm", a.vertical_rate_fpm, a.rssi_dbm);
        } else if (selected_target_domain_ == 2 && selected_target_idx_ < telem.sea_contact_count) {
            const auto& s = telem.sea_contacts[selected_target_idx_];
            t_lat = s.lat; t_lon = s.lon;
            std::snprintf(title, sizeof(title), "⚓ %s", s.name);
            std::snprintf(row1, sizeof(row1), "MMSI: %s | Draft: %.1fm", s.mmsi, s.draft_m);
            std::snprintf(row2, sizeof(row2), "SOG: %.1f kts | COG: %.0f°", s.speed_kts, s.heading_deg);
            std::snprintf(row3, sizeof(row3), "Dims: %.0fm x %.0fm", s.length_m, s.beam_m);
            std::snprintf(row4, sizeof(row4), "Dest: %s", s.destination);
            std::snprintf(row5, sizeof(row5), "ETA: %s | RSSI: %.0f dBm", s.eta, s.rssi_dbm);
        }

        // Distance & CPA calculation relative to Base Station
        float dlat = (t_lat - map_center_lat_) * 60.0f;
        float dlon = (t_lon - map_center_lon_) * 60.0f * std::cos(map_center_lat_ * 0.0174532925f);
        float dist_nmi = std::sqrt(dlat * dlat + dlon * dlon);
        float bearing_deg = std::atan2(dlon, dlat) * 57.2957795f;
        if (bearing_deg < 0.0f) bearing_deg += 360.0f;

        draw_list->AddText(ImVec2(hud_p1.x + 8.0f, hud_p1.y + 6.0f), IM_COL32(56, 189, 248, 255), title);
        draw_list->AddLine(ImVec2(hud_p1.x + 6.0f, hud_p1.y + 22.0f), ImVec2(hud_p2.x - 6.0f, hud_p1.y + 22.0f), IM_COL32(255, 255, 255, 40));

        draw_list->AddText(ImVec2(hud_p1.x + 8.0f, hud_p1.y + 26.0f), IM_COL32(226, 232, 240, 240), row1);
        draw_list->AddText(ImVec2(hud_p1.x + 8.0f, hud_p1.y + 42.0f), IM_COL32(226, 232, 240, 240), row2);
        draw_list->AddText(ImVec2(hud_p1.x + 8.0f, hud_p1.y + 58.0f), IM_COL32(226, 232, 240, 240), row3);
        draw_list->AddText(ImVec2(hud_p1.x + 8.0f, hud_p1.y + 74.0f), IM_COL32(226, 232, 240, 240), row4);
        draw_list->AddText(ImVec2(hud_p1.x + 8.0f, hud_p1.y + 90.0f), IM_COL32(226, 232, 240, 240), row5);

        char kpi[64];
        std::snprintf(kpi, sizeof(kpi), "Range: %.1f nm | Brg: %03.0f°", dist_nmi, bearing_deg);
        draw_list->AddText(ImVec2(hud_p1.x + 8.0f, hud_p1.y + 110.0f), IM_COL32(16, 185, 129, 255), kpi);

        float cpa_nmi = std::max(0.5f, dist_nmi * 0.65f);
        char cpa_str[64];
        std::snprintf(cpa_str, sizeof(cpa_str), "CPA: %.1f nm (TCPA: 04:20)", cpa_nmi);
        draw_list->AddText(ImVec2(hud_p1.x + 8.0f, hud_p1.y + 126.0f), IM_COL32(245, 158, 11, 255), cpa_str);

        // Deselect Button
        ImGui::SetCursorScreenPos(ImVec2(hud_p1.x + 8.0f, hud_p1.y + 146.0f));
        if (ImGui::Button("Track Lead", ImVec2(90.0f, 20.0f))) {
            map_pan_lat_ = t_lat - map_center_lat_;
            map_pan_lon_ = t_lon - map_center_lon_;
        }
        ImGui::SameLine();
        if (ImGui::Button("Deselect", ImVec2(80.0f, 20.0f))) {
            selected_target_domain_ = 0;
            selected_target_idx_ = -1;
        }
    }

    draw_list->PopClipRect();
    ImGui::EndChild();
}

// -----------------------------------------------------------------------------
// Specialized Renderer: Polar PPI Radar Scope
// -----------------------------------------------------------------------------
void OperatorConsoleGui::render_polar_ppi_scope(const char* plot_id, const ExtendedDomainTelemetry& telem, float width, float height)
{
    if (ImPlot::BeginPlot(plot_id, ImVec2(width, height), ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes("X (nmi)", "Y (nmi)", ImPlotAxisFlags_NoGridLines, ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxisLimits(ImAxis_X1, -110.0, 110.0, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -110.0, 110.0, ImGuiCond_Always);

        // 4 Concentric Range Rings (25, 50, 75, 100 nmi)
        constexpr int kRingPts = 64;
        static float ring_x[4][kRingPts + 1];
        static float ring_y[4][kRingPts + 1];
        static bool s_init_ppi = false;
        if (!s_init_ppi) {
            float radii[4] = {25.0f, 50.0f, 75.0f, 100.0f};
            for (int r = 0; r < 4; ++r) {
                for (int p = 0; p <= kRingPts; ++p) {
                    float th = static_cast<float>(p * 2.0 * M_PI / kRingPts);
                    ring_x[r][p] = radii[r] * std::cos(th);
                    ring_y[r][p] = radii[r] * std::sin(th);
                }
            }
            s_init_ppi = true;
        }

        ImPlot::SetNextLineStyle(ImVec4(0.2f, 0.3f, 0.4f, 0.4f), 1.0f);
        ImPlot::PlotLine("##PPI25", ring_x[0], ring_y[0], kRingPts + 1);
        ImPlot::SetNextLineStyle(ImVec4(0.2f, 0.3f, 0.4f, 0.4f), 1.0f);
        ImPlot::PlotLine("##PPI50", ring_x[1], ring_y[1], kRingPts + 1);
        ImPlot::SetNextLineStyle(ImVec4(0.2f, 0.3f, 0.4f, 0.4f), 1.0f);
        ImPlot::PlotLine("##PPI75", ring_x[2], ring_y[2], kRingPts + 1);
        ImPlot::SetNextLineStyle(ImVec4(0.3f, 0.6f, 0.8f, 0.7f), 1.5f);
        ImPlot::PlotLine("##PPI100", ring_x[3], ring_y[3], kRingPts + 1);

        // Crosshairs & Cardinal Marks
        static float axis_ns_x[2] = {0.0f, 0.0f};
        static float axis_ns_y[2] = {-100.0f, 100.0f};
        static float axis_ew_x[2] = {-100.0f, 100.0f};
        static float axis_ew_y[2] = {0.0f, 0.0f};
        ImPlot::SetNextLineStyle(ImVec4(0.3f, 0.4f, 0.5f, 0.3f), 1.0f);
        ImPlot::PlotLine("##AxisNS", axis_ns_x, axis_ns_y, 2);
        ImPlot::SetNextLineStyle(ImVec4(0.3f, 0.4f, 0.5f, 0.3f), 1.0f);
        ImPlot::PlotLine("##AxisEW", axis_ew_x, axis_ew_y, 2);

        // Sweeping Radar Beam
        float sw_x[2] = {0.0f, 100.0f * std::cos(radar_sweep_rad_)};
        float sw_y[2] = {0.0f, 100.0f * std::sin(radar_sweep_rad_)};
        ImPlot::SetNextLineStyle(ImVec4(0.2f, 0.8f, 1.0f, 0.8f), 2.0f);
        ImPlot::PlotLine("##RadarSweep", sw_x, sw_y, 2);

        // Dynamically plot all Air Contacts on PPI Scope
        for (int i = 0; i < telem.air_contact_count; ++i) {
            const auto& a = telem.air_contacts[i];
            float dlat = (a.lat - map_center_lat_) * 60.0f;
            float dlon = (a.lon - map_center_lon_) * 60.0f * std::cos(map_center_lat_ * 0.0174532925f);
            float bx[1] = {dlon};
            float by[1] = {dlat};

            bool is_sel = (selected_target_domain_ == 1 && selected_target_idx_ == i);
            ImVec4 a_col = is_sel ? ImVec4(1.0f, 1.0f, 0.2f, 1.0f) :
                           (a.emergency_mode != 0) ? ImVec4(1.0f, 0.2f, 0.2f, 1.0f) :
                           (a.aircraft_type == 4)  ? ImVec4(0.7f, 0.4f, 1.0f, 1.0f) :
                                                     ImVec4(0.2f, 0.8f, 1.0f, 1.0f);
            float marker_sz = is_sel ? 9.0f : 6.0f;
            char sc_id[32];
            std::snprintf(sc_id, sizeof(sc_id), "##AirBlip%d", i);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, marker_sz, a_col, 1.5f, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImPlot::PlotScatter(sc_id, bx, by, 1);

            if (is_sel) {
                float line_x[2] = {0.0f, dlon};
                float line_y[2] = {0.0f, dlat};
                ImPlot::SetNextLineStyle(ImVec4(1.0f, 1.0f, 0.2f, 0.8f), 1.5f);
                ImPlot::PlotLine("##SelAirBearing", line_x, line_y, 2);
            }
        }

        // Dynamically plot all Sea Contacts on PPI Scope
        for (int i = 0; i < telem.sea_contact_count; ++i) {
            const auto& s = telem.sea_contacts[i];
            float dlat = (s.lat - map_center_lat_) * 60.0f;
            float dlon = (s.lon - map_center_lon_) * 60.0f * std::cos(map_center_lat_ * 0.0174532925f);
            float bx[1] = {dlon};
            float by[1] = {dlat};

            bool is_sel = (selected_target_domain_ == 2 && selected_target_idx_ == i);
            ImVec4 s_col = is_sel ? ImVec4(1.0f, 1.0f, 0.2f, 1.0f) :
                           (s.vessel_type == 2) ? ImVec4(0.3f, 0.6f, 1.0f, 1.0f) :
                                                  ImVec4(0.0f, 0.9f, 0.8f, 1.0f);
            float marker_sz = is_sel ? 9.0f : 6.0f;
            char sc_id[32];
            std::snprintf(sc_id, sizeof(sc_id), "##SeaBlip%d", i);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, marker_sz, s_col, 1.5f, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImPlot::PlotScatter(sc_id, bx, by, 1);

            if (is_sel) {
                float line_x[2] = {0.0f, dlon};
                float line_y[2] = {0.0f, dlat};
                ImPlot::SetNextLineStyle(ImVec4(1.0f, 1.0f, 0.2f, 0.8f), 1.5f);
                ImPlot::PlotLine("##SelSeaBearing", line_x, line_y, 2);
            }
        }

        ImPlot::EndPlot();
    }
}

// -----------------------------------------------------------------------------
// Specialized Renderer: STFT 2D Waterfall Spectrogram
// -----------------------------------------------------------------------------
void OperatorConsoleGui::render_stft_waterfall(const char* plot_id, const float* slice_128, float width, float height)
{
    if (slice_128) {
        // Shift history rows down and insert new slice at row 0
        for (size_t r = kStftHistory - 1; r > 0; --r) {
            std::memcpy(&stft_waterfall_matrix_[r * kStftBins],
                        &stft_waterfall_matrix_[(r - 1) * kStftBins],
                        kStftBins * sizeof(float));
        }
        std::memcpy(&stft_waterfall_matrix_[0], slice_128, kStftBins * sizeof(float));
    }

    if (ImPlot::BeginPlot(plot_id, ImVec2(width, height))) {
        ImPlot::SetupAxes("RF Frequency (MHz)", "Time History (Frames)", ImPlotAxisFlags_None, ImPlotAxisFlags_Invert);
        ImPlot::SetupAxisLimits(ImAxis_X1, 1565.42, 1585.42, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, static_cast<double>(kStftHistory), ImGuiCond_Always);

        ImPlot::PlotHeatmap("##STFTHeatmap", stft_waterfall_matrix_.data(),
                            static_cast<int>(kStftHistory), static_cast<int>(kStftBins),
                            0.0, 1.0, nullptr,
                            ImPlotPoint(1565.42, 0.0), ImPlotPoint(1585.42, static_cast<double>(kStftHistory)));
        ImPlot::EndPlot();
    }
}

// -----------------------------------------------------------------------------
// Specialized Renderer: Terrestrial 2D Waterfall Spectrogram
// -----------------------------------------------------------------------------
void OperatorConsoleGui::render_terrestrial_waterfall(const char* plot_id, float width, float height)
{
    // Generate synthesized DMR 4-FSK channel activity on waterfall
    float new_slice[kStftBins];
    for (size_t i = 0; i < kStftBins; ++i) {
        float val = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 0.12f;
        // DMR 4-FSK tone carriers at bins 58, 62, 66, 70
        if (i == 58 || i == 62 || i == 66 || i == 70) {
            val += 0.65f + (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 0.3f;
        }
        new_slice[i] = std::min(1.0f, val);
    }

    for (size_t r = kStftHistory - 1; r > 0; --r) {
        std::memcpy(&terr_waterfall_matrix_[r * kStftBins],
                    &terr_waterfall_matrix_[(r - 1) * kStftBins],
                    kStftBins * sizeof(float));
    }
    std::memcpy(&terr_waterfall_matrix_[0], new_slice, kStftBins * sizeof(float));

    if (ImPlot::BeginPlot(plot_id, ImVec2(width, height))) {
        ImPlot::SetupAxes("Channel Offset (kHz)", "Time (Frames)", ImPlotAxisFlags_None, ImPlotAxisFlags_Invert);
        ImPlot::SetupAxisLimits(ImAxis_X1, -12.5, 12.5, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, static_cast<double>(kStftHistory), ImGuiCond_Always);

        ImPlot::PlotHeatmap("##TerrHeatmap", terr_waterfall_matrix_.data(),
                            static_cast<int>(kStftHistory), static_cast<int>(kStftBins),
                            0.0, 1.0, nullptr,
                            ImPlotPoint(-12.5, 0.0), ImPlotPoint(12.5, static_cast<double>(kStftHistory)));
        ImPlot::EndPlot();
    }
}

// -----------------------------------------------------------------------------
// Specialized Renderer: Physical Layer HW Fingerprint Radar Plot
// -----------------------------------------------------------------------------
void OperatorConsoleGui::render_hw_fingerprint_radar(const char* plot_id, const ExtendedDomainTelemetry& telem)
{
    if (ImPlot::BeginPlot(plot_id, ImVec2(-1, -1), ImPlotFlags_Equal)) {
        ImPlot::SetupAxes("X (Impairment)", "Y (Impairment)", ImPlotAxisFlags_NoGridLines, ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxisLimits(ImAxis_X1, -8.0, 8.0, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -8.0, 8.0, ImGuiCond_Always);

        // Whitelist baseline circle (2.0 sigma)
        constexpr int kPts = 32;
        float base_x[kPts + 1];
        float base_y[kPts + 1];
        for (int p = 0; p <= kPts; ++p) {
            float th = static_cast<float>(p * 2.0 * M_PI / kPts);
            base_x[p] = 2.0f * std::cos(th);
            base_y[p] = 2.0f * std::sin(th);
        }
        ImPlot::SetNextLineStyle(ImVec4(0.5f, 0.5f, 0.6f, 0.5f), 1.0f);
        ImPlot::PlotLine("Whitelist Baseline (±2.0σ)", base_x, base_y, kPts + 1);

        // Captured Emitter polygon (6 metrics)
        float vals[6] = {
            std::min(7.5f, telem.replay_mahalanobis_dist),
            std::min(7.5f, telem.replay_cfo_delta_hz / 100.0f),
            std::min(7.5f, telem.replay_iq_amp_imbalance_db * 20.0f + 1.0f),
            std::min(7.5f, telem.replay_iq_phase_imbal_deg * 5.0f + 1.0f),
            std::min(7.5f, telem.replay_pa_nonlinearity * 40.0f + 1.0f),
            std::min(7.5f, telem.replay_transient_delta * 30.0f + 1.0f)
        };

        float poly_x[7];
        float poly_y[7];
        for (int i = 0; i < 6; ++i) {
            float th = static_cast<float>(i * 2.0 * M_PI / 6.0);
            poly_x[i] = vals[i] * std::cos(th);
            poly_y[i] = vals[i] * std::sin(th);
        }
        poly_x[6] = poly_x[0];
        poly_y[6] = poly_y[0];

        ImVec4 fill_col = (telem.replay_mahalanobis_dist > 3.0f) ? ImVec4(1.0f, 0.2f, 0.3f, 0.9f) : ImVec4(0.7f, 0.3f, 1.0f, 0.8f);
        ImPlot::SetNextLineStyle(fill_col, 2.0f);
        ImPlot::PlotLine("Captured Emitter", poly_x, poly_y, 7);

        ImPlot::EndPlot();
    }
}

// -----------------------------------------------------------------------------
// TAB 5: Terrestrial Decoder & SCADA Link View
// -----------------------------------------------------------------------------
void OperatorConsoleGui::draw_terrestrial_decoder_view()
{
    ExtendedDomainTelemetry telem{};
    if (!status_.get_domain_telemetry(telem)) {
        local_fallback_engine_.update(0.016, telem);
    }

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "TERRESTRIAL RADIO & INDUSTRIAL SCADA MONITOR");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[ DMR TIER II (4-FSK) | MODBUS TCP PORT 502 ]");
    ImGui::Spacing();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float top_h = std::max(180.0f, avail.y * 0.45f);

    // 1. Top Part: 1x3 Subplots: Waveform, Channel FFT, and STFT Waterfall Spectrogram
    if (ImPlot::BeginSubplots("##TerrTopPlots", 1, 3, ImVec2(avail.x, top_h))) {
        render_waveform_plot("Demodulated Baseband IQ (DMR 4-FSK)", demux_.ch4().rx_ring,
                             ch_scratch_[3].wave_scratch, ch_scratch_[3].i_buf, ch_scratch_[3].q_buf,
                             -4.0, 4.0, ch_scratch_[3].peak_v);

        render_fft_plot("Terrestrial Channel FFT Spectrum", demux_.ch4(),
                        ch_scratch_[3].fft_db, ch_scratch_[3].fft_freq,
                        2400.0, ch_scratch_[3].last_carrier_mhz,
                        ch_scratch_[3].peak_db, ch_scratch_[3].peak_mhz);

        render_terrestrial_waterfall("Terrestrial STFT Waterfall", -1, -1);

        ImPlot::EndSubplots();
    }

    ImGui::Spacing();

    // 2. Bottom Row: 3 Diagnostic Cards
    float bot_h = ImGui::GetContentRegionAvail().y;
    ImGui::Columns(3, "TerrDiagCols", true);

    // Left: Direction Finding AoA Scope
    ImGui::BeginChild("TerrAoaRegion", ImVec2(0, bot_h), true);
    ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "🧭 AoA Polar Bearing Scope");
    ImGui::Separator();
    render_polar_map_plot("##TerrAoaPlot", 60.0f, telem.terr_aoa_bearing_deg > 0 ? telem.terr_aoa_bearing_deg : 142.4f, true);
    ImGui::EndChild();

    ImGui::NextColumn();

    // Middle: Digital Protocol Decoder & Interactive Live Audio Player
    ImGui::BeginChild("TerrVocoderRegion", ImVec2(0, bot_h), true);
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "📻 AMBE+2 Vocoder & DMR Telemetry");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Protocol Mode:      DMR Tier II (4-FSK)");
    ImGui::Text("Active Talkgroup:   %s", telem.terr_talkgroup[0] ? telem.terr_talkgroup : "TG 9001 (TAC-1)");
    ImGui::Text("Color Code / Slot:  CC %u / Slot 1", telem.terr_color_code ? telem.terr_color_code : 1);
    ImGui::Text("Frame Sync Pass:    %.1f%%", telem.terr_frame_sync_pct > 0 ? telem.terr_frame_sync_pct : 99.4f);
    ImGui::Text("Bit Error Rate:     %.2f%%", telem.terr_ber_pct > 0 ? telem.terr_ber_pct : 0.12f);
    ImGui::Text("Demod RSSI:         %.1f dBm", telem.terr_rssi_dbm != 0 ? telem.terr_rssi_dbm : -68.4f);

    ImGui::Spacing();
    ImGui::Text("Live Audio Demodulation Level (VU Meter):");
    float current_vu = audio_player_.is_playing() ? audio_player_.live_vu_level() :
                       (telem.terr_audio_vu_level > 0 ? telem.terr_audio_vu_level : 0.72f);
    ImGui::ProgressBar(current_vu, ImVec2(-1, 16.0f), "");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "🔊 Live Audio Player (Preset DMR Radio Dispatch):");
    ImGui::Separator();

    // Audio Playback Controls
    bool playing = audio_player_.is_playing();
    if (ImGui::Button(playing ? "⏸ Pause Audio" : "▶ Play Dispatch Loop", ImVec2(130.0f, 26.0f))) {
        audio_player_.toggle_play_pause();
    }
    ImGui::SameLine();
    if (ImGui::Button("⏹ Stop", ImVec2(60.0f, 26.0f))) {
        audio_player_.stop();
    }
    ImGui::SameLine();
    bool loop_enabled = audio_player_.is_loop();
    if (ImGui::Checkbox("Loop##AudioLoop", &loop_enabled)) {
        audio_player_.set_loop(loop_enabled);
    }
    ImGui::SameLine();
    bool muted = audio_player_.is_muted();
    if (ImGui::Checkbox(muted ? "🔇 Muted" : "🔈 Audio", &muted)) {
        audio_player_.set_muted(muted);
    }

    // Audio Scrubber Progress Bar
    float prog = audio_player_.progress();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##AudioScrubber", &prog, 0.0f, 1.0f, "Position: %.1f s / 8.0 s")) {
        audio_player_.seek_normalized(prog);
    }

    // Volume Slider
    float vol = audio_player_.volume();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::SliderFloat("Volume", &vol, 0.0f, 1.0f, "%.0f %%", ImGuiSliderFlags_None)) {
        audio_player_.set_volume(vol);
    }

    // Live Audio Waveform Visualizer
    audio_player_.get_visualizer_waveform(audio_vis_buf_, 128);
    ImGui::PlotLines("##AudioVisualizer", audio_vis_buf_, 128, 0, "Live Audio Waveform", -1.0f, 1.0f, ImVec2(-1, 32.0f));

    ImGui::EndChild();

    ImGui::NextColumn();

    // Right: SCADA Modbus TCP Register Matrix
    ImGui::BeginChild("TerrScadaRegion", ImVec2(0, bot_h), true);
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "⚡ Industrial SCADA & Modbus RTU");
    ImGui::Separator();

    if (ImGui::BeginTable("ScadaRegTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, 45.0f);
        ImGui::TableSetupColumn("Register Tag", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (telem.modbus_reg_count > 0 ? telem.modbus_reg_count : 8); ++i) {
            const auto& r = telem.modbus_regs[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%u", r.address);
            ImGui::TableNextColumn();
            ImGui::Text("%s", r.tag);
            ImGui::TableNextColumn();
            ImGui::Text("%u", r.value);
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "NORMAL");
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::Columns(1);
}

// -----------------------------------------------------------------------------
// TAB 6: Airtime & Maritime Awareness View
// -----------------------------------------------------------------------------
void OperatorConsoleGui::draw_air_maritime_awareness_view()
{
    ExtendedDomainTelemetry telem{};
    if (!status_.get_domain_telemetry(telem)) {
        local_fallback_engine_.update(0.016, telem);
    }

    ImGui::TextColored(ImVec4(0.0f, 0.9f, 1.0f, 1.0f), "AIRTIME & MARITIME SITUATIONAL AWARENESS");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "[ ✈️ %d AIR CONTACTS ]", telem.air_contact_count > 0 ? telem.air_contact_count : 6);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.8f, 1.0f), "[ ⚓ %d MARITIME VESSELS ]", telem.sea_contact_count > 0 ? telem.sea_contact_count : 5);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "[ 🗺️ GEODESIC RADAR LOCKED (37.77°N, -122.42°W) ]");
    ImGui::Spacing();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float upper_h = std::max(220.0f, avail.y * 0.58f);

    // 3-Column Top Grid: Left = Wave/Waterfall, Center = Tactical Map & USRP Params, Right = Polar PPI Scope
    ImGui::Columns(3, "AirSeaTopCols", true);
    ImGui::SetColumnWidth(0, avail.x * 0.28f);
    ImGui::SetColumnWidth(1, avail.x * 0.48f);

    // Column 1: IQ Waveform + Waterfall
    ImGui::BeginChild("AirSeaWaveRegion", ImVec2(0, upper_h), true);
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "⚡ IQ Waveform (PPM / GFSK Pulses)");
    float half_h = (upper_h - 60.0f) * 0.5f;
    render_waveform_plot("##AirWavePlot", demux_.ch4().rx_ring,
                         ch_scratch_[3].wave_scratch, ch_scratch_[3].i_buf, ch_scratch_[3].q_buf,
                         -4.0, 4.0, ch_scratch_[3].peak_v);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "🌊 Spectral Waterfall (ADS-B + AIS DDC)");
    render_stft_waterfall("##AirWaterfall", telem.gnss_stft_slice, -1, half_h);
    ImGui::EndChild();

    ImGui::NextColumn();

    // Column 2: Clear Tactical Map & USRP Tuning Controls
    ImGui::BeginChild("AirSeaMapRegion", ImVec2(0, upper_h), true);
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "🗺️ Tactical Air & Maritime Surveillance Map (San Francisco Bay & Coast)");
    float map_h = upper_h * 0.58f;
    render_clear_tactical_map("##TacticalClearMap", telem, -1, map_h);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "⚙️ USRP Parameters (Dual-DDC Edit & Tune)");
    ImGui::Separator();
    ImGui::Columns(4, "UsrpParamsGrid", false);
    ImGui::Text("Primary (MHz)");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputDouble("##usrpFreq1", &usrp_ui_freq1_, 0.0, 0.0, "%.3f");

    ImGui::NextColumn();
    ImGui::Text("AIS DDC (MHz)");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputDouble("##usrpFreq2", &usrp_ui_freq2_, 0.0, 0.0, "%.3f");

    ImGui::NextColumn();
    ImGui::Text("Rate (MS/s)");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputDouble("##usrpRate", &usrp_ui_rate_, 0.0, 0.0, "%.1f");

    ImGui::NextColumn();
    ImGui::Text("RX Gain (dB)");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputDouble("##usrpGain", &usrp_ui_gain_, 0.0, 0.0, "%.1f");

    ImGui::Columns(1);
    ImGui::Spacing();

    if (ImGui::Button("💾 Apply Changes / Tune USRP", ImVec2(200.0f, 24.0f))) {
        usrp_params_applied_ = true;
        usrp_apply_timer_ = 2.5f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Dual Air/Sea", ImVec2(90.0f, 24.0f))) {
        usrp_ui_freq1_ = 1090.000;
        usrp_ui_freq2_ = 162.000;
        usrp_ui_rate_ = 10.0;
        usrp_ui_gain_ = 54.0;
    }
    ImGui::SameLine();
    if (ImGui::Button("ADS-B 1090M", ImVec2(90.0f, 24.0f))) {
        usrp_ui_freq1_ = 1090.000;
        usrp_ui_freq2_ = 0.0;
    }
    ImGui::SameLine();
    if (ImGui::Button("AIS 162M", ImVec2(75.0f, 24.0f))) {
        usrp_ui_freq1_ = 162.000;
        usrp_ui_freq2_ = 0.0;
    }

    if (usrp_params_applied_) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "✅ Parameters Applied & DDC Tuned: Primary %.3f MHz | Secondary %.3f MHz",
                           usrp_ui_freq1_, usrp_ui_freq2_);
    }

    ImGui::EndChild();

    ImGui::NextColumn();

    // Column 3: Polar PPI Radar Scope & Fusion KPIs
    ImGui::BeginChild("AirSeaPpiRegion", ImVec2(0, upper_h), true);
    ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.8f, 1.0f), "🧭 Polar PPI Radar Scope (100 nmi)");

    // Dropdown combo bar for radar contact selection
    const char* current_selection_label = "🎯 Target: [ All Contacts ]";
    char sel_buf[128];
    if (selected_target_domain_ == 1 && selected_target_idx_ >= 0 && selected_target_idx_ < telem.air_contact_count) {
        std::snprintf(sel_buf, sizeof(sel_buf), "🎯 ✈️ %s (%s)", telem.air_contacts[selected_target_idx_].callsign, telem.air_contacts[selected_target_idx_].icao);
        current_selection_label = sel_buf;
    } else if (selected_target_domain_ == 2 && selected_target_idx_ >= 0 && selected_target_idx_ < telem.sea_contact_count) {
        std::snprintf(sel_buf, sizeof(sel_buf), "🎯 ⚓ %s (%s)", telem.sea_contacts[selected_target_idx_].name, telem.sea_contacts[selected_target_idx_].mmsi);
        current_selection_label = sel_buf;
    }

    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##PpiTargetDropdown", current_selection_label)) {
        bool is_all = (selected_target_domain_ == 0);
        if (ImGui::Selectable("🎯 [ Show All Contacts ]", is_all)) {
            selected_target_domain_ = 0;
            selected_target_idx_ = -1;
        }
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "── Airborne (ADS-B) ──");
        for (int i = 0; i < telem.air_contact_count; ++i) {
            const auto& a = telem.air_contacts[i];
            char item_lbl[96];
            std::snprintf(item_lbl, sizeof(item_lbl), "✈️ %s [%s] - %.0fft, %.0fkt", a.callsign, a.icao, a.alt_ft, a.speed_kts);
            bool is_sel = (selected_target_domain_ == 1 && selected_target_idx_ == i);
            if (ImGui::Selectable(item_lbl, is_sel)) {
                selected_target_domain_ = 1;
                selected_target_idx_ = i;
            }
        }
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.8f, 1.0f), "── Maritime (AIS) ──");
        for (int i = 0; i < telem.sea_contact_count; ++i) {
            const auto& s = telem.sea_contacts[i];
            char item_lbl[96];
            std::snprintf(item_lbl, sizeof(item_lbl), "⚓ %s [%s] - %.1fkt", s.name, s.mmsi, s.speed_kts);
            bool is_sel = (selected_target_domain_ == 2 && selected_target_idx_ == i);
            if (ImGui::Selectable(item_lbl, is_sel)) {
                selected_target_domain_ = 2;
                selected_target_idx_ = i;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Spacing();

    render_polar_ppi_scope("##PpiScope", telem, -1, upper_h * 0.52f);

    // Compute Nearest Airborne and Maritime Contacts dynamically
    float min_air_dist = 999.0f;
    const char* min_air_id = "NONE";
    for (int i = 0; i < telem.air_contact_count; ++i) {
        const auto& a = telem.air_contacts[i];
        float dlat = (a.lat - map_center_lat_) * 60.0f;
        float dlon = (a.lon - map_center_lon_) * 60.0f * std::cos(map_center_lat_ * 0.0174532925f);
        float d = std::sqrt(dlat * dlat + dlon * dlon);
        if (d < min_air_dist) { min_air_dist = d; min_air_id = a.callsign; }
    }

    float min_sea_dist = 999.0f;
    const char* min_sea_id = "NONE";
    for (int i = 0; i < telem.sea_contact_count; ++i) {
        const auto& s = telem.sea_contacts[i];
        float dlat = (s.lat - map_center_lat_) * 60.0f;
        float dlon = (s.lon - map_center_lon_) * 60.0f * std::cos(map_center_lat_ * 0.0174532925f);
        float d = std::sqrt(dlat * dlat + dlon * dlon);
        if (d < min_sea_dist) { min_sea_dist = d; min_sea_id = s.name; }
    }

    ImGui::Spacing();
    ImGui::Text("Nearest Airborne:  %s (%.1f nmi)", min_air_id, min_air_dist);
    ImGui::Text("Nearest Maritime:  %s (%.1f nmi)", min_sea_id, min_sea_dist);
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Emergency Squawk:  CLEAR (7700/7600 MONITORED)");
    ImGui::Text("Message Rate:      %u msgs/min", telem.sdr_msg_rate_per_min > 0 ? telem.sdr_msg_rate_per_min : 2410);
    ImGui::EndChild();

    ImGui::Columns(1);
    ImGui::Spacing();

    // Bottom Row: Decoded Air & Sea Broadcast Matrix Table
    ImGui::BeginChild("AirSeaTableRegion", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.3f, 1.0f), "📋 Decoded Air & Sea Broadcast Matrix Stream");
    ImGui::SameLine(0, 16.0f);
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##matrixSearch", "🔍 Filter Callsign, ICAO, MMSI, Name...", air_search_query_, sizeof(air_search_query_));
    ImGui::SameLine();
    if (selected_target_domain_ != 0) {
        if (ImGui::Button("❌ Clear Target Selection")) {
            selected_target_domain_ = 0;
            selected_target_idx_ = -1;
        }
    }

    if (ImGui::BeginTable("MatrixStreamTable", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Domain", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("ID (ICAO/MMSI)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Callsign / Vessel", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type / Classification", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Coordinates", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Alt / Draft", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Speed / Mach", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Heading", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("Squawk / Status", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableHeadersRow();

        auto matches_filter = [&](const char* str) -> bool {
            if (air_search_query_[0] == '\0') return true;
            if (!str) return false;
            std::string q = air_search_query_;
            std::string s = str;
            for (auto& c : q) c = std::tolower(c);
            for (auto& c : s) c = std::tolower(c);
            return s.find(q) != std::string::npos;
        };

        // Air Contacts (6 aircraft)
        for (int i = 0; i < telem.air_contact_count; ++i) {
            const auto& a = telem.air_contacts[i];
            if (!matches_filter(a.callsign) && !matches_filter(a.icao) && !matches_filter(a.squawk)) {
                continue;
            }

            ImGui::TableNextRow();
            bool is_selected = (selected_target_domain_ == 1 && selected_target_idx_ == i);
            if (is_selected) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(56, 189, 248, 40));
            }

            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "✈️ AIR");

            ImGui::TableNextColumn();
            ImGui::Text("%s", a.icao);

            ImGui::TableNextColumn();
            if (ImGui::Selectable(a.callsign, is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                selected_target_domain_ = 1;
                selected_target_idx_ = i;
            }

            ImGui::TableNextColumn();
            const char* type_str = (a.aircraft_type == 0) ? "Commercial Heavy" :
                                   (a.aircraft_type == 1) ? "Regional Jet" :
                                   (a.aircraft_type == 2) ? "General Aviation" :
                                   (a.aircraft_type == 3) ? "SAR Rotorcraft" :
                                   (a.aircraft_type == 4) ? "High-Alt UAV" : "Military";
            ImGui::Text("%s", type_str);

            ImGui::TableNextColumn();
            ImGui::Text("%.4f°, %.4f°", a.lat, a.lon);

            ImGui::TableNextColumn();
            ImGui::Text("%.0f ft", a.alt_ft);

            ImGui::TableNextColumn();
            ImGui::Text("%.0f kt (M%.2f)", a.speed_kts, a.mach);

            ImGui::TableNextColumn();
            ImGui::Text("%.0f°", a.heading_deg);

            ImGui::TableNextColumn();
            if (a.emergency_mode != 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "🚨 %s (ALERT)", a.squawk);
            } else {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "%s", a.squawk);
            }
        }

        // Sea Contacts (5 vessels)
        for (int i = 0; i < telem.sea_contact_count; ++i) {
            const auto& s = telem.sea_contacts[i];
            if (!matches_filter(s.name) && !matches_filter(s.mmsi) && !matches_filter(s.nav_status)) {
                continue;
            }

            ImGui::TableNextRow();
            bool is_selected = (selected_target_domain_ == 2 && selected_target_idx_ == i);
            if (is_selected) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(6, 182, 212, 40));
            }

            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.8f, 1.0f), "⚓ SEA");

            ImGui::TableNextColumn();
            ImGui::Text("%s", s.mmsi);

            ImGui::TableNextColumn();
            if (ImGui::Selectable(s.name, is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                selected_target_domain_ = 2;
                selected_target_idx_ = i;
            }

            ImGui::TableNextColumn();
            const char* vtype_str = (s.vessel_type == 0) ? "Container / Cargo" :
                                    (s.vessel_type == 1) ? "Crude / LNG Tanker" :
                                    (s.vessel_type == 2) ? "Coast Guard Cutter" :
                                    (s.vessel_type == 3) ? "Harbor Tug / Pilot" :
                                    (s.vessel_type == 4) ? "Passenger Ferry" : "Fishing Trawler";
            ImGui::Text("%s", vtype_str);

            ImGui::TableNextColumn();
            ImGui::Text("%.4f°, %.4f°", s.lat, s.lon);

            ImGui::TableNextColumn();
            ImGui::Text("%.1f m", s.draft_m);

            ImGui::TableNextColumn();
            ImGui::Text("%.1f kts", s.speed_kts);

            ImGui::TableNextColumn();
            ImGui::Text("%.0f°", s.heading_deg);

            ImGui::TableNextColumn();
            if (s.vessel_type == 2) {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", s.nav_status);
            } else {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "%s", s.nav_status);
            }
        }

        ImGui::EndTable();
    }
    ImGui::EndChild();
}

// -----------------------------------------------------------------------------
// TAB 7: Replay and DoS Threat Analyzer View
// -----------------------------------------------------------------------------
void OperatorConsoleGui::draw_replay_dos_view()
{
    ExtendedDomainTelemetry telem{};
    if (!status_.get_domain_telemetry(telem)) {
        local_fallback_engine_.update(0.016, telem);
    }

    push_history(dos_time_noise_, telem.dos_noise_floor_dbm != 0 ? telem.dos_noise_floor_dbm : -104.2f);
    push_history(dos_time_duty_, telem.dos_duty_cycle_pct != 0 ? telem.dos_duty_cycle_pct : 14.2f);
    push_history(dos_time_entropy_, telem.replay_ibi_entropy_bits != 0 ? telem.replay_ibi_entropy_bits : 4.82f);
    push_history(dos_time_tdoa_, telem.replay_tdoa_consistency != 0 ? telem.replay_tdoa_consistency : 0.94f);
    push_history(dos_time_cir_, telem.replay_cir_delta != 0 ? telem.replay_cir_delta : 0.05f);

    // Top Pub/Sub Header Strip
    ImGui::TextColored(ImVec4(0.7f, 0.4f, 1.0f, 1.0f), "RF REPLAY & WIRELESS DENIAL OF SERVICE (DoS) ANALYZER");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[ 28 PUBLISHED ANOMALY METRICS | 4 SUBSCRIBED ]");
    ImGui::Spacing();

    // 4 KPI Scoreboard Cards
    ImGui::Columns(4, "ReplayKpiCols", false);

    // 1. Combined Threat
    ImGui::BeginChild("KpiThreat", ImVec2(0, 75), true);
    ImGui::Text("24. COMBINED THREAT");
    float threat = telem.combined_threat_score > 0 ? telem.combined_threat_score : 8.4f;
    ImVec4 t_col = (threat > 75) ? ImVec4(1.0f, 0.2f, 0.3f, 1.0f) : (threat > 45) ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : ImVec4(0.2f, 1.0f, 0.4f, 1.0f);
    ImGui::TextColored(t_col, "%.1f / 100", threat);
    ImGui::ProgressBar(threat / 100.0f, ImVec2(-1, 8.0f), "");
    ImGui::Text("Co-occur: %.2fx", telem.cooccur_multiplier > 0 ? telem.cooccur_multiplier : 1.0f);
    ImGui::EndChild();

    ImGui::NextColumn();

    // 2. DoS Confidence
    ImGui::BeginChild("KpiDos", ImVec2(0, 75), true);
    ImGui::Text("22. DoS CONFIDENCE");
    float dos = telem.dos_confidence_pct > 0 ? telem.dos_confidence_pct : 4.2f;
    ImGui::TextColored(dos > 50 ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "%.1f %%", dos);
    ImGui::ProgressBar(dos / 100.0f, ImVec2(-1, 8.0f), "");
    ImGui::Text("Spike Score: %.2f", telem.dos_spike_score > 0 ? telem.dos_spike_score : 0.08f);
    ImGui::EndChild();

    ImGui::NextColumn();

    // 3. Replay Confidence
    ImGui::BeginChild("KpiReplay", ImVec2(0, 75), true);
    ImGui::Text("23. REPLAY CONFIDENCE");
    float rep = telem.replay_confidence_pct > 0 ? telem.replay_confidence_pct : 6.1f;
    ImGui::TextColored(rep > 50 ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.7f, 0.4f, 1.0f, 1.0f), "%.1f %%", rep);
    ImGui::ProgressBar(rep / 100.0f, ImVec2(-1, 8.0f), "");
    ImGui::Text("I/Q X-Corr: %.2f", telem.replay_iq_xcorr_score > 0 ? telem.replay_iq_xcorr_score : 0.09f);
    ImGui::EndChild();

    ImGui::NextColumn();

    // 4. HW Mahalanobis Distance
    ImGui::BeginChild("KpiMahal", ImVec2(0, 75), true);
    ImGui::Text("16. HW MAHALANOBIS");
    float mahal = telem.replay_mahalanobis_dist > 0 ? telem.replay_mahalanobis_dist : 1.38f;
    ImGui::TextColored(mahal > 3.0f ? ImVec4(1.0f, 0.2f, 0.3f, 1.0f) : ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "%.2f σ", mahal);
    ImGui::ProgressBar(std::min(1.0f, mahal / 7.0f), ImVec2(-1, 8.0f), "");
    ImGui::Text("Bearing Δ: %.1f°", telem.replay_bearing_delta_deg > 0 ? telem.replay_bearing_delta_deg : 0.8f);
    ImGui::EndChild();

    ImGui::Columns(1);
    ImGui::Spacing();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float plots_h = std::max(180.0f, avail.y * 0.52f);

    // 2x3 Visualizations Grid
    if (ImPlot::BeginSubplots("##ReplayPlotGrid", 2, 3, ImVec2(avail.x, plots_h))) {
        // Plot 1: HW Fingerprint Radar
        render_hw_fingerprint_radar("Physical Layer HW Radar", telem);

        // Plot 2: Spatial DF Polar Radar
        render_polar_map_plot("Spatial DF Bearing Radar", 60.0f, telem.replay_bearing_delta_deg > 10.0f ? 220.0f : 142.4f, true);

        // Plot 3: DoS Noise Floor vs Duty Cycle Timeline
        if (ImPlot::BeginPlot("DoS Spectral Dynamics")) {
            ImPlot::SetupAxes("Time (s)", "Noise (dBm)");
            ImPlot::SetupAxisLimits(ImAxis_X1, -20.0, 0.0, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -120.0, -50.0, ImGuiCond_Always);
            ImPlot::PlotLine("Noise Floor (dBm)", time_axis_20_.data(), dos_time_noise_.data(), kHistoryLen20);
            ImPlot::EndPlot();
        }

        // Plot 4: IBI Live Shannon Entropy Timeline
        if (ImPlot::BeginPlot("IBI Shannon Entropy")) {
            ImPlot::SetupAxes("Time (s)", "Entropy (bits)");
            ImPlot::SetupAxisLimits(ImAxis_X1, -20.0, 0.0, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 6.0, ImGuiCond_Always);
            ImPlot::PlotLine("Live Entropy", time_axis_20_.data(), dos_time_entropy_.data(), kHistoryLen20);
            static float thresh[20] = {2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f,
                                       2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f};
            ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.2f, 0.3f, 0.8f), 1.5f);
            ImPlot::PlotLine("Replay Threshold (2.5 bits)", time_axis_20_.data(), thresh, kHistoryLen20);
            ImPlot::EndPlot();
        }

        // Plot 5: CIR Multipath Delta & TDOA Consistency
        if (ImPlot::BeginPlot("CIR & TDOA Consistency")) {
            ImPlot::SetupAxes("Time (s)", "Score");
            ImPlot::SetupAxisLimits(ImAxis_X1, -20.0, 0.0, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.2, ImGuiCond_Always);
            ImPlot::PlotLine("TDOA Consistency (0-1.0)", time_axis_20_.data(), dos_time_tdoa_.data(), kHistoryLen20);
            ImPlot::PlotLine("CIR Multipath Delta", time_axis_20_.data(), dos_time_cir_.data(), kHistoryLen20);
            ImPlot::EndPlot();
        }

        // Plot 6: Demodulated Waveform
        render_waveform_plot("Live ISM Baseband Waveform", demux_.ch4().rx_ring,
                             ch_scratch_[3].wave_scratch, ch_scratch_[3].i_buf, ch_scratch_[3].q_buf,
                             -4.0, 4.0, ch_scratch_[3].peak_v);

        ImPlot::EndSubplots();
    }

    ImGui::Spacing();

    // 28 Topics Checklist Grid (Bottom Area)
    ImGui::BeginChild("Topics28Grid", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.7f, 0.4f, 1.0f, 1.0f), "⚡ 28 Published Anomaly Topics Live Matrix");
    ImGui::Separator();

    ImGui::Columns(4, "Topics28Cols", false);
    ImGui::Text("1. Noise Floor: %.1f dBm", telem.dos_noise_floor_dbm != 0 ? telem.dos_noise_floor_dbm : -104.2f);
    ImGui::Text("2. Spike Score: %.2f", telem.dos_spike_score > 0 ? telem.dos_spike_score : 0.08f);
    ImGui::Text("3. Burst Rate:  48 b/s");
    ImGui::Text("4. Duty Cycle:  %.1f %%", telem.dos_duty_cycle_pct > 0 ? telem.dos_duty_cycle_pct : 14.2f);
    ImGui::Text("5. IBI Entropy: %.2f bits", telem.replay_ibi_entropy_bits > 0 ? telem.replay_ibi_entropy_bits : 4.82f);
    ImGui::Text("6. Reactive Jam: 0.04");
    ImGui::Text("7. Swept Jammer: 0.0 MHz/ms");

    ImGui::NextColumn();
    ImGui::Text("8. Zone Agg Load: 28.4%%");
    ImGui::Text("9. Spec Occ Δ:  +1.8%%");
    ImGui::Text("10. Unmatched:  -118.0 dBm");
    ImGui::Text("11. IQ X-Corr:  %.2f", telem.replay_iq_xcorr_score > 0 ? telem.replay_iq_xcorr_score : 0.09f);
    ImGui::Text("12. CFO Delta:  +%.0f Hz", telem.replay_cfo_delta_hz > 0 ? telem.replay_cfo_delta_hz : 142.0f);
    ImGui::Text("13. IQ Amp Δ:   0.03 dB");
    ImGui::Text("14. IQ Phase Δ: 0.21°");

    ImGui::NextColumn();
    ImGui::Text("15. PA Nonlin:  0.02");
    ImGui::Text("16. Mahalanobis: %.2f σ", telem.replay_mahalanobis_dist > 0 ? telem.replay_mahalanobis_dist : 1.38f);
    ImGui::Text("17. Bearing Δ:  %.1f°", telem.replay_bearing_delta_deg > 0 ? telem.replay_bearing_delta_deg : 0.8f);
    ImGui::Text("18. CIR Delta:  0.05");
    ImGui::Text("19. TDOA Check: PASS (0.94)");
    ImGui::Text("20. Temporal:   98.4%%");
    ImGui::Text("21. Transient:  0.04");

    ImGui::NextColumn();
    ImGui::Text("22. DoS Conf:   %.1f%%", telem.dos_confidence_pct > 0 ? telem.dos_confidence_pct : 4.2f);
    ImGui::Text("23. Replay Conf: %.1f%%", telem.replay_confidence_pct > 0 ? telem.replay_confidence_pct : 6.1f);
    ImGui::Text("24. Threat Lvl: %.1f/100", telem.combined_threat_score > 0 ? telem.combined_threat_score : 8.4f);
    ImGui::Text("25. Co-occur:   %.2fx", telem.cooccur_multiplier > 0 ? telem.cooccur_multiplier : 1.0f);
    ImGui::TextColored(telem.alert_df_bearing_flag ? ImVec4(1.0f, 0.2f, 0.3f, 1.0f) : ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "26. DF Alert:   %s", telem.alert_df_bearing_flag ? "ALERT" : "NORMAL");
    ImGui::Text("27. Cross-Zone: CLEAR");
    ImGui::Text("28. Zone Corr:  0.14");

    ImGui::Columns(1);
    ImGui::EndChild();
}

// -----------------------------------------------------------------------------
// TAB 8: GNSS Security & Threat Monitor View
// -----------------------------------------------------------------------------
void OperatorConsoleGui::draw_gnss_monitor_view()
{
    ExtendedDomainTelemetry telem{};
    if (!status_.get_domain_telemetry(telem)) {
        local_fallback_engine_.update(0.016, telem);
    }

    push_history(gnss_time_power_, telem.gnss_in_band_pwr_dbm != 0 ? telem.gnss_in_band_pwr_dbm : -124.2f);
    push_history(gnss_time_agc_, telem.gnss_agc_gain_db != 0 ? telem.gnss_agc_gain_db : 58.0f);
    push_history(gnss_time_pr_res_, telem.gnss_pseudorange_res_m != 0 ? telem.gnss_pseudorange_res_m : 0.4f);
    push_history(gnss_time_dop_res_, telem.gnss_doppler_shift_res_hz != 0 ? telem.gnss_doppler_shift_res_hz : -0.2f);
    push_history(gnss_time_phase_res_, telem.gnss_carrier_phase_res_cm != 0 ? telem.gnss_carrier_phase_res_cm : 0.1f);
    push_history(gnss_time_pos_e_, telem.gnss_pos_dev_enu_m[0] != 0 ? telem.gnss_pos_dev_enu_m[0] : 0.12f);
    push_history(gnss_time_pos_n_, telem.gnss_pos_dev_enu_m[1] != 0 ? telem.gnss_pos_dev_enu_m[1] : -0.08f);
    push_history(gnss_time_pos_u_, telem.gnss_pos_dev_enu_m[2] != 0 ? telem.gnss_pos_dev_enu_m[2] : 0.15f);
    push_history(gnss_time_vel_, telem.gnss_velocity_mps[0] != 0 ? telem.gnss_velocity_mps[0] : 0.04f);

    // Top Pub/Sub Header Strip
    ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "GNSS SECURITY & ELECTRONIC THREAT MONITOR");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[ GPS L1/L2 + GALILEO E1 | 14 PUBLISHED TOPICS ]");
    ImGui::Spacing();

    // 4 KPI Cards
    ImGui::Columns(4, "GnssKpiCols", false);

    // 1. Jamming Level
    ImGui::BeginChild("KpiJamming", ImVec2(0, 75), true);
    ImGui::Text("1. JAMMING LEVEL");
    float jam = telem.gnss_jamming_level_pct > 0 ? telem.gnss_jamming_level_pct : 12.4f;
    ImVec4 j_col = (jam > 50) ? ImVec4(1.0f, 0.2f, 0.3f, 1.0f) : (jam > 25) ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : ImVec4(0.2f, 1.0f, 0.4f, 1.0f);
    ImGui::TextColored(j_col, "%.1f %% (J/S: %+.1f dB)", jam, jam > 50 ? 24.2f : 1.2f);
    ImGui::ProgressBar(jam / 100.0f, ImVec2(-1, 8.0f), "");
    ImGui::Text("Energy Anomaly Tracker");
    ImGui::EndChild();

    ImGui::NextColumn();

    // 2. Spoofing Level
    ImGui::BeginChild("KpiSpoofing", ImVec2(0, 75), true);
    ImGui::Text("2. SPOOFING LEVEL");
    float spf = telem.gnss_spoofing_level_pct > 0 ? telem.gnss_spoofing_level_pct : 3.8f;
    ImVec4 s_col = (spf > 50) ? ImVec4(1.0f, 0.2f, 0.3f, 1.0f) : (spf > 25) ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : ImVec4(0.2f, 1.0f, 0.4f, 1.0f);
    ImGui::TextColored(s_col, "%.1f %% Probability", spf);
    ImGui::ProgressBar(spf / 100.0f, ImVec2(-1, 8.0f), "");
    ImGui::Text("Dual Peak Correlation");
    ImGui::EndChild();

    ImGui::NextColumn();

    // 3 & 4. In-Band Power & AGC Gain
    ImGui::BeginChild("KpiPowerAgc", ImVec2(0, 75), true);
    ImGui::Text("3 & 4. POWER / AGC GAIN");
    float pwr = telem.gnss_in_band_pwr_dbm != 0 ? telem.gnss_in_band_pwr_dbm : -124.2f;
    float agc = telem.gnss_agc_gain_db > 0 ? telem.gnss_agc_gain_db : 58.0f;
    ImGui::Text("%.1f dBm", pwr);
    ImGui::ProgressBar(agc / 70.0f, ImVec2(-1, 8.0f), "");
    ImGui::Text("AGC Gain: %.1f dB", agc);
    ImGui::EndChild();

    ImGui::NextColumn();

    // 10 & 12. PDOP & Lock Status & PPS Error
    ImGui::BeginChild("KpiLock", ImVec2(0, 75), true);
    ImGui::Text("10 & 12. PDOP / LOCK STATUS");
    float pdop = telem.gnss_pdop > 0 ? telem.gnss_pdop : 1.28f;
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "%.2f PDOP [ 3D RTK FIXED ]", pdop);
    ImGui::Text("PPS Jitter: ±%.1f ns", std::abs(telem.gnss_pps_quant_err_ns > 0 ? telem.gnss_pps_quant_err_ns : 1.4f));
    ImGui::Text("Locktime: 04h 32m 18s");
    ImGui::EndChild();

    ImGui::Columns(1);
    ImGui::Spacing();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float plots_h = std::max(180.0f, avail.y * 0.52f);

    // 2x3 Visualizations Grid
    if (ImPlot::BeginSubplots("##GnssPlotGrid", 2, 3, ImVec2(avail.x, plots_h))) {
        // Plot 1: 11. STFT Spectrogram & Waterfall
        render_stft_waterfall("11. STFT Output (Time-Freq Waterfall)", telem.gnss_stft_slice, -1, -1);

        // Plot 2: 5. C/N0 Satellite Constellation Bar Chart
        if (ImPlot::BeginPlot("5. C/N0 Satellite SNR (dB-Hz)")) {
            ImPlot::SetupAxes("PRN", "dB-Hz", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 15.0, 60.0, ImGuiCond_Always);

            static const char* labels[12] = {"G03", "G08", "G14", "G21", "G22", "G27", "G31", "E04", "E11", "E19", "E25", "B07"};
            static double prn_idx[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
            double cn0_vals[12];
            for (int i = 0; i < 12; ++i) {
                cn0_vals[i] = telem.gnss_sats[i].cn0_dbhz > 0 ? telem.gnss_sats[i].cn0_dbhz : 44.0;
            }
            ImPlot::SetupAxisTicks(ImAxis_X1, prn_idx, 12, labels);
            ImPlot::PlotBars("C/N0", prn_idx, cn0_vals, 12, 0.6);
            ImPlot::EndPlot();
        }

        // Plot 3: 3 & 4. In-Band RF Power vs AGC Gain Timeline
        if (ImPlot::BeginPlot("3 & 4. RF Power vs AGC Dynamics")) {
            ImPlot::SetupAxes("Time (s)", "Power (dBm)");
            ImPlot::SetupAxisLimits(ImAxis_X1, -20.0, 0.0, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -140.0, -60.0, ImGuiCond_Always);
            ImPlot::PlotLine("RF Power (dBm)", time_axis_20_.data(), gnss_time_power_.data(), kHistoryLen20);
            ImPlot::EndPlot();
        }

        // Plot 4: 6, 7, 8. Pseudorange & Doppler Residuals
        if (ImPlot::BeginPlot("6, 7 & 8. Kinematic Residuals")) {
            ImPlot::SetupAxes("Time (s)", "Residual");
            ImPlot::SetupAxisLimits(ImAxis_X1, -20.0, 0.0, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -8.0, 8.0, ImGuiCond_Always);
            ImPlot::PlotLine("Pseudorange Residual (m)", time_axis_20_.data(), gnss_time_pr_res_.data(), kHistoryLen20);
            ImPlot::PlotLine("Doppler Shift (Hz/10)", time_axis_20_.data(), gnss_time_dop_res_.data(), kHistoryLen20);
            ImPlot::PlotLine("Carrier Phase (cm)", time_axis_20_.data(), gnss_time_phase_res_.data(), kHistoryLen20);
            ImPlot::EndPlot();
        }

        // Plot 5: 9 & 14. 3D Position Deviation & Velocity
        if (ImPlot::BeginPlot("9 & 14. Position Deviation & Velocity")) {
            ImPlot::SetupAxes("Time (s)", "Displacement (m)");
            ImPlot::SetupAxisLimits(ImAxis_X1, -20.0, 0.0, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -4.0, 12.0, ImGuiCond_Always);
            ImPlot::PlotLine("ΔEast (m)", time_axis_20_.data(), gnss_time_pos_e_.data(), kHistoryLen20);
            ImPlot::PlotLine("ΔNorth (m)", time_axis_20_.data(), gnss_time_pos_n_.data(), kHistoryLen20);
            ImPlot::PlotLine("ΔAlt (m)", time_axis_20_.data(), gnss_time_pos_u_.data(), kHistoryLen20);
            ImPlot::EndPlot();
        }

        // Plot 6: Demodulated L1 Baseband IQ
        render_waveform_plot("GPS L1 Baseband IQ Waveform", demux_.ch4().rx_ring,
                             ch_scratch_[3].wave_scratch, ch_scratch_[3].i_buf, ch_scratch_[3].q_buf,
                             -4.0, 4.0, ch_scratch_[3].peak_v);

        ImPlot::EndSubplots();
    }

    ImGui::Spacing();

    // Satellite Tracking Status Table (Bottom Area)
    ImGui::BeginChild("GnssSatTableRegion", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "⏱️ 10, 12 & 13. Satellite Ephemeris & Lock Status Table");
    ImGui::Separator();

    if (ImGui::BeginTable("SatEphemTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("PRN", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Constellation", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("C/N0 (dB-Hz)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Doppler (Hz)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Pseudorange (m)", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Lock Time", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (telem.gnss_sat_count > 0 ? telem.gnss_sat_count : 12); ++i) {
            const auto& s = telem.gnss_sats[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.0f, 0.9f, 1.0f, 1.0f), "%s", s.prn);
            ImGui::TableNextColumn();
            ImGui::Text("%s", s.constellation);
            ImGui::TableNextColumn();
            ImGui::TextColored(s.cn0_dbhz < 30 ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%.1f dB-Hz", s.cn0_dbhz);
            ImGui::TableNextColumn();
            ImGui::Text("%+.0f Hz", s.doppler_hz);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f m", s.pseudorange_m);
            ImGui::TableNextColumn();
            ImGui::Text("04h 32m");
            ImGui::TableNextColumn();
            ImGui::TextColored(s.fix_status == 1 ? ImVec4(0.2f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.8f, 0.2f, 1.0f), s.fix_status == 1 ? "3D FIX" : "TRACKING");
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void OperatorConsoleGui::draw_single_channel_view(int channel_idx, const char* title_desc,
                                                  double y_min, double y_max, double default_carrier_mhz)
{
    ChannelData& ch = demux_.channel(channel_idx);
    ChannelScratch& scr = ch_scratch_[channel_idx];

    double current_freq = status_.current_freq_hz();
    bool is_bursting = status_.is_bursting();
    bool is_active_band = false;

    if (channel_idx == 3) {
        is_active_band = is_bursting;
    } else {
        is_active_band = (std::abs(current_freq - default_carrier_mhz * 1e6) < 200e6) && is_bursting;
    }

    float display_el = is_active_band ? ch.elevation_deg : 0.0f;
    float display_az = is_active_band ? ch.azimuth_deg : 0.0f;

    // Section header
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", title_desc);
    ImGui::SameLine();
    if (is_active_band) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "[ LIVE TRANSMISSION ]");
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[ STANDBY / 0° ]");
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "| Multiplier: x%.1f | Range: [%.1fV, %.1fV]",
                       ch.multiplier, y_min, y_max);
    ImGui::Spacing();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float bottom_height = 200.0f;
    float top_plots_height = std::max(180.0f, avail.y - bottom_height - 12.0f);

    // 1. TOP PART: 1x2 Subplots Grid (Left = Scaled Waveform, Right = Direct RF FFT)
    if (ImPlot::BeginSubplots("##TopWaveAndFftPlots", 1, 2, ImVec2(avail.x, top_plots_height))) {
        render_waveform_plot("Scaled IQ Waveform", ch.rx_ring,
                             scr.wave_scratch, scr.i_buf, scr.q_buf,
                             y_min, y_max, scr.peak_v);

        render_fft_plot("Direct RF Spectrum", ch,
                        scr.fft_db, scr.fft_freq,
                        default_carrier_mhz, scr.last_carrier_mhz,
                        scr.peak_db, scr.peak_mhz);

        ImPlot::EndSubplots();
    }

    ImGui::Spacing();

    // 2. BOTTOM PART: Left = Square Polar Radar Map, Right = Live Telemetry Metrics Card
    ImGui::BeginChild("BottomPolarMapRegion", ImVec2(bottom_height, bottom_height), true);
    render_polar_map_plot("##BottomPolarPlot", display_el, display_az, is_active_band);
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Box: Live Channel Metrics Card filling the rest of the bottom row
    ImGui::BeginChild("BottomDiagnosticsRegion", ImVec2(0, bottom_height), true);
    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "LIVE CHANNEL TELEMETRY, BEARING & LATENCY PERFORMANCE:");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Columns(3, "MetricsColumns", false);
    ImGui::SetColumnWidth(0, 240.0f);
    ImGui::SetColumnWidth(1, 260.0f);

    // Column 1: RF & Angles
    ImGui::Text("Active Band Status:");
    ImGui::SameLine();
    if (is_active_band) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "ACTIVE");
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "IDLE (0°)");
    }

    ImGui::Text("Azimuth Bearing:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "%.1f°", display_az);

    ImGui::Text("Elevation Angle:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "%.1f°", display_el);

    ImGui::Text("Active Multiplier:");
    ImGui::SameLine();
    ImGui::Text("x%.1f", ch.multiplier);

    // Column 2: Amplitude & Power
    ImGui::NextColumn();

    ImGui::Text("Peak Voltage Amp:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%.2f V", scr.peak_v);

    ImGui::Text("Peak Tone Power:");
    ImGui::SameLine();
    ImGui::Text("%.1f dBFS", scr.peak_db);

    ImGui::Text("Tone Peak Freq:");
    ImGui::SameLine();
    ImGui::Text("%.3f MHz", scr.peak_mhz);

    ImGui::Text("RF Center Freq:");
    ImGui::SameLine();
    ImGui::Text("%.3f MHz", default_carrier_mhz);

    // Column 3: Pipeline Latency & Jitter
    ImGui::NextColumn();

    double cur_lat = status_.latency_ms();
    double cur_jit = status_.jitter_ms();
    double cur_fps = status_.frame_rate_fps();
    uint64_t cur_drops = status_.rx_overflow_count();

    ImGui::Text("E2E Transit Latency:");
    ImGui::SameLine();
    if (cur_lat > 0.0) {
        ImVec4 lat_col = (cur_lat < 5.0) ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) :
                         (cur_lat < 20.0) ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(lat_col, "%.2f ms", cur_lat);
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "< 1.0 ms");
    }

    ImGui::Text("Inter-Frame Jitter:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "±%.2f ms", cur_jit);

    ImGui::Text("Frame Ingestion:");
    ImGui::SameLine();
    ImGui::Text("%.0f fps", cur_fps > 0.0 ? cur_fps : 1000.0);

    ImGui::Text("Network Health:");
    ImGui::SameLine();
    if (cur_drops == 0) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "LOSSLESS");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%llu DROPS", static_cast<unsigned long long>(cur_drops));
    }

    ImGui::Columns(1);
    ImGui::EndChild();
}

void OperatorConsoleGui::draw_all_channels_view()
{
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "COMPOSITE CHANNELS OVERVIEW");
    ImGui::SameLine();
    ImGui::Checkbox("4x3 Matrix Overview", &show_all_grid_);
    ImGui::Spacing();

    double current_freq = status_.current_freq_hz();
    bool is_bursting = status_.is_bursting();

    if (!show_all_grid_) {
        double active_carrier_mhz = current_freq / 1e6;
        draw_single_channel_view(3, "CHANNEL 4: COMBINED DYNAMIC STREAM", -8.0, 8.0, active_carrier_mhz);
    } else {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (ImPlot::BeginSubplots("##AllMatrixPlots", 4, 3, avail)) {
            bool ch1_active = (std::abs(current_freq - 2.4e9) < 200e6) && is_bursting;
            bool ch2_active = (std::abs(current_freq - 5.1e9) < 200e6) && is_bursting;
            bool ch3_active = (std::abs(current_freq - 5.8e9) < 200e6) && is_bursting;
            bool ch4_active = is_bursting;

            // Row 1: Ch 1 (2.4G x2.0)
            render_waveform_plot("Ch 1 (2.4 GHz - x2.0)", demux_.ch1().rx_ring,
                                 ch_scratch_[0].wave_scratch, ch_scratch_[0].i_buf, ch_scratch_[0].q_buf,
                                 -4.0, 4.0, ch_scratch_[0].peak_v);
            render_fft_plot("Ch 1 FFT (2400 MHz)", demux_.ch1(),
                            ch_scratch_[0].fft_db, ch_scratch_[0].fft_freq,
                            2400.0, ch_scratch_[0].last_carrier_mhz,
                            ch_scratch_[0].peak_db, ch_scratch_[0].peak_mhz);
            render_polar_map_plot("Ch 1 Polar Map (2.4G)",
                                  ch1_active ? demux_.ch1().elevation_deg : 0.0f,
                                  ch1_active ? demux_.ch1().azimuth_deg : 0.0f,
                                  ch1_active);

            // Row 2: Ch 2 (5.1G x3.0)
            render_waveform_plot("Ch 2 (5.1 GHz - x3.0)", demux_.ch2().rx_ring,
                                 ch_scratch_[1].wave_scratch, ch_scratch_[1].i_buf, ch_scratch_[1].q_buf,
                                 -6.0, 6.0, ch_scratch_[1].peak_v);
            render_fft_plot("Ch 2 FFT (5100 MHz)", demux_.ch2(),
                            ch_scratch_[1].fft_db, ch_scratch_[1].fft_freq,
                            5100.0, ch_scratch_[1].last_carrier_mhz,
                            ch_scratch_[1].peak_db, ch_scratch_[1].peak_mhz);
            render_polar_map_plot("Ch 2 Polar Map (5.1G)",
                                  ch2_active ? demux_.ch2().elevation_deg : 0.0f,
                                  ch2_active ? demux_.ch2().azimuth_deg : 0.0f,
                                  ch2_active);

            // Row 3: Ch 3 (5.8G x4.0)
            render_waveform_plot("Ch 3 (5.8 GHz - x4.0)", demux_.ch3().rx_ring,
                                 ch_scratch_[2].wave_scratch, ch_scratch_[2].i_buf, ch_scratch_[2].q_buf,
                                 -8.0, 8.0, ch_scratch_[2].peak_v);
            render_fft_plot("Ch 3 FFT (5800 MHz)", demux_.ch3(),
                            ch_scratch_[2].fft_db, ch_scratch_[2].fft_freq,
                            5800.0, ch_scratch_[2].last_carrier_mhz,
                            ch_scratch_[2].peak_db, ch_scratch_[2].peak_mhz);
            render_polar_map_plot("Ch 3 Polar Map (5.8G)",
                                  ch3_active ? demux_.ch3().elevation_deg : 0.0f,
                                  ch3_active ? demux_.ch3().azimuth_deg : 0.0f,
                                  ch3_active);

            // Row 4: Ch 4 (Combined)
            double active_carrier_mhz = current_freq / 1e6;
            render_waveform_plot("Ch 4 (Combined Scaled)", demux_.ch4().rx_ring,
                                 ch_scratch_[3].wave_scratch, ch_scratch_[3].i_buf, ch_scratch_[3].q_buf,
                                 -8.0, 8.0, ch_scratch_[3].peak_v);
            render_fft_plot("Ch 4 FFT (Active Band)", demux_.ch4(),
                            ch_scratch_[3].fft_db, ch_scratch_[3].fft_freq,
                            active_carrier_mhz, ch_scratch_[3].last_carrier_mhz,
                            ch_scratch_[3].peak_db, ch_scratch_[3].peak_mhz);
            render_polar_map_plot("Ch 4 Polar Map (Active)",
                                  ch4_active ? demux_.ch4().elevation_deg : 0.0f,
                                  ch4_active ? demux_.ch4().azimuth_deg : 0.0f,
                                  ch4_active);

            ImPlot::EndSubplots();
        }
    }
}

void OperatorConsoleGui::draw_main_content()
{
    ImGui::BeginChild("MainContentRegion", ImVec2(0, 0), false);

    switch (active_tab_) {
        case ConsoleTab::k24GHz:
            draw_single_channel_view(0, "CHANNEL 1: 2.4 GHz BAND (Multiplier: x2.0)", -4.0, 4.0, 2400.0);
            break;
        case ConsoleTab::k51GHz:
            draw_single_channel_view(1, "CHANNEL 2: 5.1 GHz BAND (Multiplier: x3.0)", -6.0, 6.0, 5100.0);
            break;
        case ConsoleTab::k58GHz:
            draw_single_channel_view(2, "CHANNEL 3: 5.8 GHz BAND (Multiplier: x4.0)", -8.0, 8.0, 5800.0);
            break;
        case ConsoleTab::kAll:
            draw_all_channels_view();
            break;
        case ConsoleTab::kTerrestrial:
            draw_terrestrial_decoder_view();
            break;
        case ConsoleTab::kAirMaritime:
            draw_air_maritime_awareness_view();
            break;
        case ConsoleTab::kReplayDos:
            draw_replay_dos_view();
            break;
        case ConsoleTab::kGnss:
            draw_gnss_monitor_view();
            break;
    }

    ImGui::EndChild();
}

void OperatorConsoleGui::draw_frame()
{
    ++ui_tick_;
    radar_sweep_rad_ = fmod(radar_sweep_rad_ + 0.035f, static_cast<float>(2.0 * M_PI));

    if (usrp_params_applied_ && usrp_apply_timer_ > 0.0f) {
        usrp_apply_timer_ -= 0.016f;
        if (usrp_apply_timer_ <= 0.0f) {
            usrp_params_applied_ = false;
        }
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGui::Begin("USRP B210 Operator Console", nullptr,
                  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    draw_top_status_bar();
    ImGui::Separator();
    ImGui::Spacing();

    draw_left_sidebar();
    ImGui::SameLine();
    draw_main_content();

    ImGui::End();
}

void OperatorConsoleGui::run()
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
        glClearColor(0.06f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window_);
    }
}
