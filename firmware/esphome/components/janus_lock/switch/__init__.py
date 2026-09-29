import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from .. import janus_lock_ns, JanusLock, CONF_JANUS_LOCK_ID

DEPENDENCIES = ["janus_lock"]

JanusSettingSwitch = janus_lock_ns.class_("JanusSettingSwitch", switch.Switch, cg.Component)

CONF_SETTING = "setting"

# setting name -> (opcode, status flag bit; -1 = no status feedback)
SETTINGS = {
    "passage": ("0901", 0),        # normal_lock / free-handle
    "lock_sound": ("0902", 1),
    "auto_lock": ("0903", 2),
    "break_in_alarm": ("0904", 3),
    "unlatch": ("0906", 4),
    "button_enabled": ("0907", 5),
    "lock_direction": ("0905", -1),  # left-handed; no status bit
}

CONFIG_SCHEMA = (
    switch.switch_schema(JanusSettingSwitch)
    .extend(
        {
            cv.GenerateID(CONF_JANUS_LOCK_ID): cv.use_id(JanusLock),
            cv.Required(CONF_SETTING): cv.one_of(*SETTINGS.keys(), lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_JANUS_LOCK_ID])
    cg.add(var.set_parent(parent))
    opcode, bit = SETTINGS[config[CONF_SETTING]]
    cg.add(var.set_opcode(opcode))
    cg.add(var.set_flag_bit(bit))
    cg.add(parent.add_setting_switch(var))
