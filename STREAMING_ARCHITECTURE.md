# Distributed IQ & FFT Streaming Architecture (System 1 → System 2 → System 3)

This document tracks all critical system parameters, network protocol layouts, RF configurations, and architectural invariants essential to the operation of the 3-node streaming pipeline.

---

## 1. System Parameters & Calibration Table

| Parameter | Value | Notes |
|---|---|---|
| **Sample Rate** | `2.0 MS/s` (2.0 MHz BW) | Constant across all 3 nodes |
| **Tone Frequency** | `10.0 kHz` baseband | Divided evenly by sample rate (200 samples/cycle) |
| **Burst Duration** | `1.0 s` | Hardware-timed via UHD `time_spec` |
| **Silence Duration** | `1.0 s` | Paced with zero-amplitude samples (`0.0f, 0.0f`) |
| **IQ Format (Wire)** | `sc16` (signed 16-bit) | Interleaved `int16_t I`, `int16_t Q` (4 bytes/sample) |
| **Throughput** | `8.0 MB/s` (64.0 Mbps) | Per channel ($16\text{ MB/s}$ combined dual-channel) |
| **Waveform Amplitude Limit** | `[-2.0, 2.0]` | Fixed vertical plot scale |
| **Default TX Gain** | `65.0 dB` (Constant) | Constant across 2.4 GHz, 5.1 GHz, 5.8 GHz |

### Calibrated Band Table
| Band Center | TX Gain | RX Gain | Calibrated RX Peak Amplitude |
|---|---|---|---|
| **2.4 GHz** | `65.0 dB` | `51.0 dB` | $\approx 1.0\text{ V}$ |
| **5.1 GHz** | `65.0 dB` | `61.0 dB` | $\approx 1.0\text{ V}$ |
| **5.8 GHz** | `65.0 dB` | `63.0 dB` | $\approx 1.0\text{ V}$ |

---

## 2. Network Protocol Specification (`net_protocol.h`)

### Frame Header Structure (`IqFrameHeader` — 32 Bytes)
```cpp
#pragma pack(push, 1)
struct IqFrameHeader {
    uint32_t magic;           // 0x49515333 ('IQS3')
    uint32_t sequence_num;    // Monotonically increasing counter (0, 1, 2, ...)
    uint64_t timestamp_ns;    // USRP / system hardware timestamp
    double   center_freq_hz;  // Active RF carrier frequency (2.4e9, 5.1e9, 5.8e9)
    uint32_t sample_count;    // Complex samples per channel (e.g. 2000)
    uint32_t fft_size;        // FFT points (e.g. 4096 or 0)
    uint32_t is_bursting;     // 1 during active burst, 0 during silence
    uint32_t reserved;        // 64-bit alignment padding
};
#pragma pack(pop)
```

### Full Wire Payload Layout
```text
[ 32-byte IqFrameHeader ]
[ TX sc16 data: sample_count * 4 bytes ]
[ RX sc16 data: sample_count * 4 bytes ]
[ (Optional) TX FFT float data: fft_size * 4 bytes ]
[ (Optional) RX FFT float data: fft_size * 4 bytes ]
```

### Fast `sc16` ↔ `float` Conversion Rules
- **Float to `sc16`**:
  $$I_{16} = \text{clamp}(\text{round}(I_{\text{float}} \times 32767.0), -32768, 32767)$$
  $$Q_{16} = \text{clamp}(\text{round}(Q_{\text{float}} \times 32767.0), -32768, 32767)$$
- **`sc16` to Float**:
  $$I_{\text{float}} = \frac{I_{16}}{32768.0}$$
  $$Q_{\text{float}} = \frac{Q_{16}}{32768.0}$$

---

## 3. TCP Low-Latency & Zero-Loss Configuration

1. **`TCP_NODELAY` (Nagle's Algorithm Disabled)**:
   - Eliminates packet coalescing latency, forcing chunks to transmit immediately upon `send()` ($< 0.5\text{ ms}$ latency).
2. **Socket Buffer Expansion**:
   - `SO_SNDBUF` and `SO_RCVBUF` set to **`4 MB`** to absorb OS thread scheduling jitter without drops.
3. **Loss Verification**:
   - Receiver monitors `hdr.sequence_num`. Any sequence gap increments `dropped_frames_` and is logged immediately.

---

## 4. Pipeline Topology & Executables

### System 1: SDR Source Node (`usrp_burst_gui`)
- **Role**: Drives USRP B210 hardware (TX on RF A, RX on RF B), renders local 2×2 GUI, packs samples into `sc16`, and streams over TCP.
- **Run Command**:
  ```bash
  ./usrp_burst_gui --stream-to <SYSTEM_2_IP>:5000
  ```

### System 2: Relay Node (`usrp_relay_gui`)
- **Role**:
  1. Receives `sc16` stream from System 1 on Port `5000`.
  2. Converts `sc16` $\to$ float complex (`fc32`).
  3. Feeds local ring buffers and renders the identical live 2×2 GUI in real time.
  4. In parallel, converts float $\to$ `sc16` and forwards the stream over TCP to System 3 on Port `6000`.
- **Run Command**:
  ```bash
  ./usrp_relay_gui --listen-port 5000 --stream-to <SYSTEM_3_IP>:6000
  ```

### System 3: Sink Node (`usrp_sink_gui`)
- **Role**:
  1. Receives `sc16` stream from System 2 on Port `6000`.
  2. Converts `sc16` $\to$ float complex (`fc32`).
  3. Feeds local ring buffers and renders the identical live 2×2 GUI in real time with continuous sequence / zero-drop validation.
- **Run Command**:
  ```bash
  ./usrp_sink_gui --listen-port 6000
  ```

---

## 5. Local Testing on a Single Machine

To test all 3 systems concurrently on loopback:

```bash
# Terminal 1 (System 3):
cd /home/sudeep/clink/build
./usrp_sink_gui --listen-port 6000

# Terminal 2 (System 2):
cd /home/sudeep/clink/build
./usrp_relay_gui --listen-port 5000 --stream-to 127.0.0.1:6000

# Terminal 3 (System 1):
cd /home/sudeep/clink/build
./usrp_burst_gui --stream-to 127.0.0.1:5000
```
