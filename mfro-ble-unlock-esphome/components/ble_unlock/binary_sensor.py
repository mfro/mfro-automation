import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_UID
from esphome.core import HexInt

from . import BleUnlockComponent, ble_unlock_ns

DEPENDENCIES = ["ble_unlock"]

CONF_BLE_UNLOCK_ID = "ble_unlock_id"

BleUnlockBinarySensor = ble_unlock_ns.class_("BleUnlockBinarySensor", binary_sensor.BinarySensor)

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(BleUnlockBinarySensor).extend(
    {
        cv.GenerateID(CONF_BLE_UNLOCK_ID): cv.use_id(BleUnlockComponent),
    }
)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)

    hub = await cg.get_variable(config[CONF_BLE_UNLOCK_ID])
    cg.add(hub.add_binary_sensor(var))
