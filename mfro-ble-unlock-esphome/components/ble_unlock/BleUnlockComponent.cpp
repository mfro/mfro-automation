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

        static char *addr_str(const ble_addr_t *addr)
        {
            static char buf[6 * 2 + 5 + 1];
            const uint8_t *u8p = addr->val;

            sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x",
                    u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);

            return buf;
        }

        static char *uuid_str(const ble_uuid_any_t *uuid)
        {
            static char buf[37];

            switch (uuid->u.type)
            {
            case 16:
                sprintf(buf, "%04x", uuid->u16.value);
                break;
            case 32:
                sprintf(buf, "%08x", uuid->u32.value);
                break;
            case 128:
                sprintf(buf, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                        uuid->u128.value[15], uuid->u128.value[14], uuid->u128.value[13], uuid->u128.value[12],
                        uuid->u128.value[11], uuid->u128.value[10],
                        uuid->u128.value[9], uuid->u128.value[8],
                        uuid->u128.value[7], uuid->u128.value[6],
                        uuid->u128.value[5], uuid->u128.value[4], uuid->u128.value[3],
                        uuid->u128.value[2], uuid->u128.value[1], uuid->u128.value[0]);
                break;
            }

            return buf;
        }

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

        static void print_connection_description(ble_gap_conn_desc *desc)
        {
            ESP_LOGI(TAG, "handle=%d our_ota_addr_type=%d our_ota_addr=%s ",
                     desc->conn_handle, desc->our_ota_addr.type,
                     addr_str(&desc->our_ota_addr));
            ESP_LOGI(TAG, "our_id_addr_type=%d our_id_addr=%s ",
                     desc->our_id_addr.type, addr_str(&desc->our_id_addr));
            ESP_LOGI(TAG, "peer_ota_addr_type=%d peer_ota_addr=%s ",
                     desc->peer_ota_addr.type, addr_str(&desc->peer_ota_addr));
            ESP_LOGI(TAG, "peer_id_addr_type=%d peer_id_addr=%s ",
                     desc->peer_id_addr.type, addr_str(&desc->peer_id_addr));
            ESP_LOGI(TAG, "conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                          "encrypted=%d authenticated=%d bonded=%d",
                     desc->conn_itvl, desc->conn_latency,
                     desc->supervision_timeout,
                     desc->sec_state.encrypted,
                     desc->sec_state.authenticated,
                     desc->sec_state.bonded);
        }

        static int on_gatt_included_service_discovery(uint16_t conn_handle,
                                                      const struct ble_gatt_error *error,
                                                      const struct ble_gatt_incl_svc *service,
                                                      void *arg)
        {
            ESP_LOGI(TAG, "on_gatt_included_service_discovery");

            if (error->status == 0)
            {
                ESP_LOGI(TAG, "%d %d %d %s", service->handle, service->start_handle, service->end_handle, uuid_str(&service->uuid));
            }
            else if (error->status == BLE_HS_EDONE)
            {
                ESP_LOGI(TAG, "on_gatt_included_service_discovery done");

                size_t index = (size_t)arg;
                if (index < instance->gatt_services.size())
                {
                    ble_gattc_find_inc_svcs(conn_handle, instance->gatt_services[index].start_handle, instance->gatt_services[index].end_handle,
                                            on_gatt_included_service_discovery, (void *)(index + 1));
                }
            }
            else
            {
                ESP_LOGE(TAG, "on_gatt_included_service_discovery %d", error->status);
            }

            return 0;
        }

        static int on_gatt_attribute(uint16_t conn_handle,
                                     const struct ble_gatt_error *error,
                                     struct ble_gatt_attr *attribute,
                                     void *arg)
        {
            ESP_LOGI(TAG, "on_gatt_attribute");

            size_t index = (size_t)arg;

            if (error->status == 0)
            {
                ESP_LOG_BUFFER_HEX(TAG, attribute->om->om_data, attribute->om->om_len);

                if (index + 1 < instance->gatt_characteristics.size())
                {
                    ble_gattc_read(conn_handle, instance->gatt_characteristics[index + 1].val_handle, on_gatt_attribute, (void *)(index + 1));
                }
            }
            else
            {
                ESP_LOGE(TAG, "on_gatt_attribute %d", error->status);
            }

            return 0;
        }

        static int on_gatt_characteristic_discovery(uint16_t conn_handle,
                                                    const struct ble_gatt_error *error,
                                                    const struct ble_gatt_chr *characteristic,
                                                    void *arg)
        {
            ESP_LOGI(TAG, "on_gatt_characteristic_discovery");

            size_t index = (size_t)arg;

            if (error->status == 0)
            {
                instance->gatt_characteristics.push_back(*characteristic);

                ESP_LOGI(TAG, "%d %d %d %s", characteristic->def_handle, characteristic->val_handle, characteristic->properties, uuid_str(&characteristic->uuid));
            }
            else if (error->status == BLE_HS_EDONE)
            {
                ESP_LOGI(TAG, "on_gatt_characteristic_discovery done");

                if (index + 1 < instance->gatt_services.size())
                {
                    ble_gattc_disc_all_chrs(conn_handle,
                                            instance->gatt_services[index + 1].start_handle,
                                            instance->gatt_services[index + 1].end_handle,
                                            on_gatt_characteristic_discovery, (void *)(index + 1));
                }
                else
                {
                    ble_gattc_read(conn_handle, instance->gatt_characteristics[0].val_handle, on_gatt_attribute, (void *)(0));
                }
            }
            else
            {
                ESP_LOGE(TAG, "on_gatt_characteristic_discovery %d", error->status);
            }

            return 0;
        }

        static int on_gatt_service_discovery(uint16_t conn_handle,
                                             const struct ble_gatt_error *error,
                                             const struct ble_gatt_svc *service,
                                             void *arg)
        {
            ESP_LOGI(TAG, "on_gatt_service_discovery");

            if (error->status == 0)
            {
                instance->gatt_services.push_back(*service);

                ESP_LOGI(TAG, "%d %d %s", service->start_handle, service->end_handle, uuid_str(&service->uuid));
            }
            else if (error->status == BLE_HS_EDONE)
            {
                ESP_LOGI(TAG, "on_gatt_service_discovery done");

                ble_gattc_disc_all_chrs(conn_handle,
                                        instance->gatt_services[0].start_handle,
                                        instance->gatt_services[0].end_handle,
                                        on_gatt_characteristic_discovery, (void *)0);
            }
            else
            {
                ESP_LOGE(TAG, "on_gatt_service_discovery %d", error->status);
            }

            return 0;
        }

        static void start_discovery(void);
        static int on_gap_event(ble_gap_event *event, void *arg)
        {
            int r;
            psa_status_t t;

            if (event->type == BLE_GAP_EVENT_EXT_DISC)
            {
                for (int i = 0; i < instance->irks.size(); ++i)
                {
                    if (is_irk_match(instance->irks[i], &event->ext_disc.addr))
                    {
                        FoundDevice device = {
                            .rssi = event->ext_disc.rssi,
                            .address = event->ext_disc.addr,
                        };

                        instance->found_devices.emplace(i, device);
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
                ESP_LOGI(TAG, "discovery: %d", instance->found_devices.size());
                for (auto [key, value] : instance->found_devices)
                {
                    ESP_LOGI(TAG, "  %s %d", addr_str(&value.address), value.rssi);
                }

                if (instance->found_devices.contains(1))
                {
                    auto &device = instance->found_devices[1];

                    r = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &device.address, 1000, NULL, on_gap_event, NULL);
                    if (r != 0)
                        ESP_LOGE(TAG, "ble_gap_connect %d", r);
                }
                else
                {
                    instance->found_devices.clear();
                    start_discovery();
                }
            }
            else if (event->type == BLE_GAP_EVENT_CONNECT)
            {
                ESP_LOGI(TAG, "BLE_GAP_EVENT_CONNECT %d", event->connect.status);

                ble_gap_conn_desc description;
                r = ble_gap_conn_find(event->connect.conn_handle, &description);
                if (r != 0)
                    ESP_LOGE(TAG, "ble_gap_conn_find %d", r);

                print_connection_description(&description);

                // r = ble_gap_security_initiate(event->connect.conn_handle);
                // if (r != 0)
                //     ESP_LOGE(TAG, "ble_gap_security_initiate %d", r);

                r = ble_gattc_disc_all_svcs(event->enc_change.conn_handle, on_gatt_service_discovery, NULL);
                if (r != 0)
                    ESP_LOGE(TAG, "ble_gattc_disc_all_svcs %d", r);
            }
            else if (event->type == BLE_GAP_EVENT_ENC_CHANGE)
            {
                ESP_LOGI(TAG, "BLE_GAP_EVENT_ENC_CHANGE %d", event->enc_change.status);
                ble_gap_conn_desc description;
                r = ble_gap_conn_find(event->enc_change.conn_handle, &description);
                if (r != 0)
                    ESP_LOGE(TAG, "ble_gap_conn_find %d", r);

                print_connection_description(&description);
            }
            else if (event->type == BLE_GAP_EVENT_LINK_ESTAB)
            {
                ESP_LOGI(TAG, "BLE_GAP_EVENT_LINK_ESTAB %d", event->link_estab.status);
                ble_gap_conn_desc description;
                r = ble_gap_conn_find(event->link_estab.conn_handle, &description);
                if (r != 0)
                    ESP_LOGE(TAG, "ble_gap_conn_find %d", r);

                print_connection_description(&description);
            }
            else if (event->type == BLE_GAP_EVENT_DATA_LEN_CHG)
            {
                ESP_LOGI(TAG, "BLE_GAP_EVENT_DATA_LEN_CHG");

                ble_gap_conn_desc description;
                r = ble_gap_conn_find(event->data_len_chg.conn_handle, &description);
                if (r != 0)
                    ESP_LOGE(TAG, "ble_gap_conn_find %d", r);

                print_connection_description(&description);
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

            r = ble_gap_disc(address_type, 5000, &discovery, on_gap_event, NULL);
            if (r != 0)
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
            gpio_set_level(GPIO_NUM_14, 1);

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
//  phone:  32 1d
//  phone:  34 1d
//  phone:  38 1d
//  phone:  39 1d
//  phone:  3e 1d
//  watch:  28 98
//  watch:  29 98
//  watch:  2d 98

// apple watch locked, iphone locked
//  phone:  31 1d
//  phone:  34 1d
//  phone   3d 1d
//  watch:  2d 18

// apple watch locked, iphone unlocked
//  phone:  75 1d
//  phone:  79 1d
//  watch:  2d 18
