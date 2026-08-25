// audio_player.h
//
// Thread-safe Audio Player with preset tactical radio audio (DMR/LMR dispatch),
// interactive playback controls (Play, Pause, Stop, Seek, Volume, Mute, Loop),
// ALSA hardware playback with graceful headless software clock fallback,
// and real-time visualizer waveform/VU metrics.

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class AudioPlayer
{
public:
    AudioPlayer();
    ~AudioPlayer();

    void play();
    void pause();
    void toggle_play_pause();
    void stop();

    void seek_normalized(float norm_01);
    void set_volume(float vol_01);
    float volume() const { return volume_.load(); }

    void set_loop(bool loop);
    bool is_loop() const { return is_loop_.load(); }

    void set_muted(bool mute);
    bool is_muted() const { return is_muted_.load(); }

    bool is_playing() const { return is_playing_.load(); }
    float progress() const;
    float current_time_sec() const;
    float total_duration_sec() const;

    float live_vu_level() const { return live_vu_.load(); }
    void get_visualizer_waveform(float* out_buf, size_t count);

private:
    void generate_preset_audio();
    void audio_thread_loop();

    std::vector<int16_t> pcm_samples_;
    size_t total_samples_ = 0;
    static constexpr int kSampleRate = 16000;

    std::atomic<size_t> playhead_{0};
    std::atomic<bool> is_playing_{false};
    std::atomic<bool> is_loop_{true};
    std::atomic<bool> is_muted_{false};
    std::atomic<float> volume_{0.8f};
    std::atomic<float> live_vu_{0.0f};

    std::atomic<bool> stop_thread_{false};
    std::thread audio_thread_;
    mutable std::mutex audio_mutex_;
};
