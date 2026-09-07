#include "fortuna-model.hpp"

#include <cmath>
#include <iostream>

namespace {
int failures = 0;

void expect(bool condition, const char *message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
} // namespace

int main()
{
  using namespace excel_fortuna;
  const auto entry = parse_protocol_event(
      R"({"type":"entry","user_id":"42","display_name":"A \"Lucky\" User","entry_count":7})");
  expect(entry.type == EventType::Entry, "entry event type");
  expect(entry.user_id == "42", "entry user id");
  expect(entry.display_name == "A \"Lucky\" User", "escaped display name");
  expect(entry.entry_count == 7, "entry count");

  const auto spin = parse_protocol_event(
      R"({"type":"spin","winner_id":"99","winner_display_name":"Fortuna","winner_index":3,"spin_duration_ms":6000})");
  expect(spin.type == EventType::Spin, "spin event type");
  expect(spin.winner_index == 3, "winner index");
  expect(spin.spin_duration_ms == 6000, "spin duration");

  const auto command = make_start_command("Big \"Giveaway\"", "Giveaway Entry", 25, 300, 8000);
  expect(command.find("Big \\\"Giveaway\\\"") != std::string::npos,
         "command JSON escaping");
  expect(command.find("\"target_entries\":25") != std::string::npos,
         "command target");

  SpinAnimation animation;
  animation.start(0.0f, 2, 8, 4.0f, 5);
  expect(animation.active(), "animation starts");
  const float first = animation.angle();
  animation.tick(2.0f);
  expect(animation.angle() > first, "animation advances");
  animation.tick(2.0f);
  expect(animation.finished(), "animation completes");
  expect(std::isfinite(animation.angle()), "animation angle is finite");

  if (failures == 0)
    std::cout << "ExcelFortuna model tests passed\n";
  return failures == 0 ? 0 : 1;
}
