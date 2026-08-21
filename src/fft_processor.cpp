// fft_processor.cpp

#include "fft_processor.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace
{
static std::mutex g_fftw_planner_mutex;
} // namespace

FftProcessor::FftProcessor(size_t fft_size) : fft_size_(fft_size)
{
    in_ = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * fft_size_));
    out_ = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * fft_size_));

    {
        std::lock_guard<std::mutex> lock(g_fftw_planner_mutex);
        plan_ = fftwf_plan_dft_1d(static_cast<int>(fft_size_), in_, out_, FFTW_FORWARD,
                                   FFTW_ESTIMATE);
    }

    // Precompute a Hann window and its coherent-gain correction factor so
    // magnitude values reflect the input amplitude correctly rather than
    // being suppressed by the window's average attenuation.
    window_.resize(fft_size_);
    double sum = 0.0;
    for (size_t i = 0; i < fft_size_; ++i) {
        double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (fft_size_ - 1));
        window_[i] = static_cast<float>(w);
        sum += w;
    }
    double mean_window = sum / fft_size_;
    window_power_correction_ = 1.0 / mean_window;
}

FftProcessor::~FftProcessor()
{
    {
        std::lock_guard<std::mutex> lock(g_fftw_planner_mutex);
        fftwf_destroy_plan(plan_);
    }
    fftwf_free(in_);
    fftwf_free(out_);
}

void FftProcessor::compute(const std::complex<float>* input,
                            std::vector<float>& out_db, double sample_rate_hz,
                            std::vector<float>& out_freq_axis_hz)
{
    for (size_t i = 0; i < fft_size_; ++i) {
        float w = window_[i];
        in_[i][0] = input[i].real() * w;
        in_[i][1] = input[i].imag() * w;
    }

    fftwf_execute(plan_);

    out_db.resize(fft_size_);
    out_freq_axis_hz.resize(fft_size_);

    const double bin_mhz = (sample_rate_hz / 1e6) / static_cast<double>(fft_size_);
    const size_t half = fft_size_ / 2;

    for (size_t i = 0; i < fft_size_; ++i) {
        // FFT-shift: bin (i + half) mod N maps output index i to a
        // baseband frequency axis in MHz that runs from -Fs/2 to +Fs/2.
        size_t src = (i + half) % fft_size_;
        double re = out_[src][0] * window_power_correction_;
        double im = out_[src][1] * window_power_correction_;
        double mag = std::sqrt(re * re + im * im) / static_cast<double>(fft_size_);
        double mag_db = 20.0 * std::log10(std::max(mag, 1e-12));
        out_db[i] = static_cast<float>(mag_db);

        double freq_mhz = (static_cast<double>(i) - static_cast<double>(half)) * bin_mhz;
        out_freq_axis_hz[i] = static_cast<float>(freq_mhz);
    }
}
