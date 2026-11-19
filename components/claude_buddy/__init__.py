"""Claude Buddy state on the dial, fed over Wi-Fi by the dial bridge (bridge/dial-bridge.mjs).

The bridge calls the `buddy_line` API action with Hardware Buddy JSON lines; permission answers are published on
`decision_sensor`. Protocol: docs/DIAL-PROTOCOL.md.
"""
from esphome import automation
import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import CONF_ID, CONF_TRIGGER_ID

AUTO_LOAD = ["json"]

CONF_ON_UPDATE = "on_update"
CONF_DECISION_SENSOR = "decision_sensor"

ns = cg.esphome_ns.namespace("claude_buddy")
ClaudeBuddy = ns.class_("ClaudeBuddy", cg.Component)
UpdateTrigger = ns.class_("UpdateTrigger", automation.Trigger.template())

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ClaudeBuddy),
        # Permission answers are published here as "<prompt id> once|deny".
        cv.Optional(CONF_DECISION_SENSOR): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_ON_UPDATE): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(UpdateTrigger)}
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


def _gen_character_pack():
    # Every compile/run: rebuild bridge/assets/claude from docs/harness-pets. Unchanged art = same rev, no files
    # touched; changed art = new rev, and the dial pulls it from the bridge on its next heartbeat.
    import logging
    import shutil
    import subprocess

    from esphome.core import CORE

    log = logging.getLogger(__name__)
    script = CORE.relative_config_path("scripts", "gen_buddy_pets.mjs")
    node = shutil.which("node")
    if not node:
        log.warning("node not on PATH: character pack not regenerated (%s)", script)
        return
    r = subprocess.run([node, script], capture_output=True, text=True, check=False)
    for line in (r.stdout + r.stderr).strip().splitlines():
        (log.info if r.returncode == 0 else log.error)("gen_buddy_pets: %s", line)
    if r.returncode != 0:
        raise cv.Invalid(f"gen_buddy_pets.mjs failed (exit {r.returncode})")


async def to_code(config):
    _gen_character_pack()
    from esphome.core import CORE

    if CORE.is_esp32:
        # buddy/character.h: character packs on the `storage` LittleFS partition, pulled from the bridge over HTTP.
        # (Host sim reads bridge/assets directly.)
        from esphome.components.esp32 import (
            add_idf_component,
            add_idf_sdkconfig_option,
            request_http_client,
            require_vfs_dir,
        )

        add_idf_component(name="joltwallet/littlefs", ref="1.23.1")
        # LittleFS file/cache buffers in PSRAM: internal heap is tight once Wi-Fi, LVGL and the API are up.
        add_idf_sdkconfig_option("CONFIG_LITTLEFS_MALLOC_STRATEGY_DEFAULT", False)
        add_idf_sdkconfig_option("CONFIG_LITTLEFS_MALLOC_STRATEGY_SPIRAM_PREFER", True)
        request_http_client()
        require_vfs_dir()  # mkdir/stat/rename/unlink; ESPHome turns VFS dir support off by default (ENOENT)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if CONF_DECISION_SENSOR in config:
        cg.add(var.set_decision_sensor(await cg.get_variable(config[CONF_DECISION_SENSOR])))
    for conf in config.get(CONF_ON_UPDATE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)


def _lvgl_defines(config):
    # buddy/ui.h draws the name and status on arcs (lv_arclabel) and pets as RGB565A8 images. ESPHome's
    # lv_conf.h turns these off unless something asks; asking here, before lvgl writes lv_conf.h.
    if "lvgl" in fv.full_config.get():
        from esphome.components.lvgl import defines as df

        df.add_define("LV_USE_ARCLABEL")
        df.add_define("LV_USE_IMAGE")
        df.add_define("LV_DRAW_SW_SUPPORT_A8")  # arclabel rotates A8 glyph bitmaps
        df.add_define("LV_DRAW_SW_SUPPORT_RGB565A8")
    return config


FINAL_VALIDATE_SCHEMA = _lvgl_defines
