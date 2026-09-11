import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import switch
from esphome.const import CONF_ID

from .. import EspNowNetProtocolComponent, espnow_net_protocol_ns

DEPENDENCIES = ["espnow_net_protocol"]

CONF_ESPNOW_NET_PROTOCOL_ID = "espnow_net_protocol_id"

EspNowNetProtocolSwitch = espnow_net_protocol_ns.class_(
    "EspNowNetProtocolSwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = switch.switch_schema(EspNowNetProtocolSwitch).extend(
    {
        cv.Required(CONF_ESPNOW_NET_PROTOCOL_ID): cv.use_id(
            EspNowNetProtocolComponent
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await switch.register_switch(var, config)
    parent = await cg.get_variable(config[CONF_ESPNOW_NET_PROTOCOL_ID])
    cg.add(var.set_parent(parent))
