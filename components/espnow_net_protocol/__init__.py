import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import (
    add_idf_sdkconfig_option,
    include_builtin_idf_component,
)
from esphome.const import CONF_CHANNEL, CONF_ID

CODEOWNERS = ["@project-maintainers"]
CONF_PMK = "pmk"
CONF_PEERS = "peers"
CONF_ADDRESS = "address"
CONF_LMK = "lmk"
CONF_ACK_TIMEOUT = "ack_timeout"
CONF_MAX_ATTEMPTS = "max_attempts"

espnow_net_protocol_ns = cg.esphome_ns.namespace("espnow_net_protocol")
EspNowNetProtocolComponent = espnow_net_protocol_ns.class_(
    "EspNowNetProtocolComponent", cg.Component
)

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


# Import for native automation action registration side effects.
from . import actions  # noqa: E402, F401
