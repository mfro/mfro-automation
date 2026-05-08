import base64
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import PLATFORM_ESP32, CONF_ID
from esphome.components.esp32 import add_idf_sdkconfig_option, add_idf_component

DEPENDENCIES = ["esp32", "network", "mdns"]
CODEOWNERS = ["@mfro"]
MULTI_CONF = True

ble_unlock_ns = cg.esphome_ns.namespace("ble_unlock")
BleUnlockComponent = ble_unlock_ns.class_("BleUnlockComponent", cg.Component)

CONFIG_SCHEMA = cv.All(cv.Schema({
    cv.GenerateID(): cv.declare_id(BleUnlockComponent),
    cv.Required("irk"): cv.ensure_list(
        cv.All(cv.string_strict, cv.Length(min=24, max=24))
    ),
}).extend(cv.COMPONENT_SCHEMA),
cv.only_on([PLATFORM_ESP32]),
cv.only_with_esp_idf)

async def to_code(config):
    add_idf_sdkconfig_option("CONFIG_BT_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_BLUEDROID_ENABLED", False)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_EXT_ADV", True)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT", True)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_CHANNEL_SOUNDING", True)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    for entry in config["irk"]:
        cg.add(var.add_irk(list(base64.b64decode(entry)[::-1])))
