import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import (
    add_idf_sdkconfig_option,
    include_builtin_idf_component,
)
from esphome import automation
from esphome.components import light
from esphome.const import (
    CONF_CHANNEL,
    CONF_ID,
    CONF_LIGHT_ID,
    CONF_TIMEOUT,
    CONF_TRIGGER_ID,
)

CODEOWNERS = ["@project-maintainers"]
CONF_PMK = "pmk"
CONF_PEERS = "peers"
CONF_ADDRESS = "address"
CONF_LMK = "lmk"
CONF_ACK_TIMEOUT = "ack_timeout"
CONF_MAX_ATTEMPTS = "max_attempts"
CONF_INBOUND = "inbound"
CONF_DEVICE_ID = "device_id"
CONF_BINDINGS = "bindings"
CONF_RESOURCE = "resource"
CONF_COMMAND = "command"
CONF_COMPLETION = "completion"
CONF_EXPECTED = "expected"

espnow_net_protocol_ns = cg.esphome_ns.namespace("espnow_net_protocol")
EspNowNetProtocolComponent = espnow_net_protocol_ns.class_(
    "EspNowNetProtocolComponent", cg.Component
)
DeclarativeCommandBinding = espnow_net_protocol_ns.class_(
    "DeclarativeCommandBinding", automation.Trigger.template()
)
LightCommandCompletionProbe = espnow_net_protocol_ns.class_(
    "LightCommandCompletionProbe"
)
LightExpectedState = espnow_net_protocol_ns.enum(
    "LightExpectedState", is_class=True
)

LIGHT_EXPECTED_STATES = {
    "on": LightExpectedState.ON,
    "off": LightExpectedState.OFF,
    "toggled": LightExpectedState.TOGGLED,
}


def _bounded_text(maximum, label):
    def validator(value):
        value = cv.string_strict(value)
        if not value or len(value) > maximum:
            raise cv.Invalid(f"{label} must contain 1 to {maximum} characters")
        return value

    return validator

def _key(value):
    value = cv.string_strict(value)
    if len(value) != 32 or any(c not in "0123456789abcdefABCDEF" for c in value):
        raise cv.Invalid("ESP-NOW PMK/LMK must contain exactly 32 hex digits")
    return value.upper()

def _address(value):
    value = cv.string_strict(value).upper()
    parts = value.split(":")
    if len(parts) != 6 or any(len(p) != 2 or any(c not in "0123456789ABCDEF" for c in p) for p in parts):
        raise cv.Invalid("ESP-NOW address must use AA:BB:CC:DD:EE:FF format")
    if value == "FF:FF:FF:FF:FF:FF":
        raise cv.Invalid("ESP-NOW peers must use unicast addresses")
    return value

PEER_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): cv.string_strict,
    cv.Required(CONF_ADDRESS): _address,
    cv.Required(CONF_LMK): _key,
})

LIGHT_COMPLETION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LightCommandCompletionProbe),
        cv.Required(CONF_LIGHT_ID): cv.use_id(light.LightState),
        cv.Optional(CONF_EXPECTED, default="toggled"): cv.enum(
            LIGHT_EXPECTED_STATES, lower=True
        ),
        cv.Optional(CONF_TIMEOUT, default="2s"):
            cv.positive_time_period_milliseconds,
    }
)

BINDING_SCHEMA = automation.validate_automation(
    {
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
            DeclarativeCommandBinding
        ),
        cv.Required(CONF_RESOURCE): _bounded_text(63, "resource"),
        cv.Required(CONF_COMMAND): _bounded_text(47, "command"),
        cv.Optional(CONF_COMPLETION): LIGHT_COMPLETION_SCHEMA,
    },
    single=True,
)

INBOUND_SCHEMA = cv.Schema({
    cv.Required(CONF_DEVICE_ID): _bounded_text(63, "device_id"),
    cv.Required(CONF_BINDINGS): cv.All(
        cv.ensure_list(BINDING_SCHEMA), cv.Length(min=1, max=16)
    ),
})

def _validate(config):
    ids = set()
    addresses = set()
    for peer in config[CONF_PEERS]:
        if not peer[CONF_ID] or len(peer[CONF_ID]) > 63:
            raise cv.Invalid("ESP-NOW peer id must contain 1 to 63 characters")
        if peer[CONF_ID] in ids:
            raise cv.Invalid(f"duplicate ESP-NOW peer id: {peer[CONF_ID]}")
        if peer[CONF_ADDRESS] in addresses:
            raise cv.Invalid(f"duplicate ESP-NOW peer address: {peer[CONF_ADDRESS]}")
        ids.add(peer[CONF_ID])
        addresses.add(peer[CONF_ADDRESS])
    if CONF_INBOUND in config:
        routes = set()
        for binding in config[CONF_INBOUND][CONF_BINDINGS]:
            route = (binding[CONF_RESOURCE], binding[CONF_COMMAND])
            if route in routes:
                raise cv.Invalid(
                    "duplicate inbound resource/command binding: "
                    f"{route[0]}/{route[1]}"
                )
            routes.add(route)
    return config

CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(EspNowNetProtocolComponent),
        cv.Required(CONF_CHANNEL): cv.int_range(min=1, max=14),
        cv.Required(CONF_PMK): _key,
        cv.Required(CONF_PEERS): cv.All(
            cv.ensure_list(PEER_SCHEMA), cv.Length(min=1, max=16)
        ),
        cv.Optional(CONF_ACK_TIMEOUT, default="250ms"):
            cv.positive_time_period_milliseconds,
        cv.Optional(CONF_MAX_ATTEMPTS, default=3): cv.int_range(min=1, max=8),
        cv.Optional(CONF_INBOUND): INBOUND_SCHEMA,
    }).extend(cv.COMPONENT_SCHEMA),
    _validate,
)

async def to_code(config):
    cg.add_define("USE_ESPNOW_NET_PROTOCOL_RADIO")
    include_builtin_idf_component("esp_wifi")
    include_builtin_idf_component("esp_system")
    add_idf_sdkconfig_option(
        "CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM", len(config[CONF_PEERS])
    )
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_ack_timeout(config[CONF_ACK_TIMEOUT].total_milliseconds))
    cg.add(var.set_max_attempts(config[CONF_MAX_ATTEMPTS]))
    cg.add(var.configure(config[CONF_CHANNEL], config[CONF_PMK]))
    for peer in config[CONF_PEERS]:
        cg.add(var.add_peer(peer[CONF_ID], peer[CONF_ADDRESS], peer[CONF_LMK]))
    if CONF_INBOUND in config:
        cg.add_define("USE_ESPNOW_NET_PROTOCOL_DECLARATIVE_INBOUND")
        inbound = config[CONF_INBOUND]
        cg.add(var.configure_declarative_inbound(inbound[CONF_DEVICE_ID]))
        for binding_config in inbound[CONF_BINDINGS]:
            binding = cg.new_Pvariable(binding_config[CONF_TRIGGER_ID])
            cg.add(binding.set_resource(binding_config[CONF_RESOURCE]))
            cg.add(binding.set_command(binding_config[CONF_COMMAND]))
            if CONF_COMPLETION in binding_config:
                cg.add_define(
                    "USE_ESPNOW_NET_PROTOCOL_LIGHT_COMPLETION"
                )
                completion_config = binding_config[CONF_COMPLETION]
                completion = cg.new_Pvariable(completion_config[CONF_ID])
                state = await cg.get_variable(
                    completion_config[CONF_LIGHT_ID]
                )
                cg.add(completion.set_light(state))
                cg.add(
                    completion.set_expected(
                        completion_config[CONF_EXPECTED]
                    )
                )
                cg.add(binding.set_completion_probe(completion))
                cg.add(
                    binding.set_completion_timeout(
                        completion_config[CONF_TIMEOUT].total_milliseconds
                    )
                )
            cg.add(var.add_declarative_binding(binding))
            await automation.build_automation(binding, [], binding_config)


# Import for native automation action registration side effects.
from . import actions  # noqa: E402, F401
