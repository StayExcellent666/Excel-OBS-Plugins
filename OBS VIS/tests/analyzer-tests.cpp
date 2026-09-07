#include "audio-analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

using spectrum_canvas::AnalyzerConfig;
using spectrum_canvas::AudioAnalyzer;

namespace {
constexpr float kPi = 3.14159265358979323846f;

void require(bool condition, const char *message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}
} // namespace

int main()
{
  AudioAnalyzer analyzer;
  AnalyzerConfig config;
  config.fft_size = 2048;
  config.bands = 48;
  config.sample_rate = 48000.0f;
  config.min_frequency = 40.0f;
  config.max_frequency = 16000.0f;
  config.floor_db = -90.0f;
  config.ceiling_db = 0.0f;
  config.attack_ms = 0.0f;
  config.release_ms = 0.0f;
  analyzer.set_config(config);

  std::vector<float> left(4096);
  std::vector<float> right(4096);
  for (std::size_t i = 0; i < left.size(); ++i) {
    left[i] = 0.8f * std::sin(2.0f * kPi * 1000.0f *
                             static_cast<float>(i) / config.sample_rate);
    right[i] = left[i];
  }
  const float *channels[] = {left.data(), right.data()};
  analyzer.push_planar(channels, 2, left.size());
  const auto snapshot = analyzer.analyze(1.0f / 60.0f);

  require(snapshot.spectrum.size() == config.bands,
          "spectrum has configured band count");
  require(snapshot.peaks.size() == config.bands,
          "peaks have configured band count");
  require(snapshot.waveform.size() == config.waveform_points,
          "waveform has configured point count");
  require(snapshot.rms > 0.4f && snapshot.rms < 0.7f,
          "RMS matches a loud sine wave");

  const auto loudest = std::max_element(snapshot.spectrum.begin(),
                                        snapshot.spectrum.end());
  const std::size_t loudest_band =
      static_cast<std::size_t>(loudest - snapshot.spectrum.begin());
  const float band_t = (static_cast<float>(loudest_band) + 0.5f) /
                       static_cast<float>(config.bands);
  const float estimated_frequency =
      config.min_frequency *
      std::pow(config.max_frequency / config.min_frequency, band_t);
  require(estimated_frequency > 750.0f && estimated_frequency < 1350.0f,
          "FFT identifies the 1 kHz band");

  analyzer.clear();
  analyzer.push_planar(channels, 2, left.size(), true);
  const auto muted = analyzer.analyze(1.0f);
  require(*std::max_element(muted.spectrum.begin(), muted.spectrum.end()) <
              0.001f,
          "muted audio produces silence");

  std::cout << "All analyzer tests passed\n";
  return 0;
}
