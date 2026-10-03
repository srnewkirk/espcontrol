"""Opt-in Stage 2 capture experiment; no motion or ambient-light entities."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, esp32, i2c
from esphome.const import CONF_ID, CONF_I2C_ID

DEPENDENCIES = ["esp32", "i2c", "psram"]
AUTO_LOAD = ["button"]
MULTI_CONF = False
ns = cg.esphome_ns.namespace("jc4880_camera_capture")
Capture = ns.class_("Capture", cg.Component)
PreviewButton = ns.class_("PreviewButton", button.Button)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Capture),
    cv.Required(CONF_I2C_ID): cv.use_id(i2c.InternalI2CBus),
    cv.Optional("preview_button"): button.button_schema(PreviewButton),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    if esp32.get_esp32_variant() != "ESP32P4":
        raise cv.Invalid("JC4880 capture requires ESP32-P4")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    bus = await cg.get_variable(config[CONF_I2C_ID])
    cg.add(var.set_bus(bus))
    if "preview_button" in config:
        await button.new_button(config["preview_button"], var)
    cg.add_define("USE_JC4880_CAMERA_CAPTURE")
    for component in ("esp_driver_cam", "esp_driver_isp"):
        esp32.include_builtin_idf_component(component)
