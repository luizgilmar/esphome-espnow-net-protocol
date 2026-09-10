import esphome.codegen as cg
import esphome.config_validation as cv

from esphome import automation
from esphome.const import CONF_ID

from . import EspNowNetProtocolComponent, espnow_net_protocol_ns

CONF_PEER = "peer"
CONF_DEVICE = "device"
CONF_RESOURCE = "resource"
CONF_COMMAND = "command"
CONF_PAYLOAD = "payload"
CONF_TIMEOUT = "timeout"

EspNowSendCommandAction = espnow_net_protocol_ns.class_(
    "EspNowSendCommandAction", automation.Action
)

SEND_COMMAND_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(EspNowNetProtocolComponent),
        cv.Required(CONF_PEER): cv.string_strict,
        cv.Required(CONF_DEVICE): cv.string_strict,
        cv.Required(CONF_RESOURCE): cv.string_strict,
        cv.Required(CONF_COMMAND): cv.string_strict,
        cv.Optional(CONF_PAYLOAD, default=""): cv.string_strict,
        cv.Optional(CONF_TIMEOUT, default="5s"):
            cv.positive_time_period_milliseconds,
    }
)


@automation.register_action(
    "espnow_net_protocol.send_command",
    EspNowSendCommandAction,
    SEND_COMMAND_SCHEMA,
    synchronous=True,
)
async def send_command_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    cg.add(var.set_peer(config[CONF_PEER]))
    cg.add(var.set_device(config[CONF_DEVICE]))
    cg.add(var.set_resource(config[CONF_RESOURCE]))
    cg.add(var.set_command(config[CONF_COMMAND]))
    cg.add(var.set_payload(config[CONF_PAYLOAD]))
    cg.add(var.set_timeout(config[CONF_TIMEOUT].total_milliseconds))
    return var
