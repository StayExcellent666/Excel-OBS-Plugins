#pragma once

#include <cstdint>
#include <string>

namespace excel_fortuna {

enum class EventType {
  Unknown,
  Hello,
  ResetEntries,
  State,
  Entry,
  Spin,
  Winner,
  Cancelled,
  Error,
  Pong,
};

struct ProtocolEvent {
  EventType type = EventType::Unknown;
  std::string status;
  std::string channel;
  std::string title;
  std::string user_id;
  std::string display_name;
  std::string winner_id;
  std::string winner_display_name;
  std::string message;
  std::int64_t giveaway_id = 0;
  int entry_count = 0;
  int target_entries = 0;
  int remaining_seconds = 0;
  int winner_index = -1;
  int spin_duration_ms = 8000;
};

ProtocolEvent parse_protocol_event(const std::string &json);
std::string json_escape(const std::string &value);
std::string make_start_command(const std::string &title,
                               const std::string &reward_title,
                               int target_entries, int duration_seconds,
                               int spin_duration_ms);
std::string make_simple_command(const char *type);

class SpinAnimation {
public:
  void start(float current_angle, int winner_index, int entry_count,
             float duration_seconds, int rotations);
  void tick(float seconds);
  float angle() const;
  bool active() const;
  bool finished() const;

private:
  float start_angle_ = 0.0f;
  float target_angle_ = 0.0f;
  float elapsed_ = 0.0f;
  float duration_ = 0.0f;
  bool started_ = false;
};

} // namespace excel_fortuna
