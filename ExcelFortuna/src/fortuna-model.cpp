#include "fortuna-model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace excel_fortuna {
namespace {
constexpr float kPi = 3.14159265358979323846f;

std::size_t value_position(const std::string &json, const char *key)
{
  const std::string needle = std::string("\"") + key + "\"";
  auto position = json.find(needle);
  if (position == std::string::npos)
    return position;
  position = json.find(':', position + needle.size());
  if (position == std::string::npos)
    return position;
  ++position;
  while (position < json.size() &&
         (json[position] == ' ' || json[position] == '\t' ||
          json[position] == '\r' || json[position] == '\n'))
    ++position;
  return position;
}

std::string string_value(const std::string &json, const char *key)
{
  auto position = value_position(json, key);
  if (position == std::string::npos || position >= json.size() ||
      json[position] != '"')
    return {};
  ++position;
  std::string value;
  bool escaped = false;
  for (; position < json.size(); ++position) {
    const char ch = json[position];
    if (escaped) {
      switch (ch) {
      case 'n': value.push_back('\n'); break;
      case 'r': value.push_back('\r'); break;
      case 't': value.push_back('\t'); break;
      case 'b': value.push_back('\b'); break;
      case 'f': value.push_back('\f'); break;
      default: value.push_back(ch); break;
      }
      escaped = false;
    } else if (ch == '\\') {
      escaped = true;
    } else if (ch == '"') {
      break;
    } else {
      value.push_back(ch);
    }
  }
  return value;
}

std::int64_t integer_value(const std::string &json, const char *key,
                           std::int64_t fallback = 0)
{
  const auto position = value_position(json, key);
  if (position == std::string::npos || position >= json.size())
    return fallback;
  char *end = nullptr;
  const auto result = std::strtoll(json.c_str() + position, &end, 10);
  return end == json.c_str() + position ? fallback : result;
}
} // namespace

ProtocolEvent parse_protocol_event(const std::string &json)
{
  ProtocolEvent event;
  const auto type = string_value(json, "type");
  if (type == "hello") event.type = EventType::Hello;
  else if (type == "reset_entries") event.type = EventType::ResetEntries;
  else if (type == "state") event.type = EventType::State;
  else if (type == "entry") event.type = EventType::Entry;
  else if (type == "spin") event.type = EventType::Spin;
  else if (type == "winner") event.type = EventType::Winner;
  else if (type == "cancelled") event.type = EventType::Cancelled;
  else if (type == "error") event.type = EventType::Error;
  else if (type == "pong") event.type = EventType::Pong;

  event.status = string_value(json, "status");
  event.channel = string_value(json, "channel");
  event.title = string_value(json, "title");
  event.user_id = string_value(json, "user_id");
  event.display_name = string_value(json, "display_name");
  event.winner_id = string_value(json, "winner_id");
  event.winner_display_name = string_value(json, "winner_display_name");
  event.message = string_value(json, "message");
  event.giveaway_id = integer_value(json, "giveaway_id");
  event.entry_count = static_cast<int>(integer_value(json, "entry_count"));
  event.target_entries = static_cast<int>(integer_value(json, "target_entries"));
  event.remaining_seconds =
      static_cast<int>(integer_value(json, "remaining_seconds"));
  event.winner_index = static_cast<int>(integer_value(json, "winner_index", -1));
  event.spin_duration_ms =
      static_cast<int>(integer_value(json, "spin_duration_ms", 8000));
  return event;
}

std::string json_escape(const std::string &value)
{
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (const unsigned char ch : value) {
    switch (ch) {
    case '"': escaped += "\\\""; break;
    case '\\': escaped += "\\\\"; break;
    case '\b': escaped += "\\b"; break;
    case '\f': escaped += "\\f"; break;
    case '\n': escaped += "\\n"; break;
    case '\r': escaped += "\\r"; break;
    case '\t': escaped += "\\t"; break;
    default:
      if (ch >= 0x20)
        escaped.push_back(static_cast<char>(ch));
      break;
    }
  }
  return escaped;
}

std::string make_start_command(const std::string &title,
                               const std::string &reward_title,
                               int target_entries, int duration_seconds,
                               int spin_duration_ms)
{
  std::ostringstream json;
  json << "{\"type\":\"start\",\"title\":\"" << json_escape(title)
       << "\",\"reward_title\":\"" << json_escape(reward_title)
       << "\",\"target_entries\":" << std::max(0, target_entries)
       << ",\"duration_seconds\":" << std::max(0, duration_seconds)
       << ",\"spin_duration_ms\":"
       << std::clamp(spin_duration_ms, 2000, 30000) << '}';
  return json.str();
}

std::string make_simple_command(const char *type)
{
  return std::string("{\"type\":\"") + json_escape(type ? type : "") + "\"}";
}

void SpinAnimation::start(float current_angle, int winner_index,
                          int entry_count, float duration_seconds,
                          int rotations)
{
  start_angle_ = current_angle;
  elapsed_ = 0.0f;
  duration_ = std::clamp(duration_seconds, 0.25f, 30.0f);
  started_ = entry_count > 0 && winner_index >= 0 && winner_index < entry_count;
  if (!started_) {
    target_angle_ = current_angle;
    return;
  }

  const float segment = 2.0f * kPi / static_cast<float>(entry_count);
  const float pointer_angle = -kPi * 0.5f;
  float desired = pointer_angle - (static_cast<float>(winner_index) + 0.5f) * segment;
  while (desired <= current_angle)
    desired += 2.0f * kPi;
  target_angle_ = desired + static_cast<float>(std::max(1, rotations)) * 2.0f * kPi;
}

void SpinAnimation::tick(float seconds)
{
  if (!started_ || elapsed_ >= duration_)
    return;
  elapsed_ = std::min(duration_, elapsed_ + std::max(0.0f, seconds));
}

float SpinAnimation::angle() const
{
  if (!started_)
    return start_angle_;
  const float progress = duration_ <= 0.0f ? 1.0f : elapsed_ / duration_;
  const float inverse = 1.0f - std::clamp(progress, 0.0f, 1.0f);
  const float eased = 1.0f - inverse * inverse * inverse * inverse * inverse;
  return start_angle_ + (target_angle_ - start_angle_) * eased;
}

bool SpinAnimation::active() const
{
  return started_ && elapsed_ < duration_;
}

bool SpinAnimation::finished() const
{
  return started_ && elapsed_ >= duration_;
}

} // namespace excel_fortuna
