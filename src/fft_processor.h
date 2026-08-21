// fft_processor.h
//
// Computes a Hann-windowed magnitude spectrum (in dB) from a block of
// complex IQ samples, using FFTW3. One instance is used for TX, another for
// RX; both are driven from the GUI thread each redraw (FFT size is small
// enough - a few thousand points - that recomputing at ~30-60 Hz is cheap).

#pragma once

#include <complex>
#include <vector>

#include <fftw3.h>

class FftProcessor
{
public:
    explicit FftProcessor(size_t fft_size);
    ~FftProcessor();

    FftProcessor(const FftProcessor&) = delete;
    FftProcessor& operator=(const FftProcessor&) = delete;

    // Input must contain exactly fft_size() complex samples. Output is
    // fft_size() magnitude values in dB, FFT-shifted so bin 0 is the most
    // negative frequency and the center bin is DC - convenient for
    // straight-line plotting against a symmetric frequency axis.
    void compute(const std::complex<float>* input, std::vector<float>& out_db,
                 double sample_rate_hz, std::vector<float>& out_freq_axis_hz);

    size_t fft_size() const { return fft_size_; }

private:
    size_t fft_size_;
    std::vector<float> window_;      // precomputed Hann window
    double window_power_correction_; // coherent gain correction for the window

    fftwf_complex* in_;
    fftwf_complex* out_;
    fftwf_plan plan_;
};
