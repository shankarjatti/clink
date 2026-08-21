# USRP B210 Multi-System Streaming Instruction Manual

This manual provides step-by-step instructions for installing dependencies, building the codebase, and executing the 3-node real-time streaming pipeline across physical machines or locally on a single machine.

---

## 1. System Requirements & Dependencies

### System 1: SDR Source Node (USRP B210 Connected)
```bash
sudo apt update && sudo apt install -y \
    build-essential \
    cmake \
    libuhd-dev \
    uhd-host \
    libglfw3-dev \
    libfftw3-dev \
    libgl1-mesa-dev \
    git
```

### System 2 (Relay Node) & System 3 (Sink Node)
*(Note: Systems 2 and 3 do not require USRP hardware drivers or `libuhd`)*
```bash
sudo apt update && sudo apt install -y \
    build-essential \
    cmake \
    libglfw3-dev \
    libfftw3-dev \
    libgl1-mesa-dev \
    git
```

---

## 2. Cloning the Repository & Third-Party Dependencies

Clone the repository and download the pinned UI dependencies on each system:

```bash
git clone https://github.com/subeep/clink.git
cd clink

# Download Dear ImGui (v1.90.9) and ImPlot (v0.16)
mkdir -p third_party
cd third_party
git clone --branch v1.90.9 --depth 1 https://github.com/ocornut/imgui.git
git clone --branch v0.16 --depth 1 https://github.com/epezent/implot.git
cd ..
```

---

## 3. Building the Executables

From the repository root on each machine:

```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

This generates three binaries:
1. `usrp_burst_gui` (System 1: SDR Source Node)
2. `usrp_relay_gui` (System 2: Relay & Live GUI Node)
3. `usrp_sink_gui`  (System 3: Sink & Live GUI Node)

---

## 4. Running the Pipeline

> **Launch Order**: Always start the nodes in reverse order (**System 3 $\to$ System 2 $\to$ System 1**) so listening sockets are ready when senders connect.

### Option A: Distributed Setup (3 Separate Physical Machines)

Assuming the following network IP addresses:
- **System 3 (Sink)**: `192.168.1.103`
- **System 2 (Relay)**: `192.168.1.102`
- **System 1 (SDR Source)**: `192.168.1.101`

#### Step 1: Start System 3 (Sink)
```bash
cd clink/build
./usrp_sink_gui --listen-port 6001
```

#### Step 2: Start System 2 (Relay)
```bash
cd clink/build
./usrp_relay_gui --listen-port 5000 --stream-to 192.168.1.103:6001
```

#### Step 3: Start System 1 (SDR Source)
```bash
cd clink/build
./usrp_burst_gui --stream-to 192.168.1.102:5000
```

---

### Option B: Local Testing (Single Machine Loopback)

Open three separate terminal windows:

#### Terminal 1 — Start System 3 (Sink)
```bash
cd clink/build
./usrp_sink_gui --listen-port 6001
```

#### Terminal 2 — Start System 2 (Relay)
```bash
cd clink/build
./usrp_relay_gui --listen-port 5000 --stream-to 127.0.0.1:6001
```

#### Terminal 3 — Start System 1 (SDR Source)
```bash
cd clink/build
./usrp_burst_gui --stream-to 127.0.0.1:5000
```

---

## 5. Command-Line Options Reference

### `usrp_burst_gui` (System 1)
| Option | Description | Example |
|---|---|---|
| `--stream-to <ip:port>` | Target System 2 IP and listening port | `--stream-to 192.168.1.102:5000` |
| `--args=<uhd_args>` | Custom UHD device discovery arguments | `--args="serial=30B4567"` |

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

## 6. Firewall & Network Troubleshooting

If systems are on different subnets or fail to connect:
1. Ensure TCP ports `5000` and `6001` are open on the firewall:
   ```bash
   # On System 2:
   sudo ufw allow 5000/tcp

   # On System 3:
   sudo ufw allow 6001/tcp
   ```
2. Verify ping reachability:
   ```bash
   ping 192.168.1.102
   ping 192.168.1.103
   ```
