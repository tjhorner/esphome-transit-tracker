#include "localization.h"

namespace esphome {
namespace transit_tracker {

std::string Localization::fmt_duration_from_now(time_t unix_timestamp, uint rtc_now) const {
  int diff = unix_timestamp - rtc_now;

  if (diff < 30) {
    return this->now_string_;
  }

  if (diff < 60) {
    switch (this->unit_display_) {
      case UNIT_DISPLAY_LONG:
        return "0" + this->minutes_long_string_;
      case UNIT_DISPLAY_SHORT:
        return "0" + this->minutes_short_string_;
      case UNIT_DISPLAY_NONE:
        return "0";
    }
  }

  int minutes = diff / 60;

  if (minutes < 60) {
    switch (this->unit_display_) {
      case UNIT_DISPLAY_LONG:
        return str_sprintf("%d%s", minutes, this->minutes_long_string_.c_str());
      case UNIT_DISPLAY_SHORT:
        return str_sprintf("%d%s", minutes, this->minutes_short_string_.c_str());
      case UNIT_DISPLAY_NONE:
      default:
        return str_sprintf("%d", minutes);
    }
  }

  int hours = minutes / 60;
  minutes = minutes % 60;

  switch (this->unit_display_) {
    case UNIT_DISPLAY_LONG:
    case UNIT_DISPLAY_SHORT:
      return str_sprintf("%d%s%d%s", hours, this->hours_short_string_.c_str(), minutes, this->minutes_short_string_.c_str());
    case UNIT_DISPLAY_NONE:
    default:
      return str_sprintf("%d:%02d", hours, minutes);
  }
}

}
}