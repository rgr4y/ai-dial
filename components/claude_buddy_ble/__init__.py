"""UNCONFIGURED (kept for later). BLE transport for the Claude desktop Hardware Buddy: claude-buddy.yaml now uses
the Wi-Fi bridge (components/claude_buddy). Renamed to claude_buddy_ble so both can coexist.
"""
"""Claude desktop Hardware Buddy: encrypted Nordic UART over ESPHome's Bluedroid stack.

Protocol: https://github.com/anthropics/claude-desktop-buddy/blob/main/REFERENCE.md
Needs `esp32_ble_server:` in the config (that is what turns on Bluedroid GATTS and the GATTS
event dispatch). This component registers its own GATTS app so its characteristics can be
encrypted-only, which esp32_ble_server can't express.
"""
from esphome import automation
import esphome.codegen as cg
from esphome.components import binary_sensor, esp32_ble, sensor, text_sensor
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import CONF_ID, CONF_TRIGGER_ID

AUTO_LOAD = ["json"]
DEPENDENCIES = ["esp32_ble_server"]

CONF_NAME_PREFIX = "name_prefix"
CONF_BATTERY_LEVEL = "battery_level"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_CHARGING = "charging"
CONF_ON_UPDATE = "on_update"
CONF_DECISION_SENSOR = "decision_sensor"

ns = cg.esphome_ns.namespace("claude_buddy_ble")
ClaudeBuddy = ns.class_("ClaudeBuddy", cg.Component)
UpdateTrigger = ns.class_("UpdateTrigger", automation.Trigger.template())

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ClaudeBuddy),
        cv.GenerateID(esp32_ble.CONF_BLE_ID): cv.use_id(esp32_ble.ESP32BLE),
        cv.Optional(CONF_NAME_PREFIX, default="Claude"): cv.All(cv.string, cv.Length(max=14)),
        cv.Optional(CONF_BATTERY_LEVEL): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_BATTERY_VOLTAGE): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_CHARGING): cv.use_id(binary_sensor.BinarySensor),
        # Bridge (API) prompts are answered by publishing "<id> once|deny" here.
        cv.Optional(CONF_DECISION_SENSOR): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_ON_UPDATE): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(UpdateTrigger)}
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[esp32_ble.CONF_BLE_ID])
    cg.add(var.set_parent(parent))
    esp32_ble.register_gap_event_handler(parent, var)
    esp32_ble.register_gatts_event_handler(parent, var)
    esp32_ble.register_bt_logger(esp32_ble.BTLoggers.GATT, esp32_ble.BTLoggers.SMP)
    cg.add(var.set_name_prefix(config[CONF_NAME_PREFIX]))
    for key, setter in (
        (CONF_BATTERY_LEVEL, "set_battery_level"),
        (CONF_BATTERY_VOLTAGE, "set_battery_voltage"),
        (CONF_CHARGING, "set_charging"),
        (CONF_DECISION_SENSOR, "set_decision_sensor"),
    ):
        if key in config:
            cg.add(getattr(var, setter)(await cg.get_variable(config[key])))
    for conf in config.get(CONF_ON_UPDATE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)


def _lvgl_defines(config):
    # buddy/ui.h draws the name and status on arcs (lv_arclabel) and pets as RGB565A8 images. ESPHome's
    # lv_conf.h turns both off unless something asks; asking here, before lvgl writes lv_conf.h.
    if "lvgl" in fv.full_config.get():
        from esphome.components.lvgl import defines as df

        df.add_define("LV_USE_ARCLABEL")
        df.add_define("LV_USE_IMAGE")
        df.add_define("LV_DRAW_SW_SUPPORT_A8")  # arclabel rotates A8 glyph bitmaps
        df.add_define("LV_DRAW_SW_SUPPORT_RGB565A8")
    return config


FINAL_VALIDATE_SCHEMA = _lvgl_defines
