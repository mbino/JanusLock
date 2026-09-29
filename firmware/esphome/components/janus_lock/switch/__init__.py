import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from .. import janus_lock_ns, JanusLock, CONF_JANUS_LOCK_ID

DEPENDENCIES = ["janus_lock"]

JanusPassageSwitch = janus_lock_ns.class_(
    "JanusPassageSwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = switch.switch_schema(JanusPassageSwitch).extend(
    {
        cv.GenerateID(CONF_JANUS_LOCK_ID): cv.use_id(JanusLock),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_JANUS_LOCK_ID])
    cg.add(var.set_parent(parent))
    cg.add(parent.set_passage_switch(var))
