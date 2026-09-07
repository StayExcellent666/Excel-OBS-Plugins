#include "audio-analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace spectrum_canvas {
namespace {
constexpr float kPi = 3.14159265358979323846f;

float smoothing_alpha(float elapsed_seconds, float milliseconds)
{
  if (milliseconds <= 0.0f)
    return 1.0f;
  const float seconds = milliseconds * 0.001f;
  return 1.0f - std::exp(-std::max(0.0f, elapsed_seconds) / seconds);
}
} // namespace

AudioAnalyzer::AudioAnalyzer() : ring_(kRingCapacity, 0.0f) {}

std::size_t AudioAnalyzer::sanitize_fft_size(std::size_t value)
{
  value = std::clamp<std::size_t>(value, 256, 8192);
  std::size_t result = 256;
  while ((result << 1U) <= value)
    result <<= 1U;
  return result;
}

void AudioAnalyzer::set_config(const AnalyzerConfig &config)
{
  std::lock_guard<std::mutex> lock(mutex_);
  config_ = config;
  config_.fft_size = sanitize_fft_size(config_.fft_size);
  config_.bands = std::clamp<std::size_t>(config_.bands, 4, 256);
  config_.waveform_points =
      std::clamp<std::size_t>(config_.waveform_points, 32, 2048);
  config_.sample_rate = std::max(config_.sample_rate, 8000.0f);
  config_.min_frequency = std::max(config_.min_frequency, 1.0f);
  config_.max_frequency = std::clamp(
      config_.max_frequency, config_.min_frequency + 1.0f,
      config_.sample_rate * 0.5f);
  if (config_.ceiling_db <= config_.floor_db)
    config_.ceiling_db = config_.floor_db + 1.0f;

  if (smoothed_.size() != config_.bands) {
    smoothed_.assign(config_.bands, 0.0f);
    peaks_.assign(config_.bands, 0.0f);
    peak_holds_.assign(config_.bands, 0.0f);
  }
}

AnalyzerConfig AudioAnalyzer::config() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return config_;
}

void AudioAnalyzer::push_planar(const float *const *channels,
                                std::size_t channel_count,
                                std::size_t frames, bool muted)
{
  if (frames == 0)
    return;

  std::lock_guard<std::mutex> lock(mutex_);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    float sample = 0.0f;
    if (!muted && channels && channel_count > 0) {
      switch (config_.channel_mode) {
      case ChannelMode::Left:
        sample = channels[0] ? channels[0][frame] : 0.0f;
        break;
      case ChannelMode::Right: {
        const std::size_t right = channel_count > 1 ? 1 : 0;
        sample = channels[right] ? channels[right][frame] : 0.0f;
        break;
      }
      case ChannelMode::Mix: {
        std::size_t valid = 0;
        for (std::size_t channel = 0; channel < channel_count; ++channel) {
          if (channels[channel]) {
            sample += channels[channel][frame];
            ++valid;
          }
        }
        if (valid > 0)
          sample /= static_cast<float>(valid);
        break;
      }
      }
    }

    ring_[write_pos_] = std::isfinite(sample) ? sample : 0.0f;
    write_pos_ = (write_pos_ + 1) % ring_.size();
    available_ = std::min(available_ + 1, ring_.size());
  }
}

void AudioAnalyzer::clear()
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::fill(ring_.begin(), ring_.end(), 0.0f);
  std::fill(smoothed_.begin(), smoothed_.end(), 0.0f);
  std::fill(peaks_.begin(), peaks_.end(), 0.0f);
  std::fill(peak_holds_.begin(), peak_holds_.end(), 0.0f);
  write_pos_ = 0;
  available_ = 0;
}

float AudioAnalyzer::clamp01(float value)
{
  return std::clamp(value, 0.0f, 1.0f);
}

void AudioAnalyzer::fft(std::vector<std::complex<float>> &values)
{
  const std::size_t count = values.size();
  for (std::size_t i = 1, j = 0; i < count; ++i) {
    std::size_t bit = count >> 1U;
    for (; j & bit; bit >>= 1U)
      j ^= bit;
    j ^= bit;
    if (i < j)
      std::swap(values[i], values[j]);
  }

  for (std::size_t length = 2; length <= count; length <<= 1U) {
    const float angle = -2.0f * kPi / static_cast<float>(length);
    const std::complex<float> root(std::cos(angle), std::sin(angle));
    for (std::size_t start = 0; start < count; start += length) {
      std::complex<float> twiddle(1.0f, 0.0f);
      for (std::size_t offset = 0; offset < length / 2; ++offset) {
        const auto even = values[start + offset];
        const auto odd = values[start + offset + length / 2] * twiddle;
        values[start + offset] = even + odd;
        values[start + offset + length / 2] = even - odd;
        twiddle *= root;
      }
    }
  }
}

AnalysisSnapshot AudioAnalyzer::analyze(float elapsed_seconds)
{
  AnalyzerConfig config;
  std::vector<float> samples;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    config = config_;
    samples.assign(config.fft_size, 0.0f);
    const std::size_t copy_count = std::min(available_, config.fft_size);
    const std::size_t zero_count = config.fft_size - copy_count;
    const std::size_t start =
        (write_pos_ + ring_.size() - copy_count) % ring_.size();
    for (std::size_t i = 0; i < copy_count; ++i)
      samples[zero_count + i] = ring_[(start + i) % ring_.size()];
  }

  AnalysisSnapshot snapshot;
  snapshot.waveform.resize(config.waveform_points, 0.0f);
  const std::size_t waveform_source = std::min(samples.size(), config.waveform_points * 4);
  const std::size_t waveform_start = samples.size() - waveform_source;
  for (std::size_t point = 0; point < config.waveform_points; ++point) {
    const std::size_t begin = waveform_start + point * waveform_source /
                                                  config.waveform_points;
    const std::size_t end = waveform_start + (point + 1) * waveform_source /
                                                config.waveform_points;
    float strongest = 0.0f;
    const std::size_t safe_begin = std::min(begin, samples.size() - 1);
    const std::size_t safe_end =
        std::min(samples.size(), std::max(safe_begin + 1, end));
    for (std::size_t i = safe_begin; i < safe_end; ++i) {
      if (std::abs(samples[i]) > std::abs(strongest))
        strongest = samples[i];
    }
    snapshot.waveform[point] = std::clamp(strongest, -1.0f, 1.0f);
  }

  double energy = 0.0;
  for (float sample : samples)
    energy += static_cast<double>(sample) * sample;
  snapshot.rms = static_cast<float>(
      std::sqrt(energy / std::max<std::size_t>(1, samples.size())));

  std::vector<std::complex<float>> bins(config.fft_size);
  for (std::size_t i = 0; i < config.fft_size; ++i) {
    const float window = 0.5f -
                         0.5f * std::cos(2.0f * kPi * static_cast<float>(i) /
                                         static_cast<float>(config.fft_size - 1));
    bins[i] = std::complex<float>(samples[i] * window, 0.0f);
  }
  fft(bins);

  std::vector<float> target(config.bands, 0.0f);
  const float nyquist = config.sample_rate * 0.5f;
  const float max_frequency = std::min(config.max_frequency, nyquist);
  for (std::size_t band = 0; band < config.bands; ++band) {
    const float t0 = static_cast<float>(band) / static_cast<float>(config.bands);
    const float t1 = static_cast<float>(band + 1) /
                     static_cast<float>(config.bands);
    const auto frequency_at = [&](float t) {
      if (!config.logarithmic)
        return config.min_frequency +
               (max_frequency - config.min_frequency) * t;
      return config.min_frequency *
             std::pow(max_frequency / config.min_frequency, t);
    };
    const float frequency0 = frequency_at(t0);
    const float frequency1 = frequency_at(t1);
    std::size_t bin0 = static_cast<std::size_t>(
        std::floor(frequency0 * config.fft_size / config.sample_rate));
    std::size_t bin1 = static_cast<std::size_t>(
        std::ceil(frequency1 * config.fft_size / config.sample_rate));
    bin0 = std::clamp<std::size_t>(bin0, 1, config.fft_size / 2 - 1);
    bin1 = std::clamp<std::size_t>(bin1, bin0 + 1, config.fft_size / 2);

    float magnitude = 0.0f;
    for (std::size_t bin = bin0; bin < bin1; ++bin)
      magnitude = std::max(magnitude, std::abs(bins[bin]));
    magnitude = magnitude * 4.0f / static_cast<float>(config.fft_size);
    const float db = 20.0f * std::log10(std::max(magnitude, 1.0e-8f)) +
                     config.gain_db;
    target[band] = clamp01((db - config.floor_db) /
                           (config.ceiling_db - config.floor_db));
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (smoothed_.size() != config.bands) {
      smoothed_.assign(config.bands, 0.0f);
      peaks_.assign(config.bands, 0.0f);
      peak_holds_.assign(config.bands, 0.0f);
    }
    for (std::size_t band = 0; band < config.bands; ++band) {
      const float time = target[band] > smoothed_[band] ? config.attack_ms
                                                        : config.release_ms;
      smoothed_[band] +=
          (target[band] - smoothed_[band]) * smoothing_alpha(elapsed_seconds, time);

      if (smoothed_[band] >= peaks_[band]) {
        peaks_[band] = smoothed_[band];
        peak_holds_[band] = config.peak_hold_ms * 0.001f;
      } else if (peak_holds_[band] > 0.0f) {
        peak_holds_[band] -= elapsed_seconds;
      } else {
        peaks_[band] = std::max(smoothed_[band],
                                peaks_[band] - config.peak_decay_per_second *
                                                   elapsed_seconds);
      }
    }
    snapshot.spectrum = smoothed_;
    snapshot.peaks = peaks_;
  }
  return snapshot;
}

} // namespace spectrum_canvas
