#pragma once

#include <atomic>
#include <functional>
#include <string>

#include "esp_websocket_client.h"

namespace esphome {
namespace transit_tracker {

class WebSocketClient {
 public:
  using MessageCallback = std::function<void(const std::string &)>;
  using StateCallback = std::function<void()>;

  WebSocketClient() = default;
  ~WebSocketClient();

  WebSocketClient(const WebSocketClient &) = delete;
  WebSocketClient &operator=(const WebSocketClient &) = delete;

  void set_uri(const std::string &uri) { uri_ = uri; }
  void set_user_agent(const std::string &user_agent) { user_agent_ = user_agent; }
  void set_headers(const std::string &headers) { headers_ = headers; }
  void set_reconnect_timeout_ms(int ms) { reconnect_timeout_ms_ = ms; }
  void set_network_timeout_ms(int ms) { network_timeout_ms_ = ms; }
  void set_buffer_size(int bytes) { buffer_size_ = bytes; }

  void set_on_message(MessageCallback cb) { on_message_ = std::move(cb); }
  void set_on_connected(StateCallback cb) { on_connected_ = std::move(cb); }
  void set_on_disconnected(StateCallback cb) { on_disconnected_ = std::move(cb); }

  bool start();
  void stop();
  bool send_text(const std::string &data);
  bool is_connected() const;

 protected:
  static void event_handler_(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
  void handle_data_(const esp_websocket_event_data_t *data);

  esp_websocket_client_handle_t client_{nullptr};
  std::string uri_;
  std::string user_agent_;
  std::string headers_;
  int reconnect_timeout_ms_{5000};
  int network_timeout_ms_{10000};
  int buffer_size_{4096};

  MessageCallback on_message_;
  StateCallback on_connected_;
  StateCallback on_disconnected_;

  std::string message_buffer_;
};

}  // namespace transit_tracker
}  // namespace esphome
