import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, sensor, binary_sensor
from esphome.const import (
    CONF_ID,
    CONF_BATTERY_LEVEL,
    UNIT_PERCENT,
    DEVICE_CLASS_BATTERY,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

CODEOWNERS = ["@alexmbino"]
DEPENDENCIES = ["ble_client"]
AUTO_LOAD = ["sensor", "binary_sensor"]
MULTI_CONF = True

janus_lock_ns = cg.esphome_ns.namespace("janus_lock")
JanusLock = janus_lock_ns.class_(
    "JanusLock", cg.PollingComponent, ble_client.BLEClientNode
)

CONF_MASTER_TOKEN = "master_token"
CONF_JANUS_LOCK_ID = "janus_lock_id"
CONF_NORMAL_LOCK = "normal_lock"
CONF_AUTO_LOCK = "auto_lock"
CONF_LOCK_SOUND = "lock_sound"
CONF_CALIBRATED = "calibrated"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(JanusLock),
            cv.Required(CONF_MASTER_TOKEN): cv.string,
            cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                device_class=DEVICE_CLASS_BATTERY,
                accuracy_decimals=0,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_NORMAL_LOCK): binary_sensor.binary_sensor_schema(),
            cv.Optional(CONF_AUTO_LOCK): binary_sensor.binary_sensor_schema(),
            cv.Optional(CONF_LOCK_SOUND): binary_sensor.binary_sensor_schema(),
            cv.Optional(CONF_CALIBRATED): binary_sensor.binary_sensor_schema(),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(ble_client.BLE_CLIENT_SCHEMA)
)

_BS = [
    (CONF_NORMAL_LOCK, "set_bs_normal_lock"),
    (CONF_AUTO_LOCK, "set_bs_auto_lock"),
    (CONF_LOCK_SOUND, "set_bs_lock_sound"),
    (CONF_CALIBRATED, "set_bs_calibrated"),
]


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    cg.add(var.set_master_token(config[CONF_MASTER_TOKEN]))
    if CONF_BATTERY_LEVEL in config:
        s = await sensor.new_sensor(config[CONF_BATTERY_LEVEL])
        cg.add(var.set_battery_sensor(s))
    for key, setter in _BS:
        if key in config:
            bs = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(var, setter)(bs))
