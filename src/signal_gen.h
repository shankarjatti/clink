// signal_gen.h
//
// Generates a complex baseband sine tone (10 kHz by default) sample-by-sample
// using a running phase accumulator. Using an accumulator instead of
// re-deriving phase from an absolute sample index means consecutive bursts
// are phase-continuous with each other (no click/discontinuity at burst
// boundaries) while still being trivial to reset if a clean restart is ever
// wanted between frequency hops.

#pragma once

#include <complex>
#include <cstddef>
#include <vector>

class ToneGenerator
{
public:
    ToneGenerator(double tone_freq_hz, double sample_rate_hz)
        : phase_inc_(2.0 * M_PI * tone_freq_hz / sample_rate_hz)
    {
    }

    // Fills `out` with `count` complex samples continuing from wherever the
    // internal phase accumulator left off.
    void generate(std::complex<float>* out, size_t count)
    {
        for (size_t i = 0; i < count; ++i) {
            out[i] = std::complex<float>(
                static_cast<float>(std::cos(phase_)),
                static_cast<float>(std::sin(phase_)));
            phase_ += phase_inc_;
            if (phase_ > 2.0 * M_PI) {
                phase_ -= 2.0 * M_PI;
            }
        }
    }

    // Pre-generate a full burst's worth of samples (used once at startup;
    // the same buffer is replayed for every burst since the tone frequency
    // never changes, only the RF carrier does).
    std::vector<std::complex<float>> generate_buffer(size_t count)
    {
        std::vector<std::complex<float>> buf(count);
        generate(buf.data(), count);
        return buf;
    }

    void reset_phase() { phase_ = 0.0; }

private:
    double phase_inc_;
    double phase_ = 0.0;
};
