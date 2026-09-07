#pragma once

#include "fortuna-model.hpp"

#include <functional>
#include <memory>
#include <string>

namespace excel_fortuna {

struct ConnectionConfig {
  std::string server_url;
  std::string access_key;
};

class FortunaNetwork {
public:
  using EventCallback = std::function<void(const ProtocolEvent &)>;
  using StatusCallback = std::function<void(bool, const std::string &)>;

  FortunaNetwork(EventCallback event_callback, StatusCallback status_callback);
  ~FortunaNetwork();

  FortunaNetwork(const FortunaNetwork &) = delete;
  FortunaNetwork &operator=(const FortunaNetwork &) = delete;

  void connect(ConnectionConfig config);
  void disconnect();
  void send(std::string json);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace excel_fortuna
