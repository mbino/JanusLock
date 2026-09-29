import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from .. import janus_lock_ns, JanusLock, CONF_JANUS_LOCK_ID

DEPENDENCIES = ["janus_lock"]

JanusButton = janus_lock_ns.class_("JanusButton", button.Button, cg.Component)

CONF_ACTION = "action"
ACTIONS = ["unlock", "lock"]

CONFIG_SCHEMA = (
    button.button_schema(JanusButton)
    .extend(
        {
            cv.GenerateID(CONF_JANUS_LOCK_ID): cv.use_id(JanusLock),
            cv.Optional(CONF_ACTION, default="unlock"): cv.one_of(*ACTIONS, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_JANUS_LOCK_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_action(config[CONF_ACTION]))
