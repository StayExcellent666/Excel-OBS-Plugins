#include "visualizer-source.hpp"

#include "audio-analyzer.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
using spectrum_canvas::AnalysisSnapshot;
using spectrum_canvas::AnalyzerConfig;
using spectrum_canvas::AudioAnalyzer;
using spectrum_canvas::ChannelMode;

constexpr float kPi = 3.14159265358979323846f;
constexpr const char *kSourceId = "spectrum_canvas_source";

struct Point {
  float x;
  float y;
};

struct Vertex {
  Point point;
  uint32_t color;
};

enum class Style { Bars, MirrorBars, Waveform, RadialBars, RadialLine, Dots };
enum class Overlay { None, Line, Waveform, Dots };
enum class ColorMode { Solid, Gradient, Rainbow };
enum class MirrorMode { Vertical, Horizontal, Both };

struct RenderSettings {
  uint32_t width = 1280;
  uint32_t height = 360;
  Style style = Style::Bars;
  Overlay overlay = Overlay::None;
  ColorMode color_mode = ColorMode::Gradient;
  bool gradient_by_level = false;
  MirrorMode mirror_mode = MirrorMode::Both;
  bool bass_outside = false;
  bool rounded_caps = false;
  bool glow_enabled = false;
  float glow_size = 14.0f;
  float glow_intensity = 0.75f;
  uint32_t color_a = 0xFFFFA63D;
  uint32_t color_b = 0xFFFF3DDB;
  uint32_t background = 0x00000000;
  float opacity = 1.0f;
  float padding = 18.0f;
  float gap_percent = 20.0f;
  float minimum_height = 2.0f;
  float line_thickness = 5.0f;
  float dot_size = 8.0f;
  float waveform_gain = 1.0f;
  float inner_radius_percent = 24.0f;
  float radial_length_percent = 42.0f;
  float start_angle_degrees = -90.0f;
  float arc_degrees = 360.0f;
  float rotation_speed = 0.0f;
  float hue_offset = 0.0f;
  bool show_peaks = false;
  float peak_thickness = 3.0f;
  bool invert = false;
};

struct VisualizerSource {
  obs_source_t *context = nullptr;
  obs_source_t *audio_source = nullptr;
  std::string audio_source_name;
  AudioAnalyzer analyzer;
  AnalysisSnapshot snapshot;
  RenderSettings render;
  float phase_degrees = 0.0f;
};

uint8_t byte_lerp(uint8_t a, uint8_t b, float t)
{
  return static_cast<uint8_t>(std::lround(static_cast<float>(a) +
                                          (static_cast<float>(b) - a) * t));
}

uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
  return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8U) |
         (static_cast<uint32_t>(b) << 16U) |
         (static_cast<uint32_t>(a) << 24U);
}

uint32_t with_opacity(uint32_t color, float opacity)
{
  const auto alpha = static_cast<uint8_t>((color >> 24U) & 0xffU);
  const auto adjusted = static_cast<uint8_t>(std::lround(
      static_cast<float>(alpha) * std::clamp(opacity, 0.0f, 1.0f)));
  return (color & 0x00ffffffU) | (static_cast<uint32_t>(adjusted) << 24U);
}

uint32_t lerp_color(uint32_t a, uint32_t b, float t)
{
  t = std::clamp(t, 0.0f, 1.0f);
  return rgba(byte_lerp(a & 0xffU, b & 0xffU, t),
              byte_lerp((a >> 8U) & 0xffU, (b >> 8U) & 0xffU, t),
              byte_lerp((a >> 16U) & 0xffU, (b >> 16U) & 0xffU, t),
              byte_lerp((a >> 24U) & 0xffU, (b >> 24U) & 0xffU, t));
}

uint32_t hsv_color(float hue, float saturation, float value, float opacity)
{
  hue = hue - std::floor(hue);
  const float h = hue * 6.0f;
  const int sector = static_cast<int>(std::floor(h));
  const float fraction = h - std::floor(h);
  const float p = value * (1.0f - saturation);
  const float q = value * (1.0f - saturation * fraction);
  const float t = value * (1.0f - saturation * (1.0f - fraction));
  float r = 0.0f, g = 0.0f, b = 0.0f;
  switch (sector % 6) {
  case 0: r = value; g = t; b = p; break;
  case 1: r = q; g = value; b = p; break;
  case 2: r = p; g = value; b = t; break;
  case 3: r = p; g = q; b = value; break;
  case 4: r = t; g = p; b = value; break;
  default: r = value; g = p; b = q; break;
  }
  return rgba(static_cast<uint8_t>(r * 255.0f),
              static_cast<uint8_t>(g * 255.0f),
              static_cast<uint8_t>(b * 255.0f),
              static_cast<uint8_t>(std::clamp(opacity, 0.0f, 1.0f) * 255.0f));
}

uint32_t style_color(const VisualizerSource &source, float position,
                     float level = 1.0f)
{
  uint32_t color = source.render.color_a;
  if (source.render.color_mode == ColorMode::Gradient)
    color = lerp_color(source.render.color_a, source.render.color_b,
                       source.render.gradient_by_level ? level : position);
  else if (source.render.color_mode == ColorMode::Rainbow)
    return hsv_color(position + source.render.hue_offset / 360.0f +
                         source.phase_degrees / 360.0f,
                     0.82f, 0.55f + 0.45f * level, source.render.opacity);
  return with_opacity(color, source.render.opacity);
}

void triangle(std::vector<Vertex> &vertices, Point a, Point b, Point c,
              uint32_t ca, uint32_t cb, uint32_t cc)
{
  vertices.push_back({a, ca});
  vertices.push_back({b, cb});
  vertices.push_back({c, cc});
}

void quad(std::vector<Vertex> &vertices, Point a, Point b, Point c, Point d,
          uint32_t ca, uint32_t cb, uint32_t cc, uint32_t cd)
{
  triangle(vertices, a, b, c, ca, cb, cc);
  triangle(vertices, a, c, d, ca, cc, cd);
}

void rectangle(std::vector<Vertex> &vertices, float x0, float y0, float x1,
               float y1, uint32_t bottom_color, uint32_t top_color)
{
  quad(vertices, {x0, y1}, {x0, y0}, {x1, y0}, {x1, y1}, bottom_color,
       top_color, top_color, bottom_color);
}

void vertical_bar(std::vector<Vertex> &vertices, float x0, float x1,
                  float base_y, float tip_y, uint32_t base_color,
                  uint32_t tip_color, bool rounded)
{
  if (!rounded || std::abs(tip_y - base_y) < 1.0f) {
    if (tip_y < base_y)
      rectangle(vertices, x0, tip_y, x1, base_y, base_color, tip_color);
    else
      rectangle(vertices, x0, base_y, x1, tip_y, tip_color, base_color);
    return;
  }

  const float radius_x = (x1 - x0) * 0.5f;
  const float radius_y =
      std::min(radius_x, std::abs(tip_y - base_y));
  const float center_x = (x0 + x1) * 0.5f;
  constexpr int segments = 10;
  if (tip_y < base_y) {
    const float center_y = tip_y + radius_y;
    rectangle(vertices, x0, center_y, x1, base_y, base_color, tip_color);
    for (int segment = 0; segment < segments; ++segment) {
      const float a0 = kPi + kPi * static_cast<float>(segment) / segments;
      const float a1 = kPi + kPi * static_cast<float>(segment + 1) / segments;
      triangle(vertices, {center_x, center_y},
               {center_x + std::cos(a0) * radius_x,
                center_y + std::sin(a0) * radius_y},
               {center_x + std::cos(a1) * radius_x,
                center_y + std::sin(a1) * radius_y},
               tip_color, tip_color, tip_color);
    }
  } else {
    const float center_y = tip_y - radius_y;
    rectangle(vertices, x0, base_y, x1, center_y, tip_color, base_color);
    for (int segment = 0; segment < segments; ++segment) {
      const float a0 = kPi * static_cast<float>(segment) / segments;
      const float a1 = kPi * static_cast<float>(segment + 1) / segments;
      triangle(vertices, {center_x, center_y},
               {center_x + std::cos(a0) * radius_x,
                center_y + std::sin(a0) * radius_y},
               {center_x + std::cos(a1) * radius_x,
                center_y + std::sin(a1) * radius_y},
               tip_color, tip_color, tip_color);
    }
  }
}

void vertical_bar_glow(std::vector<Vertex> &vertices, float x0, float x1,
                       float base_y, float tip_y, uint32_t color,
                       float glow_size, float intensity, bool rounded)
{
  const float bar_length = std::abs(tip_y - base_y);
  for (int layer = 4; layer >= 1; --layer) {
    const float requested = glow_size * static_cast<float>(layer) / 4.0f;
    const float expansion = std::min(requested, std::max(1.0f, bar_length * 0.45f));
    const float alpha = intensity * static_cast<float>(5 - layer) * 0.08f;
    const uint32_t glow_color = with_opacity(color, alpha);
    const bool points_up = tip_y < base_y;
    vertical_bar(vertices, x0 - expansion, x1 + expansion,
                 base_y + (points_up ? expansion * 0.25f : -expansion * 0.25f),
                 tip_y + (points_up ? -expansion : expansion), glow_color,
                 glow_color, rounded);
  }
}

void thick_line(std::vector<Vertex> &vertices, Point a, Point b,
                float thickness, uint32_t color_a, uint32_t color_b)
{
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  const float length = std::sqrt(dx * dx + dy * dy);
  if (length < 0.001f)
    return;
  const float nx = -dy / length * thickness * 0.5f;
  const float ny = dx / length * thickness * 0.5f;
  quad(vertices, {a.x + nx, a.y + ny}, {b.x + nx, b.y + ny},
       {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}, color_a, color_b,
       color_b, color_a);
}

void circle(std::vector<Vertex> &vertices, Point center, float radius,
            uint32_t color)
{
  constexpr int segments = 12;
  for (int segment = 0; segment < segments; ++segment) {
    const float a0 = 2.0f * kPi * static_cast<float>(segment) / segments;
    const float a1 = 2.0f * kPi * static_cast<float>(segment + 1) / segments;
    triangle(vertices, center,
             {center.x + std::cos(a0) * radius,
              center.y + std::sin(a0) * radius},
             {center.x + std::cos(a1) * radius,
              center.y + std::sin(a1) * radius},
             color, color, color);
  }
}

Point polar(Point center, float radius, float angle)
{
  return {center.x + std::cos(angle) * radius,
          center.y + std::sin(angle) * radius};
}

void render_bars(const VisualizerSource &source, std::vector<Vertex> &vertices,
                 bool horizontal_mirror, bool vertical_mirror)
{
  const auto &levels = source.snapshot.spectrum;
  if (levels.empty())
    return;
  const float width = static_cast<float>(source.render.width);
  const float height = static_cast<float>(source.render.height);
  const float padding = std::min(source.render.padding, height * 0.45f);
  const float usable_width = std::max(1.0f, width - 2.0f * padding);
  const std::size_t count = horizontal_mirror ? levels.size() * 2 : levels.size();
  const float slot = usable_width / static_cast<float>(count);
  const float bar_width = slot * (1.0f - source.render.gap_percent * 0.01f);
  const float usable_height = vertical_mirror ? (height * 0.5f - padding)
                                              : (height - 2.0f * padding);
  std::vector<Vertex> core_vertices;
  core_vertices.reserve(count * 72);

  for (std::size_t item = 0; item < count; ++item) {
    std::size_t band = item;
    if (horizontal_mirror) {
      const bool left_half = item < levels.size();
      if (!source.render.bass_outside)
        band = left_half ? levels.size() - 1 - item : item - levels.size();
      else
        band = left_half ? item : levels.size() * 2 - 1 - item;
    }
    const float level = levels[band];
    const float signal_height = level * std::max(0.0f, usable_height);
    const float x0 = padding + slot * static_cast<float>(item) +
                     (slot - bar_width) * 0.5f;
    const float x1 = x0 + std::max(0.5f, bar_width);
    const float idle_cap_height = source.render.rounded_caps
                                      ? std::clamp(bar_width * 0.5f, 2.0f, 6.0f)
                                      : 0.0f;
    const float value = std::max(
        {source.render.minimum_height, signal_height, idle_cap_height});
    const float position = count > 1 ? static_cast<float>(item) /
                                          static_cast<float>(count - 1)
                                     : 0.0f;
    const uint32_t low = style_color(source, position, 0.2f);
    const uint32_t high = style_color(source, position, level);
    const bool rounded_bar = source.render.rounded_caps;
    const bool glow_active = source.render.glow_enabled && value >= 0.5f;

    if (vertical_mirror) {
      const float center = height * 0.5f;
      if (glow_active) {
        vertical_bar_glow(vertices, x0, x1, center, center - value, high,
                          source.render.glow_size, source.render.glow_intensity,
                          rounded_bar);
        vertical_bar_glow(vertices, x0, x1, center, center + value, high,
                          source.render.glow_size, source.render.glow_intensity,
                          rounded_bar);
      }
      vertical_bar(core_vertices, x0, x1, center, center - value, low, high,
                   rounded_bar);
      vertical_bar(core_vertices, x0, x1, center, center + value, low, high,
                   rounded_bar);
    } else if (!source.render.invert) {
      if (glow_active)
        vertical_bar_glow(vertices, x0, x1, height - padding,
                          height - padding - value, high, source.render.glow_size,
                          source.render.glow_intensity, rounded_bar);
      vertical_bar(core_vertices, x0, x1, height - padding,
                   height - padding - value, low, high,
                   rounded_bar);
    } else {
      if (glow_active)
        vertical_bar_glow(vertices, x0, x1, padding, padding + value, high,
                          source.render.glow_size, source.render.glow_intensity,
                          rounded_bar);
      vertical_bar(core_vertices, x0, x1, padding, padding + value, low, high,
                   rounded_bar);
    }

    if (source.render.show_peaks && band < source.snapshot.peaks.size()) {
      const float peak = source.snapshot.peaks[band] * usable_height;
      const uint32_t peak_color = style_color(source, position, 1.0f);
      if (vertical_mirror) {
        rectangle(core_vertices, x0,
                  height * 0.5f - peak - source.render.peak_thickness, x1,
                  height * 0.5f - peak, peak_color, peak_color);
        rectangle(core_vertices, x0, height * 0.5f + peak, x1,
                  height * 0.5f + peak + source.render.peak_thickness,
                  peak_color, peak_color);
      } else {
        const float y = source.render.invert ? padding + peak
                                             : height - padding - peak;
        rectangle(core_vertices, x0,
                  y - source.render.peak_thickness * 0.5f, x1,
                  y + source.render.peak_thickness * 0.5f, peak_color, peak_color);
      }
    }
  }
  vertices.insert(vertices.end(), core_vertices.begin(), core_vertices.end());
}

void render_spectrum_line(const VisualizerSource &source,
                          std::vector<Vertex> &vertices, bool dots_only)
{
  const auto &levels = source.snapshot.spectrum;
  if (levels.empty())
    return;
  const float width = static_cast<float>(source.render.width);
  const float height = static_cast<float>(source.render.height);
  const float padding = source.render.padding;
  const float usable_height = std::max(1.0f, height - 2.0f * padding);
  std::vector<Point> points(levels.size());
  for (std::size_t band = 0; band < levels.size(); ++band) {
    const float t = levels.size() > 1 ? static_cast<float>(band) /
                                           static_cast<float>(levels.size() - 1)
                                     : 0.5f;
    const float y = source.render.invert
                        ? padding + levels[band] * usable_height
                        : height - padding - levels[band] * usable_height;
    points[band] = {padding + t * std::max(1.0f, width - 2.0f * padding), y};
    if (dots_only)
      circle(vertices, points[band], source.render.dot_size * 0.5f,
             style_color(source, t, levels[band]));
  }
  if (!dots_only) {
    for (std::size_t i = 1; i < points.size(); ++i) {
      const float t0 = static_cast<float>(i - 1) /
                       static_cast<float>(std::max<std::size_t>(1, points.size() - 1));
      const float t1 = static_cast<float>(i) /
                       static_cast<float>(std::max<std::size_t>(1, points.size() - 1));
      thick_line(vertices, points[i - 1], points[i], source.render.line_thickness,
                 style_color(source, t0, levels[i - 1]),
                 style_color(source, t1, levels[i]));
    }
  }
}

void render_waveform(const VisualizerSource &source,
                     std::vector<Vertex> &vertices)
{
  const auto &waveform = source.snapshot.waveform;
  if (waveform.size() < 2)
    return;
  const float width = static_cast<float>(source.render.width);
  const float height = static_cast<float>(source.render.height);
  const float center = height * 0.5f;
  const float amplitude = std::max(0.0f, center - source.render.padding) *
                          source.render.waveform_gain;
  Point previous{source.render.padding,
                 center - waveform[0] * amplitude};
  for (std::size_t i = 1; i < waveform.size(); ++i) {
    const float t = static_cast<float>(i) /
                    static_cast<float>(waveform.size() - 1);
    Point current{source.render.padding +
                      t * std::max(1.0f, width - 2.0f * source.render.padding),
                  center - waveform[i] * amplitude};
    thick_line(vertices, previous, current, source.render.line_thickness,
               style_color(source, t - 1.0f / waveform.size(),
                           std::abs(waveform[i - 1])),
               style_color(source, t, std::abs(waveform[i])));
    previous = current;
  }
}

void render_radial(const VisualizerSource &source, std::vector<Vertex> &vertices,
                   bool connected)
{
  const auto &levels = source.snapshot.spectrum;
  if (levels.empty())
    return;
  const Point center{source.render.width * 0.5f, source.render.height * 0.5f};
  const float size = static_cast<float>(std::min(source.render.width,
                                                  source.render.height));
  const float inner = size * source.render.inner_radius_percent * 0.01f;
  const float max_length = size * source.render.radial_length_percent * 0.01f;
  const float start = (source.render.start_angle_degrees + source.phase_degrees) *
                      kPi / 180.0f;
  const float arc = source.render.arc_degrees * kPi / 180.0f;
  std::vector<Point> outer(levels.size());
  std::vector<Vertex> core_vertices;
  core_vertices.reserve(levels.size() * 48);

  for (std::size_t band = 0; band < levels.size(); ++band) {
    const float t = levels.size() > 1 ? static_cast<float>(band) /
                                           static_cast<float>(levels.size() - 1)
                                     : 0.0f;
    const float angle = start + arc * t;
    const float signal_length = levels[band] * max_length;
    if (!connected) {
      const float half_angle = std::min(std::abs(arc) /
                                           static_cast<float>(levels.size()) *
                                           (1.0f - source.render.gap_percent * 0.01f) *
                                           0.5f,
                                       0.22f);
      const float estimated_width =
          2.0f * (inner + signal_length) * std::sin(half_angle);
      const float idle_cap_length =
          source.render.rounded_caps
              ? std::clamp(estimated_width * 0.5f, 2.0f, 6.0f)
              : 0.0f;
      const float bar_length = std::max(
          {source.render.minimum_height, signal_length, idle_cap_length});
      const float radius = inner + bar_length;
      outer[band] = polar(center, radius, angle);
      const bool rounded_bar = source.render.rounded_caps;
      const float cap_radius = rounded_bar
                                   ? std::min(radius * std::sin(half_angle),
                                              bar_length * 0.5f)
                                   : 0.0f;
      const float body_radius = radius - cap_radius;
      const Point a = polar(center, inner, angle - half_angle);
      const Point b = polar(center, body_radius, angle - half_angle);
      const Point c = polar(center, body_radius, angle + half_angle);
      const Point d = polar(center, inner, angle + half_angle);
      const uint32_t low = style_color(source, t, 0.2f);
      const uint32_t high = style_color(source, t, levels[band]);
      if (source.render.glow_enabled && bar_length >= 0.5f) {
        for (int layer = 4; layer >= 1; --layer) {
          const float requested = source.render.glow_size *
                                  static_cast<float>(layer) / 4.0f;
          const float expansion =
              std::min(requested, std::max(1.0f, bar_length * 0.45f));
          const float glow_angle = expansion / std::max(1.0f, radius);
          const float alpha = source.render.glow_intensity *
                              static_cast<float>(5 - layer) * 0.08f;
          const uint32_t glow_color = with_opacity(high, alpha);
          const Point ga = polar(center, std::max(0.0f, inner - expansion * 0.25f),
                                 angle - half_angle - glow_angle);
          const Point gb = polar(center, radius + expansion,
                                 angle - half_angle - glow_angle);
          const Point gc = polar(center, radius + expansion,
                                 angle + half_angle + glow_angle);
          const Point gd = polar(center, std::max(0.0f, inner - expansion * 0.25f),
                                 angle + half_angle + glow_angle);
          quad(vertices, ga, gb, gc, gd, glow_color, glow_color, glow_color,
               glow_color);
          if (rounded_bar)
            circle(vertices, polar(center, body_radius, angle),
                   cap_radius + expansion, glow_color);
        }
      }
      quad(core_vertices, a, b, c, d, low, high, high, low);
      if (rounded_bar && cap_radius > 0.0f)
        circle(core_vertices, polar(center, body_radius, angle), cap_radius,
               high);
    } else {
      outer[band] = polar(center, inner + signal_length, angle);
    }
  }

  vertices.insert(vertices.end(), core_vertices.begin(), core_vertices.end());

  if (connected) {
    for (std::size_t i = 1; i < outer.size(); ++i) {
      const float t0 = static_cast<float>(i - 1) /
                       static_cast<float>(std::max<std::size_t>(1, outer.size() - 1));
      const float t1 = static_cast<float>(i) /
                       static_cast<float>(std::max<std::size_t>(1, outer.size() - 1));
      thick_line(vertices, outer[i - 1], outer[i], source.render.line_thickness,
                 style_color(source, t0, levels[i - 1]),
                 style_color(source, t1, levels[i]));
    }
    if (std::abs(source.render.arc_degrees) >= 359.9f && outer.size() > 2)
      thick_line(vertices, outer.back(), outer.front(),
                 source.render.line_thickness,
                 style_color(source, 1.0f, levels.back()),
                 style_color(source, 0.0f, levels.front()));
  }
}

Style parse_style(const char *value)
{
  if (std::strcmp(value, "mirror_bars") == 0) return Style::MirrorBars;
  if (std::strcmp(value, "line") == 0) return Style::Waveform;
  if (std::strcmp(value, "waveform") == 0) return Style::Waveform;
  if (std::strcmp(value, "radial_bars") == 0) return Style::RadialBars;
  if (std::strcmp(value, "radial_line") == 0) return Style::RadialLine;
  if (std::strcmp(value, "dots") == 0) return Style::Dots;
  return Style::Bars;
}

Overlay parse_overlay(const char *value)
{
  if (std::strcmp(value, "line") == 0) return Overlay::Line;
  if (std::strcmp(value, "waveform") == 0) return Overlay::Waveform;
  if (std::strcmp(value, "dots") == 0) return Overlay::Dots;
  return Overlay::None;
}

ColorMode parse_color_mode(const char *value)
{
  if (std::strcmp(value, "solid") == 0) return ColorMode::Solid;
  if (std::strcmp(value, "rainbow") == 0) return ColorMode::Rainbow;
  return ColorMode::Gradient;
}

MirrorMode parse_mirror_mode(const char *value)
{
  if (std::strcmp(value, "vertical") == 0) return MirrorMode::Vertical;
  if (std::strcmp(value, "horizontal") == 0) return MirrorMode::Horizontal;
  return MirrorMode::Both;
}

ChannelMode parse_channel_mode(const char *value)
{
  if (std::strcmp(value, "left") == 0) return ChannelMode::Left;
  if (std::strcmp(value, "right") == 0) return ChannelMode::Right;
  return ChannelMode::Mix;
}

void audio_capture(void *data, obs_source_t *audio_source,
                   const struct audio_data *audio, bool muted)
{
  auto *source = static_cast<VisualizerSource *>(data);
  const std::size_t channels =
      get_audio_channels(obs_source_get_speaker_layout(audio_source));
  const float *planes[MAX_AV_PLANES]{};
  for (std::size_t channel = 0; channel < channels && channel < MAX_AV_PLANES;
       ++channel)
    planes[channel] = reinterpret_cast<const float *>(audio->data[channel]);
  source->analyzer.push_planar(planes, std::min<std::size_t>(channels, MAX_AV_PLANES),
                               audio->frames, muted);
}

void set_audio_source(VisualizerSource *source, const char *name)
{
  const std::string desired_name = name ? name : "";
  if (source->audio_source) {
    obs_source_remove_audio_capture_callback(source->audio_source, audio_capture,
                                             source);
    obs_source_release(source->audio_source);
    source->audio_source = nullptr;
  }
  source->analyzer.clear();
  source->audio_source_name = desired_name;
  if (desired_name.empty())
    return;
  obs_source_t *candidate = obs_get_source_by_name(desired_name.c_str());
  if (!candidate || candidate == source->context) {
    if (candidate)
      obs_source_release(candidate);
    return;
  }
  if ((obs_source_get_output_flags(candidate) & OBS_SOURCE_AUDIO) == 0) {
    obs_source_release(candidate);
    return;
  }
  source->audio_source = candidate;
  obs_source_add_audio_capture_callback(source->audio_source, audio_capture, source);
}

const char *visualizer_name(void *)
{
  return obs_module_text("Source.Name");
}

void visualizer_update(void *data, obs_data_t *settings)
{
  auto *source = static_cast<VisualizerSource *>(data);
  const char *new_audio = obs_data_get_string(settings, "audio_source");

  source->render.width = static_cast<uint32_t>(
      std::clamp<long long>(obs_data_get_int(settings, "width"), 64, 7680));
  source->render.height = static_cast<uint32_t>(
      std::clamp<long long>(obs_data_get_int(settings, "height"), 64, 4320));
  source->render.style = parse_style(obs_data_get_string(settings, "style"));
  source->render.overlay =
      parse_overlay(obs_data_get_string(settings, "overlay"));
  source->render.color_mode =
      parse_color_mode(obs_data_get_string(settings, "color_mode"));
  source->render.gradient_by_level =
      obs_data_get_bool(settings, "gradient_by_level");
  source->render.mirror_mode =
      parse_mirror_mode(obs_data_get_string(settings, "mirror_mode"));
  source->render.bass_outside = obs_data_get_bool(settings, "bass_outside");
  source->render.rounded_caps = obs_data_get_bool(settings, "rounded_caps");
  source->render.glow_enabled = obs_data_get_bool(settings, "glow_enabled");
  source->render.glow_size =
      static_cast<float>(obs_data_get_double(settings, "glow_size"));
  source->render.glow_intensity =
      static_cast<float>(obs_data_get_double(settings, "glow_intensity"));
  source->render.color_a = static_cast<uint32_t>(obs_data_get_int(settings, "color_a"));
  source->render.color_b = static_cast<uint32_t>(obs_data_get_int(settings, "color_b"));
  source->render.background =
      static_cast<uint32_t>(obs_data_get_int(settings, "background"));
  source->render.opacity = static_cast<float>(obs_data_get_double(settings, "opacity"));
  source->render.padding = static_cast<float>(obs_data_get_double(settings, "padding"));
  source->render.gap_percent =
      static_cast<float>(obs_data_get_double(settings, "gap_percent"));
  source->render.minimum_height =
      static_cast<float>(obs_data_get_double(settings, "minimum_height"));
  source->render.line_thickness =
      static_cast<float>(obs_data_get_double(settings, "line_thickness"));
  source->render.dot_size = static_cast<float>(obs_data_get_double(settings, "dot_size"));
  source->render.waveform_gain =
      static_cast<float>(obs_data_get_double(settings, "waveform_gain"));
  source->render.inner_radius_percent =
      static_cast<float>(obs_data_get_double(settings, "inner_radius"));
  source->render.radial_length_percent =
      static_cast<float>(obs_data_get_double(settings, "radial_length"));
  source->render.start_angle_degrees =
      static_cast<float>(obs_data_get_double(settings, "start_angle"));
  source->render.arc_degrees =
      static_cast<float>(obs_data_get_double(settings, "arc"));
  source->render.rotation_speed =
      static_cast<float>(obs_data_get_double(settings, "rotation_speed"));
  source->render.hue_offset =
      static_cast<float>(obs_data_get_double(settings, "hue_offset"));
  source->render.show_peaks = obs_data_get_bool(settings, "show_peaks");
  source->render.peak_thickness =
      static_cast<float>(obs_data_get_double(settings, "peak_thickness"));
  source->render.invert = obs_data_get_bool(settings, "invert");

  AnalyzerConfig config;
  config.fft_size = static_cast<std::size_t>(obs_data_get_int(settings, "fft_size"));
  config.bands = static_cast<std::size_t>(obs_data_get_int(settings, "bands"));
  config.waveform_points =
      static_cast<std::size_t>(obs_data_get_int(settings, "waveform_points"));
  obs_audio_info audio_info{};
  config.sample_rate = obs_get_audio_info(&audio_info)
                           ? static_cast<float>(audio_info.samples_per_sec)
                           : 48000.0f;
  config.min_frequency =
      static_cast<float>(obs_data_get_double(settings, "min_frequency"));
  config.max_frequency =
      static_cast<float>(obs_data_get_double(settings, "max_frequency"));
  config.floor_db = static_cast<float>(obs_data_get_double(settings, "floor_db"));
  config.ceiling_db =
      static_cast<float>(obs_data_get_double(settings, "ceiling_db"));
  config.gain_db = static_cast<float>(obs_data_get_double(settings, "gain_db"));
  config.attack_ms = static_cast<float>(obs_data_get_double(settings, "attack_ms"));
  config.release_ms = static_cast<float>(obs_data_get_double(settings, "release_ms"));
  config.peak_hold_ms =
      static_cast<float>(obs_data_get_double(settings, "peak_hold_ms"));
  config.peak_decay_per_second =
      static_cast<float>(obs_data_get_double(settings, "peak_decay"));
  config.logarithmic = obs_data_get_bool(settings, "logarithmic");
  config.channel_mode =
      parse_channel_mode(obs_data_get_string(settings, "channel_mode"));
  source->analyzer.set_config(config);

  if (source->audio_source_name != (new_audio ? new_audio : ""))
    set_audio_source(source, new_audio);
}

void *visualizer_create(obs_data_t *settings, obs_source_t *context)
{
  auto *source = new VisualizerSource;
  source->context = context;
  visualizer_update(source, settings);
  return source;
}

void visualizer_destroy(void *data)
{
  auto *source = static_cast<VisualizerSource *>(data);
  if (source->audio_source) {
    obs_source_remove_audio_capture_callback(source->audio_source, audio_capture,
                                             source);
    obs_source_release(source->audio_source);
  }
  delete source;
}

uint32_t visualizer_width(void *data)
{
  return static_cast<VisualizerSource *>(data)->render.width;
}

uint32_t visualizer_height(void *data)
{
  return static_cast<VisualizerSource *>(data)->render.height;
}

void visualizer_tick(void *data, float seconds)
{
  auto *source = static_cast<VisualizerSource *>(data);
  if (source->audio_source && obs_source_removed(source->audio_source))
    set_audio_source(source, source->audio_source_name.c_str());
  else if (!source->audio_source && !source->audio_source_name.empty())
    set_audio_source(source, source->audio_source_name.c_str());
  source->snapshot = source->analyzer.analyze(std::clamp(seconds, 0.0f, 0.25f));
  source->phase_degrees = std::fmod(
      source->phase_degrees + source->render.rotation_speed * seconds, 360.0f);
}

void visualizer_render(void *data, gs_effect_t *)
{
  const auto &source = *static_cast<VisualizerSource *>(data);
  std::vector<Vertex> vertices;
  vertices.reserve(source.snapshot.spectrum.size() * 72 + 24);

  if ((source.render.background >> 24U) != 0) {
    rectangle(vertices, 0.0f, 0.0f, static_cast<float>(source.render.width),
              static_cast<float>(source.render.height), source.render.background,
              source.render.background);
  }

  switch (source.render.style) {
  case Style::Bars: render_bars(source, vertices, false, false); break;
  case Style::MirrorBars: {
    const bool horizontal = source.render.mirror_mode != MirrorMode::Vertical;
    const bool vertical = source.render.mirror_mode != MirrorMode::Horizontal;
    render_bars(source, vertices, horizontal, vertical);
    break;
  }
  case Style::Waveform: render_waveform(source, vertices); break;
  case Style::RadialBars: render_radial(source, vertices, false); break;
  case Style::RadialLine: render_radial(source, vertices, true); break;
  case Style::Dots: render_spectrum_line(source, vertices, true); break;
  }
  switch (source.render.overlay) {
  case Overlay::None: break;
  case Overlay::Line: render_spectrum_line(source, vertices, false); break;
  case Overlay::Waveform: render_waveform(source, vertices); break;
  case Overlay::Dots: render_spectrum_line(source, vertices, true); break;
  }

  if (vertices.empty())
    return;
  gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
  while (gs_effect_loop(effect, "SolidColored")) {
    gs_render_start(true);
    for (const auto &vertex : vertices) {
      gs_color(vertex.color);
      gs_vertex2f(vertex.point.x, vertex.point.y);
    }
    gs_render_stop(GS_TRIS);
  }
}

bool enumerate_audio_source(void *data, obs_source_t *source)
{
  auto *property = static_cast<obs_property_t *>(data);
  if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0) {
    const char *name = obs_source_get_name(source);
    obs_property_list_add_string(property, name, name);
  }
  return true;
}

void apply_preset(obs_data_t *settings, const char *preset)
{
  obs_data_set_bool(settings, "glow_enabled", false);
  if (std::strcmp(preset, "clean") == 0) {
    obs_data_set_string(settings, "style", "bars");
    obs_data_set_string(settings, "overlay", "none");
    obs_data_set_int(settings, "bands", 48);
    obs_data_set_double(settings, "gap_percent", 24.0);
    obs_data_set_bool(settings, "rounded_caps", true);
    obs_data_set_string(settings, "color_mode", "gradient");
    obs_data_set_int(settings, "color_a", rgba(45, 212, 191));
    obs_data_set_int(settings, "color_b", rgba(59, 130, 246));
    obs_data_set_double(settings, "attack_ms", 35.0);
    obs_data_set_double(settings, "release_ms", 220.0);
  } else if (std::strcmp(preset, "neon_wings") == 0) {
    obs_data_set_string(settings, "style", "mirror_bars");
    obs_data_set_string(settings, "mirror_mode", "both");
    obs_data_set_bool(settings, "bass_outside", false);
    obs_data_set_string(settings, "overlay", "line");
    obs_data_set_int(settings, "bands", 64);
    obs_data_set_double(settings, "gap_percent", 32.0);
    obs_data_set_bool(settings, "rounded_caps", true);
    obs_data_set_bool(settings, "glow_enabled", true);
    obs_data_set_double(settings, "glow_size", 18.0);
    obs_data_set_double(settings, "glow_intensity", 0.9);
    obs_data_set_string(settings, "color_mode", "rainbow");
    obs_data_set_double(settings, "release_ms", 280.0);
  } else if (std::strcmp(preset, "bass_outside") == 0) {
    obs_data_set_string(settings, "style", "mirror_bars");
    obs_data_set_string(settings, "mirror_mode", "horizontal");
    obs_data_set_bool(settings, "bass_outside", true);
    obs_data_set_string(settings, "overlay", "none");
    obs_data_set_int(settings, "bands", 40);
    obs_data_set_double(settings, "gap_percent", 18.0);
    obs_data_set_bool(settings, "rounded_caps", true);
    obs_data_set_bool(settings, "glow_enabled", true);
    obs_data_set_double(settings, "glow_size", 12.0);
    obs_data_set_double(settings, "glow_intensity", 0.75);
    obs_data_set_string(settings, "color_mode", "gradient");
    obs_data_set_int(settings, "color_a", rgba(251, 146, 60));
    obs_data_set_int(settings, "color_b", rgba(236, 72, 153));
    obs_data_set_double(settings, "gain_db", 6.0);
  } else if (std::strcmp(preset, "orbit") == 0) {
    obs_data_set_string(settings, "style", "radial_bars");
    obs_data_set_string(settings, "overlay", "none");
    obs_data_set_int(settings, "bands", 96);
    obs_data_set_double(settings, "inner_radius", 22.0);
    obs_data_set_double(settings, "radial_length", 25.0);
    obs_data_set_double(settings, "gap_percent", 35.0);
    obs_data_set_double(settings, "rotation_speed", 12.0);
    obs_data_set_bool(settings, "rounded_caps", true);
    obs_data_set_bool(settings, "glow_enabled", true);
    obs_data_set_double(settings, "glow_size", 10.0);
    obs_data_set_double(settings, "glow_intensity", 0.65);
    obs_data_set_string(settings, "color_mode", "rainbow");
  } else if (std::strcmp(preset, "scope") == 0) {
    obs_data_set_string(settings, "style", "waveform");
    obs_data_set_string(settings, "overlay", "none");
    obs_data_set_string(settings, "color_mode", "solid");
    obs_data_set_int(settings, "color_a", rgba(74, 222, 128));
    obs_data_set_double(settings, "line_thickness", 4.0);
    obs_data_set_double(settings, "waveform_gain", 1.35);
    obs_data_set_int(settings, "waveform_points", 768);
  } else if (std::strcmp(preset, "dots") == 0) {
    obs_data_set_string(settings, "style", "dots");
    obs_data_set_string(settings, "overlay", "none");
    obs_data_set_int(settings, "bands", 36);
    obs_data_set_double(settings, "dot_size", 12.0);
    obs_data_set_string(settings, "color_mode", "gradient");
    obs_data_set_int(settings, "color_a", rgba(250, 204, 21));
    obs_data_set_int(settings, "color_b", rgba(239, 68, 68));
    obs_data_set_double(settings, "release_ms", 320.0);
  }

}

bool preset_changed(obs_properties_t *, obs_property_t *, obs_data_t *settings)
{
  const char *preset = obs_data_get_string(settings, "preset");
  if (!preset || !*preset)
    return false;
  apply_preset(settings, preset);
  obs_data_set_string(settings, "preset", "");
  return true;
}

obs_properties_t *visualizer_properties(void *)
{
  obs_properties_t *properties = obs_properties_create();

  obs_properties_t *input = obs_properties_create();
  obs_property_t *audio = obs_properties_add_list(
      input, "audio_source", obs_module_text("Audio.Source"),
      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(audio, obs_module_text("Audio.None"), "");
  obs_enum_sources(enumerate_audio_source, audio);
  obs_property_t *channel = obs_properties_add_list(
      input, "channel_mode", obs_module_text("Audio.ChannelMode"),
      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(channel, obs_module_text("Audio.Mix"), "mix");
  obs_property_list_add_string(channel, obs_module_text("Audio.Left"), "left");
  obs_property_list_add_string(channel, obs_module_text("Audio.Right"), "right");
  obs_properties_add_group(properties, "input_group", obs_module_text("Group.Input"),
                           OBS_GROUP_NORMAL, input);

  obs_properties_t *presets = obs_properties_create();
  obs_property_t *preset = obs_properties_add_list(
      presets, "preset", obs_module_text("Preset.Select"), OBS_COMBO_TYPE_LIST,
      OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(preset, obs_module_text("Preset.None"), "");
  obs_property_list_add_string(preset, obs_module_text("Preset.Clean"), "clean");
  obs_property_list_add_string(preset, obs_module_text("Preset.NeonWings"),
                               "neon_wings");
  obs_property_list_add_string(preset, obs_module_text("Preset.BassOutside"),
                               "bass_outside");
  obs_property_list_add_string(preset, obs_module_text("Preset.Orbit"), "orbit");
  obs_property_list_add_string(preset, obs_module_text("Preset.Scope"), "scope");
  obs_property_list_add_string(preset, obs_module_text("Preset.Dots"), "dots");
  obs_property_set_modified_callback(preset, preset_changed);
  obs_properties_add_group(properties, "presets_group",
                           obs_module_text("Group.Presets"), OBS_GROUP_NORMAL,
                           presets);

  obs_properties_t *layout = obs_properties_create();
  obs_properties_add_int(layout, "width", obs_module_text("Layout.Width"), 64,
                         7680, 1);
  obs_properties_add_int(layout, "height", obs_module_text("Layout.Height"), 64,
                         4320, 1);
  obs_property_t *style = obs_properties_add_list(
      layout, "style", obs_module_text("Layout.Style"), OBS_COMBO_TYPE_LIST,
      OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(style, obs_module_text("Style.Bars"), "bars");
  obs_property_list_add_string(style, obs_module_text("Style.MirrorBars"),
                               "mirror_bars");
  obs_property_list_add_string(style, obs_module_text("Style.Waveform"),
                               "waveform");
  obs_property_list_add_string(style, obs_module_text("Style.RadialBars"),
                               "radial_bars");
  obs_property_list_add_string(style, obs_module_text("Style.RadialLine"),
                               "radial_line");
  obs_property_list_add_string(style, obs_module_text("Style.Dots"), "dots");
  obs_property_t *overlay = obs_properties_add_list(
      layout, "overlay", obs_module_text("Layout.Overlay"), OBS_COMBO_TYPE_LIST,
      OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(overlay, obs_module_text("Overlay.None"), "none");
  obs_property_list_add_string(overlay, obs_module_text("Overlay.Line"), "line");
  obs_property_list_add_string(overlay, obs_module_text("Overlay.Waveform"),
                               "waveform");
  obs_property_list_add_string(overlay, obs_module_text("Overlay.Dots"), "dots");
  obs_property_t *bands = obs_properties_add_int_slider(
      layout, "bands", obs_module_text("Analysis.Bands"), 4, 256, 1);
  obs_property_set_long_description(bands, obs_module_text("Analysis.Bands.Help"));
  obs_property_t *mirror = obs_properties_add_list(
      layout, "mirror_mode", obs_module_text("Layout.MirrorMode"),
      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(mirror, obs_module_text("Mirror.Vertical"),
                               "vertical");
  obs_property_list_add_string(mirror, obs_module_text("Mirror.Horizontal"),
                               "horizontal");
  obs_property_list_add_string(mirror, obs_module_text("Mirror.Both"), "both");
  obs_properties_add_bool(layout, "bass_outside",
                          obs_module_text("Layout.BassOutside"));
  obs_properties_add_bool(layout, "rounded_caps",
                          obs_module_text("Layout.RoundedCaps"));
  obs_properties_add_float_slider(layout, "padding", obs_module_text("Layout.Padding"),
                                  0.0, 500.0, 1.0);
  obs_properties_add_float_slider(layout, "gap_percent", obs_module_text("Layout.Gap"),
                                  0.0, 95.0, 1.0);
  obs_properties_add_float_slider(layout, "minimum_height",
                                  obs_module_text("Layout.MinimumHeight"), 0.0,
                                  100.0, 0.5);
  obs_properties_add_bool(layout, "invert", obs_module_text("Layout.Invert"));
  obs_properties_add_group(properties, "layout_group", obs_module_text("Group.Layout"),
                           OBS_GROUP_NORMAL, layout);

  obs_properties_t *appearance = obs_properties_create();
  obs_property_t *color_mode = obs_properties_add_list(
      appearance, "color_mode", obs_module_text("Appearance.ColorMode"),
      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(color_mode, obs_module_text("Color.Solid"), "solid");
  obs_property_list_add_string(color_mode, obs_module_text("Color.Gradient"),
                               "gradient");
  obs_property_list_add_string(color_mode, obs_module_text("Color.Rainbow"),
                               "rainbow");
  obs_properties_add_color_alpha(appearance, "color_a",
                                 obs_module_text("Appearance.ColorA"));
  obs_properties_add_color_alpha(appearance, "color_b",
                                 obs_module_text("Appearance.ColorB"));
  obs_properties_add_bool(appearance, "gradient_by_level",
                          obs_module_text("Appearance.GradientByLevel"));
  obs_properties_add_color_alpha(appearance, "background",
                                 obs_module_text("Appearance.Background"));
  obs_properties_add_float_slider(appearance, "opacity",
                                  obs_module_text("Appearance.Opacity"), 0.0, 1.0,
                                  0.01);
  obs_properties_add_float_slider(appearance, "hue_offset",
                                  obs_module_text("Appearance.HueOffset"), 0.0,
                                  360.0, 1.0);
  obs_properties_add_group(properties, "appearance_group",
                           obs_module_text("Group.Appearance"), OBS_GROUP_NORMAL,
                           appearance);

  obs_properties_t *shape = obs_properties_create();
  obs_properties_add_float_slider(shape, "line_thickness",
                                  obs_module_text("Shape.LineThickness"), 0.5,
                                  50.0, 0.5);
  obs_properties_add_float_slider(shape, "dot_size", obs_module_text("Shape.DotSize"),
                                  1.0, 100.0, 1.0);
  obs_properties_add_float_slider(shape, "waveform_gain",
                                  obs_module_text("Shape.WaveformGain"), 0.1,
                                  5.0, 0.05);
  obs_properties_add_bool(shape, "show_peaks", obs_module_text("Shape.ShowPeaks"));
  obs_properties_add_float_slider(shape, "peak_thickness",
                                  obs_module_text("Shape.PeakThickness"), 0.5,
                                  20.0, 0.5);
  obs_property_t *glow = obs_properties_add_bool(
      shape, "glow_enabled", obs_module_text("Glow.Enabled"));
  obs_property_set_long_description(glow, obs_module_text("Glow.Enabled.Help"));
  obs_properties_add_float_slider(shape, "glow_size",
                                  obs_module_text("Glow.Size"), 1.0, 50.0, 1.0);
  obs_properties_add_float_slider(shape, "glow_intensity",
                                  obs_module_text("Glow.Intensity"), 0.05, 2.0,
                                  0.05);
  obs_properties_add_group(properties, "shape_group", obs_module_text("Group.Shape"),
                           OBS_GROUP_NORMAL, shape);

  obs_properties_t *radial = obs_properties_create();
  obs_properties_add_float_slider(radial, "inner_radius",
                                  obs_module_text("Radial.InnerRadius"), 0.0,
                                  49.0, 0.5);
  obs_properties_add_float_slider(radial, "radial_length",
                                  obs_module_text("Radial.Length"), 1.0, 50.0,
                                  0.5);
  obs_properties_add_float_slider(radial, "start_angle",
                                  obs_module_text("Radial.StartAngle"), -360.0,
                                  360.0, 1.0);
  obs_properties_add_float_slider(radial, "arc", obs_module_text("Radial.Arc"),
                                  10.0, 360.0, 1.0);
  obs_properties_add_float_slider(radial, "rotation_speed",
                                  obs_module_text("Radial.RotationSpeed"), -360.0,
                                  360.0, 1.0);
  obs_properties_add_group(properties, "radial_group", obs_module_text("Group.Radial"),
                           OBS_GROUP_NORMAL, radial);

  obs_properties_t *analysis = obs_properties_create();
  obs_property_t *fft = obs_properties_add_list(
      analysis, "fft_size", obs_module_text("Analysis.FftSize"),
      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
  for (int value : {256, 512, 1024, 2048, 4096, 8192}) {
    const std::string label = std::to_string(value);
    obs_property_list_add_int(fft, label.c_str(), value);
  }
  obs_property_set_long_description(fft, obs_module_text("Analysis.FftSize.Help"));
  obs_properties_add_int_slider(analysis, "waveform_points",
                                obs_module_text("Analysis.WaveformPoints"), 32,
                                2048, 16);
  obs_properties_add_float(analysis, "min_frequency",
                           obs_module_text("Analysis.MinFrequency"), 1.0, 20000.0,
                           1.0);
  obs_properties_add_float(analysis, "max_frequency",
                           obs_module_text("Analysis.MaxFrequency"), 20.0, 24000.0,
                           10.0);
  obs_property_t *logarithmic = obs_properties_add_bool(
      analysis, "logarithmic", obs_module_text("Analysis.Logarithmic"));
  obs_property_set_long_description(
      logarithmic, obs_module_text("Analysis.Logarithmic.Help"));
  obs_property_t *floor = obs_properties_add_float_slider(
      analysis, "floor_db", obs_module_text("Analysis.FloorDb"), -120.0,
      -10.0, 1.0);
  obs_property_set_long_description(floor, obs_module_text("Analysis.FloorDb.Help"));
  obs_properties_add_float_slider(analysis, "ceiling_db",
                                  obs_module_text("Analysis.CeilingDb"), -60.0,
                                  12.0, 1.0);
  obs_property_t *gain = obs_properties_add_float_slider(
      analysis, "gain_db", obs_module_text("Analysis.GainDb"), -30.0, 60.0,
      0.5);
  obs_property_set_long_description(gain, obs_module_text("Analysis.GainDb.Help"));
  obs_properties_add_float_slider(analysis, "attack_ms",
                                  obs_module_text("Analysis.Attack"), 0.0, 1000.0,
                                  1.0);
  obs_properties_add_float_slider(analysis, "release_ms",
                                  obs_module_text("Analysis.Release"), 0.0, 3000.0,
                                  1.0);
  obs_properties_add_float_slider(analysis, "peak_hold_ms",
                                  obs_module_text("Analysis.PeakHold"), 0.0, 3000.0,
                                  10.0);
  obs_properties_add_float_slider(analysis, "peak_decay",
                                  obs_module_text("Analysis.PeakDecay"), 0.0, 5.0,
                                  0.05);
  obs_properties_add_group(properties, "analysis_group",
                           obs_module_text("Group.Analysis"), OBS_GROUP_NORMAL,
                           analysis);
  return properties;
}

void visualizer_defaults(obs_data_t *settings)
{
  obs_data_set_default_string(settings, "preset", "");
  obs_data_set_default_string(settings, "audio_source", "");
  obs_data_set_default_string(settings, "channel_mode", "mix");
  obs_data_set_default_int(settings, "width", 1280);
  obs_data_set_default_int(settings, "height", 360);
  obs_data_set_default_string(settings, "style", "bars");
  obs_data_set_default_string(settings, "overlay", "none");
  obs_data_set_default_string(settings, "mirror_mode", "both");
  obs_data_set_default_bool(settings, "bass_outside", false);
  obs_data_set_default_bool(settings, "rounded_caps", false);
  obs_data_set_default_bool(settings, "glow_enabled", false);
  obs_data_set_default_double(settings, "glow_size", 14.0);
  obs_data_set_default_double(settings, "glow_intensity", 0.75);
  obs_data_set_default_double(settings, "padding", 18.0);
  obs_data_set_default_double(settings, "gap_percent", 20.0);
  obs_data_set_default_double(settings, "minimum_height", 2.0);
  obs_data_set_default_bool(settings, "invert", false);
  obs_data_set_default_string(settings, "color_mode", "gradient");
  obs_data_set_default_bool(settings, "gradient_by_level", false);
  obs_data_set_default_int(settings, "color_a", rgba(61, 166, 255));
  obs_data_set_default_int(settings, "color_b", rgba(219, 61, 255));
  obs_data_set_default_int(settings, "background", rgba(0, 0, 0, 0));
  obs_data_set_default_double(settings, "opacity", 1.0);
  obs_data_set_default_double(settings, "hue_offset", 0.0);
  obs_data_set_default_double(settings, "line_thickness", 5.0);
  obs_data_set_default_double(settings, "dot_size", 8.0);
  obs_data_set_default_double(settings, "waveform_gain", 1.0);
  obs_data_set_default_bool(settings, "show_peaks", false);
  obs_data_set_default_double(settings, "peak_thickness", 3.0);
  obs_data_set_default_double(settings, "inner_radius", 24.0);
  obs_data_set_default_double(settings, "radial_length", 24.0);
  obs_data_set_default_double(settings, "start_angle", -90.0);
  obs_data_set_default_double(settings, "arc", 360.0);
  obs_data_set_default_double(settings, "rotation_speed", 0.0);
  obs_data_set_default_int(settings, "fft_size", 2048);
  obs_data_set_default_int(settings, "bands", 64);
  obs_data_set_default_int(settings, "waveform_points", 512);
  obs_data_set_default_double(settings, "min_frequency", 40.0);
  obs_data_set_default_double(settings, "max_frequency", 16000.0);
  obs_data_set_default_bool(settings, "logarithmic", true);
  obs_data_set_default_double(settings, "floor_db", -72.0);
  obs_data_set_default_double(settings, "ceiling_db", -6.0);
  obs_data_set_default_double(settings, "gain_db", 0.0);
  obs_data_set_default_double(settings, "attack_ms", 45.0);
  obs_data_set_default_double(settings, "release_ms", 240.0);
  obs_data_set_default_double(settings, "peak_hold_ms", 180.0);
  obs_data_set_default_double(settings, "peak_decay", 1.2);
}
} // namespace

void register_visualizer_source()
{
  obs_source_info info{};
  info.id = kSourceId;
  info.type = OBS_SOURCE_TYPE_INPUT;
  info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB;
  info.get_name = visualizer_name;
  info.create = visualizer_create;
  info.destroy = visualizer_destroy;
  info.update = visualizer_update;
  info.get_defaults = visualizer_defaults;
  info.get_properties = visualizer_properties;
  info.get_width = visualizer_width;
  info.get_height = visualizer_height;
  info.video_tick = visualizer_tick;
  info.video_render = visualizer_render;
  info.icon_type = OBS_ICON_TYPE_AUDIO_OUTPUT;
  obs_register_source(&info);
}
