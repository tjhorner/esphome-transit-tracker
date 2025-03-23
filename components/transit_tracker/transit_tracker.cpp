#include "transit_tracker.h"
#include "string_utils.h"

#include "esphome/core/log.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/watchdog/watchdog.h"
#include "esphome/components/network/util.h"

namespace esphome {
namespace transit_tracker {

static const char *TAG = "transit_tracker.component";

void TransitTracker::setup() {
  this->ws_client_.onMessage([this](websockets::WebsocketsMessage message) {
    this->on_ws_message_(message);
  });

  this->ws_client_.onEvent([this](websockets::WebsocketsEvent event, String data) {
    this->on_ws_event_(event, data);
  });

  this->connect_ws_();
}

void TransitTracker::loop() {
  this->ws_client_.poll();

  if (this->last_heartbeat_ != 0 && millis() - this->last_heartbeat_ > 60000) {
    ESP_LOGW(TAG, "Heartbeat timeout, reconnecting");
    this->reconnect();
  }
}

void TransitTracker::dump_config(){
  ESP_LOGCONFIG(TAG, "Transit Tracker:");
  ESP_LOGCONFIG(TAG, "  Base URL: %s", this->base_url_.c_str());
  ESP_LOGCONFIG(TAG, "  Schedule: %s", this->schedule_string_.c_str());
  ESP_LOGCONFIG(TAG, "  Limit: %d", this->limit_);
}

void TransitTracker::reconnect() {
  this->close();
  this->connect_ws_();
}

void TransitTracker::close(bool fully) {
  if (fully) {
    this->fully_closed_ = true;
  }

  this->ws_client_.close();
}

void TransitTracker::on_shutdown() {
  this->close(true);
}

void TransitTracker::on_ws_message_(websockets::WebsocketsMessage message) {
  ESP_LOGV(TAG, "Received message: %s", message.rawData().c_str());

  bool valid = json::parse_json(message.rawData(), [this](JsonObject root) -> bool {
    if (root["event"].as<std::string>() == "heartbeat") {
      ESP_LOGD(TAG, "Received heartbeat");
      this->last_heartbeat_ = millis();
      return true;
    }

    if (root["event"].as<std::string>() != "schedule") {
      return true;
    }

    ESP_LOGD(TAG, "Received schedule update");

    this->schedule_state_.mutex.lock();

    this->schedule_state_.trips.clear();

    auto data = root["data"].as<JsonObject>();

    for (auto trip : data["trips"].as<JsonArray>()) {
      std::string headsign = trip["headsign"].as<std::string>();
      for (const auto &abbr : this->abbreviations_) {
        size_t pos = headsign.find(abbr.first);
        if (pos != std::string::npos) {
          ESP_LOGV(TAG, "Applying abbreviation '%s' -> '%s' in headsign", abbr.first.c_str(), abbr.second.c_str());
          headsign.replace(pos, abbr.first.length(), abbr.second);
        }
      }

      auto route_id = trip["routeId"].as<std::string>();
      auto route_style = this->route_styles_.find(route_id);

      Color route_color = this->default_route_color_;
      std::string route_name = trip["routeName"].as<std::string>();

      if (route_style != this->route_styles_.end()) {
        route_color = route_style->second.color;
        route_name = route_style->second.name;
      } else if (!trip["routeColor"].isNull()) {
        route_color = Color(std::stoul(trip["routeColor"].as<std::string>(), nullptr, 16));
      }

      this->schedule_state_.trips.push_back({
        .route_id = route_id,
        .route_name = route_name,
        .route_color = route_color,
        .headsign = headsign,
        .arrival_time = trip["arrivalTime"].as<time_t>(),
        .departure_time = trip["departureTime"].as<time_t>(),
        .is_realtime = trip["isRealtime"].as<bool>(),
      });
    }

    this->schedule_state_.mutex.unlock();

    return true;
  });

  if (!valid) {
    this->status_set_error("Failed to parse schedule data");
    return;
  }
}

void TransitTracker::on_ws_event_(websockets::WebsocketsEvent event, String data) {
  if (event == websockets::WebsocketsEvent::ConnectionOpened) {
    ESP_LOGD(TAG, "WebSocket connection opened");

    auto message = json::build_json([this](JsonObject root) {
      root["event"] = "schedule:subscribe";

      auto data = root.createNestedObject("data");
      data["feedCode"] = this->feed_code_;
      data["routeStopPairs"] = this->schedule_string_;
      data["limit"] = this->limit_;
      data["sortByDeparture"] = this->display_departure_times_;
      data["listMode"] = this->list_mode_;
    });

    ESP_LOGV(TAG, "Sending message: %s", message.c_str());
    this->ws_client_.send(message.c_str());
  } else if (event == websockets::WebsocketsEvent::ConnectionClosed) {
    ESP_LOGD(TAG, "WebSocket connection closed");
    if (!this->fully_closed_ && this->connection_attempts_ == 0) {
      this->defer([this]() {
        this->connect_ws_();
      });
    }
  } else if (event == websockets::WebsocketsEvent::GotPing) {
    ESP_LOGV(TAG, "Received ping");
  } else if (event == websockets::WebsocketsEvent::GotPong) {
    ESP_LOGV(TAG, "Received pong");
  }
}

void TransitTracker::connect_ws_() {
  if (this->base_url_.empty()) {
    ESP_LOGW(TAG, "No base URL set, not connecting");
    return;
  }

  if (this->fully_closed_) {
    ESP_LOGW(TAG, "Connection fully closed, not reconnecting");
    return;
  }

  if (this->ws_client_.available(true)) {
    ESP_LOGV(TAG, "Not reconnecting, already connected");
    return;
  }

  watchdog::WatchdogManager wdm(20000);

  this->last_heartbeat_ = 0;

  ESP_LOGD(TAG, "Connecting to WebSocket server (attempt %d): %s", this->connection_attempts_, this->base_url_.c_str());

  bool connection_success = false;
  if (esphome::network::is_connected()) {
    connection_success = this->ws_client_.connect(this->base_url_.c_str());
  } else {
    ESP_LOGW(TAG, "Not connected to network; skipping connection attempt");
  }

  if (!connection_success) {
    this->connection_attempts_++;

    if (this->connection_attempts_ >= 3) {
      this->status_set_error("Failed to connect to WebSocket server");
    }

    auto timeout = std::min(15000, this->connection_attempts_ * 5000);
    ESP_LOGW(TAG, "Failed to connect, retrying in %ds", timeout / 1000);

    this->set_timeout("reconnect", timeout, [this]() {
      this->connect_ws_();
    });
  } else {
    this->has_ever_connected_ = true;
    this->connection_attempts_ = 0;
    this->status_clear_error();
  }
}

void TransitTracker::set_display_mode_from_text(const std::string &text) {
  if (text == "destination") {
    this->set_display_mode("destination");
  } else {
    this->set_display_mode("sequential");
  }
}

void TransitTracker::set_abbreviations_from_text(const std::string &text) {
  this->abbreviations_.clear();
  for (const auto &line : split(text, '\n')) {
    auto parts = split(line, ';');
    if (parts.size() == 1) {
      // If only one part is provided, treat it as a removal (replace with empty string)
      this->add_abbreviation(parts[0], "");
      continue;
    }
    if (parts.size() != 2) {
      ESP_LOGW(TAG, "Invalid abbreviation line: %s", line.c_str());
      continue;
    }
    this->add_abbreviation(parts[0], parts[1]);
  }
}

void TransitTracker::set_route_styles_from_text(const std::string &text) {
  this->route_styles_.clear();
  for (const auto &line : split(text, '\n')) {
    auto parts = split(line, ';');
    if (parts.size() != 3) {
      ESP_LOGW(TAG, "Invalid route style line: %s", line.c_str());
      continue;
    }
    uint32_t color = std::stoul(parts[2], nullptr, 16);
    this->add_route_style(parts[0], parts[1], Color(color));
  }
}

void TransitTracker::draw_text_centered_(const char *text, Color color) {
  int display_center_x = this->display_->get_width() / 2;
  int display_center_y = this->display_->get_height() / 2;
  this->display_->print(display_center_x, display_center_y, this->font_, color, display::TextAlign::CENTER, text);
}

std::string TransitTracker::from_now_(time_t unix_timestamp) const {
  if (this->rtc_ == nullptr) {
    return "";
  }

  uint now = this->rtc_->now().timestamp;

  int diff = unix_timestamp - now;

  if (diff < 30) {
    return "Now";
  }

  if (diff < 60) {
    return "<1m";
  }

  int minutes = diff / 60;

  if (minutes < 60) {
    return str_sprintf("%dm", minutes);
  }

  int hours = minutes / 60;
  minutes = minutes % 60;

  return str_sprintf("%dh%dm", hours, minutes);
}

const uint8_t realtime_icon[6][6] = {
  {0, 0, 0, 3, 3, 3},
  {0, 0, 3, 0, 0, 0},
  {0, 3, 0, 0, 2, 2},
  {3, 0, 0, 2, 0, 0},
  {3, 0, 2, 0, 0, 1},
  {3, 0, 2, 0, 1, 1}
};

void HOT TransitTracker::draw_realtime_icon_(int bottom_right_x, int bottom_right_y) {
  const int num_frames = 6;
  const int idle_frame_duration = 3000;
  const int anim_frame_duration = 200;
  const int cycle_duration = idle_frame_duration + (num_frames - 1) * anim_frame_duration;

  long now = millis();
  long cycle_time = now % cycle_duration;

  int frame;
  if (cycle_time < idle_frame_duration) {
    frame = 0;
  } else {
    frame = 1 + (cycle_time - idle_frame_duration) / anim_frame_duration;
  }

  auto is_segment_lit = [frame](uint8_t segment) {
    switch (segment) {
      case 1: return frame >= 1 && frame <= 3;
      case 2: return frame >= 2 && frame <= 4;
      case 3: return frame >= 3 && frame <= 5;
      default: return false;
    }
  };

  const Color lit_color = Color(0x20FF00);
  const Color unlit_color = Color(0x00A700);

  for (uint8_t i = 0; i < 6; ++i) {
    for (uint8_t j = 0; j < 6; ++j) {
      uint8_t segment_number = realtime_icon[i][j];
      if (segment_number == 0) {
        continue;
      }

      Color icon_color = is_segment_lit(segment_number) ? lit_color : unlit_color;
      this->display_->draw_pixel_at(bottom_right_x - (5 - j), bottom_right_y - (5 - i), icon_color);
    }
  }
}

void HOT TransitTracker::draw_schedule() {
  if (this->display_ == nullptr) {
    ESP_LOGW(TAG, "No display attached, cannot draw schedule");
    return;
  }
  if (!esphome::network::is_connected()) {
    this->draw_text_centered_("Waiting for network", Color(0x252627));
    return;
  }
  if (!this->rtc_->now().is_valid()) {
    this->draw_text_centered_("Waiting for time sync", Color(0x252627));
    return;
  }
  if (this->base_url_.empty()) {
    this->draw_text_centered_("No base URL set", Color(0x252627));
    return;
  }
  if (this->status_has_error()) {
    this->draw_text_centered_("Error loading schedule", Color(0xFE4C5C));
    return;
  }
  if (!this->has_ever_connected_) {
    this->draw_text_centered_("Loading...", Color(0x252627));
    return;
  }
  if (this->schedule_state_.trips.empty()) {
    auto message = this->display_departure_times_ ? "No upcoming departures" : "No upcoming arrivals";
    this->draw_text_centered_(message, Color(0x252627));
    return;
  }

  this->schedule_state_.mutex.lock();

  if (this->display_mode_ == "destination") {
    // Gather trips by headsign
    std::map<std::string, std::vector<const Trip *>> destinations;
    for (auto &trip : this->schedule_state_.trips) {
      destinations[trip.headsign].push_back(&trip);
    }

    // We want to show Seattle first, then Bellevue, then everything else
    std::vector<std::string> ordered_heads;
    if (destinations.find("Seattle") != destinations.end())
      ordered_heads.push_back("Seattle");
    if (destinations.find("Bellevue") != destinations.end())
      ordered_heads.push_back("Bellevue");
    for (auto &it : destinations) {
      if (it.first != "Seattle" && it.first != "Bellevue") {
        ordered_heads.push_back(it.first);
      }
    }

    int y_offset = 2;
    for (auto &head : ordered_heads) {
      auto &trip_list = destinations[head];
      std::sort(trip_list.begin(), trip_list.end(), [this](const Trip *a, const Trip *b) {
        time_t at = this->display_departure_times_ ? a->departure_time : a->arrival_time;
        time_t bt = this->display_departure_times_ ? b->departure_time : b->arrival_time;
        return at < bt;
      });

      // Hardcoded colors
      Color head_color;
      if (head == "Seattle") {
        head_color = Color(0x3e54b6);
      } else if (head == "Bellevue") {
        head_color = Color(0x9135ed);
      } else {
        head_color = Color(0xa7a7a7);
      }

      // Print times right-aligned, each preceded by an icon if realtime
      int right_margin = this->display_->get_width();
      bool first_segment = true;
      for (int i = trip_list.size() - 1; i >= 0; i--) {
        const Trip *t = trip_list[i];
        std::string disp_time = this->from_now_(
          this->display_departure_times_ ? t->departure_time : t->arrival_time
        );

        // Add ", " if it's not the last segment to display
        if (!first_segment) {
          disp_time += ", ";
        }
        first_segment = false;

        // Measure text
        int time_w, x_off, baseline, time_h;
        this->font_->measure(disp_time.c_str(), &time_w, &x_off, &baseline, &time_h);

        right_margin -= time_w;

        Color time_color;
        if (t->is_realtime) {
          time_color = Color(0x20FF00);
        } else {
          time_color = Color(0xa7a7a7);
        }

        this->display_->print(
          right_margin, y_offset,
          this->font_, time_color,
          display::TextAlign::TOP_LEFT, disp_time.c_str());
      }

      // Print the headsign at the left
      this->display_->start_clipping(0, 0, right_margin - 4, this->display_->get_height());
      this->display_->print(0, y_offset, this->font_, head_color, head.c_str());
      this->display_->end_clipping();

      // Move down
      int w, x_off, bs, lh;
      this->font_->measure(head.c_str(), &w, &x_off, &bs, &lh);
      y_offset += lh + 2;
    }
  } else {
    int y_offset = 2;
    for (const Trip &trip : this->schedule_state_.trips) {
      this->display_->print(0, y_offset, this->font_, trip.route_color,
                            display::TextAlign::TOP_LEFT, trip.route_name.c_str());

      int route_width, route_x_offset, route_baseline, route_height;
      this->font_->measure(trip.route_name.c_str(), &route_width, &route_x_offset,
                           &route_baseline, &route_height);

      auto time_display = this->from_now_(
        this->display_departure_times_ ? trip.departure_time : trip.arrival_time
      );

      int time_width, time_x_offset, time_baseline, time_height;
      this->font_->measure(time_display.c_str(), &time_width, &time_x_offset,
                           &time_baseline, &time_height);

      int headsign_clipping_end = this->display_->get_width() - time_width - 2;
      Color time_color = trip.is_realtime ? Color(0x20FF00) : Color(0xa7a7a7);
      this->display_->print(this->display_->get_width() + 1, y_offset, this->font_,
                            time_color, display::TextAlign::TOP_RIGHT,
                            time_display.c_str());

      if (trip.is_realtime) {
        int icon_bottom_right_x = this->display_->get_width() - time_width - 2;
        int icon_bottom_right_y = y_offset + time_height - 6;
        headsign_clipping_end -= 8;
        this->draw_realtime_icon_(icon_bottom_right_x, icon_bottom_right_y);
      }

      this->display_->start_clipping(0, 0, headsign_clipping_end, this->display_->get_height());
      this->display_->print(route_width + 3, y_offset, this->font_, trip.headsign.c_str());
      this->display_->end_clipping();
      y_offset += route_height;
    }
  }

  this->schedule_state_.mutex.unlock();
}

}  // namespace transit_tracker
}  // namespace esphome