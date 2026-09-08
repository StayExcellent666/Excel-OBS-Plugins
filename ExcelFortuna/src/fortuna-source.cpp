#include "fortuna-source.hpp"

#include "fortuna-model.hpp"
#include "fortuna-network.hpp"

#include <obs-module.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {
using excel_fortuna::ConnectionConfig;
using excel_fortuna::DisplayMode;
using excel_fortuna::EventType;
using excel_fortuna::FortunaNetwork;
using excel_fortuna::ProtocolEvent;
using excel_fortuna::SpinAnimation;

constexpr float kPi = 3.14159265358979323846f;
constexpr const char *kSourceId = "excel_fortuna_source";

struct Point { float x; float y; };
struct Vertex { Point point; uint32_t color; };
struct Entrant { std::string id; std::string name; };

enum class ColorStyle { ExcelPalette, Gradient, Rainbow };
enum class TimerPosition { TopLeft, TopCenter, TopRight, BottomLeft, BottomCenter, BottomRight };

struct TextLayer {
  obs_source_t *source = nullptr;
  std::string text;
  std::string face;
  int size = 32;
  uint32_t color = 0xffffffffU;
};

struct Settings {
  uint32_t width = 1280;
  uint32_t height = 720;
  std::string server_url = "https://excelprotocol.fly.dev";
  std::string access_key;
  bool auto_connect = true;
  std::string title = "Fortuna Giveaway";
  std::string reward_title = "Giveaway Entry";
  std::string entry_mode = "channel_reward";
  std::string chat_command = "!enter";
  int target_entries = 0;
  int duration_seconds = 300;
  int countdown_reveal_seconds = 300;
  int dramatic_countdown_seconds = 10;
  TimerPosition timer_position = TimerPosition::TopRight;
  int spin_duration_ms = 8000;
  int rotations = 7;
  uint32_t color_a = 0xffffc33dU;
  uint32_t color_b = 0xffff4f9aU;
  uint32_t accent = 0xfff5f5ffU;
  uint32_t text_color = 0xffffffffU;
  uint32_t background = 0x00000000U;
  std::string font_face = "Segoe UI";
  float wheel_size = 0.78f;
  ColorStyle color_style = ColorStyle::ExcelPalette;
  bool glow = true;
  float glow_strength = 0.7f;
  bool depth = true;
  float depth_strength = 0.72f;
  bool show_status = true;
  bool confetti = true;
  bool only_when_running = false;
};

struct SharedState {
  std::mutex mutex;
  bool connected = false;
  std::string connection_status = "Not connected";
  std::string phase = "idle";
  std::string title = "Fortuna Giveaway";
  std::vector<Entrant> entrants;
  int entry_count = 0;
  int target_entries = 0;
  int remaining_seconds = 0;
  std::string winner_id;
  std::string winner_name;
  int pending_winner_index = -1;
  std::int64_t pending_giveaway_id = 0;
  int pending_spin_ms = 8000;
  bool pending_spin = false;
  bool pending_remote_spin = false;
  bool reset_requested = false;
};

struct FortunaSource {
  obs_source_t *context = nullptr;
  Settings settings;
  SharedState state;
  std::unique_ptr<FortunaNetwork> network;
  std::string active_server_url;
  std::string active_access_key;
  SpinAnimation spin;
  float wheel_angle = 0.0f;
  float winner_elapsed = 0.0f;
  std::mt19937 random{std::random_device{}()};
  TextLayer title_text;
  TextLayer counter_text;
  TextLayer status_text;
  TextLayer countdown_label_text;
  TextLayer countdown_time_text;
  TextLayer countdown_entries_text;
  TextLayer winner_kicker_text;
  TextLayer winner_text;
  TextLayer winner_message_text;
  std::vector<TextLayer> slice_texts;
  std::int64_t active_giveaway_id = 0;
  std::string active_winner_id;
  bool spin_completion_sent = true;
};

uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
  return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8U) |
         (static_cast<uint32_t>(b) << 16U) |
         (static_cast<uint32_t>(a) << 24U);
}

uint32_t with_alpha(uint32_t color, float alpha)
{
  const auto original = static_cast<uint8_t>((color >> 24U) & 0xffU);
  const auto adjusted = static_cast<uint8_t>(std::lround(
      static_cast<float>(original) * std::clamp(alpha, 0.0f, 1.0f)));
  return (color & 0x00ffffffU) | (static_cast<uint32_t>(adjusted) << 24U);
}

uint8_t mix_byte(uint8_t a, uint8_t b, float t)
{
  return static_cast<uint8_t>(std::lround(a + (b - a) * t));
}

uint32_t mix_color(uint32_t a, uint32_t b, float t)
{
  t = std::clamp(t, 0.0f, 1.0f);
  return rgba(mix_byte(a & 0xffU, b & 0xffU, t),
              mix_byte((a >> 8U) & 0xffU, (b >> 8U) & 0xffU, t),
              mix_byte((a >> 16U) & 0xffU, (b >> 16U) & 0xffU, t),
              mix_byte((a >> 24U) & 0xffU, (b >> 24U) & 0xffU, t));
}

uint32_t shade_color(uint32_t color, float brightness)
{
  brightness = std::max(0.0f, brightness);
  const auto scale = [brightness](uint8_t value) {
    return static_cast<uint8_t>(std::clamp(
        std::lround(static_cast<float>(value) * brightness), 0L, 255L));
  };
  return rgba(scale(color & 0xffU), scale((color >> 8U) & 0xffU),
              scale((color >> 16U) & 0xffU),
              static_cast<uint8_t>((color >> 24U) & 0xffU));
}

std::string countdown_clock(int total_seconds)
{
  total_seconds = std::max(0, total_seconds);
  const int hours = total_seconds / 3600;
  const int minutes = (total_seconds % 3600) / 60;
  const int seconds = total_seconds % 60;
  std::ostringstream text;
  if (hours > 0) {
    text << hours << ':';
    if (minutes < 10) text << '0';
    text << minutes << ':';
  } else {
    text << minutes << ':';
  }
  if (seconds < 10) text << '0';
  text << seconds;
  return text.str();
}

uint32_t hsv_color(float hue, float saturation, float value)
{
  hue -= std::floor(hue);
  const float scaled = hue * 6.0f;
  const int sector = static_cast<int>(std::floor(scaled));
  const float fraction = scaled - std::floor(scaled);
  const float p = value * (1.0f - saturation);
  const float q = value * (1.0f - saturation * fraction);
  const float t = value * (1.0f - saturation * (1.0f - fraction));
  float red = 0.0f, green = 0.0f, blue = 0.0f;
  switch (sector % 6) {
  case 0: red = value; green = t; blue = p; break;
  case 1: red = q; green = value; blue = p; break;
  case 2: red = p; green = value; blue = t; break;
  case 3: red = p; green = q; blue = value; break;
  case 4: red = t; green = p; blue = value; break;
  default: red = value; green = p; blue = q; break;
  }
  return rgba(static_cast<uint8_t>(red * 255.0f),
              static_cast<uint8_t>(green * 255.0f),
              static_cast<uint8_t>(blue * 255.0f));
}

ColorStyle parse_color_style(const char *value)
{
  if (value && std::string(value) == "gradient")
    return ColorStyle::Gradient;
  if (value && std::string(value) == "rainbow")
    return ColorStyle::Rainbow;
  return ColorStyle::ExcelPalette;
}

TimerPosition parse_timer_position(const char *value)
{
  const std::string position = value ? value : "top_right";
  if (position == "top_left") return TimerPosition::TopLeft;
  if (position == "top_center") return TimerPosition::TopCenter;
  if (position == "bottom_left") return TimerPosition::BottomLeft;
  if (position == "bottom_center") return TimerPosition::BottomCenter;
  if (position == "bottom_right") return TimerPosition::BottomRight;
  return TimerPosition::TopRight;
}

uint32_t segment_color(const Settings &settings, int index, int count)
{
  const float position = count <= 1 ? 0.0f :
      static_cast<float>(index) / static_cast<float>(count - 1);
  if (settings.color_style == ColorStyle::Gradient)
    return mix_color(settings.color_a, settings.color_b, position);
  if (settings.color_style == ColorStyle::Rainbow)
    return hsv_color(static_cast<float>(index) * 0.61803398875f,
                     0.72f, 0.96f);

  // A six-step palette built from all three user colors gives adjacent
  // entrants clear visual separation without discarding customization.
  switch (index % 6) {
  case 0: return settings.color_a;
  case 1: return mix_color(settings.color_a, settings.color_b, 0.48f);
  case 2: return settings.color_b;
  case 3: return mix_color(settings.color_b, settings.accent, 0.45f);
  case 4: return settings.accent;
  default: return mix_color(settings.accent, settings.color_a, 0.45f);
  }
}

void triangle(std::vector<Vertex> &vertices, Point a, Point b, Point c,
              uint32_t color)
{
  vertices.push_back({a, color});
  vertices.push_back({b, color});
  vertices.push_back({c, color});
}

void triangle_gradient(std::vector<Vertex> &vertices,
                       Point a, uint32_t color_a,
                       Point b, uint32_t color_b,
                       Point c, uint32_t color_c)
{
  vertices.push_back({a, color_a});
  vertices.push_back({b, color_b});
  vertices.push_back({c, color_c});
}

void quad_gradient(std::vector<Vertex> &vertices,
                   Point a, uint32_t color_a,
                   Point b, uint32_t color_b,
                   Point c, uint32_t color_c,
                   Point d, uint32_t color_d)
{
  triangle_gradient(vertices, a, color_a, b, color_b, c, color_c);
  triangle_gradient(vertices, a, color_a, c, color_c, d, color_d);
}

void quad(std::vector<Vertex> &vertices, Point a, Point b, Point c, Point d,
          uint32_t color)
{
  triangle(vertices, a, b, c, color);
  triangle(vertices, a, c, d, color);
}

void rectangle(std::vector<Vertex> &vertices, float x0, float y0, float x1,
               float y1, uint32_t color)
{
  quad(vertices, {x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, color);
}

void circle(std::vector<Vertex> &vertices, Point center, float radius,
            uint32_t color, int segments);

void rounded_rectangle(std::vector<Vertex> &vertices, float x0, float y0,
                       float x1, float y1, float radius, uint32_t color)
{
  radius = std::clamp(radius, 0.0f,
                      std::min((x1 - x0) * 0.5f, (y1 - y0) * 0.5f));
  if (radius <= 0.0f) {
    rectangle(vertices, x0, y0, x1, y1, color);
    return;
  }

  // A single convex fan avoids the dark discs created when translucent
  // rectangles and full corner circles overlap.
  std::vector<Point> perimeter;
  perimeter.reserve(36);
  constexpr int corner_steps = 8;
  const Point centers[] = {{x1 - radius, y0 + radius},
                           {x1 - radius, y1 - radius},
                           {x0 + radius, y1 - radius},
                           {x0 + radius, y0 + radius}};
  const float starts[] = {-kPi * 0.5f, 0.0f, kPi * 0.5f, kPi};
  for (int corner = 0; corner < 4; ++corner) {
    for (int step = 0; step <= corner_steps; ++step) {
      const float angle = starts[corner] + kPi * 0.5f *
          static_cast<float>(step) / static_cast<float>(corner_steps);
      perimeter.push_back({centers[corner].x + std::cos(angle) * radius,
                           centers[corner].y + std::sin(angle) * radius});
    }
  }
  const Point center{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f};
  for (std::size_t i = 0; i < perimeter.size(); ++i)
    triangle(vertices, center, perimeter[i],
             perimeter[(i + 1) % perimeter.size()], color);
}

void thick_line(std::vector<Vertex> &vertices, Point start, Point end,
                float thickness, uint32_t color)
{
  const float dx = end.x - start.x;
  const float dy = end.y - start.y;
  const float length = std::sqrt(dx * dx + dy * dy);
  if (length < 0.001f)
    return;
  const float nx = -dy / length * thickness * 0.5f;
  const float ny = dx / length * thickness * 0.5f;
  quad(vertices, {start.x + nx, start.y + ny},
       {end.x + nx, end.y + ny}, {end.x - nx, end.y - ny},
       {start.x - nx, start.y - ny}, color);
}

void circle(std::vector<Vertex> &vertices, Point center, float radius,
            uint32_t color, int segments = 64)
{
  for (int i = 0; i < segments; ++i) {
    const float a0 = 2.0f * kPi * static_cast<float>(i) / segments;
    const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / segments;
    triangle(vertices, center,
             {center.x + std::cos(a0) * radius,
              center.y + std::sin(a0) * radius},
             {center.x + std::cos(a1) * radius,
              center.y + std::sin(a1) * radius}, color);
  }
}

void wheel_segment(std::vector<Vertex> &vertices, Point center, float inner,
                   float outer, float start, float end,
                   uint32_t inner_color, uint32_t outer_color)
{
  // Keep the full circumference at roughly 256 edges regardless of entrant
  // count. The old fixed eight edges per slice made small wheels look faceted.
  const int subdivisions = std::max(2, static_cast<int>(std::ceil(
      std::abs(end - start) / (2.0f * kPi) * 256.0f)));
  for (int i = 0; i < subdivisions; ++i) {
    const float a0 = start + (end - start) * static_cast<float>(i) / subdivisions;
    const float a1 = start + (end - start) * static_cast<float>(i + 1) / subdivisions;
    const Point i0{center.x + std::cos(a0) * inner,
                   center.y + std::sin(a0) * inner};
    const Point o0{center.x + std::cos(a0) * outer,
                   center.y + std::sin(a0) * outer};
    const Point o1{center.x + std::cos(a1) * outer,
                   center.y + std::sin(a1) * outer};
    const Point i1{center.x + std::cos(a1) * inner,
                   center.y + std::sin(a1) * inner};
    quad_gradient(vertices, i0, inner_color, o0, outer_color,
                  o1, outer_color, i1, inner_color);
  }
}

void update_text(TextLayer &layer, const std::string &text,
                 const std::string &face, int size, uint32_t color)
{
  if (!layer.source || (layer.text == text && layer.size == size &&
                        layer.color == color && layer.face == face))
    return;
  layer.text = text;
  layer.face = face;
  layer.size = size;
  layer.color = color;
  obs_data_t *settings = obs_data_create();
  obs_data_t *font = obs_data_create();
  obs_data_set_string(settings, "text", text.c_str());
  obs_data_set_string(font, "face", face.empty() ? "Segoe UI" : face.c_str());
  obs_data_set_string(font, "style", "Bold");
  obs_data_set_int(font, "size", size);
  obs_data_set_obj(settings, "font", font);
  obs_data_set_int(settings, "color", color & 0x00ffffffU);
  obs_data_set_int(settings, "opacity", (color >> 24U) * 100 / 255);
  obs_data_set_bool(settings, "outline", true);
  obs_data_set_int(settings, "outline_size", std::clamp(size / 24, 1, 4));
  obs_data_set_int(settings, "outline_color", 0x000000U);
  obs_data_set_int(settings, "outline_opacity", 75);
  obs_source_update(layer.source, settings);
  obs_data_release(font);
  obs_data_release(settings);
}

void render_text(const TextLayer &layer, float x, float y, float max_width,
                 bool centered)
{
  if (!layer.source || layer.text.empty())
    return;
  const uint32_t width = obs_source_get_width(layer.source);
  const uint32_t height = obs_source_get_height(layer.source);
  if (!width || !height)
    return;
  const float scale = max_width > 0.0f && width > max_width
                          ? max_width / static_cast<float>(width)
                          : 1.0f;
  gs_matrix_push();
  gs_matrix_translate3f(centered ? x - width * scale * 0.5f : x, y, 0.0f);
  gs_matrix_scale3f(scale, scale, 1.0f);
  obs_source_video_render(layer.source);
  gs_matrix_pop();
}

void render_rotated_text(const TextLayer &layer, float x, float y,
                         float max_width, float angle)
{
  if (!layer.source || layer.text.empty())
    return;
  const uint32_t width = obs_source_get_width(layer.source);
  const uint32_t height = obs_source_get_height(layer.source);
  if (!width || !height)
    return;
  const float scale = max_width > 0.0f && width > max_width
                          ? max_width / static_cast<float>(width)
                          : 1.0f;
  gs_matrix_push();
  gs_matrix_translate3f(x, y, 0.0f);
  gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, angle);
  gs_matrix_scale3f(scale, scale, 1.0f);
  gs_matrix_translate3f(-static_cast<float>(width) * 0.5f,
                        -static_cast<float>(height) * 0.5f, 0.0f);
  obs_source_video_render(layer.source);
  gs_matrix_pop();
}

void render_text_centered(const TextLayer &layer, float x, float y,
                          float max_width)
{
  if (!layer.source || layer.text.empty())
    return;
  const uint32_t width = obs_source_get_width(layer.source);
  const uint32_t height = obs_source_get_height(layer.source);
  if (!width || !height)
    return;
  const float scale = max_width > 0.0f && width > max_width
                          ? max_width / static_cast<float>(width)
                          : 1.0f;
  gs_matrix_push();
  gs_matrix_translate3f(x - width * scale * 0.5f,
                        y - height * scale * 0.5f, 0.0f);
  gs_matrix_scale3f(scale, scale, 1.0f);
  obs_source_video_render(layer.source);
  gs_matrix_pop();
}

void create_text_layer(TextLayer &layer, const char *name)
{
  obs_data_t *settings = obs_data_create();
  layer.source = obs_source_create_private("text_gdiplus", name, settings);
  obs_data_release(settings);
}

void destroy_text_layer(TextLayer &layer)
{
  if (layer.source)
    obs_source_release(layer.source);
  layer.source = nullptr;
}

void handle_event(FortunaSource *source, const ProtocolEvent &event)
{
  std::lock_guard<std::mutex> lock(source->state.mutex);
  auto &state = source->state;
  switch (event.type) {
  case EventType::Hello:
    state.connection_status = "Connected to @" + event.channel;
    break;
  case EventType::ResetEntries:
    state.entrants.clear();
    state.entry_count = 0;
    state.winner_id.clear();
    state.winner_name.clear();
    break;
  case EventType::State:
    state.phase = event.status.empty() ? "idle" : event.status;
    if (!event.title.empty()) state.title = event.title;
    state.entry_count = event.entry_count;
    state.target_entries = event.target_entries;
    state.remaining_seconds = event.remaining_seconds;
    break;
  case EventType::Entry: {
    const auto found = std::find_if(state.entrants.begin(), state.entrants.end(),
        [&event](const Entrant &entrant) { return entrant.id == event.user_id; });
    if (found == state.entrants.end())
      state.entrants.push_back({event.user_id, event.display_name});
    state.entry_count = std::max(event.entry_count,
                                 static_cast<int>(state.entrants.size()));
    break;
  }
  case EventType::Spin:
    state.phase = "spinning";
    state.winner_id = event.winner_id;
    state.winner_name = event.winner_display_name;
    state.pending_winner_index = event.winner_index;
    state.pending_giveaway_id = event.giveaway_id;
    state.pending_spin_ms = event.spin_duration_ms;
    state.pending_spin = true;
    state.pending_remote_spin = true;
    break;
  case EventType::Winner:
    state.phase = "winner";
    state.winner_id = event.winner_id;
    state.winner_name = event.winner_display_name;
    break;
  case EventType::Cancelled:
    state.phase = "cancelled";
    break;
  case EventType::Error:
    state.connection_status = "Error: " + event.message;
    break;
  default: break;
  }
}

void handle_status(FortunaSource *source, bool connected,
                   const std::string &message)
{
  std::lock_guard<std::mutex> lock(source->state.mutex);
  source->state.connected = connected;
  source->state.connection_status = message;
}

void reconnect(FortunaSource *source)
{
  source->active_server_url = source->settings.server_url;
  source->active_access_key = source->settings.access_key;
  if (!source->settings.auto_connect && source->settings.access_key.empty()) {
    source->network->disconnect();
    return;
  }
  source->network->connect({source->settings.server_url,
                            source->settings.access_key});
}

const char *source_name(void *) { return obs_module_text("Source.Name"); }

void source_update(void *data, obs_data_t *settings);

void *source_create(obs_data_t *settings, obs_source_t *context)
{
  auto *source = new FortunaSource;
  source->context = context;
  source->network = std::make_unique<FortunaNetwork>(
      [source](const ProtocolEvent &event) { handle_event(source, event); },
      [source](bool connected, const std::string &message) {
        handle_status(source, connected, message);
      });
  create_text_layer(source->title_text, "ExcelFortuna Title");
  create_text_layer(source->counter_text, "ExcelFortuna Counter");
  create_text_layer(source->status_text, "ExcelFortuna Status");
  create_text_layer(source->countdown_label_text, "ExcelFortuna Countdown Label");
  create_text_layer(source->countdown_time_text, "ExcelFortuna Countdown Time");
  create_text_layer(source->countdown_entries_text, "ExcelFortuna Countdown Entrants");
  create_text_layer(source->winner_kicker_text, "ExcelFortuna Winner Kicker");
  create_text_layer(source->winner_text, "ExcelFortuna Winner");
  create_text_layer(source->winner_message_text, "ExcelFortuna Winner Message");
  source_update(source, settings);
  return source;
}

void source_destroy(void *data)
{
  auto *source = static_cast<FortunaSource *>(data);
  source->network.reset();
  destroy_text_layer(source->title_text);
  destroy_text_layer(source->counter_text);
  destroy_text_layer(source->status_text);
  destroy_text_layer(source->countdown_label_text);
  destroy_text_layer(source->countdown_time_text);
  destroy_text_layer(source->countdown_entries_text);
  destroy_text_layer(source->winner_kicker_text);
  destroy_text_layer(source->winner_text);
  destroy_text_layer(source->winner_message_text);
  for (auto &layer : source->slice_texts)
    destroy_text_layer(layer);
  delete source;
}

void source_update(void *data, obs_data_t *settings)
{
  auto *source = static_cast<FortunaSource *>(data);
  Settings next;
  next.width = static_cast<uint32_t>(std::clamp<long long>(obs_data_get_int(settings, "width"), 320, 7680));
  next.height = static_cast<uint32_t>(std::clamp<long long>(obs_data_get_int(settings, "height"), 240, 4320));
  next.server_url = obs_data_get_string(settings, "server_url");
  next.access_key = obs_data_get_string(settings, "access_key");
  next.auto_connect = obs_data_get_bool(settings, "auto_connect");
  next.title = obs_data_get_string(settings, "giveaway_title");
  next.reward_title = obs_data_get_string(settings, "reward_title");
  next.entry_mode = obs_data_get_string(settings, "entry_mode");
  next.chat_command = obs_data_get_string(settings, "chat_command");
  next.target_entries = static_cast<int>(obs_data_get_int(settings, "target_entries"));
  next.duration_seconds = static_cast<int>(obs_data_get_int(settings, "duration_seconds"));
  next.countdown_reveal_seconds = static_cast<int>(
      obs_data_get_int(settings, "countdown_reveal_seconds"));
  next.dramatic_countdown_seconds = static_cast<int>(
      obs_data_get_int(settings, "dramatic_countdown_seconds"));
  next.timer_position = parse_timer_position(
      obs_data_get_string(settings, "timer_position"));
  next.spin_duration_ms = static_cast<int>(obs_data_get_int(settings, "spin_duration_ms"));
  next.rotations = static_cast<int>(obs_data_get_int(settings, "rotations"));
  next.color_a = static_cast<uint32_t>(obs_data_get_int(settings, "color_a"));
  next.color_b = static_cast<uint32_t>(obs_data_get_int(settings, "color_b"));
  next.accent = static_cast<uint32_t>(obs_data_get_int(settings, "accent"));
  next.text_color = static_cast<uint32_t>(obs_data_get_int(settings, "text_color"));
  next.background = static_cast<uint32_t>(obs_data_get_int(settings, "background"));
  next.font_face = obs_data_get_string(settings, "font_face");
  next.wheel_size = static_cast<float>(obs_data_get_double(settings, "wheel_size"));
  next.color_style = parse_color_style(obs_data_get_string(settings, "color_style"));
  next.glow = obs_data_get_bool(settings, "glow");
  next.glow_strength = static_cast<float>(obs_data_get_double(settings, "glow_strength"));
  next.depth = obs_data_get_bool(settings, "depth");
  next.depth_strength = static_cast<float>(obs_data_get_double(settings, "depth_strength"));
  next.show_status = obs_data_get_bool(settings, "show_status");
  next.confetti = obs_data_get_bool(settings, "confetti");
  next.only_when_running = obs_data_get_bool(settings, "only_when_running");

  const bool reconnect_needed = next.server_url != source->active_server_url ||
                                next.access_key != source->active_access_key;
  source->settings = std::move(next);
  {
    std::lock_guard<std::mutex> lock(source->state.mutex);
    if (source->state.phase == "idle")
      source->state.title = source->settings.title;
  }
  if (reconnect_needed && source->settings.auto_connect &&
      !source->settings.access_key.empty())
    reconnect(source);
}

void source_defaults(obs_data_t *settings)
{
  obs_data_set_default_int(settings, "width", 1280);
  obs_data_set_default_int(settings, "height", 720);
  obs_data_set_default_string(settings, "server_url", "https://excelprotocol.fly.dev");
  obs_data_set_default_string(settings, "access_key", "");
  obs_data_set_default_bool(settings, "auto_connect", true);
  obs_data_set_default_string(settings, "giveaway_title", "Fortuna Giveaway");
  obs_data_set_default_string(settings, "reward_title", "Giveaway Entry");
  obs_data_set_default_string(settings, "entry_mode", "channel_reward");
  obs_data_set_default_string(settings, "chat_command", "!enter");
  obs_data_set_default_int(settings, "target_entries", 0);
  obs_data_set_default_int(settings, "duration_seconds", 300);
  obs_data_set_default_int(settings, "countdown_reveal_seconds", 300);
  obs_data_set_default_int(settings, "dramatic_countdown_seconds", 10);
  obs_data_set_default_string(settings, "timer_position", "top_right");
  obs_data_set_default_int(settings, "spin_duration_ms", 8000);
  obs_data_set_default_int(settings, "rotations", 7);
  obs_data_set_default_int(settings, "color_a", rgba(61, 195, 255));
  obs_data_set_default_int(settings, "color_b", rgba(154, 79, 255));
  obs_data_set_default_int(settings, "accent", rgba(255, 235, 92));
  obs_data_set_default_int(settings, "text_color", rgba(255, 255, 255));
  obs_data_set_default_int(settings, "background", rgba(0, 0, 0, 0));
  obs_data_set_default_string(settings, "font_face", "Segoe UI");
  obs_data_set_default_double(settings, "wheel_size", 0.78);
  obs_data_set_default_string(settings, "color_style", "excel");
  obs_data_set_default_bool(settings, "glow", true);
  obs_data_set_default_double(settings, "glow_strength", 0.7);
  obs_data_set_default_bool(settings, "depth", true);
  obs_data_set_default_double(settings, "depth_strength", 0.72);
  obs_data_set_default_bool(settings, "show_status", true);
  obs_data_set_default_bool(settings, "confetti", true);
  obs_data_set_default_bool(settings, "only_when_running", false);
}

bool reconnect_button(obs_properties_t *, obs_property_t *, void *data)
{
  reconnect(static_cast<FortunaSource *>(data));
  return false;
}

bool start_button(obs_properties_t *, obs_property_t *, void *data)
{
  auto *source = static_cast<FortunaSource *>(data);
  source->network->send(excel_fortuna::make_start_command(
      source->settings.title, source->settings.reward_title,
      source->settings.entry_mode, source->settings.chat_command,
      source->settings.target_entries, source->settings.duration_seconds,
      source->settings.spin_duration_ms));
  return false;
}

bool spin_button(obs_properties_t *, obs_property_t *, void *data)
{
  static_cast<FortunaSource *>(data)->network->send(
      excel_fortuna::make_simple_command("spin"));
  return false;
}

bool cancel_button(obs_properties_t *, obs_property_t *, void *data)
{
  static_cast<FortunaSource *>(data)->network->send(
      excel_fortuna::make_simple_command("cancel"));
  return false;
}

bool demo_entry_button(obs_properties_t *, obs_property_t *, void *data)
{
  static const char *names[] = {"LuckyLuna", "PixelPilot", "NovaNoodle",
      "CozyCritter", "EchoEmber", "MintMeteor", "TurboTurtle", "StarSage",
      "VelvetViking", "CosmicCactus", "NeonNomad", "QuestQueen"};
  auto *source = static_cast<FortunaSource *>(data);
  std::lock_guard<std::mutex> lock(source->state.mutex);
  auto &state = source->state;
  const auto index = state.entrants.size();
  state.phase = "open";
  state.title = source->settings.title;
  state.entrants.push_back({"demo-" + std::to_string(index),
                            names[index % (sizeof(names) / sizeof(names[0]))]});
  state.entry_count = static_cast<int>(state.entrants.size());
  state.target_entries = source->settings.target_entries;
  return false;
}

bool demo_spin_button(obs_properties_t *, obs_property_t *, void *data)
{
  auto *source = static_cast<FortunaSource *>(data);
  bool connected = false;
  {
    std::lock_guard<std::mutex> lock(source->state.mutex);
    connected = source->state.connected;
  }
  if (connected) {
    std::ostringstream command;
    command << "{\"type\":\"test_spin\",\"spin_duration_ms\":"
            << source->settings.spin_duration_ms << "}";
    source->network->send(command.str());
    return false;
  }

  std::lock_guard<std::mutex> lock(source->state.mutex);
  if (source->state.entrants.empty())
    return false;
  std::uniform_int_distribution<int> distribution(
      0, static_cast<int>(source->state.entrants.size()) - 1);
  const int index = distribution(source->random);
  source->state.winner_id = source->state.entrants[index].id;
  source->state.winner_name = source->state.entrants[index].name;
  source->state.pending_winner_index = index;
  source->state.pending_spin_ms = source->settings.spin_duration_ms;
  source->state.pending_spin = true;
  source->state.pending_remote_spin = false;
  source->state.phase = "spinning";
  return false;
}

obs_properties_t *source_properties(void *data)
{
  obs_properties_t *properties = obs_properties_create();
  obs_properties_t *connection = obs_properties_create();
  obs_properties_add_text(connection, "server_url", obs_module_text("Connection.Server"), OBS_TEXT_DEFAULT);
  obs_properties_add_text(connection, "access_key", obs_module_text("Connection.Key"), OBS_TEXT_PASSWORD);
  obs_properties_add_bool(connection, "auto_connect", obs_module_text("Connection.Auto"));
  obs_properties_add_button(connection, "reconnect", obs_module_text("Connection.Reconnect"), reconnect_button);
  obs_properties_add_group(properties, "connection_group", obs_module_text("Group.Connection"), OBS_GROUP_NORMAL, connection);

  obs_properties_t *giveaway = obs_properties_create();
  obs_properties_add_text(giveaway, "giveaway_title", obs_module_text("Giveaway.Title"), OBS_TEXT_DEFAULT);
  obs_property_t *entry_mode = obs_properties_add_list(
      giveaway, "entry_mode", obs_module_text("Giveaway.EntryMode"),
      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(entry_mode, obs_module_text("EntryMode.Reward"), "channel_reward");
  obs_property_list_add_string(entry_mode, obs_module_text("EntryMode.Chat"), "chat_command");
  obs_properties_add_text(giveaway, "reward_title", obs_module_text("Giveaway.Reward"), OBS_TEXT_DEFAULT);
  obs_properties_add_text(giveaway, "chat_command", obs_module_text("Giveaway.ChatCommand"), OBS_TEXT_DEFAULT);
  obs_properties_add_int(giveaway, "target_entries", obs_module_text("Giveaway.Target"), 0, 100000, 1);
  obs_properties_add_int(giveaway, "duration_seconds", obs_module_text("Giveaway.Duration"), 0, 86400, 5);
  obs_properties_add_int(giveaway, "countdown_reveal_seconds", obs_module_text("Giveaway.CountdownReveal"), 0, 3600, 5);
  obs_properties_add_int(giveaway, "dramatic_countdown_seconds", obs_module_text("Giveaway.DramaticCountdown"), 0, 60, 1);
  obs_property_t *timer_position = obs_properties_add_list(
      giveaway, "timer_position", obs_module_text("Giveaway.TimerPosition"),
      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(timer_position, obs_module_text("TimerPosition.TopLeft"), "top_left");
  obs_property_list_add_string(timer_position, obs_module_text("TimerPosition.TopCenter"), "top_center");
  obs_property_list_add_string(timer_position, obs_module_text("TimerPosition.TopRight"), "top_right");
  obs_property_list_add_string(timer_position, obs_module_text("TimerPosition.BottomLeft"), "bottom_left");
  obs_property_list_add_string(timer_position, obs_module_text("TimerPosition.BottomCenter"), "bottom_center");
  obs_property_list_add_string(timer_position, obs_module_text("TimerPosition.BottomRight"), "bottom_right");
  obs_properties_add_int_slider(giveaway, "spin_duration_ms", obs_module_text("Giveaway.SpinDuration"), 2000, 30000, 250);
  obs_properties_add_int_slider(giveaway, "rotations", obs_module_text("Giveaway.Rotations"), 1, 20, 1);
  obs_properties_add_button(giveaway, "start", obs_module_text("Giveaway.Start"), start_button);
  obs_properties_add_button(giveaway, "spin", obs_module_text("Giveaway.Spin"), spin_button);
  obs_properties_add_button(giveaway, "cancel", obs_module_text("Giveaway.Cancel"), cancel_button);
  obs_properties_add_group(properties, "giveaway_group", obs_module_text("Group.Giveaway"), OBS_GROUP_NORMAL, giveaway);

  obs_properties_t *appearance = obs_properties_create();
  obs_properties_add_int(appearance, "width", obs_module_text("Appearance.Width"), 320, 7680, 1);
  obs_properties_add_int(appearance, "height", obs_module_text("Appearance.Height"), 240, 4320, 1);
  obs_properties_add_color_alpha(appearance, "color_a", obs_module_text("Appearance.ColorA"));
  obs_properties_add_color_alpha(appearance, "color_b", obs_module_text("Appearance.ColorB"));
  obs_properties_add_color_alpha(appearance, "accent", obs_module_text("Appearance.Accent"));
  obs_properties_add_color_alpha(appearance, "text_color", obs_module_text("Appearance.Text"));
  obs_properties_add_color_alpha(appearance, "background", obs_module_text("Appearance.Background"));
  obs_property_t *font_face = obs_properties_add_list(
      appearance, "font_face", obs_module_text("Appearance.Font"),
      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(font_face, "Segoe UI", "Segoe UI");
  obs_property_list_add_string(font_face, "Bahnschrift SemiBold", "Bahnschrift SemiBold");
  obs_property_list_add_string(font_face, "Aptos Display", "Aptos Display");
  obs_property_list_add_string(font_face, "Trebuchet MS", "Trebuchet MS");
  obs_property_t *color_style = obs_properties_add_list(
      appearance, "color_style", obs_module_text("Appearance.ColorStyle"),
      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(color_style, obs_module_text("ColorStyle.Excel"), "excel");
  obs_property_list_add_string(color_style, obs_module_text("ColorStyle.Gradient"), "gradient");
  obs_property_list_add_string(color_style, obs_module_text("ColorStyle.Rainbow"), "rainbow");
  obs_properties_add_float_slider(appearance, "wheel_size", obs_module_text("Appearance.WheelSize"), 0.35, 0.95, 0.01);
  obs_properties_add_bool(appearance, "glow", obs_module_text("Appearance.Glow"));
  obs_properties_add_float_slider(appearance, "glow_strength", obs_module_text("Appearance.GlowStrength"), 0.0, 1.0, 0.05);
  obs_properties_add_bool(appearance, "depth", obs_module_text("Appearance.Depth"));
  obs_properties_add_float_slider(appearance, "depth_strength", obs_module_text("Appearance.DepthStrength"), 0.0, 1.0, 0.05);
  obs_properties_add_bool(appearance, "show_status", obs_module_text("Appearance.Status"));
  obs_properties_add_bool(appearance, "confetti", obs_module_text("Appearance.Confetti"));
  obs_properties_add_bool(appearance, "only_when_running", obs_module_text("Appearance.OnlyRunning"));
  obs_properties_add_group(properties, "appearance_group", obs_module_text("Group.Appearance"), OBS_GROUP_NORMAL, appearance);

  obs_properties_t *testing = obs_properties_create();
  obs_properties_add_button(testing, "demo_entry", obs_module_text("Testing.Entry"), demo_entry_button);
  obs_properties_add_button(testing, "demo_spin", obs_module_text("Testing.Spin"), demo_spin_button);
  obs_properties_add_group(properties, "testing_group", obs_module_text("Group.Testing"), OBS_GROUP_NORMAL, testing);
  (void)data;
  return properties;
}

uint32_t source_width(void *data)
{
  return static_cast<FortunaSource *>(data)->settings.width;
}

uint32_t source_height(void *data)
{
  return static_cast<FortunaSource *>(data)->settings.height;
}

void source_tick(void *data, float seconds)
{
  auto *source = static_cast<FortunaSource *>(data);
  std::string title;
  std::string phase;
  std::string status;
  std::string winner;
  std::vector<Entrant> entrants;
  int entry_count = 0;
  int target = 0;
  int remaining = 0;
  int spin_index = -1;
  std::int64_t spin_giveaway_id = 0;
  int spin_ms = 8000;
  bool begin_spin = false;
  bool remote_spin = false;
  {
    std::lock_guard<std::mutex> lock(source->state.mutex);
    auto &state = source->state;
    title = state.title;
    phase = state.phase;
    status = state.connection_status;
    winner = state.winner_name;
    const std::string winner_id = state.winner_id;
    entrants = state.entrants;
    entry_count = state.entry_count;
    target = state.target_entries;
    remaining = state.remaining_seconds;
    if (state.pending_spin) {
      begin_spin = true;
      spin_index = state.pending_winner_index;
      spin_giveaway_id = state.pending_giveaway_id;
      spin_ms = state.pending_spin_ms;
      remote_spin = state.pending_remote_spin;
      state.pending_spin = false;
      state.pending_remote_spin = false;
      source->active_winner_id = winner_id;
    }
  }

  if (begin_spin) {
    const auto selected = std::find_if(
        entrants.begin(), entrants.end(), [source](const Entrant &entrant) {
          return entrant.id == source->active_winner_id;
        });
    if (selected != entrants.end())
      spin_index = static_cast<int>(std::distance(entrants.begin(), selected));
    source->spin.start(source->wheel_angle, spin_index,
                       static_cast<int>(entrants.size()), spin_ms / 1000.0f,
                       source->settings.rotations);
    source->active_giveaway_id = spin_giveaway_id;
    source->spin_completion_sent = !remote_spin;
    source->winner_elapsed = 0.0f;
  }
  if (source->spin.active()) {
    source->spin.tick(std::clamp(seconds, 0.0f, 0.25f));
    source->wheel_angle = source->spin.angle();
    if (source->spin.finished() && !source->spin_completion_sent) {
      source->network->send(excel_fortuna::make_spin_complete_command(
          source->active_giveaway_id, source->active_winner_id));
      source->spin_completion_sent = true;
    }
  } else if (phase == "winner" || source->spin.finished()) {
    source->winner_elapsed += std::max(0.0f, seconds);
  }

  update_text(source->title_text, title.empty() ? source->settings.title : title,
              source->settings.font_face, 42, source->settings.text_color);
  std::ostringstream counter;
  counter << entry_count;
  if (target > 0) counter << " / " << target;
  counter << (entry_count == 1 ? " ENTRY" : " ENTRIES");
  if (remaining > 0) counter << "  |  " << remaining << "s";
  update_text(source->counter_text, counter.str(), source->settings.font_face,
              28, source->settings.accent);
  update_text(source->status_text, status, source->settings.font_face, 18,
              phase == "open" ? rgba(67, 255, 173) : source->settings.text_color);
  const bool dramatic_countdown = phase == "open" && remaining > 0 &&
      remaining <= source->settings.dramatic_countdown_seconds;
  update_text(source->countdown_label_text,
              phase == "open" && remaining > 0 ? "GIVEAWAY DRAW IN" : "",
              source->settings.font_face, dramatic_countdown ? 34 : 17,
              source->settings.text_color);
  update_text(source->countdown_time_text,
              phase == "open" && remaining > 0
                  ? (dramatic_countdown ? std::to_string(remaining)
                                        : countdown_clock(remaining))
                  : "",
              source->settings.font_face, dramatic_countdown ? 210 : 48,
              source->settings.accent);
  std::ostringstream countdown_entries;
  countdown_entries << entry_count
                    << (entry_count == 1 ? " ENTRANT" : " ENTRANTS");
  update_text(source->countdown_entries_text,
              phase == "open" && remaining > 0 ? countdown_entries.str() : "",
              source->settings.font_face, dramatic_countdown ? 26 : 15,
              source->settings.text_color);
  update_text(source->winner_kicker_text,
              winner.empty() ? "" : "FORTUNA HAS CHOSEN",
              source->settings.font_face, 23, source->settings.text_color);
  update_text(source->winner_text, winner, source->settings.font_face,
              52, source->settings.accent);
  update_text(source->winner_message_text,
              winner.empty() ? "" : "CONGRATULATIONS!",
              source->settings.font_face, 21, source->settings.text_color);

  const std::size_t label_count = std::min<std::size_t>(entrants.size(), 128);
  while (source->slice_texts.size() < label_count) {
    TextLayer layer;
    const std::string name = "ExcelFortuna Slice " +
                             std::to_string(source->slice_texts.size());
    create_text_layer(layer, name.c_str());
    source->slice_texts.push_back(std::move(layer));
  }
  while (source->slice_texts.size() > label_count) {
    destroy_text_layer(source->slice_texts.back());
    source->slice_texts.pop_back();
  }
  const int slice_font_size = entrants.size() <= 12 ? 23 :
                              entrants.size() <= 24 ? 17 :
                              entrants.size() <= 48 ? 13 : 10;
  for (std::size_t i = 0; i < label_count; ++i)
    update_text(source->slice_texts[i], entrants[i].name,
                source->settings.font_face, slice_font_size,
                source->settings.text_color);
}

void render_confetti(std::vector<Vertex> &vertices, const FortunaSource &source)
{
  if (!source.settings.confetti || source.winner_elapsed <= 0.0f ||
      source.winner_elapsed > 8.0f)
    return;
  const float width = static_cast<float>(source.settings.width);
  const float height = static_cast<float>(source.settings.height);
  for (int i = 0; i < 70; ++i) {
    const uint32_t seed = static_cast<uint32_t>(i * 2654435761U);
    const float x = static_cast<float>(seed % 10000U) / 10000.0f * width;
    const float speed = 70.0f + static_cast<float>((seed >> 8U) % 190U);
    const float y = std::fmod(static_cast<float>((seed >> 16U) % 1000U) -
                                  300.0f + source.winner_elapsed * speed,
                              height + 80.0f) - 30.0f;
    const float size = 4.0f + static_cast<float>((seed >> 5U) % 8U);
    const uint32_t color = mix_color(source.settings.color_a,
                                     source.settings.color_b,
                                     static_cast<float>(i % 10) / 9.0f);
    rectangle(vertices, x, y, x + size, y + size * 2.0f, color);
  }
}

void source_render(void *data, gs_effect_t *)
{
  auto *source = static_cast<FortunaSource *>(data);
  std::vector<Entrant> entrants;
  std::string phase;
  int remaining = 0;
  int entry_count = 0;
  {
    std::lock_guard<std::mutex> lock(source->state.mutex);
    entrants = source->state.entrants;
    phase = source->state.phase;
    remaining = source->state.remaining_seconds;
    entry_count = source->state.entry_count;
  }

  const bool winner_visible =
      ((!source->spin.active() && source->spin.finished()) || phase == "winner") &&
      source->winner_elapsed <= 10.0f;
  const DisplayMode display_mode = excel_fortuna::choose_display_mode(
      source->settings.only_when_running, phase, remaining,
      source->settings.countdown_reveal_seconds,
      source->settings.dramatic_countdown_seconds, winner_visible);
  if (display_mode == DisplayMode::Hidden)
    return;
  const bool countdown_visible = display_mode == DisplayMode::Countdown ||
      display_mode == DisplayMode::DramaticCountdown;

  std::vector<Vertex> vertices;
  vertices.reserve(20000);
  const float width = static_cast<float>(source->settings.width);
  const float height = static_cast<float>(source->settings.height);
  if ((source->settings.background >> 24U) != 0)
    rectangle(vertices, 0.0f, 0.0f, width, height, source->settings.background);

  if (countdown_visible) {
    const bool dramatic = display_mode == DisplayMode::DramaticCountdown;
    float text_center_x = width * 0.5f;
    float label_y = 0.0f;
    float time_center_y = 0.0f;
    float entries_y = 0.0f;
    float text_width = width * 0.72f;
    if (dramatic) {
      rectangle(vertices, 0.0f, 0.0f, width, height, rgba(7, 8, 18, 198));
      const Point countdown_center{width * 0.5f, height * 0.50f};
      const float ring_radius = std::min(width, height) * 0.265f;
      if (source->settings.glow) {
        for (int layer = 4; layer >= 1; --layer) {
          wheel_segment(vertices, countdown_center,
                        ring_radius + layer * 8.0f,
                        ring_radius + layer * 12.0f,
                        0.0f, 2.0f * kPi,
                        with_alpha(source->settings.color_b, 0.0f),
                        with_alpha(source->settings.color_b,
                                   source->settings.glow_strength * 0.10f));
        }
      }
      wheel_segment(vertices, countdown_center, ring_radius - 4.0f,
                    ring_radius + 4.0f, 0.0f, 2.0f * kPi,
                    shade_color(source->settings.accent, 0.50f),
                    source->settings.accent);
      label_y = std::max(18.0f, countdown_center.y - ring_radius - 72.0f);
      time_center_y = countdown_center.y;
      entries_y = countdown_center.y + ring_radius - 46.0f;
      text_width = ring_radius * 1.55f;
    } else {
      const float margin = std::clamp(std::min(width, height) * 0.03f,
                                      14.0f, 34.0f);
      const float panel_width = std::min(width - margin * 2.0f,
          std::clamp(width * 0.245f, 250.0f, 390.0f));
      const float panel_height = std::min(height - margin * 2.0f,
          std::clamp(height * 0.16f, 104.0f, 142.0f));
      float panel_x0 = margin;
      float panel_y0 = margin;
      const bool centered = source->settings.timer_position == TimerPosition::TopCenter ||
          source->settings.timer_position == TimerPosition::BottomCenter;
      const bool right = source->settings.timer_position == TimerPosition::TopRight ||
          source->settings.timer_position == TimerPosition::BottomRight;
      const bool bottom = source->settings.timer_position == TimerPosition::BottomLeft ||
          source->settings.timer_position == TimerPosition::BottomCenter ||
          source->settings.timer_position == TimerPosition::BottomRight;
      if (centered) panel_x0 = (width - panel_width) * 0.5f;
      else if (right) panel_x0 = width - margin - panel_width;
      if (bottom) panel_y0 = height - margin - panel_height;
      const float panel_x1 = panel_x0 + panel_width;
      const float panel_y1 = panel_y0 + panel_height;
      const float radius = std::min(20.0f, panel_height * 0.17f);
      rounded_rectangle(vertices, panel_x0 + 6.0f, panel_y0 + 7.0f,
                        panel_x1 + 6.0f, panel_y1 + 7.0f, radius,
                        rgba(0, 0, 0, 94));
      rounded_rectangle(vertices, panel_x0, panel_y0, panel_x1, panel_y1,
                        radius, with_alpha(source->settings.color_b, 0.82f));
      rounded_rectangle(vertices, panel_x0 + 2.0f, panel_y0 + 2.0f,
                        panel_x1 - 2.0f, panel_y1 - 2.0f,
                        std::max(1.0f, radius - 2.0f),
                        rgba(17, 18, 31, 238));
      text_center_x = (panel_x0 + panel_x1) * 0.5f;
      label_y = panel_y0 + 8.0f;
      time_center_y = panel_y0 + panel_height * 0.53f;
      entries_y = panel_y1 - 27.0f;
      text_width = panel_width - 24.0f;
    }

    if (!vertices.empty()) {
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
    render_text(source->countdown_label_text, text_center_x, label_y,
                text_width, true);
    render_text_centered(source->countdown_time_text, text_center_x,
                         time_center_y, text_width);
    render_text(source->countdown_entries_text, text_center_x, entries_y,
                text_width, true);
    return;
  }

  const bool side_panel = width >= height * 1.38f;
  const float wheel_region = side_panel ? width * 0.67f : width;
  const Point center{wheel_region * 0.5f, height * 0.53f};
  const float radius = std::min(wheel_region * 0.43f, height * 0.37f) *
                       source->settings.wheel_size / 0.78f;

  const float depth = source->settings.depth
                          ? 5.0f + source->settings.depth_strength * 13.0f
                          : 0.0f;
  if (source->settings.depth) {
    // Soft cast shadow and a lowered dark wheel body create real separation
    // from the scene without requiring an expensive custom shader.
    circle(vertices, {center.x + depth * 0.30f, center.y + depth * 0.72f},
           radius + 10.0f, rgba(0, 0, 0, 82), 192);
  }

  if (source->settings.glow) {
    for (int layer = 5; layer >= 1; --layer) {
      const float alpha = source->settings.glow_strength *
                          static_cast<float>(6 - layer) * 0.025f;
      circle(vertices, center, radius + layer * 8.0f,
             with_alpha(source->settings.color_b, alpha), 192);
    }
  }

  const int segments = std::max(1, static_cast<int>(entrants.empty() ? 12 : entrants.size()));
  const float segment_angle = 2.0f * kPi / static_cast<float>(segments);
  const float inner_radius = radius * 0.21f;
  const uint32_t outline = rgba(8, 8, 12, 255);
  if (source->settings.depth) {
    circle(vertices, {center.x, center.y + depth}, radius + 5.5f,
           shade_color(source->settings.color_b,
                       0.20f + source->settings.depth_strength * 0.18f),
           192);
  }
  circle(vertices, center, radius + 6.0f, outline, 192);
  circle(vertices, center, radius + 3.2f,
         shade_color(source->settings.accent,
                     source->settings.depth ? 0.72f : 0.52f), 192);
  circle(vertices, center, radius + 0.8f, outline, 192);
  for (int i = 0; i < segments; ++i) {
    const auto color = segment_color(source->settings, i, segments);
    const float start = source->wheel_angle + i * segment_angle;
    const float end = source->wheel_angle + (i + 1) * segment_angle;
    if (source->settings.depth) {
      const float crown_radius = radius * 0.72f;
      const float strength = source->settings.depth_strength;
      const uint32_t inner = shade_color(color, 0.66f + 0.15f * (1.0f - strength));
      const uint32_t crown = mix_color(color, rgba(255, 255, 255, 255),
                                       0.08f + 0.12f * strength);
      const uint32_t outer = shade_color(color, 0.72f + 0.14f * (1.0f - strength));
      wheel_segment(vertices, center, inner_radius, crown_radius,
                    start, end, inner, crown);
      wheel_segment(vertices, center, crown_radius, radius,
                    start, end, crown, outer);
    } else {
      wheel_segment(vertices, center, inner_radius, radius,
                    start, end, color, color);
    }
  }
  for (int i = 0; i < segments; ++i) {
    const float angle = source->wheel_angle + i * segment_angle;
    thick_line(vertices,
               {center.x + std::cos(angle) * inner_radius,
                center.y + std::sin(angle) * inner_radius},
               {center.x + std::cos(angle) * radius,
                center.y + std::sin(angle) * radius},
               1.35f, with_alpha(outline, 0.92f));
  }
  if (source->settings.depth) {
    // A restrained upper rim highlight reads as polished material while the
    // radial slice gradients give every wedge a curved, beveled surface.
    wheel_segment(vertices, center, radius * 0.82f, radius * 0.965f,
                  -kPi, 0.0f, rgba(255, 255, 255, 0),
                  rgba(255, 255, 255,
                       static_cast<uint8_t>(18 + source->settings.depth_strength * 28)));
  }
  circle(vertices, center, inner_radius + 3.5f, outline, 128);
  circle(vertices, center, radius * 0.208f,
         source->settings.depth
             ? shade_color(source->settings.accent, 0.42f)
             : outline,
         128);
  circle(vertices, center, radius * 0.195f,
         with_alpha(source->settings.accent, 0.98f), 128);
  if (source->settings.depth)
    wheel_segment(vertices, center, radius * 0.145f, radius * 0.185f,
                  -kPi, 0.0f, rgba(255, 255, 255, 4),
                  rgba(255, 255, 255, 52));
  circle(vertices, center, radius * 0.10f, rgba(20, 18, 36, 255), 96);

  const float pointer_y = center.y - radius - 12.0f;
  triangle(vertices, {center.x - 22.0f, pointer_y - 31.0f},
           {center.x + 22.0f, pointer_y - 31.0f},
           {center.x, pointer_y + 21.0f}, outline);
  if (source->settings.depth)
    triangle(vertices, {center.x - 18.0f, pointer_y - 22.0f},
             {center.x + 18.0f, pointer_y - 22.0f},
             {center.x, pointer_y + 20.0f},
             shade_color(source->settings.accent, 0.42f));
  triangle(vertices, {center.x - 18.0f, pointer_y - 27.0f},
           {center.x + 18.0f, pointer_y - 27.0f},
           {center.x, pointer_y + 15.0f},
           source->settings.depth
               ? mix_color(source->settings.accent, rgba(255, 255, 255, 255), 0.12f)
               : source->settings.accent);

  render_confetti(vertices, *source);
  if (winner_visible && side_panel) {
    const float panel_x0 = width * 0.685f;
    const float panel_x1 = width * 0.965f;
    const float panel_y0 = center.y - radius * 0.47f;
    const float panel_y1 = center.y + radius * 0.47f;
    rounded_rectangle(vertices, panel_x0 + 9.0f, panel_y0 + 11.0f,
                      panel_x1 + 9.0f, panel_y1 + 11.0f, 24.0f,
                      rgba(0, 0, 0, 92));
    rounded_rectangle(vertices, panel_x0, panel_y0, panel_x1, panel_y1,
                      24.0f, with_alpha(source->settings.accent, 0.92f));
    rounded_rectangle(vertices, panel_x0 + 3.0f, panel_y0 + 3.0f,
                      panel_x1 - 3.0f, panel_y1 - 3.0f, 21.0f,
                      rgba(17, 18, 31, 238));
  }

  if (!vertices.empty()) {
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

  render_text(source->title_text, wheel_region * 0.5f, 20.0f,
              wheel_region * 0.86f, true);
  render_text(source->counter_text, wheel_region * 0.5f, height - 64.0f,
              wheel_region * 0.86f, true);
  if (source->settings.show_status)
    render_text(source->status_text, 14.0f, height - 26.0f,
                width * 0.75f, false);
  const std::size_t visible_labels = std::min(entrants.size(), source->slice_texts.size());
  for (std::size_t i = 0; i < visible_labels; ++i) {
    const float middle = source->wheel_angle +
                         (static_cast<float>(i) + 0.5f) * segment_angle;
    float text_angle = middle;
    if (std::cos(middle) < 0.0f)
      text_angle += kPi;
    const float label_radius = radius * 0.60f;
    render_rotated_text(source->slice_texts[i],
                        center.x + std::cos(middle) * label_radius,
                        center.y + std::sin(middle) * label_radius,
                        radius * 0.62f, text_angle);
  }
  if (winner_visible) {
    if (side_panel) {
      const float panel_center = width * 0.825f;
      const float panel_width = width * 0.235f;
      render_text(source->winner_kicker_text, panel_center,
                  center.y - radius * 0.19f, panel_width, true);
      render_text(source->winner_text, panel_center,
                  center.y - 18.0f, panel_width, true);
      render_text(source->winner_message_text, panel_center,
                  center.y + radius * 0.20f, panel_width, true);
    } else {
      render_text(source->winner_kicker_text, wheel_region * 0.5f,
                  center.y - 78.0f, wheel_region * 0.62f, true);
      render_text(source->winner_text, wheel_region * 0.5f,
                  center.y - 35.0f, wheel_region * 0.62f, true);
      render_text(source->winner_message_text, wheel_region * 0.5f,
                  center.y + 38.0f, wheel_region * 0.62f, true);
    }
  }
}
} // namespace

void register_fortuna_source()
{
  obs_source_info info{};
  info.id = kSourceId;
  info.type = OBS_SOURCE_TYPE_INPUT;
  info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB;
  info.get_name = source_name;
  info.create = source_create;
  info.destroy = source_destroy;
  info.update = source_update;
  info.get_defaults = source_defaults;
  info.get_properties = source_properties;
  info.get_width = source_width;
  info.get_height = source_height;
  info.video_tick = source_tick;
  info.video_render = source_render;
  info.icon_type = OBS_ICON_TYPE_BROWSER;
  obs_register_source(&info);
}
