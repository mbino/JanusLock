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
AUTO_LOAD = ["sensor", "binary_sensor", "switch", "lock"]
MULTI_CONF = True

janus_lock_ns = cg.esphome_ns.namespace("janus_lock")
JanusLock = janus_lock_ns.class_(
    "JanusLock", cg.PollingComponent, ble_client.BLEClientNode
)

CONF_MASTER_TOKEN = "master_token"
CONF_JANUS_LOCK_ID = "janus_lock_id"
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
            cv.Optional(CONF_CALIBRATED): binary_sensor.binary_sensor_schema(),
        }
    )
    .extend(cv.polling_component_schema("12h"))
    .extend(ble_client.BLE_CLIENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    cg.add(var.set_master_token(config[CONF_MASTER_TOKEN]))
    if CONF_BATTERY_LEVEL in config:
        s = await sensor.new_sensor(config[CONF_BATTERY_LEVEL])
        cg.add(var.set_battery_sensor(s))
    if CONF_CALIBRATED in config:
        bs = await binary_sensor.new_binary_sensor(config[CONF_CALIBRATED])
        cg.add(var.set_bs_calibrated(bs))
