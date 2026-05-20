/*
 * TRELAY - Matter-over-Thread Relay Controller
 * BLE Configuration Service Implementation
 */

#include "app_ble_config.h"
#include "app_nvs_config.h"
#include "app_driver.h"

#include <esp_log.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>

static const char *TAG = "trelay_ble_cfg";

// BLE UUIDs - Unique to TRELAY project
// Service UUID: a1e8d550-a72f-4c89-b5fc-4e3f6d2c1b00
static const ble_uuid128_t tled_svc_uuid =
    BLE_UUID128_INIT(0x00, 0x1b, 0x2c, 0x6d, 0x3f, 0x4e, 0xfc, 0xb5,
                     0x89, 0x4c, 0x2f, 0xa7, 0x50, 0xd5, 0xe8, 0xa1);

// Characteristic UUIDs (same base, different last byte)
// GPIO Pin:          a1e8d550-a72f-4c89-b5fc-4e3f6d2c1b01
static const ble_uuid128_t chr_gpio_pin_uuid =
    BLE_UUID128_INIT(0x01, 0x1b, 0x2c, 0x6d, 0x3f, 0x4e, 0xfc, 0xb5,
                     0x89, 0x4c, 0x2f, 0xa7, 0x50, 0xd5, 0xe8, 0xa1);
// Power-on behavior: a1e8d550-a72f-4c89-b5fc-4e3f6d2c1b02
static const ble_uuid128_t chr_power_on_uuid =
    BLE_UUID128_INIT(0x02, 0x1b, 0x2c, 0x6d, 0x3f, 0x4e, 0xfc, 0xb5,
                     0x89, 0x4c, 0x2f, 0xa7, 0x50, 0xd5, 0xe8, 0xa1);
// Device Name:       a1e8d550-a72f-4c89-b5fc-4e3f6d2c1b03
static const ble_uuid128_t chr_dev_name_uuid =
    BLE_UUID128_INIT(0x03, 0x1b, 0x2c, 0x6d, 0x3f, 0x4e, 0xfc, 0xb5,
                     0x89, 0x4c, 0x2f, 0xa7, 0x50, 0xd5, 0xe8, 0xa1);
// Save & Reboot:     a1e8d550-a72f-4c89-b5fc-4e3f6d2c1bff
static const ble_uuid128_t chr_save_uuid =
    BLE_UUID128_INIT(0xff, 0x1b, 0x2c, 0x6d, 0x3f, 0x4e, 0xfc, 0xb5,
                     0x89, 0x4c, 0x2f, 0xa7, 0x50, 0xd5, 0xe8, 0xa1);

// State
static bool s_ble_active = false;
static bool s_config_saved = false;
static bool s_connected = false;
static EventGroupHandle_t s_event_group = NULL;

#define EVT_CONFIG_SAVED BIT0
#define EVT_TIMEOUT      BIT1

// Staged config values (held before save)
static uint8_t s_staged_gpio_pin;
static uint8_t s_staged_power_on;
static char s_staged_dev_name[32];

// Forward declarations
static int gatt_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);
static void ble_advertise(void);

// GATT service definition
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &tled_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &chr_gpio_pin_uuid.u,
                .access_cb = gatt_chr_access,
                .arg = (void*)1,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &chr_power_on_uuid.u,
                .access_cb = gatt_chr_access,
                .arg = (void*)2,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &chr_dev_name_uuid.u,
                .access_cb = gatt_chr_access,
                .arg = (void*)3,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &chr_save_uuid.u,
                .access_cb = gatt_chr_access,
                .arg = (void*)0xFF,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            { 0 } // Terminator
        },
    },
    { 0 } // Terminator
};

// Handle characteristic access
static int gatt_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int chr_id = (int)(intptr_t)arg;
    int rc = 0;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        switch (chr_id) {
            case 1: // GPIO Pin (uint8)
                rc = os_mbuf_append(ctxt->om, &s_staged_gpio_pin, sizeof(s_staged_gpio_pin));
                break;
            case 2: // Power-on behavior (uint8)
                rc = os_mbuf_append(ctxt->om, &s_staged_power_on, sizeof(s_staged_power_on));
                break;
            case 3: // Device Name (string)
                rc = os_mbuf_append(ctxt->om, s_staged_dev_name, strlen(s_staged_dev_name));
                break;
            default:
                return BLE_ATT_ERR_UNLIKELY;
        }
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

        switch (chr_id) {
            case 1: { // GPIO Pin (uint8)
                if (len != 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
                uint8_t val;
                os_mbuf_copydata(ctxt->om, 0, 1, &val);
                if (!tled_config_validate_gpio(val)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
                s_staged_gpio_pin = val;
                ESP_LOGI(TAG, "Staged GPIO pin: %d", val);
                break;
            }
            case 2: { // Power-on behavior (uint8)
                if (len != 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
                uint8_t val;
                os_mbuf_copydata(ctxt->om, 0, 1, &val);
                if (val > 2) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
                s_staged_power_on = val;
                ESP_LOGI(TAG, "Staged power-on behavior: %d", val);
                break;
            }
            case 3: { // Device Name (string)
                if (len == 0 || len > 31) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
                os_mbuf_copydata(ctxt->om, 0, len, s_staged_dev_name);
                s_staged_dev_name[len] = '\0';
                ESP_LOGI(TAG, "Staged device name: %s", s_staged_dev_name);
                break;
            }
            case 0xFF: { // Save & Reboot
                if (len != 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
                uint8_t val;
                os_mbuf_copydata(ctxt->om, 0, 1, &val);
                if (val == 1) {
                    ESP_LOGI(TAG, "Save requested - applying config");
                    tled_config_t *cfg = tled_config_get_mutable();
                    cfg->gpio_pin = s_staged_gpio_pin;
                    cfg->power_on_behavior = s_staged_power_on;
                    strncpy(cfg->device_name, s_staged_dev_name, sizeof(cfg->device_name) - 1);
                    cfg->device_name[sizeof(cfg->device_name) - 1] = '\0';
                    esp_err_t err = tled_config_save();
                    if (err == ESP_OK) {
                        s_config_saved = true;
                        if (s_event_group) {
                            xEventGroupSetBits(s_event_group, EVT_CONFIG_SAVED);
                        }
                        ESP_LOGI(TAG, "Config saved! Will reboot...");
                    }
                }
                break;
            }
            default:
                return BLE_ATT_ERR_UNLIKELY;
        }
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

// GAP event handler
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "BLE connected");
                s_connected = true;
            } else {
                ESP_LOGW(TAG, "BLE connection failed, status=%d", event->connect.status);
                ble_advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE disconnected, reason=%d", event->disconnect.reason);
            s_connected = false;
            if (s_ble_active && !s_config_saved) {
                ble_advertise();
            }
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            if (s_ble_active && !s_connected) {
                ble_advertise();
            }
            break;

        default:
            break;
    }
    return 0;
}

// Start advertising
static void ble_advertise(void)
{
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};

    // Set advertising flags
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // Set device name
    const char *name = "TRELAY-Config";
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv fields: %d", rc);
        return;
    }

    // Advertising parameters
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 160;  // 100ms
    adv_params.itvl_max = 320;  // 200ms

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising as 'TRELAY-Config'");
    }
}

// BLE host task sync callback
static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "BLE host synced");

    // Generate a random address if needed
    int rc = ble_hs_id_infer_auto(0, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to infer address type: %d", rc);
        return;
    }

    ble_advertise();
}

// BLE host reset callback
static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset, reason=%d", reason);
}

esp_err_t tled_ble_config_start(uint32_t timeout_ms)
{
    if (s_ble_active) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting BLE config mode");

    // Initialize staged values from current config
    const tled_config_t *cfg = tled_config_get();
    s_staged_gpio_pin = cfg->gpio_pin;
    s_staged_power_on = cfg->power_on_behavior;
    strncpy(s_staged_dev_name, cfg->device_name, sizeof(s_staged_dev_name) - 1);
    s_staged_dev_name[sizeof(s_staged_dev_name) - 1] = '\0';

    s_config_saved = false;
    s_connected = false;
    s_ble_active = true;

    // Create event group
    s_event_group = xEventGroupCreate();
    if (s_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        s_ble_active = false;
        return ESP_ERR_NO_MEM;
    }

    // Set BLE host callbacks
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    // Register GATT services
    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count GATT services: %d", rc);
        vEventGroupDelete(s_event_group);
        s_event_group = NULL;
        s_ble_active = false;
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add GATT services: %d", rc);
        vEventGroupDelete(s_event_group);
        s_event_group = NULL;
        s_ble_active = false;
        return ESP_FAIL;
    }

    // Wait for config or timeout
    TickType_t wait_ticks = (timeout_ms > 0) ? pdMS_TO_TICKS(timeout_ms) : portMAX_DELAY;
    EventBits_t bits = xEventGroupWaitBits(s_event_group, EVT_CONFIG_SAVED | EVT_TIMEOUT,
                                           pdFALSE, pdFALSE, wait_ticks);

    // Cleanup
    tled_ble_config_stop();

    if (bits & EVT_CONFIG_SAVED) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

void tled_ble_config_stop(void)
{
    if (!s_ble_active) {
        return;
    }

    ESP_LOGI(TAG, "Stopping BLE config mode");

    // Stop advertising
    ble_gap_adv_stop();

    // Disconnect if connected
    if (s_connected) {
        // Terminate all connections
        ble_gap_terminate(0, BLE_ERR_REM_USER_CONN_TERM);
    }

    s_ble_active = false;
    s_connected = false;

    if (s_event_group) {
        vEventGroupDelete(s_event_group);
        s_event_group = NULL;
    }
}

bool tled_ble_config_is_active(void)
{
    return s_ble_active;
}

bool tled_ble_config_was_saved(void)
{
    return s_config_saved;
}
