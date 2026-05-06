import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_UID
from esphome.core import HexInt

from . import HomeKeyComponent, homekit_ns

DEPENDENCIES = ["homekey"]

CONF_HOMEKEY_ID = "homekey_id"

HomeKeyBinarySensor = homekit_ns.class_("HomeKeyBinarySensor", binary_sensor.BinarySensor)

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(HomeKeyBinarySensor).extend(
    {
        cv.GenerateID(CONF_HOMEKEY_ID): cv.use_id(HomeKeyComponent),
    }
)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)

    hub = await cg.get_variable(config[CONF_HOMEKEY_ID])
    cg.add(hub.add_binary_sensor(var))
