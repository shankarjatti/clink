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
    state_.air_contact_count = 6;

    // 1. UAL1244 (Commercial Heavy Jet - Boeing 777-300ER)
    {
        auto& a = state_.air_contacts[0];
        std::snprintf(a.callsign, sizeof(a.callsign), "UAL1244");
        std::snprintf(a.icao, sizeof(a.icao), "A4B291");
        a.lat = 37.8912f;
        a.lon = -122.1402f;
        a.alt_ft = 34000.0f;
        a.speed_kts = 460.0f;
        a.heading_deg = 110.0f;
        std::snprintf(a.squawk, sizeof(a.squawk), "SQK 4321");
        a.rssi_dbm = -68.0f;
        a.msg_count = 1420;
        a.vx = 1.2f;
        a.vy = -0.4f;
        a.aircraft_type = 0; // Heavy Jet
        a.emergency_mode = 0;
        a.vertical_rate_fpm = 0.0f;
        a.mach = 0.84f;
        std::snprintf(a.origin, sizeof(a.origin), "KSFO");
        std::snprintf(a.destination, sizeof(a.destination), "KJFK");
        a.trail_count = 6;
        for (int k = 0; k < 6; ++k) {
            a.trail_lat[k] = a.lat + k * 0.010f;
            a.trail_lon[k] = a.lon - k * 0.025f;
        }
    }

    // 2. SWA812 (Regional / Narrow-body Jet - Boeing 737-800)
    {
        auto& a = state_.air_contacts[1];
        std::snprintf(a.callsign, sizeof(a.callsign), "SWA812");
        std::snprintf(a.icao, sizeof(a.icao), "A1C802");
        a.lat = 37.6190f;
        a.lon = -122.3748f;
        a.alt_ft = 8400.0f;
        a.speed_kts = 240.0f;
        a.heading_deg = 280.0f;
        std::snprintf(a.squawk, sizeof(a.squawk), "SQK 1200");
        a.rssi_dbm = -54.0f;
        a.msg_count = 2110;
        a.vx = -0.9f;
        a.vy = 0.2f;
        a.aircraft_type = 1; // Regional Jet
        a.emergency_mode = 0;
        a.vertical_rate_fpm = -1200.0f;
        a.mach = 0.52f;
        std::snprintf(a.origin, sizeof(a.origin), "KLAS");
        std::snprintf(a.destination, sizeof(a.destination), "KSFO");
        a.trail_count = 6;
        for (int k = 0; k < 6; ++k) {
            a.trail_lat[k] = a.lat - k * 0.005f;
            a.trail_lon[k] = a.lon + k * 0.020f;
        }
    }

    // 3. DAL405 (Commercial Airliner - Airbus A321)
    {
        auto& a = state_.air_contacts[2];
        std::snprintf(a.callsign, sizeof(a.callsign), "DAL405");
        std::snprintf(a.icao, sizeof(a.icao), "A890F3");
        a.lat = 37.9520f;
        a.lon = -122.5800f;
        a.alt_ft = 22500.0f;
        a.speed_kts = 380.0f;
        a.heading_deg = 320.0f;
        std::snprintf(a.squawk, sizeof(a.squawk), "SQK 5514");
        a.rssi_dbm = -64.0f;
        a.msg_count = 950;
        a.vx = -0.8f;
        a.vy = 0.9f;
        a.aircraft_type = 0; // Heavy Jet
        a.emergency_mode = 0;
        a.vertical_rate_fpm = +2100.0f;
        a.mach = 0.74f;
        std::snprintf(a.origin, sizeof(a.origin), "KSFO");
        std::snprintf(a.destination, sizeof(a.destination), "KSEA");
        a.trail_count = 6;
        for (int k = 0; k < 6; ++k) {
            a.trail_lat[k] = a.lat - k * 0.015f;
            a.trail_lon[k] = a.lon + k * 0.015f;
        }
    }

    // 4. UAV-GUARD (High-Altitude Surveillance Drone)
    {
        auto& a = state_.air_contacts[3];
        std::snprintf(a.callsign, sizeof(a.callsign), "UAV-GUARD");
        std::snprintf(a.icao, sizeof(a.icao), "A9FF01");
        a.lat = 37.7650f;
        a.lon = -122.4150f;
        a.alt_ft = 15000.0f;
        a.speed_kts = 85.0f;
        a.heading_deg = 45.0f;
        std::snprintf(a.squawk, sizeof(a.squawk), "SQK 0024");
        a.rssi_dbm = -48.0f;
        a.msg_count = 4890;
        a.vx = 0.4f;
        a.vy = 0.4f;
        a.aircraft_type = 4; // High-Altitude UAV
        a.emergency_mode = 0;
        a.vertical_rate_fpm = 0.0f;
        a.mach = 0.18f;
        std::snprintf(a.origin, sizeof(a.origin), "KNUQ");
        std::snprintf(a.destination, sizeof(a.destination), "ORBIT");
        a.trail_count = 6;
        for (int k = 0; k < 6; ++k) {
            a.trail_lat[k] = a.lat - k * 0.006f;
            a.trail_lon[k] = a.lon - k * 0.006f;
        }
    }

    // 5. USCG-6012 (MH-60T Jayhawk SAR Helicopter)
    {
        auto& a = state_.air_contacts[4];
        std::snprintf(a.callsign, sizeof(a.callsign), "USCG-6012");
        std::snprintf(a.icao, sizeof(a.icao), "A66B99");
        a.lat = 37.8200f;
        a.lon = -122.4780f;
        a.alt_ft = 800.0f;
        a.speed_kts = 120.0f;
        a.heading_deg = 210.0f;
        std::snprintf(a.squawk, sizeof(a.squawk), "SQK 7700"); // SAR Emergency squawk
        a.rssi_dbm = -42.0f;
        a.msg_count = 3200;
        a.vx = -0.4f;
        a.vy = -0.7f;
        a.aircraft_type = 3; // SAR Rotorcraft
        a.emergency_mode = 3; // SQK 7700
        a.vertical_rate_fpm = -50.0f;
        a.mach = 0.16f;
        std::snprintf(a.origin, sizeof(a.origin), "KSFO_CG");
        std::snprintf(a.destination, sizeof(a.destination), "RESCUE");
        a.trail_count = 6;
        for (int k = 0; k < 6; ++k) {
            a.trail_lat[k] = a.lat + k * 0.006f;
            a.trail_lon[k] = a.lon + k * 0.004f;
        }
    }

    // 6. N4285P (Cessna 172 Skyhawk - General Aviation)
    {
        auto& a = state_.air_contacts[5];
        std::snprintf(a.callsign, sizeof(a.callsign), "N4285P");
        std::snprintf(a.icao, sizeof(a.icao), "A522CD");
        a.lat = 37.7100f;
        a.lon = -122.2500f;
        a.alt_ft = 3500.0f;
        a.speed_kts = 115.0f;
        a.heading_deg = 15.0f;
        std::snprintf(a.squawk, sizeof(a.squawk), "SQK 1200");
        a.rssi_dbm = -50.0f;
        a.msg_count = 1120;
        a.vx = 0.1f;
        a.vy = 0.8f;
        a.aircraft_type = 2; // General Aviation
        a.emergency_mode = 0;
        a.vertical_rate_fpm = +300.0f;
        a.mach = 0.15f;
        std::snprintf(a.origin, sizeof(a.origin), "KOAK");
        std::snprintf(a.destination, sizeof(a.destination), "KCCR");
        a.trail_count = 6;
        for (int k = 0; k < 6; ++k) {
            a.trail_lat[k] = a.lat - k * 0.007f;
            a.trail_lon[k] = a.lon - k * 0.002f;
        }
    }

    // Maritime Contacts (5 Vessels)
    state_.sea_contact_count = 5;

    // 1. PACIFIC VOYAGER (Container Vessel - 366m)
    {
        auto& s = state_.sea_contacts[0];
        std::snprintf(s.name, sizeof(s.name), "PACIFIC VOYAGER");
        std::snprintf(s.mmsi, sizeof(s.mmsi), "368124000");
        s.lat = 37.8150f;
        s.lon = -122.4200f;
        s.draft_m = 13.8f;
        s.speed_kts = 16.4f;
        s.heading_deg = 70.0f;
        std::snprintf(s.nav_status, sizeof(s.nav_status), "Underway Engine");
        s.rssi_dbm = -56.0f;
        s.msg_count = 540;
        s.vx = 0.35f;
        s.vy = 0.12f;
        s.vessel_type = 0; // Container / Cargo
        s.nav_status_code = 0;
        s.length_m = 366.0f;
        s.beam_m = 51.0f;
        s.rate_of_turn_dpm = 0.5f;
        std::snprintf(s.destination, sizeof(s.destination), "PORT OF OAKLAND");
        std::snprintf(s.eta, sizeof(s.eta), "18:45 UTC");
        s.wake_count = 6;
        for (int k = 0; k < 6; ++k) {
            s.wake_lat[k] = s.lat - k * 0.002f;
            s.wake_lon[k] = s.lon - k * 0.005f;
        }
    }

    // 2. EVER GALAXY (Ultra-Large Container Ship - 400m)
    {
        auto& s = state_.sea_contacts[1];
        std::snprintf(s.name, sizeof(s.name), "EVER GALAXY");
        std::snprintf(s.mmsi, sizeof(s.mmsi), "413290000");
        s.lat = 37.7800f;
        s.lon = -122.5400f;
        s.draft_m = 15.5f;
        s.speed_kts = 18.8f;
        s.heading_deg = 62.0f;
        std::snprintf(s.nav_status, sizeof(s.nav_status), "Underway Engine");
        s.rssi_dbm = -72.0f;
        s.msg_count = 320;
        s.vx = 0.40f;
        s.vy = 0.20f;
        s.vessel_type = 0; // Container / Cargo
        s.nav_status_code = 0;
        s.length_m = 400.0f;
        s.beam_m = 59.0f;
        s.rate_of_turn_dpm = 0.0f;
        std::snprintf(s.destination, sizeof(s.destination), "PORT OF OAKLAND");
        std::snprintf(s.eta, sizeof(s.eta), "19:15 UTC");
        s.wake_count = 6;
        for (int k = 0; k < 6; ++k) {
            s.wake_lat[k] = s.lat - k * 0.002f;
            s.wake_lon[k] = s.lon - k * 0.006f;
        }
    }

    // 3. USCG CUTTER 752 (National Security Cutter - 127m)
    {
        auto& s = state_.sea_contacts[2];
        std::snprintf(s.name, sizeof(s.name), "USCG CUTTER 752");
        std::snprintf(s.mmsi, sizeof(s.mmsi), "369970000");
        s.lat = 37.8450f;
        s.lon = -122.4600f;
        s.draft_m = 6.8f;
        s.speed_kts = 24.5f;
        s.heading_deg = 195.0f;
        std::snprintf(s.nav_status, sizeof(s.nav_status), "Restricted Ops");
        s.rssi_dbm = -48.0f;
        s.msg_count = 1120;
        s.vx = -0.15f;
        s.vy = -0.55f;
        s.vessel_type = 2; // Coast Guard / Naval
        s.nav_status_code = 2; // Restricted Maneuverability
        s.length_m = 127.0f;
        s.beam_m = 16.0f;
        s.rate_of_turn_dpm = -1.2f;
        std::snprintf(s.destination, sizeof(s.destination), "ALAMEDA BASE");
        std::snprintf(s.eta, sizeof(s.eta), "17:30 UTC");
        s.wake_count = 6;
        for (int k = 0; k < 6; ++k) {
            s.wake_lat[k] = s.lat + k * 0.004f;
            s.wake_lon[k] = s.lon + k * 0.001f;
        }
    }

    // 4. GOLDEN GATE FERRY (High-Speed Catamaran - 45m)
    {
        auto& s = state_.sea_contacts[3];
        std::snprintf(s.name, sizeof(s.name), "GOLDEN GATE FERRY");
        std::snprintf(s.mmsi, sizeof(s.mmsi), "367112000");
        s.lat = 37.8100f;
        s.lon = -122.4050f;
        s.draft_m = 2.4f;
        s.speed_kts = 34.0f;
        s.heading_deg = 340.0f;
        std::snprintf(s.nav_status, sizeof(s.nav_status), "Underway (Transit)");
        s.rssi_dbm = -50.0f;
        s.msg_count = 940;
        s.vx = -0.2f;
        s.vy = 0.6f;
        s.vessel_type = 4; // Passenger Ferry
        s.nav_status_code = 0;
        s.length_m = 45.0f;
        s.beam_m = 12.0f;
        s.rate_of_turn_dpm = 0.0f;
        std::snprintf(s.destination, sizeof(s.destination), "LARKSPUR TERMINAL");
        std::snprintf(s.eta, sizeof(s.eta), "17:50 UTC");
        s.wake_count = 6;
        for (int k = 0; k < 6; ++k) {
            s.wake_lat[k] = s.lat - k * 0.004f;
            s.wake_lon[k] = s.lon + k * 0.001f;
        }
    }

    // 5. SF PILOT #1 (Harbor Pilot Workboat - 22m)
    {
        auto& s = state_.sea_contacts[4];
        std::snprintf(s.name, sizeof(s.name), "SF PILOT #1");
        std::snprintf(s.mmsi, sizeof(s.mmsi), "366890000");
        s.lat = 37.7950f;
        s.lon = -122.4900f;
        s.draft_m = 3.1f;
        s.speed_kts = 11.5f;
        s.heading_deg = 245.0f;
        std::snprintf(s.nav_status, sizeof(s.nav_status), "Underway (Pilot)");
        s.rssi_dbm = -54.0f;
        s.msg_count = 720;
        s.vx = -0.3f;
        s.vy = -0.15f;
        s.vessel_type = 3; // Tugboat / Pilot
        s.nav_status_code = 0;
        s.length_m = 22.0f;
        s.beam_m = 7.0f;
        s.rate_of_turn_dpm = 0.0f;
        std::snprintf(s.destination, sizeof(s.destination), "PILOT STATION SF");
        std::snprintf(s.eta, sizeof(s.eta), "STANDBY");
        s.wake_count = 6;
        for (int k = 0; k < 6; ++k) {
            s.wake_lat[k] = s.lat + k * 0.001f;
            s.wake_lon[k] = s.lon + k * 0.003f;
        }
    }

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
    double sim_dt = dt_sec > 0.0 ? dt_sec : 0.016;

    // Air contacts (ADS-B Mode-S)
    for (int i = 0; i < state_.air_contact_count; ++i) {
        auto& a = state_.air_contacts[i];
        a.msg_count += static_cast<uint32_t>(rand_f(1.0f, 4.0f));

        // Speed in knots to deg/sec:
        float speed_deg_s = (a.speed_kts / 216000.0f) * 3.0f; // scaled for responsive tactical display
        float rad = (90.0f - a.heading_deg) * (static_cast<float>(M_PI) / 180.0f);
        float dlon = speed_deg_s * std::cos(rad) * static_cast<float>(sim_dt);
        float dlat = speed_deg_s * std::sin(rad) * static_cast<float>(sim_dt);

        a.lat += dlat;
        a.lon += dlon;

        // Gentle turnaround when approaching boundary of tactical region
        if (a.lat > 38.18f && std::sin(rad) > 0) { a.heading_deg = 180.0f + rand_f(-35.0f, 35.0f); }
        else if (a.lat < 37.45f && std::sin(rad) < 0) { a.heading_deg = 0.0f + rand_f(-35.0f, 35.0f); }
        if (a.lon > -121.95f && std::cos(rad) > 0) { a.heading_deg = 270.0f + rand_f(-35.0f, 35.0f); }
        else if (a.lon < -122.85f && std::cos(rad) < 0) { a.heading_deg = 90.0f + rand_f(-35.0f, 35.0f); }

        if (a.heading_deg < 0.0f) a.heading_deg += 360.0f;
        if (a.heading_deg >= 360.0f) a.heading_deg -= 360.0f;

        // Shift breadcrumb history every 15 ticks
        if (tick_count_ % 15 == 0) {
            for (int k = 7; k > 0; --k) {
                a.trail_lat[k] = a.trail_lat[k - 1];
                a.trail_lon[k] = a.trail_lon[k - 1];
            }
            a.trail_lat[0] = a.lat;
            a.trail_lon[0] = a.lon;
            if (a.trail_count < 8) a.trail_count++;
        }
    }

    // Maritime contacts (AIS Class A/B)
    for (int i = 0; i < state_.sea_contact_count; ++i) {
        auto& s = state_.sea_contacts[i];
        s.msg_count += static_cast<uint32_t>(rand_f(0.5f, 2.0f));

        float speed_deg_s = (s.speed_kts / 216000.0f) * 2.2f;
        float rad = (90.0f - s.heading_deg) * (static_cast<float>(M_PI) / 180.0f);
        float dlon = speed_deg_s * std::cos(rad) * static_cast<float>(sim_dt);
        float dlat = speed_deg_s * std::sin(rad) * static_cast<float>(sim_dt);

        s.lat += dlat;
        s.lon += dlon;

        if (s.lat > 37.96f && std::sin(rad) > 0) { s.heading_deg = 180.0f + rand_f(-25.0f, 25.0f); }
        else if (s.lat < 37.60f && std::sin(rad) < 0) { s.heading_deg = 0.0f + rand_f(-25.0f, 25.0f); }
        if (s.lon > -122.30f && std::cos(rad) > 0) { s.heading_deg = 250.0f + rand_f(-25.0f, 25.0f); }
        else if (s.lon < -122.75f && std::cos(rad) < 0) { s.heading_deg = 70.0f + rand_f(-25.0f, 25.0f); }

        if (s.heading_deg < 0.0f) s.heading_deg += 360.0f;
        if (s.heading_deg >= 360.0f) s.heading_deg -= 360.0f;

        // Shift wake history every 20 ticks
        if (tick_count_ % 20 == 0) {
            for (int k = 7; k > 0; --k) {
                s.wake_lat[k] = s.wake_lat[k - 1];
                s.wake_lon[k] = s.wake_lon[k - 1];
            }
            s.wake_lat[0] = s.lat;
            s.wake_lon[0] = s.lon;
            if (s.wake_count < 8) s.wake_count++;
        }
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
