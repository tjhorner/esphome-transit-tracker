# ESPHome Transit Tracker Component

This is an external component for [ESPHome](https://esphome.io/) that fetches and renders a live arrivals board for any transit agency supported by the [Transit Tracker API](https://github.com/tjhorner/transit-tracker-api).

This component is used by the [Transit Tracker](https://transit-tracker.eastsideurbanism.org/) project. Check it out if you want to build your own!

## Note

Though this can technically work with any display supported by ESPHome, it is optimized for and tested with a 128x64 LED matrix display.

## Usage

Check out the `examples` directory for a full example configuration.

You can use this component in your ESPHome configuration by importing it with `external_components`:

```yaml
external_components:
  - source: github://tjhorner/esphome-transit-tracker
    components: [transit_tracker]
```

You will need these components in your configuration:

- [Display](https://esphome.io/components/display/)
- [Font](https://esphome.io/components/font/)
- [Time](https://esphome.io/components/time/)

Then you can define an instance of the component like so (note that this is a complete example; most parameters are optional):

```yaml
transit_tracker:
  id: tracker

  # Base URL of the Transit Tracker API
  base_url: "wss://tt.horner.tj/"

  # The feed code of the transit agency you want to track (optional)
  feed_code: "st"

  # Maximum number of arrivals to show
  limit: 3

  # Whether to display arrival or departure times
  time_display: departure # or "arrival"

  # How to list trips:
  #   sequential: All trips across all routes in order of arrival/departure
  #   nextPerRoute: Each route's next trip in order of arrival/departure
  list_mode: sequential

  # Default color for route names
  # (See https://esphome.io/components/display/#color)
  default_route_color: my_favorite_color

  # How to display the duration units.
  # Examples:
  #   long  = "5min" / "1h15m"
  #   short = "5m"   / "1h15m"
  #   none  = "5"    / "1:15"
  show_units: long

  # List of stop and route IDs to track
  stops:
    - stop_id: "1_71971"
      # If it takes you a known amount of time to walk to the
      # stop, you can set a time offset here so that the time
      # displayed on the board is more like a "leave by" time
      time_offset: -8min # e.g., if it takes you 8 minutes to walk to the stop
      routes:
        - "1_100113"
        - "1_102704"
    - stop_id: "1_71961"
      time_offset: -10min
      routes:
        - "1_102548"

  # List of custom styles for route names and colors
  styles:
    - route_id: "1_102548"
      name: "B"
      # See https://esphome.io/components/display/#color
      color: rapidride_red

  # List of custom abbreviations for headsigns
  abbreviations:
    - from: "Bellevue Transit Center Crossroads"
      to: "Bellevue TC"
    - from: "Transit Center"
      to: "TC"
```

Then, finally, in your display's draw lambda:

```yaml
display:
  - platform: # ...
    id: # ...
    lambda: |-
      id(tracker).draw_schedule();
```

## License

```
MIT License

Copyright (c) 2025 TJ Horner

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```