#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace spectrum_canvas {

enum class ChannelMode { Mix, Left, Right };

struct AnalyzerConfig {
  std::size_t fft_size = 2048;
  std::size_t bands = 64;
  std::size_t waveform_points = 512;
  float sample_rate = 48000.0f;
  float min_frequency = 40.0f;
  float max_frequency = 16000.0f;
  float floor_db = -72.0f;
  float ceiling_db = -6.0f;
  float gain_db = 0.0f;
  float attack_ms = 45.0f;
  float release_ms = 240.0f;
  float peak_hold_ms = 180.0f;
  float peak_decay_per_second = 1.2f;
  bool logarithmic = true;
  ChannelMode channel_mode = ChannelMode::Mix;
};

struct AnalysisSnapshot {
  std::vector<float> spectrum;
  std::vector<float> peaks;
  std::vector<float> waveform;
  float rms = 0.0f;
};

class AudioAnalyzer {
public:
  AudioAnalyzer();

  void set_config(const AnalyzerConfig &config);
  AnalyzerConfig config() const;

  // OBS supplies planar float audio. This method performs only downmixing and
  // ring-buffer writes so it is safe to call from the audio callback.
  void push_planar(const float *const *channels, std::size_t channel_count,
                   std::size_t frames, bool muted = false);
  void clear();

  // Run from the video thread, never from the audio callback.
  AnalysisSnapshot analyze(float elapsed_seconds);

private:
  static constexpr std::size_t kRingCapacity = 32768;

  mutable std::mutex mutex_;
  AnalyzerConfig config_;
  std::vector<float> ring_;
  std::size_t write_pos_ = 0;
  std::size_t available_ = 0;
  std::vector<float> smoothed_;
  std::vector<float> peaks_;
  std::vector<float> peak_holds_;

  static std::size_t sanitize_fft_size(std::size_t value);
  static void fft(std::vector<std::complex<float>> &values);
  static float clamp01(float value);
};

} // namespace spectrum_canvas
