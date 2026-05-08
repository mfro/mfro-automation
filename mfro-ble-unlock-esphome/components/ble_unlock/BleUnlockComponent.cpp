#include "BleUnlockComponent.h"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esphome/core/log.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "psa/crypto.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_cs.h"
#include "host/util/util.h"

namespace esphome
{
    namespace ble_unlock
    {
        static constexpr const char *TAG = "BleUnlockComponent";

        static BleUnlockComponent *instance;

        BleUnlockComponent::BleUnlockComponent()
        {
            esp_log_level_set("esp-idf", ESP_LOG_WARN);
            esp_log_level_set("HK_HomeKit", ESP_LOG_DEBUG);
            esp_log_level_set("pn532", ESP_LOG_DEBUG);
            esp_log_level_set("HAP", ESP_LOG_DEBUG);
            esp_log_level_set(TAG, ESP_LOG_DEBUG);

            ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
            ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
            ESP_LOGI(TAG, "%s", esp_err_to_name(nvs_flash_init()));

            psa_status_t t = psa_crypto_init();
            if (t != PSA_SUCCESS)
                ESP_LOGE(TAG, "psa_crypto_init %d", t);

            instance = this;

            ESP_LOGI(TAG, "constructor complete");
        }

        static bool is_irk_match(psa_key_id_t irk, ble_addr_t *addr)
        {
            if (!BLE_ADDR_IS_RPA(addr))
                return false;

            psa_status_t t;

            uint8_t input[16] = {};
            input[13] = addr->val[5];
            input[14] = addr->val[4];
            input[15] = addr->val[3];

            size_t output_len = 0;
            uint8_t output[16] = {};
            t = psa_cipher_encrypt(irk, PSA_ALG_ECB_NO_PADDING, input, sizeof(input), output, sizeof(output), &output_len);
            if (t != PSA_SUCCESS)
                ESP_LOGE(TAG, "psa_cipher_encrypt %d", t);

            return output[13] == addr->val[2]     //
                   && output[14] == addr->val[1]  //
                   && output[15] == addr->val[0]; //
        }

        static void start_discovery(void);
        static int on_gap_event(ble_gap_event *event, void *arg)
        {
            int r;
            psa_status_t t;

            if (event->type == BLE_GAP_EVENT_EXT_DISC)
            {
                for (auto irk : instance->irks)
                {
                    if (is_irk_match(irk, &event->ext_disc.addr))
                    {
                        ble_hs_adv_fields fields;
                        r = ble_hs_adv_parse_fields(&fields, event->ext_disc.data, event->ext_disc.length_data);
                        if (r != 0)
                            ESP_LOGE(TAG, "ble_hs_util_ensure_addr %d", r);

                        ESP_LOGI(TAG, "match found: %02x:%02x:%02x:%02x:%02x:%02x", event->ext_disc.addr.val[5], event->ext_disc.addr.val[4], event->ext_disc.addr.val[3], event->ext_disc.addr.val[2], event->ext_disc.addr.val[1], event->ext_disc.addr.val[0]);
                        ESP_LOGI(TAG, "rssi: %d", event->ext_disc.rssi);
                        ESP_LOGI(TAG, "data: %d", event->ext_disc.length_data);
                        ESP_LOG_BUFFER_HEX(TAG, event->ext_disc.data, event->ext_disc.length_data);
                        ESP_LOGI(TAG, "manufacturer data: %d", fields.mfg_data_len);
                        ESP_LOG_BUFFER_HEX(TAG, fields.mfg_data, fields.mfg_data_len);
                    }
                }
            }
            else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE)
            {
                start_discovery();
            }
            else
            {
                ESP_LOGI(TAG, "gap event: %d", event->type);
            }

            return 0;
        }

        static void start_discovery(void)
        {
            uint8_t address_type;
            int r = ble_hs_id_infer_auto(0, &address_type);
            if (r != 0)
                ESP_LOGE(TAG, "ble_hs_id_infer_auto %d", r);

            ble_gap_disc_params discovery = {};
            discovery.filter_duplicates = 1;
            discovery.passive = 1;

            r = ble_gap_disc(address_type, 10000, &discovery, on_gap_event, NULL);
            ESP_LOGE(TAG, "ble_gap_disc %d", r);
        }

        static void on_reset(int reason)
        {
        }

        static void on_sync(void)
        {
            int r;

            r = ble_hs_util_ensure_addr(0x00); // public identity address
            if (r != 0)
                ESP_LOGE(TAG, "ble_hs_util_ensure_addr %d", r);

            start_discovery();
        }

        static void host_task(void *arg)
        {
            ESP_LOGI(TAG, "begin host_task");
            nimble_port_run();
        }

        void BleUnlockComponent::setup()
        {
            // disable external antenna
            gpio_set_level(GPIO_NUM_3, 0);
            gpio_set_level(GPIO_NUM_14, 0);

            int r;
            psa_status_t t;
            ESP_LOGI(TAG, "begin setup");

            r = nimble_port_init();
            if (r != 0)
                ESP_LOGE(TAG, "nimble_port_init %d", r);

            ble_hs_cfg.reset_cb = on_reset;
            ble_hs_cfg.sync_cb = on_sync;

            // r = ble_gap_set_host_feat(47, 0x01); // channel sounding
            // if (r != 0) ESP_LOGE(TAG, "ble_gap_set_host_feat %d", r);

            nimble_port_freertos_init(host_task);
        }

        void BleUnlockComponent::loop()
        {
            disable_loop();
        }

        void BleUnlockComponent::dump_config()
        {
        }

        void BleUnlockComponent::add_irk(std::vector<uint8_t> irk)
        {
            psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
            psa_set_key_type(&key_attributes, PSA_KEY_TYPE_AES);
            psa_set_key_bits(&key_attributes, 128);
            psa_set_key_algorithm(&key_attributes, PSA_ALG_ECB_NO_PADDING);
            psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
            psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);

            psa_key_id_t key;

            psa_status_t t = psa_import_key(&key_attributes, irk.data(), irk.size(), &key);
            if (t != PSA_SUCCESS)
                ESP_LOGE(TAG, "psa_import_key %d", t);

            irks.push_back(key);
        }
    } // namespace ble_unlock
} // namespace esphome

// apple watch on & unlocked, iphone locked
//  phone:  34 1d
//  phone:  38 1d
//  phone:  39 1d
//  phone:  3e 1d
//  watch:  2d 98
//  watch:  29 98

// apple watch locked, iphone locked
//  phone:  31 1d
//  phone:  34 1d
//  phone   3d 1d
//  watch:  2d 18

// apple watch locked, iphone unlocked
//  phone:  75 1d
//  phone:  79 1d
//  watch:  2d 18
