#pragma once
#include <vector>
#include <tuple>
#include <algorithm>
#include <map>

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/application.h"
#include "esphome/core/base_automation.h"
#include "esphome/components/pn532/pn532.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

#include "nvs_flash.h"
#include "hap.h"
#include "hap_apple_servs.h"
#include "hap_apple_chars.h"

#include "HK_HomeKit.h"
#include "hkAuthContext.h"

namespace esphome
{
    namespace homekit
    {
        class HomeKeyBinarySensor : public binary_sensor::BinarySensor
        {
        };

        class HomeKeyComponent : public Component
        {
        public:
            nvs_handle homekit_nvs;
            readerData_t reader_data;

            const char *setup_code;
            const char *setup_id;

            pn532::PN532 *nfc_device;

            std::vector<uint8_t> nfc_control_point = std::vector<uint8_t>(1024);
            std::vector<uint8_t> nfc_access_supported_conf = {0x01, 0x01, 0x10, 0x02, 0x01, 0x10};

            CallbackManager<void()> auth_callback;
            std::vector<HomeKeyBinarySensor *> binary_sensors;

            HomeKeyComponent(const char *setup_code, const char *setup_id);

            float get_setup_priority() const override { return setup_priority::LATE; }
            void setup() override;
            void loop() override;
            void dump_config() override;

            template<typename F> void add_auth_callback(F &&callback) { this->auth_callback.add(std::forward<F>(callback)); }

            void add_binary_sensor(HomeKeyBinarySensor *binary_sensor) {
                binary_sensor->publish_initial_state(false);
                binary_sensors.push_back(binary_sensor);
            }

            void set_nfc_ctx(pn532::PN532 *nfc);
            void update_reader_data();
        };

    } // namespace homekit
} // namespace esphome
