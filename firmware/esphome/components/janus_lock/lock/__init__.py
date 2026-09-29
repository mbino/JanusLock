import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import lock
from .. import janus_lock_ns, JanusLock, CONF_JANUS_LOCK_ID

DEPENDENCIES = ["janus_lock"]

JanusLockLock = janus_lock_ns.class_("JanusLockLock", lock.Lock, cg.Component)

CONFIG_SCHEMA = lock.lock_schema(JanusLockLock).extend(
    {
        cv.GenerateID(CONF_JANUS_LOCK_ID): cv.use_id(JanusLock),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await lock.new_lock(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_JANUS_LOCK_ID])
    cg.add(var.set_parent(parent))
    cg.add(parent.set_lock(var))
