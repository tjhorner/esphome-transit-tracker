#pragma once

#include <string>

#include "esphome/components/time/real_time_clock.h"

namespace esphome {
namespace transit_tracker {

enum UnitDisplay : uint8_t {
  UNIT_DISPLAY_LONG,
  UNIT_DISPLAY_SHORT,
  UNIT_DISPLAY_NONE
};

class Localization {
  public:
    std::string fmt_duration_from_now(time_t unix_timestamp, uint rtc_now) const;

    void set_unit_display(UnitDisplay unit_display) { unit_display_ = unit_display; }
    void set_now_string(const std::string &now_string) { now_string_ = now_string; }
    void set_minutes_long_string(const std::string &minutes_long_string) { minutes_long_string_ = minutes_long_string; }
    void set_minutes_short_string(const std::string &minutes_short_string) { minutes_short_string_ = minutes_short_string; }
    void set_hours_short_string(const std::string &hours_short_string) { hours_short_string_ = hours_short_string; }

  protected:
    UnitDisplay unit_display_ = UNIT_DISPLAY_LONG;
    std::string now_string_ = "Now";
    std::string minutes_long_string_ = "min";
    std::string minutes_short_string_ = "m";
    std::string hours_short_string_ = "h";
};

}  // namespace transit_tracker
}  // namespace esphome