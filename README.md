# USRP B210 Burst TX/RX Monitor

TX0 → 30 dB attenuator → RX1 (RX2 port) loopback tester. Cycles through
2.4 / 5.1 / 5.8 GHz, transmitting a 10 kHz baseband tone for 100 ms at
each band, followed by 100 ms of silence during which it retunes both
chains and verifies LO lock before the next burst. A live 2×2 window shows
TX IQ waveform, RX IQ waveform, TX FFT, and RX FFT.

This was written and **compiled successfully** (UHD 4.6.0, GLFW 3.3.10,
FFTW 3.3.10, GCC 13.3, Ubuntu 24.04) against Dear ImGui v1.90.9 and ImPlot
v0.16, all linking cleanly with zero warnings. It has **not** been run
against real B210 hardware or a display (this environment has neither) —
device open, tuning, streaming, and the render loop should be validated on
your bench before relying on it. Confirmed at build time: it fails cleanly
(no crash) when no USRP is attached.

## Hardware

- TX/RX port of channel A (chain "A:A") → 30 dB attenuator → RX2 port of
  chain "A:B", via SMA cable.
- **Do not skip the attenuator.** Without it, TX output will very likely
  saturate or damage the RX front end.

## Dependencies (Ubuntu 24.04)

```bash
sudo apt install libuhd-dev libglfw3-dev libfftw3-dev libgl1-mesa-dev \
                  cmake build-essential pkg-config
```

Also needs Dear ImGui and ImPlot source (not vendored in this bundle — see
below).

## Getting ImGui / ImPlot

```bash
cd third_party
git clone --depth 1 --branch v1.90.9 https://github.com/ocornut/imgui.git
git clone --depth 1 --branch v0.16 https://github.com/epezent/implot.git
```

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Run

```bash
./usrp_burst_gui                  # auto-detect the only connected B210
./usrp_burst_gui "serial=XXXXXXX" # target a specific device
```

Needs a display (X11/Wayland) for the GLFW window — run it on the bench
machine's desktop session, not over a headless SSH session without X
forwarding.

## Before your first real run: gain calibration

The gains in `src/main.cpp` (`default_bands()`) are starting points, not
calibrated values:

```cpp
{2.4e9, 40.0, 30.0},
{5.1e9, 45.0, 35.0},
{5.8e9, 45.0, 35.0},
```

With a fixed 30 dB pad (not variable), TX gain and RX gain together are
your only levers for landing the loopback signal in a good part of the RX
ADC's range. Start the app, watch the RX IQ waveform pane at each band:

- If the RX sine is tiny / buried in noise on the FFT floor → raise TX
  gain and/or RX gain for that band.
- If the RX waveform looks clipped (flat-topped instead of a clean
  sinusoid, or FFT floor unusually noisy/spurred) → lower one or both.

Adjust the three `BandGainConfig` rows and rebuild. The three bands can
need different values since the B210's TX/RX gain-vs-frequency response
isn't flat across 2.4–5.8 GHz.

## Design notes / where to look if something needs changing

- **Timing**: burst cadence is anchored to the USRP's own clock
  (`uhd::time_spec_t`), not host `sleep()` — see
  `BurstController::control_loop()`. This is what keeps the 100ms/100ms cadence
  from drifting over a long run.
- **Threading**: exactly two worker threads —
  `BurstController::control_loop` (all tuning calls + TX send()) and
  `BurstController::rx_loop` (free-running RX). All UHD device-state calls
  (tune/gain/antenna) happen only on the control thread; see the comment
  block at the top of `burst_controller.h` for the reasoning. GUI runs on
  main thread, reading lock-free ring buffers (`ring_buffer.h`) — it never
  touches the UHD device.
- **Tone/chunk sizing**: `spb_` (samples per buffer) is chosen as a
  multiple of 200 samples (2 MS/s ÷ 10 kHz), so the same tone chunk buffer
  replays with zero phase discontinuity across every `send()` call and
  every burst — see `BurstController` constructor.
- **"Other tasks that might interfere with the next burst"**: wire your
  site-specific housekeeping into `controller.set_interference_mitigation_hook(...)`
  in `main.cpp`. It runs once per silence period, on the control thread,
  after retune/lock-check.
- **FFT window**: 4096 samples (~2.05 ms @ 2 MS/s), Hann-windowed, ~488 Hz
  bin width — easily resolves the 10 kHz tone. Change `kFftSize` in
  `gui.h` if you want finer resolution (trade-off: slower per-frame FFT).
- **Error counters**: TX underflow/late and RX overflow counts are shown
  in the status bar (top of the window) — worth watching on first runs;
  persistent non-zero counts usually mean `spb_`/USB throughput/host load
  needs attention.

## File map

```
src/
  main.cpp             wiring: opens device, builds band list, starts controller + GUI
  usrp_manager.*        owns multi_usrp device, streamers, tune/gain/antenna, LO-lock checks
  burst_controller.*    the burst/silence/retune state machine, TX send loop, RX recv loop
  signal_gen.h          phase-continuous 10kHz complex tone generator
  ring_buffer.h          lock-free SPSC ring buffer (TX/RX IQ, shared with GUI thread)
  fft_processor.*        Hann-windowed FFT -> magnitude(dB) via FFTW3
  gui.*                  GLFW+OpenGL3+ImGui+ImPlot window, 2x2 TX/RX IQ+FFT plots
```
