// operator_console_gui.cpp

#include "operator_console_gui.h"

#include <algorithm>
#include <cmath>
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

    window_ = glfwCreateWindow(1440, 900, window_title_.c_str(), nullptr, nullptr);
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
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.18f, 0.22f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.30f, 0.44f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.38f, 0.56f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.16f, 0.20f, 0.28f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.30f, 0.42f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.36f, 0.50f, 1.00f);

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

    // Active band indicator
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "RF CARRIER:");
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

    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "ACTIVE MULTIPLIER: x%.1f", active_mult);

    ImGui::SameLine(0, 16.0f);
    ImGui::Text("| Rate: %.1f MS/s (8.0 MB/s)", sample_rate_hz_ / 1e6);

    ImGui::SameLine(0, 16.0f);
    ImGui::Text("| Bursts: %llu", static_cast<unsigned long long>(status_.burst_count()));

    ImGui::SameLine(0, 16.0f);
    if (drops == 0) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "| Loss: 0 Drops (0.00%% Lossless)");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "| Loss: %llu Drops", static_cast<unsigned long long>(drops));
    }
}

void OperatorConsoleGui::draw_left_sidebar()
{
    ImGui::BeginChild("SidebarRegion", ImVec2(240, 0), true);

    ImGui::TextColored(ImVec4(0.8f, 0.85f, 0.95f, 1.0f), "OPERATOR CHANNELS");
    ImGui::Separator();
    ImGui::Spacing();

    double current_freq = status_.current_freq_hz();

    // Helper for rendering styled sidebar buttons
    auto draw_tab_button = [&](ConsoleTab tab, const char* label, const char* sublabel, double target_freq_hz) {
        bool is_selected = (active_tab_ == tab);
        bool is_band_active = (std::abs(current_freq - target_freq_hz) < 200e6);

        if (is_selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.35f, 0.60f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.40f, 0.68f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.15f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.24f, 0.34f, 1.0f));
        }

        ImVec2 btn_size = ImVec2(ImGui::GetContentRegionAvail().x, 56.0f);
        std::string btn_id = std::string(label) + "##btn";
        if (ImGui::Button(btn_id.c_str(), btn_size)) {
            active_tab_ = tab;
        }

        // Overlay text onto the button
        ImVec2 p_min = ImGui::GetItemRectMin();
        ImVec2 p_max = ImGui::GetItemRectMax();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        // Main channel title
        ImVec4 text_col = is_selected ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        draw_list->AddText(ImVec2(p_min.x + 12.0f, p_min.y + 8.0f), ImGui::ColorConvertFloat4ToU32(text_col), label);

        // Sublabel (Multiplier / Details)
        draw_list->AddText(ImVec2(p_min.x + 12.0f, p_min.y + 30.0f),
                           ImGui::ColorConvertFloat4ToU32(ImVec4(0.6f, 0.65f, 0.75f, 1.0f)), sublabel);

        // Status badge on right side of button
        if (tab != ConsoleTab::kAll) {
            const char* badge_text = is_band_active ? "LIVE" : "IDLE";
            ImU32 badge_bg = is_band_active ? IM_COL32(40, 160, 60, 220) : IM_COL32(70, 75, 85, 180);
            ImVec2 badge_pos = ImVec2(p_max.x - 52.0f, p_min.y + 16.0f);
            draw_list->AddRectFilled(badge_pos, ImVec2(badge_pos.x + 42.0f, badge_pos.y + 22.0f), badge_bg, 4.0f);
            draw_list->AddText(ImVec2(badge_pos.x + 8.0f, badge_pos.y + 3.0f), IM_COL32(255, 255, 255, 255), badge_text);
        }

        ImGui::PopStyleColor(2);
        ImGui::Spacing();
    };

    draw_tab_button(ConsoleTab::k24GHz, "2.4 GHz Band", "Multiplier: x2.0", 2.4e9);
    draw_tab_button(ConsoleTab::k51GHz, "5.1 GHz Band", "Multiplier: x3.0", 5.1e9);
    draw_tab_button(ConsoleTab::k58GHz, "5.8 GHz Band", "Multiplier: x4.0", 5.8e9);
    draw_tab_button(ConsoleTab::kAll,   "All Channels", "Composite Stream", 0.0);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Node Information Box
    ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "SYSTEM STATUS");
    ImGui::Text("Node: System 3 (Sink)");
    ImGui::Text("Protocol: sc16 lossless");
    ImGui::Text("Listen Port: 6001");
    ImGui::Text("Stream: S1 -> S2 -> S3");

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
        ImPlot::SetupAxes("Sample Index", "Voltage Amplitude (V)");
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
            // Convert (Elevation, Azimuth) to Cartesian coordinates (Azimuth clockwise from North / Y-axis)
            float rad = azimuth_deg * static_cast<float>(M_PI / 180.0);
            float target_r = elevation_deg;
            float target_x = target_r * std::sin(rad);
            float target_y = target_r * std::cos(rad);

            // Bearing vector line
            float line_x[2] = {0.0f, target_x};
            float line_y[2] = {0.0f, target_y};
            ImPlot::SetNextLineStyle(ImVec4(0.2f, 1.0f, 0.4f, 0.9f), 2.0f);
            ImPlot::PlotLine("Bearing", line_x, line_y, 2);

            // Target marker
            float pt_x[1] = {target_x};
            float pt_y[1] = {target_y};
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 8.0f, ImVec4(1.0f, 0.3f, 0.2f, 1.0f), 2.0f, ImVec4(1.0f, 0.9f, 0.2f, 1.0f));
            char pt_label[64];
            std::snprintf(pt_label, sizeof(pt_label), "Target (El: %.0f°, Az: %.0f°)", elevation_deg, azimuth_deg);
            ImPlot::PlotScatter(pt_label, pt_x, pt_y, 1);
        } else {
            // Idle state: marker sits at origin (0, 0)
            float pt_x[1] = {0.0f};
            float pt_y[1] = {0.0f};
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5.0f, ImVec4(0.5f, 0.5f, 0.6f, 0.6f), 1.0f, ImVec4(0.3f, 0.3f, 0.4f, 0.8f));
            ImPlot::PlotScatter("Idle (0°, 0°)", pt_x, pt_y, 1);
        }

        ImPlot::EndPlot();
    }
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
    // Top part: Split into 1x2 for Waveform & FFT (takes upper majority)
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
    // Left Box: Square Polar Map (width = bottom_height, height = bottom_height)
    ImGui::BeginChild("BottomPolarMapRegion", ImVec2(bottom_height, bottom_height), true);
    render_polar_map_plot("##BottomPolarPlot", display_el, display_az, is_active_band);
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Box: Live Channel Metrics Card filling the rest of the bottom row
    ImGui::BeginChild("BottomDiagnosticsRegion", ImVec2(0, bottom_height), true);
    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "LIVE CHANNEL TELEMETRY & BEARING:");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Columns(2, "MetricsColumns", false);
    ImGui::SetColumnWidth(0, 260.0f);

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

    ImGui::NextColumn();

    ImGui::Text("Peak Voltage Amp:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%.2f V", scr.peak_v);

    ImGui::Text("Peak Tone Power:");
    ImGui::SameLine();
    ImGui::Text("%.1f dBFS", scr.peak_db);

    ImGui::Text("Tone Peak Frequency:");
    ImGui::SameLine();
    ImGui::Text("%.3f MHz", scr.peak_mhz);

    ImGui::Text("RF Center Frequency:");
    ImGui::SameLine();
    ImGui::Text("%.3f MHz", default_carrier_mhz);

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
        // Continuous composite stream view (Split Top Waveform/FFT, Bottom Square Polar Map)
        double active_carrier_mhz = current_freq / 1e6;
        draw_single_channel_view(3, "CHANNEL 4: COMBINED DYNAMIC STREAM", -8.0, 8.0, active_carrier_mhz);
    } else {
        // 4x3 matrix overview of all 4 channels (12 plots)
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
    }

    ImGui::EndChild();
}

void OperatorConsoleGui::draw_frame()
{
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
        glClearColor(0.06f, 0.07f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window_);
    }
}
