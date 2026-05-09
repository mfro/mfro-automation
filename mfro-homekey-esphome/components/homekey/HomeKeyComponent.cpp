#include "HomeKeyComponent.h"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esphome/core/log.h"
#include "esp_log.h"
#include "driver/gpio.h"

namespace esphome
{
    namespace homekit
    {
        static constexpr const char *TAG = "HomeKeyComponent";

        // https://github.com/kormax/apple-enhanced-contactless-polling
        static std::vector<uint8_t> ECP_HEADER{0x6A, 0x2, 0xCB, 0x2, 0x6, 0x2, 0x11, 0x0};

        // smart card APDU to tell the device to use homekey
        // https://github.com/kupa22/apple-homekey
        // https://github.com/kormax/apple-home-key
        static std::vector<uint8_t> APDU_SELECT_HOMEKEY{0x00, 0xA4, 0x04, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x08, 0x58, 0x01, 0x01, 0x0};

        static std::vector<uint8_t> HOMEKEY_COLOR_TAN{0x01, 0x04, 0xce, 0xd5, 0xda, 0x00};
        static std::vector<uint8_t> HOMEKEY_COLOR_GOLD{0x01, 0x04, 0xaa, 0xd6, 0xec, 0x00};
        static std::vector<uint8_t> HOMEKEY_COLOR_SILVER{0x01, 0x04, 0xe3, 0xe3, 0xe3, 0x00};
        static std::vector<uint8_t> HOMEKEY_COLOR_BLACK{0x01, 0x04, 0x00, 0x00, 0x00, 0x00};

        // TLV data advertising support info
        static std::vector<uint8_t> NFC_SUPPORTED_CONF{0x01, 0x01, 0x10, 0x02, 0x01, 0x10};

        static HomeKeyComponent *instance;

        void crc16a(unsigned char *data, unsigned int size, unsigned char *result)
        {
            unsigned short w_crc = 0x6363;

            for (unsigned int i = 0; i < size; ++i)
            {
                unsigned char byte = data[i];
                byte = (byte ^ (w_crc & 0x00FF));
                byte = ((byte ^ (byte << 4)) & 0xFF);
                w_crc = ((w_crc >> 8) ^ (byte << 8) ^ (byte << 3) ^ (byte >> 4)) & 0xFFFF;
            }

            result[0] = static_cast<unsigned char>(w_crc & 0xFF);
            result[1] = static_cast<unsigned char>((w_crc >> 8) & 0xFF);
        }

        static int identify_routine(hap_acc_t *ha)
        {
            ESP_LOGI("HAP", "Accessory identified");
            return HAP_SUCCESS;
        }

        static void esp_hap_event_handler(void *arg, esp_event_base_t event_base, int32_t event, void *data)
        {
            ESP_LOGI(TAG, "esp_hap_event_handler: %d", event);

            switch (event)
            {
            case HAP_EVENT_PAIRING_STARTED:
                ESP_LOGI(TAG, "Pairing Started");
                break;
            case HAP_EVENT_PAIRING_ABORTED:
                ESP_LOGI(TAG, "Pairing Aborted");
                break;
            case HAP_EVENT_CTRL_PAIRED:
                ESP_LOGI(TAG, "Controller %s Paired. Controller count: %d",
                         (char *)data, hap_get_paired_controller_count());
                break;
            case HAP_EVENT_CTRL_UNPAIRED:
                ESP_LOGI(TAG, "Controller %s Removed. Controller count: %d",
                         (char *)data, hap_get_paired_controller_count());
                break;
            case HAP_EVENT_CTRL_CONNECTED:
                ESP_LOGI(TAG, "Controller %s Connected", (char *)data);
                break;
            case HAP_EVENT_CTRL_DISCONNECTED:
                ESP_LOGI(TAG, "Controller %s Disconnected", (char *)data);
                break;
            case HAP_EVENT_ACC_REBOOTING:
            {
                char *reason = (char *)data;
                ESP_LOGI(TAG, "Accessory Rebooting (Reason: %s)", reason ? reason : "null");
                break;
            }
            case HAP_EVENT_PAIRING_MODE_TIMED_OUT:
                ESP_LOGI(TAG, "Pairing Mode timed out. Please reboot the device.");
            default:
                /* Silently ignore unknown events */
                break;
            }
        }

        static void hap_event_handler(hap_event_t event, void *data)
        {
            ESP_LOGI(TAG, "hap_event_handler: %d", event);

            if (event == HAP_EVENT_CTRL_PAIRED)
            {
                hap_ctrl_data_t *ctrl = hap_get_controller_data((char *)data);

                if (ctrl->valid)
                {
                    auto id = hk_utils::getHashIdentifier(ctrl->info.ltpk, ED_KEY_LEN, true);

                    hkIssuer_t *foundIssuer = nullptr;
                    for (auto &issuer : instance->reader_data.issuers)
                    {
                        if (!memcmp(issuer.issuer_id.data(), id.data(), 8))
                        {
                            foundIssuer = &issuer;
                            break;
                        }
                    }

                    if (foundIssuer == nullptr)
                    {
                        ESP_LOGI(TAG, "Adding new issuer - ID");
                        hkIssuer_t issuer;
                        issuer.issuer_id = id;
                        issuer.issuer_pk.insert(issuer.issuer_pk.begin(), ctrl->info.ltpk,
                                                ctrl->info.ltpk + ED_KEY_LEN);
                        instance->reader_data.issuers.emplace_back(issuer);
                        std::vector<uint8_t> data = nlohmann::json::to_msgpack(instance->reader_data);
                        esp_err_t set_nvs = nvs_set_blob(instance->homekit_nvs, "READERDATA", data.data(), data.size());
                        esp_err_t commit_nvs = nvs_commit(instance->homekit_nvs);
                        ESP_LOGI(TAG, "SET: %s", esp_err_to_name(set_nvs));
                        ESP_LOGI(TAG, "COMMIT: %s", esp_err_to_name(commit_nvs));

                        instance->update_reader_data();
                    }
                }
            }
            else if (event == HAP_EVENT_CTRL_UNPAIRED)
            {
                int ctrl_count = hap_get_paired_controller_count();
                if (ctrl_count == 0)
                {
                    instance->reader_data = {};
                    esp_err_t erase_nvs = nvs_erase_key(instance->homekit_nvs, "READERDATA");
                    esp_err_t commit_nvs = nvs_commit(instance->homekit_nvs);
                    ESP_LOGI(TAG, "ERASE: %s", esp_err_to_name(erase_nvs));
                    ESP_LOGI(TAG, "COMMIT: %s", esp_err_to_name(commit_nvs));

                    instance->update_reader_data();
                }
            }
        }

        static int lock_write_callback(hap_write_data_t write_data[], int count, void *serv_priv, void *write_priv)
        {
            ESP_LOGI(TAG, "lock_write_callback");

            return HAP_SUCCESS;
        }

        static int nfc_access_write_callback(hap_write_data_t write_data[], int count, void *serv_priv, void *write_priv)
        {
            ESP_LOGI(TAG, "nfc_access_write_callback");

            for (int i = 0; i < count; ++i)
            {
                hap_write_data_t *write = &write_data[i];
                *write->status = HAP_STATUS_VAL_INVALID;

                if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_NFC_ACCESS_CONTROL_POINT))
                {
                    hap_tlv8_val_t value = write->val.t;
                    auto data = std::vector<uint8_t>(value.buf, value.buf + value.buflen);

                    ESP_LOGI(TAG, "rx data: %d", data.size());
                    ESP_LOG_BUFFER_HEX(TAG, data.data(), data.size());

                    HK_HomeKit ctx(instance->reader_data, instance->homekit_nvs, "READERDATA", data);

                    auto result = ctx.processResult();

                    memcpy(instance->nfc_control_point.data(), result.data(), result.size());

                    hap_val_t new_value;
                    new_value.t.buf = instance->nfc_control_point.data();
                    new_value.t.buflen = result.size();
                    hap_char_update_val(write->hc, &new_value);
                    *write->status = HAP_STATUS_SUCCESS;

                    instance->update_reader_data();
                }
                else
                {
                    *write->status = HAP_STATUS_RES_ABSENT;
                }
            }

            return HAP_SUCCESS;
        }

        HomeKeyComponent::HomeKeyComponent(const char *setup_code, const char *setup_id)
        {
            esp_log_level_set("esp-idf", ESP_LOG_WARN);
            esp_log_level_set("HK_HomeKit", ESP_LOG_DEBUG);
            esp_log_level_set("pn532", ESP_LOG_DEBUG);
            esp_log_level_set("HAP", ESP_LOG_DEBUG);
            esp_log_level_set(TAG, ESP_LOG_DEBUG);

            ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
            ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
            ESP_LOGI(TAG, "%s", esp_err_to_name(nvs_flash_init()));

            this->setup_code = setup_code;
            this->setup_id = setup_id;

            auto t = nvs_open("HK_DATA", NVS_READWRITE, &homekit_nvs);
            ESP_LOGI(TAG, "NVS_OPEN: %s", esp_err_to_name(t));
            size_t len = 0;
            if (!nvs_get_blob(homekit_nvs, "READERDATA", NULL, &len))
            {
                std::vector<uint8_t> blob(len);
                nvs_get_blob(homekit_nvs, "READERDATA", blob.data(), &len);
                ESP_LOGI(TAG, "NVS DATA LENGTH: %d", len);

                nlohmann::json data = nlohmann::json::from_msgpack(blob);
                data.get_to<readerData_t>(reader_data);
            }

            instance = this;

            ESP_LOGI(TAG, "constructor complete");
        }

        void HomeKeyComponent::setup()
        {
            // disable external antenna
            gpio_set_level(GPIO_NUM_3, 0);
            gpio_set_level(GPIO_NUM_14, 0);

            hap_cfg_t hap_cfg;
            hap_get_config(&hap_cfg);
            hap_cfg.unique_param = UNIQUE_NAME;
            hap_set_config(&hap_cfg);

            hap_init(HAP_TRANSPORT_WIFI);

            ESP_LOGI(TAG, "begin setup");

            hap_tlv8_val_t hw_finish = {
                .buf = HOMEKEY_COLOR_SILVER.data(),
                .buflen = HOMEKEY_COLOR_SILVER.size(),
            };

            hap_acc_cfg_t cfg = {
                .name = strdup("mfro-homekey"),
                .model = strdup("mfro-homekey-v1"),
                .manufacturer = strdup("001122334455"),
                .serial_num = strdup("mfro"),
                .fw_rev = strdup("0.9.0"),
                .hw_rev = NULL,
                .hw_finish = &hw_finish,
                .pv = strdup("1.1.0"),
                .cid = HAP_CID_LOCK,
                .identify_routine = identify_routine,
            };

            hap_acc_t *accessory = hap_acc_create(&cfg);
            if (!accessory)
            {
                ESP_LOGE(TAG, "Failed to create accessory");
                hap_acc_delete(accessory);
                vTaskDelete(NULL);
            }

            uint8_t product_data[] = {'E', 'S', 'P', '3', '2', 'H', 'A', 'P'};
            hap_acc_add_product_data(accessory, product_data, sizeof(product_data));

            hap_acc_add_wifi_transport_service(accessory, 0);

            hap_serv_t *lock_mechanism = hap_serv_lock_mechanism_create(0x01, 0x01);
            hap_tlv8_val_t management = {
                .buf = 0,
                .buflen = 0,
            };

            hap_serv_set_priv(lock_mechanism, this);
            hap_serv_set_write_cb(lock_mechanism, lock_write_callback);

            hap_serv_t *lock_management = hap_serv_lock_management_create(&management, strdup("1.0.0"));

            hap_tlv8_val_t nfc_control_point = {
                .buf = this->nfc_control_point.data(),
                .buflen = 0,
            };

            hap_tlv8_val_t nfc_supported_conf = {
                .buf = this->nfc_access_supported_conf.data(),
                .buflen = this->nfc_access_supported_conf.size(),
            };

            hap_serv_t *nfc_access = hap_serv_nfc_access_create(0, &nfc_control_point, &nfc_supported_conf);
            hap_serv_set_priv(nfc_access, this);
            hap_serv_set_write_cb(nfc_access, nfc_access_write_callback);

            hap_acc_add_serv(accessory, lock_mechanism);
            hap_acc_add_serv(accessory, lock_management);
            hap_acc_add_serv(accessory, nfc_access);

            hap_add_accessory(accessory);

            ESP_LOGI(TAG, "Accessory is paired with %d controllers", hap_get_paired_controller_count());

            hap_set_setup_code(setup_code);
            hap_set_setup_id(setup_id);

            hap_register_event_handler(hap_event_handler);
            esp_event_handler_register(HAP_EVENT, ESP_EVENT_ANY_ID, &esp_hap_event_handler, NULL);

            hap_http_debug_enable();

            ESP_LOGI(TAG, "starting HAP");
            hap_start();
            ESP_LOGI(TAG, "hap started");

            // if (hap_get_paired_controller_count() == 1)
            // {
            //     instance->reader_data = {};
            //     esp_err_t erase_nvs = nvs_erase_key(instance->homekit_nvs, "READERDATA");
            //     esp_err_t commit_nvs = nvs_commit(instance->homekit_nvs);
            //     ESP_LOGI(TAG, "ERASE: %s", esp_err_to_name(erase_nvs));
            //     ESP_LOGI(TAG, "COMMIT: %s", esp_err_to_name(commit_nvs));

            //     auto x = hap_reset_to_factory();
            //     ESP_LOGI(TAG, "a: %d", x);
            // }
        }

        void HomeKeyComponent::loop()
        {
            disable_loop();
        }

        void HomeKeyComponent::dump_config()
        {
        }

        void HomeKeyComponent::set_nfc_ctx(pn532::PN532 *nfc)
        {
            nfc_device = nfc;
            update_reader_data();

            auto trigger = new nfc::NfcOnTagTrigger();
            auto automation = new Automation<std::string, nfc::NfcTag>(trigger);
            auto action = new LambdaAction<std::string, nfc::NfcTag>(
                [this, nfc](std::string x, nfc::NfcTag tag) -> void
                {
                    ESP_LOGI(TAG, "tag triggered");

                    std::function<bool(uint8_t *, uint8_t, uint8_t *, uint16_t *, bool)>
                        lambda = [=](uint8_t *send, uint8_t sendLen, uint8_t *res,
                                     uint16_t *resLen, bool ignoreLog) -> bool
                    {
                        auto data = nfc->inDataExchange(std::vector<uint8_t>(send, send + sendLen));
                        data.erase(data.begin());
                        ESP_LOGI(TAG, "%s", format_hex_pretty(data).c_str());
                        memcpy(res, data.data(), data.size());
                        uint16_t t = data.size();
                        memcpy(resLen, &t, sizeof(uint16_t));
                        return true;
                    };

                    auto versions = nfc->inDataExchange(APDU_SELECT_HOMEKEY);
                    if (versions.size() > 0)
                    {
                        ESP_LOGI(TAG, "HK SUPPORTED VERSIONS: %s", format_hex_pretty(versions).c_str());
                        if (versions.data()[versions.size() - 2] == 0x90 &&
                            versions.data()[versions.size() - 1] == 0x0)
                        {

                            HKAuthenticationContext authCtx(lambda, reader_data, homekit_nvs);
                            auto authResult = authCtx.authenticate(KeyFlow(kFlowFAST));
                            if (std::get<0>(authResult).size() > 0 &&
                                std::get<2>(authResult) != kFlowFailed)
                            {
                                auth_callback.call();

                                ESP_LOGI(TAG, "binary sensor %d", binary_sensors.size());
                                for (auto binary_sensor : binary_sensors)
                                {
                                    binary_sensor->publish_state(true);
                                    set_timeout(1000, [binary_sensor]() { binary_sensor->publish_state(false); });
                                }

                                ESP_LOGI(TAG, "success");
                            }
                            else
                            {
                                ESP_LOGE(TAG, "fail");
                            }
                        }
                        else
                        {
                            ESP_LOGE(TAG, "Invalid response for HK");
                        }
                    }
                    else
                    {
                        ESP_LOGW(TAG, "Target probably not Homekey");
                    }
                });

            automation->add_actions({action});
            nfc->register_ontag_trigger(trigger);
        }

        void HomeKeyComponent::update_reader_data()
        {
            uint8_t crc[2];
            std::vector<uint8_t> ecp;
            ecp.insert(ecp.end(), ECP_HEADER.begin(), ECP_HEADER.end());
            ecp.insert(ecp.end(), reader_data.reader_gid.begin(), reader_data.reader_gid.end());
            crc16a(ecp.data(), ecp.size(), crc);
            ecp.insert(ecp.end(), crc, crc + 2);

            nfc_device->set_ecp_frame(ecp);

            ESP_LOGI(TAG, "ecp frame:");
            ESP_LOG_BUFFER_HEX(TAG, ecp.data(), ecp.size());

            ESP_LOGI(TAG, "reader secret key:");
            ESP_LOG_BUFFER_HEX(TAG, reader_data.reader_sk.data(), reader_data.reader_sk.size());
            ESP_LOGI(TAG, "reader public key:");
            ESP_LOG_BUFFER_HEX(TAG, reader_data.reader_pk.data(), reader_data.reader_pk.size());
            ESP_LOGI(TAG, "reader group identifier:");
            ESP_LOG_BUFFER_HEX(TAG, reader_data.reader_gid.data(), reader_data.reader_gid.size());
            ESP_LOGI(TAG, "reader unique identifier:");
            ESP_LOG_BUFFER_HEX(TAG, reader_data.reader_id.data(), reader_data.reader_id.size());
            ESP_LOGI(TAG, "issuers count: %d", reader_data.issuers.size());
        }
    } // namespace homekit
} // namespace esphome
