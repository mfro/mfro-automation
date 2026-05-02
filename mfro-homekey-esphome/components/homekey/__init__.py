import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_PORT, PLATFORM_ESP32, CONF_ID
from esphome.components import pn532
from esphome.components.esp32 import add_idf_component, add_idf_sdkconfig_option
import re

DEPENDENCIES = ['esp32', 'network', 'mdns']
CODEOWNERS = ["@mfro"]
MULTI_CONF = True

homekit_ns = cg.esphome_ns.namespace('homekit')
HAPRootComponent = homekit_ns.class_('HAPRootComponent', cg.Component)
OnAuthTrigger = homekit_ns.class_(
    "AuthTrigger", automation.Trigger.template()
)

def hk_setup_code(value):
    """Validate that a given config value is a valid icon."""
    value = cv.string_strict(value)
    if not value:
        return value
    if re.match("^[\\d]{3}-[\\d]{2}-[\\d]{3}$", value):
        return value
    raise cv.Invalid(
        'Setup code must match the format XXX-XX-XXX'
    )

CONFIG_SCHEMA = cv.All(cv.Schema({
    cv.GenerateID(): cv.declare_id(HAPRootComponent),
    cv.Optional(CONF_PORT, default=32042): cv.port,
    cv.Optional("setup_code"): hk_setup_code,
    cv.Optional("setup_id"): cv.All(cv.string_strict,cv.Upper,cv.Length(min=4, max=4, msg="Setup ID has to be a 4 character long alpha numeric string (with capital letters)")),
    cv.Optional("nfc_id"): cv.use_id(pn532.PN532),
    cv.Optional("on_auth"): automation.validate_automation(),
}).extend(cv.COMPONENT_SCHEMA),
cv.only_on([PLATFORM_ESP32]),
cv.only_with_esp_idf)

async def to_code(config):
    add_idf_component(
        name="esp_hap_core",
        repo="https://github.com/rednblkx/esp-homekit-sdk",
        ref="esphome",
        path="components/homekit/esp_hap_core"
    )
    add_idf_component(
        name="esp_hap_apple_profiles",
        repo="https://github.com/rednblkx/esp-homekit-sdk",
        ref="esphome",
        path="components/homekit/esp_hap_apple_profiles"
    )
    add_idf_component(
        name="esp_hap_extras",
        repo="https://github.com/rednblkx/esp-homekit-sdk",
        ref="esphome",
        path="components/homekit/esp_hap_extras"
    )
    add_idf_component(
        name="esp_hap_platform",
        repo="https://github.com/rednblkx/esp-homekit-sdk",
        ref="esphome",
        path="components/homekit/esp_hap_platform"
    )
    add_idf_component(
        name="hkdf-sha",
        repo="https://github.com/rednblkx/esp-homekit-sdk",
        ref="esphome",
        path="components/homekit/hkdf-sha"
    )
    add_idf_component(
        name="mu_srp",
        repo="https://github.com/rednblkx/esp-homekit-sdk",
        ref="esphome",
        path="components/homekit/mu_srp"
    )
    add_idf_component(
        name="HK-HomeKit-Lib",
        repo="https://github.com/rednblkx/HK-HomeKit-Lib.git",
        ref="a4af730ec54536e1ba931413206fec89ce2b6c4f"
    )
    if CONF_PORT in config:
        add_idf_sdkconfig_option("CONFIG_HAP_HTTP_SERVER_PORT", config[CONF_PORT])
    add_idf_sdkconfig_option("CONFIG_MBEDTLS_HKDF_C", True)
    add_idf_sdkconfig_option("CONFIG_LWIP_MAX_SOCKETS", 16)

    var = cg.new_Pvariable(config[CONF_ID], config["setup_code"], config["setup_id"])

    nfc = await cg.get_variable(config["nfc_id"])
    cg.add(var.set_nfc_ctx(nfc))

    for trigger in config.get("on_auth", []):
        await automation.build_callback_automation(var, "add_auth_callback", [], trigger)

    await cg.register_component(var, config)
