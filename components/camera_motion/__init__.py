"""Motion detection from the OV02C10 camera built into the 7-inch JC1060P470."""

from __future__ import annotations

from esphome import automation
import esphome.codegen as cg
from esphome.components import binary_sensor, esp32, i2c, sensor, text_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_BRIGHTNESS,
    CONF_ID,
    CONF_STATUS,
    DEVICE_CLASS_MOTION,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
)

CODEOWNERS = ["@srnewkirk"]
DEPENDENCIES = ["i2c"]
AUTO_LOAD = ["binary_sensor", "sensor", "text_sensor"]

CONF_SENSOR_MODE = "sensor_mode"
CONF_LINE_SYNC = "line_sync"
CONF_FRAME_RATE_DIVIDER = "frame_rate_divider"
CONF_MOTION = "motion"
CONF_MOTION_LEVEL = "motion_level"
CONF_ON_MOTION = "on_motion"

camera_motion_ns = cg.esphome_ns.namespace("camera_motion")
CameraMotionComponent = camera_motion_ns.class_(
    "CameraMotionComponent", cg.Component, i2c.I2CDevice
)
SensorMode = camera_motion_ns.enum("SensorMode", is_class=True)
SENSOR_MODES = {
    "1920x1080_2lane": SensorMode.MIPI_2LANE_1920X1080,
    "1288x728_1lane": SensorMode.MIPI_1LANE_1288X728,
}

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CameraMotionComponent),
            cv.Optional(CONF_SENSOR_MODE, default="1920x1080_2lane"): cv.enum(
                SENSOR_MODES, lower=True
            ),
            cv.Optional(CONF_LINE_SYNC, default=True): cv.boolean,
            cv.Optional(CONF_FRAME_RATE_DIVIDER, default=4): cv.int_range(
                min=1, max=20
            ),
            cv.Optional(CONF_MOTION): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_MOTION,
            ),
            cv.Optional(CONF_MOTION_LEVEL): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                icon="mdi:motion-sensor",
                accuracy_decimals=1,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_BRIGHTNESS): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                icon="mdi:brightness-6",
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(
                icon="mdi:cctv",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_ON_MOTION): automation.validate_automation({}),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x36)),
    esp32.only_on_variant(supported=[esp32.VARIANT_ESP32P4]),
)

_CALLBACK_AUTOMATIONS = (
    automation.CallbackAutomation(CONF_ON_MOTION, "add_on_motion_callback"),
)


async def to_code(config: dict) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    # ESPHome leaves the ESP-IDF camera controller driver out unless asked.
    esp32.include_builtin_idf_component("esp_driver_cam")
    esp32.include_builtin_idf_component("esp_driver_isp")

    cg.add(var.set_sensor_mode(config[CONF_SENSOR_MODE]))
    cg.add(var.set_line_sync(config[CONF_LINE_SYNC]))
    cg.add(var.set_frame_rate_divider(config[CONF_FRAME_RATE_DIVIDER]))

    if motion_config := config.get(CONF_MOTION):
        cg.add(var.set_motion_binary_sensor(await binary_sensor.new_binary_sensor(motion_config)))
    if level_config := config.get(CONF_MOTION_LEVEL):
        cg.add(var.set_motion_level_sensor(await sensor.new_sensor(level_config)))
    if brightness_config := config.get(CONF_BRIGHTNESS):
        cg.add(var.set_brightness_sensor(await sensor.new_sensor(brightness_config)))
    if status_config := config.get(CONF_STATUS):
        cg.add(var.set_status_text_sensor(await text_sensor.new_text_sensor(status_config)))

    await automation.build_callback_automations(var, config, _CALLBACK_AUTOMATIONS)
