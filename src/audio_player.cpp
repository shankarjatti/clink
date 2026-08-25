// audio_player.cpp

#include "audio_player.h"

#include <alsa/asoundlib.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>

namespace
{
float rand_f()
{
    return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
}
} // namespace

AudioPlayer::AudioPlayer()
{
    generate_preset_audio();
    stop_thread_ = false;
    audio_thread_ = std::thread(&AudioPlayer::audio_thread_loop, this);
}

AudioPlayer::~AudioPlayer()
{
    stop_thread_ = true;
    if (audio_thread_.joinable()) {
        audio_thread_.join();
    }
}

void AudioPlayer::generate_preset_audio()
{
    constexpr float kDurationSec = 8.0f;
    total_samples_ = static_cast<size_t>(kSampleRate * kDurationSec);
    pcm_samples_.resize(total_samples_, 0);

    for (size_t i = 0; i < total_samples_; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        float sample = 0.0f;

        if (t < 0.25f) {
            // Radio Key Preamble Chime (1200 Hz + 1800 Hz)
            float env = std::sin((t / 0.25f) * static_cast<float>(M_PI));
            sample = (std::sin(2.0f * static_cast<float>(M_PI) * 1200.0f * t) * 0.5f +
                      std::sin(2.0f * static_cast<float>(M_PI) * 1800.0f * t) * 0.4f) * env;
        } else if (t >= 0.35f && t < 3.6f) {
            // Voice Transmission 1: "Tactical Control, Unit 4 active on frequency"
            float vt = t - 0.35f;
            float syllable_env = std::abs(std::sin(2.0f * static_cast<float>(M_PI) * 3.5f * vt)) *
                                 (0.6f + 0.4f * std::sin(2.0f * static_cast<float>(M_PI) * 7.0f * vt));
            float f0 = 180.0f + 25.0f * std::sin(2.0f * static_cast<float>(M_PI) * 2.0f * vt); // Pitch
            float voice = std::sin(2.0f * static_cast<float>(M_PI) * f0 * vt) * 0.4f +
                          std::sin(2.0f * static_cast<float>(M_PI) * (f0 * 2.0f) * vt) * 0.3f +
                          std::sin(2.0f * static_cast<float>(M_PI) * 850.0f * vt) * 0.25f +  // Formant F1
                          std::sin(2.0f * static_cast<float>(M_PI) * 1650.0f * vt) * 0.18f + // Formant F2
                          (rand_f() * 2.0f - 1.0f) * 0.08f;                                  // DMR vocoder noise
            sample = voice * syllable_env * 0.85f;
        } else if (t >= 3.6f && t < 4.1f) {
            // Channel Pause & Background carrier hiss
            sample = (rand_f() * 2.0f - 1.0f) * 0.03f;
        } else if (t >= 4.1f && t < 7.4f) {
            // Voice Transmission 2: "Unit 4 acknowledged, maintain surveillance sector Bravo"
            float vt = t - 4.1f;
            float syllable_env = std::abs(std::sin(2.0f * static_cast<float>(M_PI) * 4.0f * vt)) *
                                 (0.7f + 0.3f * std::sin(2.0f * static_cast<float>(M_PI) * 8.5f * vt));
            float f0 = 160.0f + 30.0f * std::sin(2.0f * static_cast<float>(M_PI) * 1.8f * vt);
            float voice = std::sin(2.0f * static_cast<float>(M_PI) * f0 * vt) * 0.4f +
                          std::sin(2.0f * static_cast<float>(M_PI) * (f0 * 2.0f) * vt) * 0.3f +
                          std::sin(2.0f * static_cast<float>(M_PI) * 750.0f * vt) * 0.28f +
                          std::sin(2.0f * static_cast<float>(M_PI) * 1900.0f * vt) * 0.20f +
                          (rand_f() * 2.0f - 1.0f) * 0.08f;
            sample = voice * syllable_env * 0.85f;
        } else if (t >= 7.4f && t < 7.8f) {
            // Squelch Tail Burst & Roger Beep
            float st = t - 7.4f;
            float env = 1.0f - (st / 0.4f);
            sample = (std::sin(2.0f * static_cast<float>(M_PI) * 600.0f * st) * 0.3f +
                      (rand_f() * 2.0f - 1.0f) * 0.35f) * env;
        }

        // Clamp to [-1.0, 1.0] and store as int16_t
        sample = std::max(-1.0f, std::min(1.0f, sample));
        pcm_samples_[i] = static_cast<int16_t>(sample * 30000.0f);
    }
}

void AudioPlayer::play()
{
    is_playing_ = true;
}

void AudioPlayer::pause()
{
    is_playing_ = false;
}

void AudioPlayer::toggle_play_pause()
{
    is_playing_ = !is_playing_.load();
}

void AudioPlayer::stop()
{
    is_playing_ = false;
    playhead_ = 0;
    live_vu_ = 0.0f;
}

void AudioPlayer::seek_normalized(float norm_01)
{
    if (total_samples_ == 0) return;
    float clamped = std::max(0.0f, std::min(1.0f, norm_01));
    playhead_ = static_cast<size_t>(clamped * (total_samples_ - 1));
}

void AudioPlayer::set_volume(float vol_01)
{
    volume_ = std::max(0.0f, std::min(1.0f, vol_01));
}

void AudioPlayer::set_loop(bool loop)
{
    is_loop_ = loop;
}

void AudioPlayer::set_muted(bool mute)
{
    is_muted_ = mute;
}

float AudioPlayer::progress() const
{
    if (total_samples_ == 0) return 0.0f;
    return static_cast<float>(playhead_.load()) / static_cast<float>(total_samples_);
}

float AudioPlayer::current_time_sec() const
{
    return static_cast<float>(playhead_.load()) / static_cast<float>(kSampleRate);
}

float AudioPlayer::total_duration_sec() const
{
    return static_cast<float>(total_samples_) / static_cast<float>(kSampleRate);
}

void AudioPlayer::get_visualizer_waveform(float* out_buf, size_t count)
{
    if (!out_buf || count == 0 || total_samples_ == 0) return;
    size_t cur = playhead_.load();
    bool playing = is_playing_.load();
    float vol = is_muted_.load() ? 0.0f : volume_.load();

    for (size_t i = 0; i < count; ++i) {
        if (!playing) {
            out_buf[i] = 0.0f;
        } else {
            size_t idx = (cur + i * 8) % total_samples_;
            out_buf[i] = (static_cast<float>(pcm_samples_[idx]) / 32768.0f) * vol;
        }
    }
}

void AudioPlayer::audio_thread_loop()
{
    snd_pcm_t* pcm_handle = nullptr;
    bool has_alsa = false;

    // Attempt to open default ALSA PCM device
    if (snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) == 0) {
        if (snd_pcm_set_params(pcm_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                               1, kSampleRate, 1, 50000) == 0) {
            has_alsa = true;
            std::cout << "[AudioPlayer] ALSA hardware audio playback device opened (16000 Hz Mono)\n";
        } else {
            snd_pcm_close(pcm_handle);
            pcm_handle = nullptr;
        }
    }

    constexpr size_t kChunkSize = 512;
    std::vector<int16_t> chunk(kChunkSize, 0);

    while (!stop_thread_.load()) {
        if (!is_playing_.load()) {
            live_vu_ = 0.0f;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        size_t cur = playhead_.load();
        float vol = is_muted_.load() ? 0.0f : volume_.load();
        double sum_sq = 0.0;

        for (size_t i = 0; i < kChunkSize; ++i) {
            size_t idx = cur + i;
            if (idx < total_samples_) {
                int16_t s = static_cast<int16_t>(static_cast<float>(pcm_samples_[idx]) * vol);
                chunk[i] = s;
                float norm = static_cast<float>(s) / 32768.0f;
                sum_sq += norm * norm;
            } else {
                chunk[i] = 0;
            }
        }

        float rms = static_cast<float>(std::sqrt(sum_sq / kChunkSize));
        live_vu_ = std::min(1.0f, rms * 2.8f);

        // Advance playhead
        size_t next_head = cur + kChunkSize;
        if (next_head >= total_samples_) {
            if (is_loop_.load()) {
                playhead_ = 0;
            } else {
                playhead_ = 0;
                is_playing_ = false;
            }
        } else {
            playhead_ = next_head;
        }

        if (has_alsa && pcm_handle) {
            snd_pcm_sframes_t frames = snd_pcm_writei(pcm_handle, chunk.data(), kChunkSize);
            if (frames < 0) {
                snd_pcm_prepare(pcm_handle);
            }
        } else {
            // Software pace: 512 samples / 16000 Hz = 32 ms
            std::this_thread::sleep_for(std::chrono::milliseconds(32));
        }
    }

    if (pcm_handle) {
        snd_pcm_drain(pcm_handle);
        snd_pcm_close(pcm_handle);
    }
}
