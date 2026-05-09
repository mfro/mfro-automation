import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from . import BleUnlockComponent, ble_unlock_ns

DEPENDENCIES = ["ble_unlock"]

CONF_BLE_UNLOCK_ID = "ble_unlock_id"

BleUnlockSwitch = ble_unlock_ns.class_("BleUnlockSwitch", switch.Switch)

CONFIG_SCHEMA = switch.switch_schema(BleUnlockSwitch).extend(
    {
        cv.GenerateID(CONF_BLE_UNLOCK_ID): cv.use_id(BleUnlockComponent),
    }
)

async def to_code(config):
    var = await switch.new_switch(config)

    hub = await cg.get_variable(config[CONF_BLE_UNLOCK_ID])
    cg.add(hub.set_switch(var))
