# Distributed IQ & FFT Streaming Architecture (System 1 → System 2 → System 3)

This document tracks all critical system parameters, network protocol layouts, RF configurations, and architectural invariants essential to the operation of the 3-node streaming pipeline.

---

## 1. System Parameters & Calibration Table

| Parameter | Value | Notes |
|---|---|---|
| **Sample Rate** | `2.0 MS/s` (2.0 MHz BW) | Constant across all 3 nodes |
| **Tone Frequency** | `10.0 kHz` baseband | Divided evenly by sample rate (200 samples/cycle) |
| **Burst Duration** | `10 ms` (`0.01 s`) | Hardware-timed via UHD `time_spec` |
| **Silence Duration** | `10 ms` (`0.01 s`) | Paced with zero-amplitude samples (`0.0f, 0.0f`) |
| **IQ Format (Wire)** | `sc16` (signed 16-bit) | Interleaved `int16_t I`, `int16_t Q` (4 bytes/sample) |
| **Throughput** | `8.0 MB/s` (64.0 Mbps) | Per channel ($16\text{ MB/s}$ combined dual-channel) |
| **Default TX Gain** | `65.0 dB` (Constant) | Constant across 2.4 GHz, 5.1 GHz, 5.8 GHz |

### Calibrated Band Table & Channel Multipliers
### Calibrated Band Table & Channel Multipliers / Polar Angles
| Channel | Band Center | TX Gain | RX Gain | Base Peak Amp | Multiplier | Scaled Peak Amp | Elevation ($\theta_{el}$) | Azimuth ($\theta_{az}$) |
|---|---|---|---|---|---|---|---|---|
| **Channel 1** | **2.4 GHz** | `65.0 dB` | `51.0 dB` | $\approx 1.0\text{ V}$ | **$\times 2.0$** | **$\approx \pm 2.0\text{ V}$** | **$30.0^\circ$** | **$40.0^\circ$** |
| **Channel 2** | **5.1 GHz** | `65.0 dB` | `61.0 dB` | $\approx 1.0\text{ V}$ | **$\times 3.0$** | **$\approx \pm 3.0\text{ V}$** | **$50.0^\circ$** | **$60.0^\circ$** |
| **Channel 3** | **5.8 GHz** | `65.0 dB` | `63.0 dB` | $\approx 1.0\text{ V}$ | **$\times 4.0$** | **$\approx \pm 4.0\text{ V}$** | **$60.0^\circ$** | **$70.0^\circ$** |
| **Channel 4** | **Combined**| `65.0 dB` | Dynamic | $\approx 1.0\text{ V}$ | **Dynamic** | **$\pm 2.0 / \pm 3.0 / \pm 4.0\text{ V}$** | **Dynamic** | **Dynamic** |

---

## 2. Network Protocol Specification (`net_protocol.h`)

### Frame Header Structure (`IqFrameHeader`)
```cpp
#pragma pack(push, 1)
struct IqFrameHeader {
    uint32_t magic;           // 0x49515333 ('IQS3')
    uint32_t sequence_num;    // Monotonically increasing counter (0, 1, 2, ...)
    uint64_t timestamp_ns;    // USRP / system hardware timestamp
    double   center_freq_hz;  // Active RF carrier frequency (2.4e9, 5.1e9, 5.8e9)
    float    iq_multiplier;   // 1.0 at S1 -> S2; 2.0 / 3.0 / 4.0 at S2 -> S3
    float    elevation_deg;   // Fixed elevation angle (30.0, 50.0, 60.0)
    float    azimuth_deg;     // Fixed azimuth angle (40.0, 60.0, 70.0)
    uint32_t sample_count;    // Complex samples per channel (e.g. 2000)
    uint32_t fft_size;        // FFT points (e.g. 4096 or 0)
    uint32_t is_bursting;     // 1 during active burst, 0 during silence
};
#pragma pack(pop)
```

### Full Wire Payload Layout (RX-Only Optimized)
```text
[ IqFrameHeader: 44 bytes ]
[ RX sc16 data: sample_count * 4 bytes ]
[ (Optional) RX FFT float data: fft_size * 4 bytes ]
```

---

## 3. End-to-End Pipeline & Execution Flow

```text
+-----------------------------------------------------------------------------------+
| System 1 (Source)                                                                 |
| Captures SDR float IQ -> Encodes to sc16 -> TCP_NODELAY stream to System 2        |
| (Attaches fixed Elevation/Azimuth degrees per RF carrier)                         |
+-----------------------------------------------------------------------------------+
                                         |  TCP Port 5000 (Unscaled sc16 + FFTs + Angles)
                                         v
+-----------------------------------------------------------------------------------+
| System 2 (Relay, Scaler & 12-Plot 4x3 Grid Monitor)                               |
| 1. Receives sc16 -> Converts to float                                             |
| 2. Multiplies IQ by band factor (2.4G x2, 5.1G x3, 5.8G x4)                       |
| 3. Direct FFT routing (no multiplier, zero recomputation)                         |
| 4. Renders live 4x3 Matrix GUI (4 Waveforms + 4 FFTs + 4 Polar Radar Maps)       |
| 5. Encodes scaled float to sc16 -> TCP_NODELAY stream to System 3                 |
+-----------------------------------------------------------------------------------+
                                         |  TCP Port 6001 (Scaled sc16 + FFTs + Angles)
                                         v
+-----------------------------------------------------------------------------------+
| System 3 (Sink & Operator Console GUI)                                            |
| 1. Receives scaled sc16 stream from System 2                                      |
| 2. Converts sc16 to scaled float                                                  |
| 3. Direct FFT and Polar Angle routing                                             |
| 4. Renders Operator Console: Sidebar Tabs + 1x3 Focused View (Wave, FFT, Polar)  |
+-----------------------------------------------------------------------------------+
```

---

## 4. User Interface Architecture

### System 2: 12-Plot Matrix Grid ($4 \times 3$)
Renders all 4 channels simultaneously in a $4 \times 3$ grid:
- **Column 1**: Scaled IQ Waveforms (`[-4V, 4V]`, `[-6V, 6V]`, `[-8V, 8V]`)
- **Column 2**: Direct RF FFT Spectra (`2400 MHz`, `5100 MHz`, `5800 MHz`)
- **Column 3**: Polar Radar Maps (Range rings $30^\circ, 60^\circ, 90^\circ$, target bearing blip at $(El, Az)$)

### System 3: Modern Operator Console (Split Top Waveform/FFT + Bottom Square Polar Map & Latency Metrics)
Features a dedicated sidebar with 2.4 GHz, 5.1 GHz, 5.8 GHz, and All Channels tabs. Each focused tab displays:
1. **Top Status Bar**:
   - Live Carrier, Burst State, Multiplier Badge, **E2E Transit Latency (ms)**, **Inter-Frame Delivery Jitter (ms)**, **Ingestion Rate (FPS)**, Burst Counter, and Lossless Drop Counter.
2. **Top Viewport ($1 \times 2$ Grid)**:
   - **Left**: Scaled IQ Waveform
   - **Right**: Direct RF FFT Spectrum
3. **Bottom Viewport**:
   - **Left Box**: Square Polar Radar Map (Target blip at $(El, Az)$ when active; drops to $0^\circ, 0^\circ$ idle when inactive)
   - **Right Box**: 3-Column Live Channel Telemetry Card:
     - *Column 1*: Active Status, Azimuth Bearing, Elevation Angle, Multiplier
     - *Column 2*: Peak Voltage Amplitude, Peak Tone Power (dBFS), Tone Peak Frequency, Center Frequency
     - *Column 3*: E2E Transit Latency, Inter-Frame Jitter, Ingestion Rate, Network Health

---

## 5. Execution Instructions

### Local Multi-Node Test (Single Machine)

```bash
# Terminal 1 — Start System 3 (Sink):
cd /home/sudeep/clink/build
./usrp_sink_gui --listen-port 6001

# Terminal 2 — Start System 2 (Relay):
cd /home/sudeep/clink/build
./usrp_relay_gui --listen-port 5000 --stream-to 127.0.0.1:6001

# Terminal 3 — Start System 1 (SDR Source):
cd /home/sudeep/clink/build
./usrp_burst_gui --stream-to 127.0.0.1:5000
```
