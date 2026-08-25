// net_protocol.h
//
// Shared networking protocol, frame headers, socket helpers, and
// high-performance sc16 <-> float (fc32) SIMD/vectorized converters
// for lossless streaming across System 1 -> System 2 -> System 3.

#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// Magic 32-bit identifier for frame alignment ('IQS3' = 0x49515333)
constexpr uint32_t kIqFrameMagic = 0x49515333;
// Extended frame magic containing domain telemetry ('IQEX' = 0x49514558)
constexpr uint32_t kIqExtendedFrameMagic = 0x49514558;
constexpr uint32_t kTelemetryMagic = 0x4558544C; // 'EXTL'

#pragma pack(push, 1)
struct IqFrameHeader
{
    uint32_t magic;           // kIqFrameMagic ('IQS3') or kIqExtendedFrameMagic ('IQEX')
    uint32_t sequence_num;    // Monotonically increasing (0, 1, 2, ...)
    uint64_t timestamp_ns;    // USRP timestamp in nanoseconds
    double   center_freq_hz;  // Active RF carrier frequency (e.g. 2.4e9, 5.1e9, 5.8e9)
    float    iq_multiplier;   // 1.0 at S1 -> S2; 2.0 / 3.0 / 4.0 at S2 -> S3
    float    elevation_deg;   // Elevation angle in degrees (e.g. 30.0, 50.0, 60.0)
    float    azimuth_deg;     // Azimuth angle in degrees (e.g. 40.0, 60.0, 70.0)
    uint32_t sample_count;    // Number of complex samples per channel in this frame
    uint32_t fft_size;        // FFT points (e.g. 4096, or 0 if no FFT vector in this frame)
    uint32_t is_bursting;     // 1 during active TX burst, 0 during silence
    uint32_t telemetry_bytes; // Size of trailing ExtendedDomainTelemetry struct (0 if none)
};

struct AirContact
{
    char callsign[16];
    char icao[12];
    float lat;
    float lon;
    float alt_ft;
    float speed_kts;
    float heading_deg;
    char squawk[16];
    float rssi_dbm;
    uint32_t msg_count;
    float vx;
    float vy;

    // Extended flight dynamics and visual classification
    uint8_t aircraft_type;       // 0=Heavy Commercial Jet, 1=Regional Jet, 2=General Aviation, 3=SAR Rotorcraft/Helo, 4=High-Altitude UAV, 5=Military
    uint8_t emergency_mode;      // 0=NORMAL, 1=SQK 7500, 2=SQK 7600, 3=SQK 7700
    float vertical_rate_fpm;     // Climb/Descent rate in ft/min (e.g. +1800 fpm)
    float mach;                  // Flight Mach number (e.g. 0.82)
    char origin[8];              // Origin ICAO/IATA code (e.g. "KSFO")
    char destination[8];         // Dest ICAO/IATA code (e.g. "KLAX")
    float trail_lat[8];          // Breadcrumb history latitude
    float trail_lon[8];          // Breadcrumb history longitude
    uint8_t trail_count;         // Valid points in history
};

struct SeaContact
{
    char name[32];
    char mmsi[16];
    float lat;
    float lon;
    float draft_m;
    float speed_kts;
    float heading_deg;
    char nav_status[32];
    float rssi_dbm;
    uint32_t msg_count;
    float vx;
    float vy;

    // Extended maritime dimensions and visual classification
    uint8_t vessel_type;         // 0=Container/Cargo, 1=Oil/LNG Tanker, 2=Coast Guard/Naval, 3=Tugboat/Pilot, 4=Passenger Ferry, 5=Fishing
    uint8_t nav_status_code;     // 0=Underway Engine, 1=At Anchor, 2=Restricted Maneuverability, 3=Moored
    float length_m;              // Vessel length in meters (e.g. 366.0m)
    float beam_m;                // Vessel beam width in meters (e.g. 51.0m)
    float rate_of_turn_dpm;      // Rate of turn in deg/min
    char destination[32];        // Destination port name (e.g. "PORT OF OAKLAND")
    char eta[16];                // ETA string (e.g. "18:30 UTC")
    float wake_lat[8];           // Hydrodynamic wake trail latitude
    float wake_lon[8];           // Hydrodynamic wake trail longitude
    uint8_t wake_count;          // Valid points in wake
};

struct GnssSatTelemetry
{
    char prn[8];
    char constellation[16];
    float cn0_dbhz;
    float doppler_hz;
    double pseudorange_m;
    uint32_t lock_seconds;
    uint8_t fix_status; // 0=TRACKING, 1=3D FIX, 2=RTK FIX
};

struct ModbusRegister
{
    uint16_t address;
    uint16_t value;
    char tag[24];
    uint8_t status_flag; // 0=OK, 1=WARN, 2=ALERT
};

struct ExtendedDomainTelemetry
{
    uint32_t magic;   // kTelemetryMagic ('EXTL')
    uint32_t version; // 1

    // --- GNSS Security Telemetry (14 Published Topics) ---
    float gnss_jamming_level_pct;    // Topic 1
    float gnss_spoofing_level_pct;   // Topic 2
    float gnss_in_band_pwr_dbm;      // Topic 3
    float gnss_agc_gain_db;          // Topic 4
    float gnss_pseudorange_res_m;    // Topic 6
    float gnss_doppler_shift_res_hz; // Topic 7
    float gnss_carrier_phase_res_cm; // Topic 8
    float gnss_pos_dev_enu_m[3];     // Topic 9 (East, North, Up)
    float gnss_velocity_mps[3];      // Topic 14 (Vx, Vy, Vz)
    float gnss_pdop;                 // Topic 12
    float gnss_pps_quant_err_ns;     // Topic 13
    uint32_t gnss_locktime_sec;      // Topic 10
    uint8_t gnss_tracking_status;    // Topic 10 (0=UNLOCKED, 1=2D, 2=3D, 3=RTK)
    uint8_t gnss_sat_count;          // 12 satellites
    GnssSatTelemetry gnss_sats[12];  // Topic 5 & 10
    float gnss_stft_slice[128];      // Topic 11 (2D Spectrogram waterfall PSD slice)

    // --- Replay & DoS Threat Analyzer (28 Published Topics) ---
    float dos_noise_floor_dbm;       // Topic 1
    float dos_spike_score;           // Topic 2
    float dos_zone_burst_rate[4];    // Topic 3 (Zones A, B, C, D)
    float dos_duty_cycle_pct;        // Topic 4
    float replay_ibi_entropy_bits;   // Topic 5
    float dos_reactive_jam_corr;     // Topic 6
    float dos_swept_jam_vel;         // Topic 7
    float dos_zone_agg_load;         // Topic 8
    float dos_spec_occ_delta_pct;    // Topic 9
    float replay_unmatched_pwr_dbm;  // Topic 10
    float replay_iq_xcorr_score;     // Topic 11
    float replay_cfo_delta_hz;       // Topic 12
    float replay_iq_amp_imbalance_db;// Topic 13
    float replay_iq_phase_imbal_deg; // Topic 14
    float replay_pa_nonlinearity;    // Topic 15
    float replay_mahalanobis_dist;   // Topic 16
    float replay_bearing_delta_deg;  // Topic 17
    float replay_cir_delta;          // Topic 18
    float replay_tdoa_consistency;   // Topic 19
    float replay_temporal_plaus_pct; // Topic 20
    float replay_transient_delta;    // Topic 21
    float dos_confidence_pct;        // Topic 22
    float replay_confidence_pct;     // Topic 23
    float combined_threat_score;     // Topic 24
    float cooccur_multiplier;        // Topic 25
    uint8_t alert_df_bearing_flag;   // Topic 26 (0=NORMAL, 1=ALERT)
    uint8_t cross_zone_flag;         // Topic 27 (0=CLEAR, 1=FLAG)
    float zone_to_zone_corr;         // Topic 28

    // --- Airtime & Maritime Awareness ---
    uint8_t air_contact_count;
    AirContact air_contacts[6];
    uint8_t sea_contact_count;
    SeaContact sea_contacts[6];
    double usrp_primary_freq_mhz;   // 1090.000
    double usrp_secondary_freq_mhz; // 162.000
    float usrp_rx_gain_db;          // 54.0
    float usrp_sample_rate_msps;    // 10.0
    uint8_t usrp_proto_mode;        // 0=DUAL, 1=ADSB, 2=UAT, 3=AIS
    uint32_t sdr_msg_rate_per_min;  // e.g. 2410

    // --- Terrestrial Decoder & SCADA Link ---
    uint8_t terr_protocol;          // 0=DMR_TIER2, 1=TETRA, 2=P25_PH2, 3=AIRBAND_AM
    char terr_talkgroup[16];
    uint32_t terr_color_code;
    float terr_ber_pct;
    float terr_frame_sync_pct;
    float terr_rssi_dbm;
    float terr_audio_vu_level;
    uint8_t scada_modbus_connected;
    uint8_t scada_interlock_active;
    uint8_t modbus_reg_count;
    ModbusRegister modbus_regs[8];
    float terr_aoa_bearing_deg;
};
#pragma pack(pop)

namespace net_util
{

// Sets TCP_NODELAY (disables Nagle's algorithm) to guarantee sub-millisecond transmission
inline bool set_tcp_nodelay(int fd)
{
    int flag = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&flag), sizeof(flag)) == 0;
}

// Configures OS send and receive socket buffer sizes to avoid drops under load
inline bool set_socket_buffers(int fd, int buffer_size_bytes = 4 * 1024 * 1024)
{
    int snd = buffer_size_bytes;
    int rcv = buffer_size_bytes;
    bool ok1 = (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&snd), sizeof(snd)) == 0);
    bool ok2 = (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&rcv), sizeof(rcv)) == 0);
    return ok1 && ok2;
}

// Blocks until all total_bytes are sent over socket_fd (handles partial writes)
inline bool send_all(int fd, const void* data, size_t total_bytes)
{
    const char* ptr = reinterpret_cast<const char*>(data);
    size_t remaining = total_bytes;
    while (remaining > 0) {
        ssize_t n = send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        ptr += n;
        remaining -= n;
    }
    return true;
}

// Blocks until exact total_bytes are received from socket_fd (handles partial reads)
inline bool recv_exact(int fd, void* data, size_t total_bytes)
{
    char* ptr = reinterpret_cast<char*>(data);
    size_t remaining = total_bytes;
    while (remaining > 0) {
        ssize_t n = recv(fd, ptr, remaining, MSG_WAITALL);
        if (n <= 0) {
            return false;
        }
        ptr += n;
        remaining -= n;
    }
    return true;
}

// Converts complex float (fc32 [-1.0, 1.0]) to interleaved sc16 (int16_t I, int16_t Q)
inline void float_to_sc16(const std::complex<float>* in, int16_t* out_sc16, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        float re = in[i].real();
        float im = in[i].imag();

        // Clamp to [-1.0, 1.0] and scale by 32767.0
        float re_clamped = std::max(-1.0f, std::min(1.0f, re));
        float im_clamped = std::max(-1.0f, std::min(1.0f, im));

        out_sc16[2 * i]     = static_cast<int16_t>(std::lround(re_clamped * 32767.0f));
        out_sc16[2 * i + 1] = static_cast<int16_t>(std::lround(im_clamped * 32767.0f));
    }
}

// Converts interleaved sc16 (int16_t I, int16_t Q) back to complex float (fc32)
inline void sc16_to_float(const int16_t* in_sc16, std::complex<float>* out, size_t count)
{
    constexpr float kInvScale = 1.0f / 32768.0f;
    for (size_t i = 0; i < count; ++i) {
        float re = static_cast<float>(in_sc16[2 * i])     * kInvScale;
        float im = static_cast<float>(in_sc16[2 * i + 1]) * kInvScale;
        out[i] = std::complex<float>(re, im);
    }
}

// Encodes scaled float to sc16 normalizing by multiplier M to preserve precision without clipping
inline void float_to_sc16_scaled(const std::complex<float>* in, int16_t* out_sc16, size_t count, float multiplier)
{
    float inv_m = (multiplier > 0.0f) ? (1.0f / multiplier) : 1.0f;
    for (size_t i = 0; i < count; ++i) {
        float re = in[i].real() * inv_m;
        float im = in[i].imag() * inv_m;

        float re_clamped = std::max(-1.0f, std::min(1.0f, re));
        float im_clamped = std::max(-1.0f, std::min(1.0f, im));

        out_sc16[2 * i]     = static_cast<int16_t>(std::lround(re_clamped * 32767.0f));
        out_sc16[2 * i + 1] = static_cast<int16_t>(std::lround(im_clamped * 32767.0f));
    }
}

// Decodes sc16 back to scaled float using multiplier M
inline void sc16_to_float_scaled(const int16_t* in_sc16, std::complex<float>* out, size_t count, float multiplier)
{
    float scale = (multiplier > 0.0f ? multiplier : 1.0f) / 32768.0f;
    for (size_t i = 0; i < count; ++i) {
        float re = static_cast<float>(in_sc16[2 * i])     * scale;
        float im = static_cast<float>(in_sc16[2 * i + 1]) * scale;
        out[i] = std::complex<float>(re, im);
    }
}

} // namespace net_util
