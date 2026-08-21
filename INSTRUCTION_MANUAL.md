# USRP B210 Distributed Streaming & Operator Console — Instruction Manual

This manual provides complete step-by-step instructions on how to build, configure, and operate the 3-node distributed RF processing and monitoring system across System 1, System 2, and System 3.

---

## 1. System Architecture Overview

```text
+-----------------------------------------------------------------------------------+
| System 1: SDR Source Node (`usrp_burst_gui`)                                      |
| - Generates hardware-timed synchronized RF bursts (2.4 GHz, 5.1 GHz, 5.8 GHz)     |
| - Transmits 10 kHz baseband tone at 2.0 MS/s with calibrated RX gains             |
| - Attaches precomputed FFT spectra & fixed Polar angles:                          |
|     * 2.4 GHz: Elevation = 30.0°, Azimuth = 40.0°                                 |
|     * 5.1 GHz: Elevation = 50.0°, Azimuth = 60.0°                                 |
|     * 5.8 GHz: Elevation = 60.0°, Azimuth = 70.0°                                 |
| - Encodes received RX IQ to signed 16-bit (`sc16`) wire format                    |
| - Streams over TCP with `TCP_NODELAY` to System 2 (Port 5000)                     |
+-----------------------------------------------------------------------------------+
                                         |  TCP Port 5000 (Unscaled RX sc16 + RX FFT + Angles)
                                         v
+-----------------------------------------------------------------------------------+
| System 2: Relay & 12-Plot Matrix Monitor (`usrp_relay_gui`)                       |
| - Receives unscaled sc16 stream from System 1                                     |
| - Demultiplexes into 4 frequency channels                                         |
| - Applies calibrated band multipliers:                                            |
|     * Channel 1 (2.4 GHz): x2.0 (Range [-4.0V, 4.0V])                            |
|     * Channel 2 (5.1 GHz): x3.0 (Range [-6.0V, 6.0V])                            |
|     * Channel 3 (5.8 GHz): x4.0 (Range [-8.0V, 8.0V])                            |
|     * Channel 4 (Combined): Dynamic composite scaling                             |
| - Routes direct FFT spectra (no recomputation or multiplier distortion)           |
| - Renders live 12-Plot 4x3 Grid (4 Waveforms + 4 FFTs + 4 Polar Radar Maps)       |
| - Encodes scaled float samples to sc16 and forwards to System 3 (Port 6001)       |
+-----------------------------------------------------------------------------------+
                                         |  TCP Port 6001 (Scaled sc16 + FFTs + Angles)
                                         v
+-----------------------------------------------------------------------------------+
| System 3: Sink & Operator Console (`usrp_sink_gui`)                               |
| - Receives scaled sc16 stream from System 2                                       |
| - Decodes sc16 back to scaled float                                               |
| - Routes to 4-channel demux and manages real-time telemetry                       |
| - Renders Modern Operator Console:                                                |
|     * Top Telemetry Header (Active Carrier, Burst Status, Multiplier, Lossless)   |
|     * Left Navigation Sidebar (2.4 GHz, 5.1 GHz, 5.8 GHz, All Channels Tabs)     |
|     * Top Viewport: Split Waveform Plot (Left) & Direct RF FFT Spectrum (Right)   |
|     * Bottom Viewport: Square Polar Radar Map (Target blip at El/Az when active;  |
|       automatically drops to 0°, 0° when inactive) + Live Channel Telemetry Card  |
+-----------------------------------------------------------------------------------+
```

---

## 2. Prerequisites & Dependencies

### Operating System & Packages (Ubuntu 20.04 / 22.04 / 24.04 / Debian)
Install the required build tools and libraries on all three machines:

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libuhd-dev \
    uhd-host \
    libfftw3-dev \
    libglfw3-dev \
    libgl1-mesa-dev
```

### USRP Hardware Setup (System 1 Only)
Ensure USB rules and UHD images are installed:
```bash
sudo uhd_images_downloader
uhd_find_devices
```

---

## 3. Building the System

From the repository root on each system:

```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

This compiles all three executables:
1. `usrp_burst_gui` (System 1: SDR Source Node)
2. `usrp_relay_gui` (System 2: Relay & 12-Plot Matrix Monitor)
3. `usrp_sink_gui`  (System 3: Sink & Operator Console)

---

## 4. Running the Pipeline

> **CRITICAL LAUNCH ORDER**: Always start the nodes in reverse order (**System 3 $\to$ System 2 $\to$ System 1**) so listening TCP sockets are active and ready when senders connect.

### Option A: Distributed Setup (3 Separate Physical Machines)

Assume the following IP configuration:
- **System 3 (Sink)**: `192.168.1.103`
- **System 2 (Relay)**: `192.168.1.102`
- **System 1 (SDR Source)**: `192.168.1.101`

#### Step 1: Start System 3 (Operator Console)
On System 3 (`192.168.1.103`):
```bash
cd clink/build
./usrp_sink_gui --listen-port 6001
```

#### Step 2: Start System 2 (Relay & Matrix Monitor)
On System 2 (`192.168.1.102`):
```bash
cd clink/build
./usrp_relay_gui --listen-port 5000 --stream-to 192.168.1.103:6001
```

#### Step 3: Start System 1 (SDR Source)
On System 1 (`192.168.1.101`):
```bash
cd clink/build
./usrp_burst_gui --stream-to 192.168.1.102:5000
```

---

### Option B: Local Testing (Single Machine Loopback)

Open three separate terminal windows:

#### Terminal 1 — Start System 3 (Operator Console):
```bash
cd clink/build
./usrp_sink_gui --listen-port 6001
```

#### Terminal 2 — Start System 2 (Relay & 12-Plot Monitor):
```bash
cd clink/build
./usrp_relay_gui --listen-port 5000 --stream-to 127.0.0.1:6001
```

#### Terminal 3 — Start System 1 (SDR Source):
```bash
cd clink/build
./usrp_burst_gui --stream-to 127.0.0.1:5000
```

---

## 5. Command-Line Options Reference

### `usrp_burst_gui` (System 1)
| Option | Description | Default | Example |
|---|---|---|---|
| `--stream-to <ip:port>` | Target System 2 IP and listening port | *(None)* | `--stream-to 192.168.1.102:5000` |
| `--args=<uhd_args>` | Custom UHD device discovery arguments | `""` | `--args="serial=30B4567"` |

### `usrp_relay_gui` (System 2)
| Option | Description | Default | Example |
|---|---|---|---|
| `--listen-port <port>` | Port to accept incoming stream from System 1 | `5000` | `--listen-port 5000` |
| `--stream-to <ip:port>` | Target System 3 IP and listening port | `127.0.0.1:6001` | `--stream-to 192.168.1.103:6001` |

### `usrp_sink_gui` (System 3)
| Option | Description | Default | Example |
|---|---|---|---|
| `--listen-port <port>` | Port to accept incoming stream from System 2 | `6001` | `--listen-port 6001` |

---

## 6. Operator Console Features (System 3)

1. **Left Navigation Sidebar**:
   - Click **`2.4 GHz Band`**: Displays Channel 1 ($\times 2.0$, El: $30^\circ$, Az: $40^\circ$).
   - Click **`5.1 GHz Band`**: Displays Channel 2 ($\times 3.0$, El: $50^\circ$, Az: $60^\circ$).
   - Click **`5.8 GHz Band`**: Displays Channel 3 ($\times 4.0$, El: $60^\circ$, Az: $70^\circ$).
   - Click **`All Channels`**: Displays dynamic composite stream with an optional $4 \times 3$ matrix toggle.
2. **Top Viewport**:
   - Left: Full-resolution **Scaled IQ Waveform Plot**.
   - Right: Full-resolution **Direct RF FFT Spectrum Plot** centered on the band.
3. **Bottom Viewport**:
   - **Square Polar Radar Map**: Displays the active target bearing vector line and circular blip. When the transmission hops to a different frequency, the polar map automatically drops to **$0^\circ, 0^\circ$ (Idle / Center origin)**.
   - **Live Telemetry & Diagnostics Card**: Shows active status, azimuth bearing, elevation angle, active multiplier, peak voltage amplitude, peak tone power (dBFS), and center RF frequency.

---

## 7. Firewall & Network Troubleshooting

If systems are on different physical networks and cannot establish connection:
1. Ensure TCP ports `5000` and `6001` are open on the firewall:
   ```bash
   # On System 2:
   sudo ufw allow 5000/tcp

   # On System 3:
   sudo ufw allow 6001/tcp
   ```
2. Verify ICMP network reachability:
   ```bash
   ping 192.168.1.102
   ping 192.168.1.103
   ```
3. Verify socket listeners are active:
   ```bash
   # Check on System 2:
   ss -tlpn | grep 5000

   # Check on System 3:
   ss -tlpn | grep 6001
   ```
