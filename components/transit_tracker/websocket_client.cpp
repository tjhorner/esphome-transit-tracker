#include "websocket_client.h"

#include <cstring>

#include "esphome/core/log.h"
#include "sdkconfig.h"

#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

namespace esphome {
namespace transit_tracker {

static const char *const TAG = "transit_tracker.ws";

static const char *error_type_to_string(esp_websocket_error_type_t t) {
  switch (t) {
    case WEBSOCKET_ERROR_TYPE_NONE: return "none";
    case WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT: return "tcp_transport";
    case WEBSOCKET_ERROR_TYPE_PONG_TIMEOUT: return "pong_timeout";
    case WEBSOCKET_ERROR_TYPE_HANDSHAKE: return "handshake";
    case WEBSOCKET_ERROR_TYPE_SERVER_CLOSE: return "server_close";
    default: return "unknown";
  }
}

static void log_error_details(const esp_websocket_event_data_t *data) {
  const auto &err = data->error_handle;
  ESP_LOGW(TAG, "  error_type=%s (%d)", error_type_to_string(err.error_type), err.error_type);
  if (err.esp_ws_handshake_status_code != 0) {
    ESP_LOGW(TAG, "  handshake_status=%d", err.esp_ws_handshake_status_code);
  }
  if (err.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT) {
    if (err.esp_tls_last_esp_err != 0) {
      ESP_LOGW(TAG, "  esp_tls_last_err=0x%x (%s)", err.esp_tls_last_esp_err,
               esp_err_to_name(err.esp_tls_last_esp_err));
    }
    if (err.esp_tls_stack_err != 0) {
      ESP_LOGW(TAG, "  tls_stack_err=-0x%04x", -err.esp_tls_stack_err);
    }
    if (err.esp_tls_cert_verify_flags != 0) {
      ESP_LOGW(TAG, "  tls_cert_verify_flags=0x%x", err.esp_tls_cert_verify_flags);
    }
    if (err.esp_transport_sock_errno != 0) {
      ESP_LOGW(TAG, "  socket_errno=%d (%s)", err.esp_transport_sock_errno,
               strerror(err.esp_transport_sock_errno));
    }
  }
}

WebSocketClient::~WebSocketClient() {
  if (client_ != nullptr) {
    esp_websocket_client_destroy(client_);
    client_ = nullptr;
  }
}

bool WebSocketClient::start() {
  if (uri_.empty()) {
    ESP_LOGW(TAG, "No URI set, cannot start");
    return false;
  }

  if (client_ == nullptr) {
    esp_websocket_client_config_t cfg = {};
    cfg.uri = uri_.c_str();
    cfg.user_agent = user_agent_.empty() ? nullptr : user_agent_.c_str();
    cfg.headers = headers_.empty() ? nullptr : headers_.c_str();
    cfg.reconnect_timeout_ms = reconnect_timeout_ms_;
    cfg.network_timeout_ms = network_timeout_ms_;
    cfg.buffer_size = buffer_size_;
    cfg.enable_close_reconnect = true;
    cfg.disable_auto_reconnect = false;
    cfg.disable_pingpong_discon = false;
    cfg.keep_alive_enable = true;
    cfg.keep_alive_idle = 5;
    cfg.keep_alive_interval = 5;
    cfg.keep_alive_count = 3;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif

    client_ = esp_websocket_client_init(&cfg);
    if (client_ == nullptr) {
      ESP_LOGE(TAG, "Failed to initialize websocket client");
      return false;
    }

    esp_websocket_register_events(client_, WEBSOCKET_EVENT_ANY, &WebSocketClient::event_handler_, this);
  }

  esp_err_t err = esp_websocket_client_start(client_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start websocket client: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

void WebSocketClient::stop() {
  if (client_ == nullptr) {
    return;
  }

  // don't call from inside an event handler; destroy waits for the WS task
  esp_websocket_client_stop(client_);
  esp_websocket_client_destroy(client_);
  client_ = nullptr;
  message_buffer_.clear();
}

bool WebSocketClient::send_text(const std::string &data) {
  if (!is_connected()) {
    return false;
  }

  int sent = esp_websocket_client_send_text(client_, data.c_str(), data.size(), pdMS_TO_TICKS(5000));
  return sent >= 0;
}

bool WebSocketClient::is_connected() const {
  return client_ != nullptr && esp_websocket_client_is_connected(client_);
}

void WebSocketClient::event_handler_(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  auto *self = static_cast<WebSocketClient *>(handler_args);
  auto *data = static_cast<esp_websocket_event_data_t *>(event_data);

  switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
      ESP_LOGI(TAG, "Connected to %s", self->uri_.c_str());
      if (self->on_connected_) {
        self->on_connected_();
      }
      break;

    case WEBSOCKET_EVENT_DISCONNECTED:
      ESP_LOGW(TAG, "Disconnected");
      log_error_details(data);
      self->message_buffer_.clear();
      if (self->on_disconnected_) {
        self->on_disconnected_();
      }
      break;

    case WEBSOCKET_EVENT_DATA:
      self->handle_data_(data);
      break;

    case WEBSOCKET_EVENT_ERROR:
      if (data->data_len > 0) {
        ESP_LOGW(TAG, "Error: %.*s", data->data_len, data->data_ptr);
      } else {
        ESP_LOGW(TAG, "Error");
      }
      log_error_details(data);
      break;

    case WEBSOCKET_EVENT_CLOSED:
      ESP_LOGI(TAG, "Closed");
      self->message_buffer_.clear();
      break;

    case WEBSOCKET_EVENT_BEFORE_CONNECT:
      ESP_LOGD(TAG, "Connecting to %s", self->uri_.c_str());
      break;

    default:
      break;
  }
}

void WebSocketClient::handle_data_(const esp_websocket_event_data_t *data) {
  const uint8_t op = data->op_code;

  // Log close frames (op 0x08) for diagnostics before the connection tears down
  if (op == 0x08 && data->data_len >= 2) {
    auto bytes = reinterpret_cast<const uint8_t *>(data->data_ptr);
    uint16_t close_code = (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
    if (data->data_len > 2) {
      ESP_LOGW(TAG, "Server sent close: code=%u reason=\"%.*s\"", close_code,
               data->data_len - 2, data->data_ptr + 2);
    } else {
      ESP_LOGW(TAG, "Server sent close: code=%u", close_code);
    }
    return;
  }

  const bool is_data_frame = (op == 0x00 || op == 0x01 || op == 0x02);
  if (!is_data_frame || data->payload_len <= 0) {
    return;
  }

  if (data->payload_offset == 0) {
    message_buffer_.clear();
    message_buffer_.reserve(data->payload_len);
  }

  message_buffer_.append(data->data_ptr, data->data_len);

  const bool message_complete = (data->payload_offset + data->data_len) >= data->payload_len;
  if (message_complete && data->fin && on_message_) {
    on_message_(message_buffer_);
    message_buffer_.clear();
  }
}

}  // namespace transit_tracker
}  // namespace esphome
