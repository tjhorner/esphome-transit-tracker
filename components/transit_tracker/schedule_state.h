#pragma once

#include <vector>
#include <mutex>

#include "esphome/components/display/display.h"

namespace esphome {
namespace transit_tracker {

class Trip {
  public:
    std::string route_id;
    std::string stop_id;
    std::string route_name;
    Color route_color;
    std::string headsign;
    time_t arrival_time;
    time_t departure_time;
    bool is_realtime;
};

class ScheduleState {
  public:
    std::mutex mutex;
    std::vector<Trip> trips;
};

} // namespace transit_tracker
} // namespace esphome