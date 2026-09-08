#include "fortuna-network.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace excel_fortuna {

class FortunaNetwork::Impl {
public:
  Impl(EventCallback event_callback, StatusCallback status_callback)
      : event_callback_(std::move(event_callback)),
        status_callback_(std::move(status_callback))
  {
  }

  ~Impl() { disconnect(); }

  void connect(ConnectionConfig config)
  {
    disconnect();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      config_ = std::move(config);
      stopping_ = false;
    }
    worker_ = std::thread([this] { run(); });
  }

  void disconnect()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
      outgoing_.clear();
#ifdef _WIN32
      if (websocket_) {
        WinHttpCloseHandle(websocket_);
        websocket_ = nullptr;
      }
#endif
    }
    wake_.notify_all();
    if (worker_.joinable())
      worker_.join();
    if (status_callback_)
      status_callback_(false, "Disconnected");
  }

  void send(std::string json)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      outgoing_.push_back(std::move(json));
    }
    wake_.notify_all();
  }

private:
  bool stopping()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return stopping_;
  }

  void status(bool connected, const std::string &message)
  {
    if (status_callback_)
      status_callback_(connected, message);
  }

#ifdef _WIN32
  static std::wstring wide(const std::string &text)
  {
    if (text.empty())
      return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
    if (length <= 0)
      return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), length);
    return result;
  }

  static std::string windows_error(const char *prefix)
  {
    return std::string(prefix) + " (Windows error " +
           std::to_string(GetLastError()) + ')';
  }

  bool open_socket(const ConnectionConfig &config)
  {
    std::string normalized = config.server_url;
    if (normalized.rfind("wss://", 0) == 0)
      normalized.replace(0, 6, "https://");
    else if (normalized.rfind("ws://", 0) == 0)
      normalized.replace(0, 5, "http://");
    if (normalized.find("/api/fortuna/plugin/ws") == std::string::npos) {
      while (!normalized.empty() && normalized.back() == '/')
        normalized.pop_back();
      normalized += "/api/fortuna/plugin/ws";
    }

    auto url = wide(normalized);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.data(), static_cast<DWORD>(url.size()), 0,
                         &parts)) {
      status(false, "Invalid server URL");
      return false;
    }

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
      path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty())
      path = L"/api/fortuna/plugin/ws";

    session_ = WinHttpOpen(L"ExcelFortuna/0.4.0",
                           WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session_) {
      status(false, windows_error("Could not initialize Windows networking"));
      return false;
    }
    WinHttpSetTimeouts(session_, 5000, 5000, 5000, 750);

    connection_ = WinHttpConnect(session_, host.c_str(), parts.nPort, 0);
    if (!connection_) {
      status(false, windows_error("Could not connect to server"));
      close_handles();
      return false;
    }

    const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS
                            ? WINHTTP_FLAG_SECURE
                            : 0;
    request_ = WinHttpOpenRequest(connection_, L"GET", path.c_str(), nullptr,
                                  WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request_) {
      status(false, windows_error("Could not create WebSocket request"));
      close_handles();
      return false;
    }

    if (!WinHttpSetOption(request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                          nullptr, 0)) {
      status(false, windows_error("WebSocket upgrade is unavailable"));
      close_handles();
      return false;
    }
    const auto auth = wide("Authorization: Bearer " + config.access_key);
    if (!WinHttpAddRequestHeaders(request_, auth.c_str(),
                                  static_cast<DWORD>(-1L),
                                  WINHTTP_ADDREQ_FLAG_ADD |
                                      WINHTTP_ADDREQ_FLAG_REPLACE) ||
        !WinHttpSendRequest(request_, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request_, nullptr)) {
      status(false, windows_error("ExcelProtocol did not accept the connection"));
      close_handles();
      return false;
    }

    HINTERNET socket = WinHttpWebSocketCompleteUpgrade(request_, 0);
    if (!socket) {
      status(false, windows_error("ExcelProtocol rejected the WebSocket upgrade"));
      close_handles();
      return false;
    }
    WinHttpCloseHandle(request_);
    request_ = nullptr;
    bool stop_now = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_now = stopping_;
      if (!stop_now)
        websocket_ = socket;
    }
    if (stop_now) {
      WinHttpCloseHandle(socket);
      close_handles();
      return false;
    }
    status(true, "Connected to ExcelProtocol");
    return true;
  }

  void close_handles()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (websocket_) {
      WinHttpCloseHandle(websocket_);
      websocket_ = nullptr;
    }
    if (request_) {
      WinHttpCloseHandle(request_);
      request_ = nullptr;
    }
    if (connection_) {
      WinHttpCloseHandle(connection_);
      connection_ = nullptr;
    }
    if (session_) {
      WinHttpCloseHandle(session_);
      session_ = nullptr;
    }
  }

  bool send_pending()
  {
    std::deque<std::string> pending;
    HINTERNET socket = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending.swap(outgoing_);
      socket = websocket_;
    }
    if (!socket)
      return false;
    for (const auto &message : pending) {
      const DWORD result = WinHttpWebSocketSend(
          socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
          const_cast<char *>(message.data()),
          static_cast<DWORD>(message.size()));
      if (result != ERROR_SUCCESS)
        return false;
    }
    return true;
  }

  bool receive_once()
  {
    HINTERNET socket = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      socket = websocket_;
    }
    if (!socket)
      return false;

    char buffer[8192];
    DWORD bytes = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
    const DWORD result = WinHttpWebSocketReceive(socket, buffer,
                                                  sizeof(buffer), &bytes,
                                                  &type);
    if (result == ERROR_WINHTTP_TIMEOUT)
      return true;
    if (result != ERROR_SUCCESS)
      return false;
    if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
      return false;
    if ((type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
         type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) &&
        bytes > 0) {
      incoming_.append(buffer, buffer + bytes);
      if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
        if (event_callback_)
          event_callback_(parse_protocol_event(incoming_));
        incoming_.clear();
      }
    }
    return true;
  }
#endif

  void run()
  {
#ifndef _WIN32
    status(false, "ExcelFortuna networking currently supports Windows only");
#else
    int retry_seconds = 1;
    while (!stopping()) {
      ConnectionConfig config;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        config = config_;
      }
      if (config.server_url.empty() || config.access_key.empty()) {
        status(false, "Enter the ExcelProtocol server URL and plugin key");
        break;
      }
      status(false, "Connecting to ExcelProtocol...");
      if (open_socket(config)) {
        retry_seconds = 1;
        while (!stopping() && send_pending() && receive_once()) {
        }
      }
      close_handles();
      if (stopping())
        break;
      status(false, "Connection lost; retrying...");
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait_for(lock, std::chrono::seconds(retry_seconds),
                     [this] { return stopping_; });
      retry_seconds = std::min(15, retry_seconds * 2);
    }
#endif
  }

  EventCallback event_callback_;
  StatusCallback status_callback_;
  ConnectionConfig config_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::string> outgoing_;
  std::thread worker_;
  bool stopping_ = true;
  std::string incoming_;
#ifdef _WIN32
  HINTERNET session_ = nullptr;
  HINTERNET connection_ = nullptr;
  HINTERNET request_ = nullptr;
  HINTERNET websocket_ = nullptr;
#endif
};

FortunaNetwork::FortunaNetwork(EventCallback event_callback,
                               StatusCallback status_callback)
    : impl_(std::make_unique<Impl>(std::move(event_callback),
                                  std::move(status_callback)))
{
}

FortunaNetwork::~FortunaNetwork() = default;
void FortunaNetwork::connect(ConnectionConfig config) { impl_->connect(std::move(config)); }
void FortunaNetwork::disconnect() { impl_->disconnect(); }
void FortunaNetwork::send(std::string json) { impl_->send(std::move(json)); }

} // namespace excel_fortuna
