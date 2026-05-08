#pragma once
#include <unordered_map>
#include <vector>
#include <tuple>
#include <algorithm>
#include <map>

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/application.h"
#include "esphome/core/base_automation.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

#include "nvs_flash.h"
#include "nimble/ble.h"
#include "host/ble_gatt.h"
#include "psa/crypto_types.h"

namespace esphome
{
    namespace ble_unlock
    {
        struct FoundDevice
        {
            int8_t rssi;
            ble_addr_t address;
        };

        class BleUnlockBinarySensor : public binary_sensor::BinarySensor
        {
        };

        class BleUnlockComponent : public Component
        {
        public:
            std::vector<ble_gatt_svc> gatt_services;
            std::vector<ble_gatt_chr> gatt_characteristics;
            std::unordered_map<size_t, FoundDevice> found_devices;

            std::vector<psa_key_id_t> irks;
            std::vector<BleUnlockBinarySensor *> binary_sensors;

            BleUnlockComponent();

            float get_setup_priority() const override { return setup_priority::LATE; }
            void setup() override;
            void loop() override;
            void dump_config() override;

            void add_binary_sensor(BleUnlockBinarySensor *binary_sensor)
            {
                binary_sensor->publish_initial_state(false);
                binary_sensors.push_back(binary_sensor);
            }

            void add_irk(std::vector<uint8_t> irk);
        };

    } // namespace ble_unlock
} // namespace esphome
