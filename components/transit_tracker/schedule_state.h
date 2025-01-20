#pragma once

#include <vector>
#include <mutex>

namespace esphome {
namespace transit_tracker {

class Trip {
  public:
    std::string route_id;
    std::string route_name;
    std::string headsign;
    time_t arrival_time;
    bool is_realtime;
};

class ScheduleState {
  public:
    std::vector<Trip> trips;
};

} // namespace transit_tracker
} // namespace esphome