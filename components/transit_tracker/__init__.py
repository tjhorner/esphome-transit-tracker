import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.http_request import CONF_HTTP_REQUEST_ID, HttpRequestComponent
from esphome.components.display import Display
from esphome.components.font import Font
from esphome.components.time import RealTimeClock
from esphome.components import color
from esphome.const import CONF_ID, CONF_DISPLAY_ID, CONF_TIME_ID

DEPENDENCIES = ["network"]
AUTO_LOAD = ["json", "watchdog"]

transit_tracker_ns = cg.esphome_ns.namespace("transit_tracker")
TransitTracker = transit_tracker_ns.class_("TransitTracker", cg.PollingComponent)

CONF_ROUTES = "routes"
CONF_STOPS = "stops"
CONF_BASE_URL = "base_url"
CONF_FONT_ID = "font_id"
CONF_LIMIT = "limit"
CONF_ABBREVIATIONS = "abbreviations"
CONF_STYLES = "styles"
CONF_FEED_CODE = "feed_code"
CONF_DEFAULT_ROUTE_COLOR = "default_route_color"
CONF_TIME_DISPLAY = "time_display"
CONF_LIST_MODE = "list_mode"


def validate_ws_url(value):
    url = cv.url(value)
    if not value.startswith("ws://") and not value.startswith("wss://"):
        raise cv.Invalid("URL must start with 'ws://' or 'wss://")

    return url


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(TransitTracker),
        cv.GenerateID(CONF_DISPLAY_ID): cv.use_id(Display),
        cv.GenerateID(CONF_FONT_ID): cv.use_id(Font),
        cv.GenerateID(CONF_TIME_ID): cv.use_id(RealTimeClock),
        cv.Optional(CONF_BASE_URL): validate_ws_url,
        cv.Optional(CONF_LIMIT, default=3): cv.positive_int,
        cv.Optional(CONF_FEED_CODE, default=""): cv.string,
        cv.Optional(CONF_TIME_DISPLAY, default="departure"): cv.one_of(
            "departure", "arrival"
        ),
        cv.Optional(CONF_LIST_MODE, default="sequential"): cv.one_of(
            "sequential", "nextPerRoute"
        ),
        cv.Optional(CONF_STOPS, default=[]): cv.ensure_list(
            cv.Schema(
                {
                    cv.Required("stop_id"): cv.string,
                    cv.Optional("time_offset", default="0s"): cv.time_period,
                    cv.Required(CONF_ROUTES): cv.ensure_list(cv.string),
                }
            )
        ),
        cv.Optional(CONF_DEFAULT_ROUTE_COLOR): cv.use_id(color.ColorStruct),
        cv.Optional(CONF_STYLES): cv.ensure_list(
            cv.Schema(
                {
                    cv.Required("route_id"): cv.string,
                    cv.Required("name"): cv.string,
                    cv.Required("color"): cv.use_id(color.ColorStruct),
                }
            )
        ),
        cv.Optional(CONF_ABBREVIATIONS): cv.ensure_list(
            cv.Schema(
                {
                    cv.Required("from"): cv.string,
                    cv.Required("to"): cv.string,
                }
            )
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


def _generate_schedule_string(stops):
    return ";".join(
        [
            f"{route},{stop['stop_id']},{stop['time_offset'].total_seconds}"
            for stop in stops
            for route in stop[CONF_ROUTES]
        ]
    )


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    drawing_display = await cg.get_variable(config[CONF_DISPLAY_ID])
    cg.add(var.set_display(drawing_display))

    font = await cg.get_variable(config[CONF_FONT_ID])
    cg.add(var.set_font(font))

    time = await cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_rtc(time))

    if CONF_BASE_URL in config:
        cg.add(var.set_base_url(config[CONF_BASE_URL]))

    cg.add(var.set_feed_code(config[CONF_FEED_CODE]))
    cg.add(var.set_schedule_string(_generate_schedule_string(config[CONF_STOPS])))

    display_departure_times = config[CONF_TIME_DISPLAY] == "departure"
    cg.add(var.set_display_departure_times(display_departure_times))

    cg.add(var.set_list_mode(config[CONF_LIST_MODE]))

    cg.add(var.set_limit(config[CONF_LIMIT]))

    if CONF_ABBREVIATIONS in config:
        for abbreviation in config[CONF_ABBREVIATIONS]:
            cg.add(var.add_abbreviation(abbreviation["from"], abbreviation["to"]))

    if CONF_DEFAULT_ROUTE_COLOR in config:
        cg.add(
            var.set_default_route_color(
                await cg.get_variable(config[CONF_DEFAULT_ROUTE_COLOR])
            )
        )

    if CONF_STYLES in config:
        for style in config[CONF_STYLES]:
            color_struct = await cg.get_variable(style["color"])
            cg.add(var.add_route_style(style["route_id"], style["name"], color_struct))

    await cg.register_component(var, config)

    cg.add_library("WiFiClientSecure", None)
    cg.add_library("HTTPClient", None)

    # Fork contains patch for TLS issue - https://github.com/gilmaimon/ArduinoWebsockets/pull/142
    cg.add_library(
        "ArduinoWebsockets", None, "https://github.com/tjhorner/ArduinoWebsockets"
    )
