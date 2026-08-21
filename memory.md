# memory.md — Design Decisions & Caveats

This documents the reasoning behind choices made while planning and coding
the USRP B210 burst TX/RX monitor, and the things worth double-checking
before trusting it on real hardware.

## 1. Topology & hardware assumptions

- **Loopback interpretation.** The request for both a TX and RX IQ/FFT
  plot was ambiguous between "verify your own TX signal via loopback" and
  "monitor the band independently." I assumed loopback (TX0 → pad → RX1)
  since that's the standard reason to want TX and RX plots side by side —
  checking spectral purity and phase continuity across retunes. Confirmed
  by the user in the next turn: TX ch0 → 30 dB attenuator → RX ch1's RX2
  port.
- **Channel/antenna mapping.** TX = channel 0, antenna `"TX/RX"` (the
  B210's only TX-capable port). RX = channel 1, antenna `"RX2"` explicitly
  (not `"TX/RX"`) — matches the physical cabling and avoids the
  TX/RX-port's switch settling-time behavior since RX isn't sharing a port
  with anything.
- **Single device, internal clock.** Both channels live on one B210, so
  they already share a clock/timebase — no MIMO cable, no external
  ref/PPS needed. `set_clock_source("internal")` /
  `set_time_source("internal")`, time reset to 0 at startup.
- **30 dB attenuator is treated as mandatory**, not optional — called out
  explicitly in the plan and the README. Without it, TX output can
  saturate or damage the RX front end.
- **Manual RX gain, not AGC.** AGC would fight the cycle: it adapts to
  silence/noise floor during the gap, then is wrong the instant the next
  burst starts. Fixed per-band gain is more predictable and reproducible
  for repeatable IQ/FFT plots — at the cost of needing a manual
  calibration pass (see README) since I have no hardware to tune it
  against.
- **Per-band gain table** (`BandGainConfig`, one TX/RX gain pair per
  frequency) rather than one global gain — the B210's gain-vs-frequency
  response isn't flat across 2.4–5.8 GHz, and the attenuator is fixed
  (not variable), so gain is the only lever available to land the
  loopback signal in a good part of the RX ADC's range at each band.

## 2. Timing / state machine

- **Device-clock-anchored scheduling**, not host `sleep()`. The next
  burst's start time is computed as `previous_burst_start + burst_duration
  + silence_duration` (using `uhd::time_spec_t`), not "now + 2s" measured
  after retuning finishes. This means retune/housekeeping duration never
  accumulates drift across a long run — a few extra ms of housekeeping one
  cycle doesn't push subsequent bursts later and later.
- **First-packet time-stamping.** Only the first packet of a burst carries
  `has_time_spec = true`; the rest stream immediately after. UHD/the FPGA
  hold the burst until the scheduled time, so the host doesn't need to
  busy-wait for precise alignment — a ~50 ms lead-in is given before the
  very first burst only, to give the device time to arm.
- **Silence period does real work**, not a fixed sleep: retune both
  chains, poll LO lock (with a 5ms timeout, not indefinitely), drain queued TX
  async error messages from the burst just finished, run the
  user-pluggable interference-mitigation hook, *then* busy-wait (250 µs
  poll) against device time for whatever's left of the ~10 ms gap. Under
  normal conditions the actual work finishes in a few ms and the remainder
  is the poll-wait — this is intentional, it's the safety margin, not wasted time.
- **"Other tasks that might interfere with the next burst"** was
  unspecified in the request, so rather than guess at behavior I made it a
  `std::function` hook (`set_interference_mitigation_hook`) that's a no-op
  by default, wired up in `main.cpp`. Runs once per silence period on the
  control thread.

## 3. Threading model

- **Exactly two worker threads**, deliberately kept minimal:
  - **control thread** — owns the entire state machine *and* does the TX
    `send()` calls itself. No separate TX thread: a burst's blocking
    `send()` calls naturally pace themselves against the device's transmit
    buffer over the ~1 s burst duration, so a dedicated thread would add
    complexity without adding capability.
  - **rx thread** — free-running continuous `recv()` loop, started once at
    startup and left running for the program's lifetime, rather than
    stopped/restarted every cycle. Simpler lifecycle, and it means the RX
    plot also shows the noise floor during silence, not just burst
    content.
- **All UHD device-state calls (tune/gain/antenna/sensors) are confined to
  the control thread only.** The RX thread only calls `recv()` on a
  streamer handle it already holds. This was a hard rule from the planning
  stage — concurrent calls into the same `multi_usrp` object from two
  threads during a retune is exactly the kind of bug that's easy to
  introduce and hard to reproduce later.
- **GUI owns the main thread** (required for GLFW/GL context) and never
  touches the UHD device at all — it only reads status atomics and ring
  buffers written by the other two threads.

## 4. Signal generation & buffering

- **Tone chunk sized as a multiple of the tone's sample period.** At 2
  MS/s and 10 kHz, one cycle is exactly 200 samples. `spb_` (samples per
  buffer, ~1 ms chunks) is a multiple of 200, so a single pre-generated
  buffer can be replayed every `send()` call — within a burst and across
  every future burst — with zero phase discontinuity, forever. This
  removes an entire class of "audible click at chunk boundary" bugs
  without needing to track a running phase accumulator across calls at
  send time (the `ToneGenerator`'s accumulator is still there and correct,
  it's just not load-bearing for this specific rate/tone combination).
- **Ring buffers are lock-free SPSC**, and deliberately "latest-N snapshot"
  semantics rather than a drained queue — the GUI wants "what does the
  signal look like right now," not "give me every sample since I last
  asked." The producer (TX/RX thread) never blocks on the GUI; if the GUI
  stalls, old ring buffer data is silently overwritten with no
  backpressure or error. This is a deliberate tradeoff for a real-time
  monitor, not an oversight — flagging it here so it isn't mistaken for a
  bug later.
- **Ring buffer capacity** is a power of two (65536 samples, ~32 ms at 2
  MS/s) specifically so wraparound indexing can use a bitmask instead of a
  modulo, and to comfortably fit the 4096-sample FFT window with margin.

## 5. FFT / plotting

- FFT computed **on the GUI thread at redraw time**, not on the
  TX/RX threads — keeps the real-time UHD threads free of any work that
  isn't strictly send/recv, at the cost of recomputing the same-ish FFT
  every frame (cheap at 4096 points, a few times per second).
- **Hann window with coherent-gain correction** so magnitude values
  reflect actual input amplitude rather than being suppressed by the
  window's average attenuation.
- **FFT-shifted output** (bin 0 = most negative frequency, center = DC) so
  it plots directly against a symmetric −Fs/2…+Fs/2 axis without extra
  reordering in the GUI code.
- **4096-point FFT** chosen as a middle ground: ~488 Hz bins, easily
  resolves a 10 kHz tone, at a size cheap enough to recompute every frame.
  Bigger (e.g. 8192) is a one-line change (`kFftSize` in `gui.h`) if finer
  resolution is wanted.

## 6. GUI stack

- **Dear ImGui + ImPlot, GLFW/OpenGL3 backend, in-process** — per your
  explicit instruction to keep the GUI in C++ itself rather than piping
  data out to Python/matplotlib. Single binary, no IPC.
- **`ImPlot::BeginSubplots(..., 2, 2, ...)`** used for the 2×2 grid — this
  is a built-in ImPlot mechanism designed exactly for a fixed grid of
  plots, rather than hand-rolling the layout with `ImGui::Columns` or
  manual child windows.
- **ImGui/ImPlot are not vendored in the delivered zip** (only the code
  that uses them) — pinned by tag in the README (`v1.90.9` / `v0.16`) with
  the exact `git clone` commands, so the build is reproducible without
  bloating the deliverable with third-party source.
- `UHD_SAFE_MAIN` used for `main()` — standard UHD-example convention for
  top-level exception handling, avoids hand-rolled try/catch boilerplate
  around device errors.

## 7. What was actually verified vs. not

**Verified in this environment:**
- Full project (UHD 4.6.0, GLFW 3.3.10, FFTW 3.3.10, GCC 13.3, Ubuntu
  24.04, real ImGui v1.90.9 / ImPlot v0.16 sources) **compiles and links
  with zero warnings.**
- Running the binary with no B210 attached **fails cleanly** at device
  lookup (caught by `UHD_SAFE_MAIN`, no crash) — confirms error handling
  works at least up through device-open.

**Not verified — no hardware or display available in this environment:**
- Actual streaming behavior: real burst timing accuracy, LO lock timing,
  TX underflow/RX overflow behavior under real load.
- The GUI actually rendering (no display attached here) — window
  creation, plot rendering, and frame timing are unexercised.
- Whether the default gain values produce a usable, non-clipped loopback
  signal on real hardware (they're explicitly labeled as placeholders).

## 8. Caveats to keep in mind

- **RX streams through retune transients.** Since the RX thread never
  stops/restarts across the cycle, a few samples right after a retune
  (before the new LO has settled) will land in the RX ring buffer and
  could show briefly as a glitch/spike on the waveform or FFT. It's
  cosmetic and self-clears within a buffer or two — not filtered out.
- **Frequencies are literal round GHz values** (2.4e9 / 5.1e9 / 5.8e9), not
  tied to any standard's actual channel plan (e.g. real WiFi ch. 1 is
  2.412 GHz). If you meant specific channel center frequencies, update
  `default_bands()` in `main.cpp`.
- **FFTW_MEASURE at startup**: each `FftProcessor` (one for TX, one for
  RX) runs FFTW's brief self-benchmarking pass on construction, which can
  add up to roughly a second of extra startup latency before the window
  appears. Switch to `FFTW_ESTIMATE` in `fft_processor.cpp` for instant
  startup if that delay is annoying — costs a slightly less-optimized FFT
  plan.
- **No link budget was computed.** The 30 dB pad plus the suggested gain
  values are a starting point, not a calculated-safe number for your
  specific TX max output power. Verify empirically (start low, watch for
  RX clipping) before assuming any gain setting is safe — see the
  calibration section in README.
- **Single-device only.** `device_args` assumes exactly one B210 unless
  you pass a serial. Not built for multi-device setups.
- **No persistent logging.** Status (band, lock, error counters) is only
  ever shown live in the GUI/stderr; nothing is written to disk for
  post-run analysis. Add it if you want a run history.
- **No automated or hardware-in-the-loop tests** — only the build-level
  smoke test described above.
- **Regulatory**: this assumes a cabled/loopback bench setup. If antenna
  connectors ever get attached instead of the loopback cable, 2.4/5.1/5.8
  GHz overlap licensed WiFi/U-NII spectrum in most jurisdictions.
