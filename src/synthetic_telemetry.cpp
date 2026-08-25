// synthetic_telemetry.cpp

#include "synthetic_telemetry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
float rand_f(float min_v, float max_v)
{
    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return min_v + r * (max_v - min_v);
}
} // namespace

SyntheticTelemetryEngine::SyntheticTelemetryEngine()
{
    std::memset(&state_, 0, sizeof(state_));
    state_.magic = kTelemetryMagic;
    state_.version = 1;

    init_contacts();
    init_gnss_sats();
    init_modbus_regs();
}

void SyntheticTelemetryEngine::set_scenario(ThreatScenario scenario)
{
    std::lock_guard<std::mutex> lock(mutex_);
    scenario_ = scenario;
}

ThreatScenario SyntheticTelemetryEngine::scenario() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return scenario_;
}

void SyntheticTelemetryEngine::init_contacts()
{
    state_.air_contact_count = 4;
    // 1. UAL1244
    std::snprintf(state_.air_contacts[0].callsign, sizeof(state_.air_contacts[0].callsign), "UAL1244");
    std::snprintf(state_.air_contacts[0].icao, sizeof(state_.air_contacts[0].icao), "A4B291");
    state_.air_contacts[0].lat = 37.8912f;
    state_.air_contacts[0].lon = -122.1402f;
    state_.air_contacts[0].alt_ft = 34000.0f;
    state_.air_contacts[0].speed_kts = 460.0f;
    state_.air_contacts[0].heading_deg = 110.0f;
    std::snprintf(state_.air_contacts[0].squawk, sizeof(state_.air_contacts[0].squawk), "SQK 4321");
    state_.air_contacts[0].rssi_dbm = -68.0f;
    state_.air_contacts[0].msg_count = 1420;
    state_.air_contacts[0].vx = 1.2f;
    state_.air_contacts[0].vy = -0.4f;

    // 2. SWA812
    std::snprintf(state_.air_contacts[1].callsign, sizeof(state_.air_contacts[1].callsign), "SWA812");
    std::snprintf(state_.air_contacts[1].icao, sizeof(state_.air_contacts[1].icao), "A1C802");
    state_.air_contacts[1].lat = 37.6190f;
    state_.air_contacts[1].lon = -122.3748f;
    state_.air_contacts[1].alt_ft = 12400.0f;
    state_.air_contacts[1].speed_kts = 280.0f;
    state_.air_contacts[1].heading_deg = 225.0f;
    std::snprintf(state_.air_contacts[1].squawk, sizeof(state_.air_contacts[1].squawk), "SQK 1200");
    state_.air_contacts[1].rssi_dbm = -58.0f;
    state_.air_contacts[1].msg_count = 2110;
    state_.air_contacts[1].vx = 0.8f;
    state_.air_contacts[1].vy = 0.6f;

    // 3. DAL405
    std::snprintf(state_.air_contacts[2].callsign, sizeof(state_.air_contacts[2].callsign), "DAL405");
    std::snprintf(state_.air_contacts[2].icao, sizeof(state_.air_contacts[2].icao), "A890F3");
    state_.air_contacts[2].lat = 38.0120f;
    state_.air_contacts[2].lon = -122.6500f;
    state_.air_contacts[2].alt_ft = 28500.0f;
    state_.air_contacts[2].speed_kts = 420.0f;
    state_.air_contacts[2].heading_deg = 280.0f;
    std::snprintf(state_.air_contacts[2].squawk, sizeof(state_.air_contacts[2].squawk), "SQK 5514");
    state_.air_contacts[2].rssi_dbm = -71.0f;
    state_.air_contacts[2].msg_count = 950;
    state_.air_contacts[2].vx = -1.1f;
    state_.air_contacts[2].vy = 0.3f;

    // 4. UAV-GUARD
    std::snprintf(state_.air_contacts[3].callsign, sizeof(state_.air_contacts[3].callsign), "UAV-GUARD");
    std::snprintf(state_.air_contacts[3].icao, sizeof(state_.air_contacts[3].icao), "A9FF01");
    state_.air_contacts[3].lat = 37.7740f;
    state_.air_contacts[3].lon = -122.4200f;
    state_.air_contacts[3].alt_ft = 1500.0f;
    state_.air_contacts[3].speed_kts = 65.0f;
    state_.air_contacts[3].heading_deg = 340.0f;
    std::snprintf(state_.air_contacts[3].squawk, sizeof(state_.air_contacts[3].squawk), "SQK 0024");
    state_.air_contacts[3].rssi_dbm = -48.0f;
    state_.air_contacts[3].msg_count = 4890;
    state_.air_contacts[3].vx = 0.3f;
    state_.air_contacts[3].vy = -0.2f;

    // Maritime Contacts
    state_.sea_contact_count = 3;
    // 1. PACIFIC VOYAGER
    std::snprintf(state_.sea_contacts[0].name, sizeof(state_.sea_contacts[0].name), "PACIFIC VOYAGER");
    std::snprintf(state_.sea_contacts[0].mmsi, sizeof(state_.sea_contacts[0].mmsi), "368124000");
    state_.sea_contacts[0].lat = 37.6401f;
    state_.sea_contacts[0].lon = -122.5890f;
    state_.sea_contacts[0].draft_m = 12.4f;
    state_.sea_contacts[0].speed_kts = 18.2f;
    state_.sea_contacts[0].heading_deg = 130.0f;
    std::snprintf(state_.sea_contacts[0].nav_status, sizeof(state_.sea_contacts[0].nav_status), "Underway (Motor)");
    state_.sea_contacts[0].rssi_dbm = -74.0f;
    state_.sea_contacts[0].msg_count = 380;
    state_.sea_contacts[0].vx = 0.2f;
    state_.sea_contacts[0].vy = 0.1f;

    // 2. EVER GALAXY
    std::snprintf(state_.sea_contacts[1].name, sizeof(state_.sea_contacts[1].name), "EVER GALAXY");
    std::snprintf(state_.sea_contacts[1].mmsi, sizeof(state_.sea_contacts[1].mmsi), "413290000");
    state_.sea_contacts[1].lat = 37.5200f;
    state_.sea_contacts[1].lon = -122.7100f;
    state_.sea_contacts[1].draft_m = 14.8f;
    state_.sea_contacts[1].speed_kts = 15.4f;
    state_.sea_contacts[1].heading_deg = 250.0f;
    std::snprintf(state_.sea_contacts[1].nav_status, sizeof(state_.sea_contacts[1].nav_status), "Underway");
    state_.sea_contacts[1].rssi_dbm = -82.0f;
    state_.sea_contacts[1].msg_count = 194;
    state_.sea_contacts[1].vx = -0.15f;
    state_.sea_contacts[1].vy = -0.05f;

    // 3. USCG CUTTER 752
    std::snprintf(state_.sea_contacts[2].name, sizeof(state_.sea_contacts[2].name), "USCG CUTTER 752");
    std::snprintf(state_.sea_contacts[2].mmsi, sizeof(state_.sea_contacts[2].mmsi), "369970000");
    state_.sea_contacts[2].lat = 37.7900f;
    state_.sea_contacts[2].lon = -122.4800f;
    state_.sea_contacts[2].draft_m = 4.2f;
    state_.sea_contacts[2].speed_kts = 26.0f;
    state_.sea_contacts[2].heading_deg = 45.0f;
    std::snprintf(state_.sea_contacts[2].nav_status, sizeof(state_.sea_contacts[2].nav_status), "Restricted Ops");
    state_.sea_contacts[2].rssi_dbm = -52.0f;
    state_.sea_contacts[2].msg_count = 840;
    state_.sea_contacts[2].vx = 0.4f;
    state_.sea_contacts[2].vy = -0.3f;

    state_.usrp_primary_freq_mhz = 1090.000;
    state_.usrp_secondary_freq_mhz = 162.000;
    state_.usrp_rx_gain_db = 54.0f;
    state_.usrp_sample_rate_msps = 10.0f;
    state_.usrp_proto_mode = 0; // DUAL
    state_.sdr_msg_rate_per_min = 2410;
}

void SyntheticTelemetryEngine::init_gnss_sats()
{
    state_.gnss_sat_count = 12;
    const char* prns[] = {"G03", "G08", "G14", "G21", "G22", "G27", "G31", "E04", "E11", "E19", "E25", "B07"};
    const char* consts[] = {"GPS L1 C/A", "GPS L1 C/A", "GPS L1 C/A", "GPS L1 C/A", "GPS L1 C/A", "GPS L1 C/A",
                            "GPS L1 C/A", "Galileo E1", "Galileo E1", "Galileo E1", "Galileo E1", "BeiDou B1I"};
    float cn0s[] = {44.2f, 46.5f, 42.1f, 47.8f, 39.4f, 45.0f, 48.2f, 43.1f, 41.5f, 44.8f, 40.2f, 45.6f};
    float dops[] = {-1420.0f, 2840.0f, -890.0f, 1120.0f, -3200.0f, 640.0f, -450.0f, 1890.0f, -2100.0f, 780.0f, -1600.0f, 310.0f};
    double prs[] = {21450210.4, 22108450.2, 23894100.8, 20950340.1, 24510900.5, 21840200.7,
                    20450910.3, 25100400.6, 26340120.9, 24890500.4, 27120800.0, 36450100.2};

    for (int i = 0; i < 12; ++i) {
        std::snprintf(state_.gnss_sats[i].prn, sizeof(state_.gnss_sats[i].prn), "%s", prns[i]);
        std::snprintf(state_.gnss_sats[i].constellation, sizeof(state_.gnss_sats[i].constellation), "%s", consts[i]);
        state_.gnss_sats[i].cn0_dbhz = cn0s[i];
        state_.gnss_sats[i].doppler_hz = dops[i];
        state_.gnss_sats[i].pseudorange_m = prs[i];
        state_.gnss_sats[i].lock_seconds = 16338;
        state_.gnss_sats[i].fix_status = (i == 4 || i == 10) ? 0 : 1; // 3D FIX or TRACKING
    }
}

void SyntheticTelemetryEngine::init_modbus_regs()
{
    state_.terr_protocol = 0; // DMR Tier II
    std::snprintf(state_.terr_talkgroup, sizeof(state_.terr_talkgroup), "TG 9001 (TAC-1)");
    state_.terr_color_code = 1;
    state_.terr_ber_pct = 0.12f;
    state_.terr_frame_sync_pct = 99.4f;
    state_.terr_rssi_dbm = -68.4f;
    state_.terr_audio_vu_level = 0.72f;
    state_.scada_modbus_connected = 1;
    state_.scada_interlock_active = 1;
    state_.terr_aoa_bearing_deg = 142.4f;

    state_.modbus_reg_count = 8;
    const char* tags[] = {"FREQ_HZ_X10", "BUS_VOLTAGE_V", "BREAKER_1_STA", "TRANSFORMER_C",
                          "FEEDER_AMP_X10", "ISOLATOR_LOCK", "BATTERY_V_X10", "ALARM_REGISTER"};
    uint16_t addrs[] = {40001, 40002, 40003, 40004, 40005, 40006, 40007, 40008};
    uint16_t vals[] = {500, 415, 1, 62, 1245, 1, 246, 0};

    for (int i = 0; i < 8; ++i) {
        state_.modbus_regs[i].address = addrs[i];
        state_.modbus_regs[i].value = vals[i];
        std::snprintf(state_.modbus_regs[i].tag, sizeof(state_.modbus_regs[i].tag), "%s", tags[i]);
        state_.modbus_regs[i].status_flag = 0;
    }
}

void SyntheticTelemetryEngine::update(double dt_sec, ExtendedDomainTelemetry& out_telem)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++tick_count_;

    // 1. Advance Kinematics for Air & Maritime Contacts
    for (int i = 0; i < state_.air_contact_count; ++i) {
        state_.air_contacts[i].msg_count += static_cast<uint32_t>(rand_f(1.0f, 4.0f));
    }
    for (int i = 0; i < state_.sea_contact_count; ++i) {
        state_.sea_contacts[i].msg_count += static_cast<uint32_t>(rand_f(0.5f, 2.0f));
    }

    // 2. Update GNSS & Threat Metrics based on active Scenario
    float jam_lvl = 12.4f + rand_f(-1.5f, 1.5f);
    float spoof_lvl = 3.8f + rand_f(-0.8f, 0.8f);
    float in_band_pwr = -124.2f + rand_f(-1.0f, 1.0f);
    float agc_gain = 58.0f + rand_f(-1.0f, 1.0f);
    float pdop = 1.28f + std::sin(tick_count_ * 0.05f) * 0.03f;
    float pps_err = rand_f(-1.5f, 1.5f);

    float comb_threat = 8.4f + rand_f(-1.0f, 1.0f);
    float dos_conf = 4.2f + rand_f(-0.8f, 0.8f);
    float replay_conf = 6.1f + rand_f(-0.8f, 0.8f);
    float mahal_dist = 1.38f + rand_f(-0.1f, 0.1f);
    float noise_floor = -104.2f + rand_f(-0.8f, 0.8f);
    float duty_cycle = 14.2f + rand_f(-2.0f, 2.0f);
    float ibi_entropy = 4.82f + std::sin(tick_count_ * 0.1f) * 0.1f;
    float cfo_delta = 142.0f + rand_f(-10.0f, 10.0f);

    if (scenario_ == ThreatScenario::kGnssChirp) {
        jam_lvl = 88.5f + rand_f(0.0f, 8.0f);
        in_band_pwr = -74.2f + rand_f(0.0f, 4.0f);
        agc_gain = 16.5f + rand_f(0.0f, 2.0f);
        pdop = 6.8f + rand_f(0.0f, 1.2f);
        pps_err = rand_f(-30.0f, 30.0f);
        comb_threat = 78.0f + rand_f(0.0f, 6.0f);
    } else if (scenario_ == ThreatScenario::kGnssSpoof) {
        spoof_lvl = 92.4f + rand_f(0.0f, 5.0f);
        in_band_pwr = -108.0f + rand_f(0.0f, 2.0f);
        comb_threat = 72.0f + rand_f(0.0f, 5.0f);
    } else if (scenario_ == ThreatScenario::kDosFlood) {
        dos_conf = 96.2f + rand_f(0.0f, 3.0f);
        comb_threat = 89.0f + rand_f(0.0f, 5.0f);
        noise_floor = -68.4f + rand_f(0.0f, 4.0f);
        duty_cycle = 94.8f + rand_f(0.0f, 4.0f);
    } else if (scenario_ == ThreatScenario::kReplayAttack) {
        replay_conf = 94.5f + rand_f(0.0f, 4.0f);
        comb_threat = 84.0f + rand_f(0.0f, 5.0f);
        mahal_dist = 5.82f + rand_f(0.0f, 0.4f);
        ibi_entropy = 1.15f + rand_f(0.0f, 0.2f);
        cfo_delta = 940.0f + rand_f(0.0f, 60.0f);
    } else if (scenario_ == ThreatScenario::kEwSurge) {
        jam_lvl = 91.0f + rand_f(0.0f, 5.0f);
        dos_conf = 92.0f + rand_f(0.0f, 5.0f);
        replay_conf = 88.0f + rand_f(0.0f, 5.0f);
        comb_threat = 97.5f + rand_f(0.0f, 2.0f);
        mahal_dist = 6.4f + rand_f(0.0f, 0.5f);
        noise_floor = -64.0f + rand_f(0.0f, 3.0f);
        duty_cycle = 96.0f + rand_f(0.0f, 3.0f);
        ibi_entropy = 0.95f + rand_f(0.0f, 0.2f);
    }

    state_.gnss_jamming_level_pct = jam_lvl;
    state_.gnss_spoofing_level_pct = spoof_lvl;
    state_.gnss_in_band_pwr_dbm = in_band_pwr;
    state_.gnss_agc_gain_db = agc_gain;
    state_.gnss_pdop = pdop;
    state_.gnss_pps_quant_err_ns = pps_err;
    state_.gnss_locktime_sec = 16338 + static_cast<uint32_t>(tick_count_ * dt_sec);
    state_.gnss_tracking_status = (jam_lvl > 50.0f) ? 0 : 3; // UNLOCKED vs 3D RTK

    // STFT Spectrogram Slice (128 bins)
    const int center_bin = 64;
    for (int i = 0; i < 128; ++i) {
        float val = rand_f(0.02f, 0.12f);
        if (std::abs(i - center_bin) < 4) val += rand_f(0.2f, 0.35f);
        if (scenario_ == ThreatScenario::kGnssChirp || scenario_ == ThreatScenario::kEwSurge) {
            int chirp_bin = static_cast<int>((tick_count_ * 4) % 128);
            int dist = std::abs(i - chirp_bin);
            if (dist < 5) val += std::max(0.0f, 1.0f - (dist / 5.0f)) * 0.95f;
        } else if (scenario_ == ThreatScenario::kDosFlood) {
            val += rand_f(0.65f, 0.95f);
        } else if (scenario_ == ThreatScenario::kGnssSpoof) {
            if (std::abs(i - center_bin - 6) < 3 || std::abs(i - center_bin + 6) < 3) {
                val += rand_f(0.75f, 0.95f);
            }
        }
        state_.gnss_stft_slice[i] = std::min(1.0f, val);
    }

    // Residuals & Position Dev
    float mult = (scenario_ == ThreatScenario::kGnssSpoof) ? 12.0f : 1.0f;
    state_.gnss_pseudorange_res_m = (scenario_ == ThreatScenario::kGnssSpoof) ? std::sin(tick_count_ * 0.2f) * 6.2f : rand_f(-0.4f, 0.4f);
    state_.gnss_doppler_shift_res_hz = (scenario_ == ThreatScenario::kGnssSpoof) ? std::cos(tick_count_ * 0.2f) * 4.5f : rand_f(-0.2f, 0.2f);
    state_.gnss_carrier_phase_res_cm = rand_f(-0.15f, 0.15f);
    state_.gnss_pos_dev_enu_m[0] = std::sin(tick_count_ * 0.05f) * mult * 0.12f;
    state_.gnss_pos_dev_enu_m[1] = std::cos(tick_count_ * 0.05f) * mult * 0.15f;
    state_.gnss_pos_dev_enu_m[2] = std::sin(tick_count_ * 0.03f) * mult * 0.18f;
    state_.gnss_velocity_mps[0] = 0.04f + rand_f(-0.01f, 0.01f);

    // Update C/N0 for sats
    for (int i = 0; i < 12; ++i) {
        if (scenario_ == ThreatScenario::kGnssSpoof) {
            state_.gnss_sats[i].cn0_dbhz = 54.0f + rand_f(-1.0f, 2.0f);
        } else if (scenario_ == ThreatScenario::kGnssChirp) {
            state_.gnss_sats[i].cn0_dbhz = 22.0f + rand_f(-2.0f, 2.0f);
        } else {
            float base_cn0s[] = {44.2f, 46.5f, 42.1f, 47.8f, 39.4f, 45.0f, 48.2f, 43.1f, 41.5f, 44.8f, 40.2f, 45.6f};
            state_.gnss_sats[i].cn0_dbhz = base_cn0s[i] + rand_f(-0.5f, 0.5f);
        }
    }

    // Replay & DoS Topics (28 metrics)
    state_.dos_noise_floor_dbm = noise_floor;
    state_.dos_spike_score = (scenario_ == ThreatScenario::kDosFlood) ? 0.84f : 0.08f;
    state_.dos_zone_burst_rate[0] = 14.0f + rand_f(-2.0f, 2.0f);
    state_.dos_zone_burst_rate[1] = 8.0f + rand_f(-1.0f, 1.0f);
    state_.dos_zone_burst_rate[2] = 22.0f + rand_f(-3.0f, 3.0f);
    state_.dos_zone_burst_rate[3] = 4.0f + rand_f(-1.0f, 1.0f);
    state_.dos_duty_cycle_pct = duty_cycle;
    state_.replay_ibi_entropy_bits = ibi_entropy;
    state_.dos_reactive_jam_corr = 0.04f + rand_f(-0.01f, 0.01f);
    state_.dos_swept_jam_vel = (scenario_ == ThreatScenario::kGnssChirp) ? 12.4f : 0.0f;
    state_.dos_zone_agg_load = 28.4f + rand_f(-2.0f, 2.0f);
    state_.dos_spec_occ_delta_pct = 1.8f + rand_f(-0.5f, 0.5f);
    state_.replay_unmatched_pwr_dbm = -118.0f + rand_f(-1.0f, 1.0f);
    state_.replay_iq_xcorr_score = (scenario_ == ThreatScenario::kReplayAttack) ? 0.94f : 0.09f;
    state_.replay_cfo_delta_hz = cfo_delta;
    state_.replay_iq_amp_imbalance_db = 0.03f + rand_f(-0.01f, 0.01f);
    state_.replay_iq_phase_imbal_deg = 0.21f + rand_f(-0.05f, 0.05f);
    state_.replay_pa_nonlinearity = 0.02f + rand_f(-0.005f, 0.005f);
    state_.replay_mahalanobis_dist = mahal_dist;
    state_.replay_bearing_delta_deg = (scenario_ == ThreatScenario::kReplayAttack) ? 42.0f : 0.8f;
    state_.replay_cir_delta = (scenario_ == ThreatScenario::kReplayAttack) ? 0.78f : 0.05f;
    state_.replay_tdoa_consistency = (scenario_ == ThreatScenario::kReplayAttack) ? 0.28f : 0.94f;
    state_.replay_temporal_plaus_pct = 98.4f + rand_f(-1.0f, 1.0f);
    state_.replay_transient_delta = 0.04f + rand_f(-0.01f, 0.01f);
    state_.dos_confidence_pct = dos_conf;
    state_.replay_confidence_pct = replay_conf;
    state_.combined_threat_score = comb_threat;
    state_.cooccur_multiplier = (scenario_ == ThreatScenario::kEwSurge) ? 1.75f : 1.00f;
    state_.alert_df_bearing_flag = (scenario_ == ThreatScenario::kReplayAttack) ? 1 : 0;
    state_.cross_zone_flag = (scenario_ == ThreatScenario::kDosFlood) ? 1 : 0;
    state_.zone_to_zone_corr = 0.14f + rand_f(-0.02f, 0.02f);

    out_telem = state_;
}
