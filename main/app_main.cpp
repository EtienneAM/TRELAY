/*
 * TRELAY - Matter-over-Thread Relay Controller
 * Main Application Entry Point
 */

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <common_macros.h>

#include "app_config.h"
#include "app_driver.h"
#include "app_nvs_config.h"
#include "app_ble_config.h"
#include "app_serial_config.h"
#include "app_device_info.h"
#include "app_monitoring.h"
#include <app_reset.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <platform/CHIPDeviceLayer.h>
#include <app/clusters/door-lock-server/door-lock-server.h>

// Version string from CMakeLists.txt
#ifndef PROJECT_VER
#define PROJECT_VER "unknown"
#endif

static const char *TAG = "trelay_main";

// Global endpoint IDs
uint16_t relay_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

// Required symbol: esp-matter's legacy cluster layer puts this in its
// door_lock function_list and the linker requires it to be defined.
void emberAfDoorLockClusterInitCallback(chip::EndpointId endpoint)
{
    ESP_LOGI(TAG, "DoorLock cluster init on endpoint %d", endpoint);
}

// Check if boot button is held
static bool is_button_held(uint32_t hold_time_ms)
{
    gpio_num_t btn_gpio = (gpio_num_t)TLED_BOOT_BUTTON_GPIO;

    // Configure GPIO as input with pullup
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << btn_gpio);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    // Check if button is pressed (active low)
    if (gpio_get_level(btn_gpio) != 0) {
        return false;  // Not pressed
    }

    // Wait and check if still held
    ESP_LOGI(TAG, "Button pressed, waiting %lu ms to confirm config mode...", (unsigned long)hold_time_ms);

    uint32_t held_time = 0;
    const uint32_t check_interval = 100;

    while (held_time < hold_time_ms) {
        vTaskDelay(pdMS_TO_TICKS(check_interval));
        held_time += check_interval;

        if (gpio_get_level(btn_gpio) != 0) {
            ESP_LOGI(TAG, "Button released, normal boot");
            return false;
        }
    }

    ESP_LOGI(TAG, "Button held for %lu ms - entering config mode", (unsigned long)hold_time_ms);
    return true;
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        {
            ESP_LOGI(TAG, "Fabric removed successfully");
            if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
                chip::CommissioningWindowManager &commissionMgr =
                    chip::Server::GetInstance().GetCommissioningWindowManager();
                constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
                if (!commissionMgr.IsCommissioningWindowOpen()) {
                    // Basic Commissioning Window is correct here: Enhanced (ECW) requires
                    // an existing controller on the fabric to generate a one-time passcode,
                    // which is unavailable after the last fabric is removed.  BCW uses the
                    // original QR-code passcode so the device is immediately re-pairable.
                    // kDnssdOnly: BLE is already deinitialized after first commissioning.
                    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(
                        kTimeoutSeconds,
                        chip::CommissioningWindowAdvertisement::kDnssdOnly);
                    if (err != CHIP_NO_ERROR) {
                        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
                    }
                }
            }
            break;
        }

    case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
        ESP_LOGI(TAG, "Fabric will be removed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
        ESP_LOGI(TAG, "Fabric is updated");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
        ESP_LOGI(TAG, "Fabric is committed");
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;

    case chip::DeviceLayer::DeviceEventType::kServerReady:
        ESP_LOGI(TAG, "Matter server ready");
        break;

    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type,
                                        uint16_t endpoint_id,
                                        uint8_t effect_id,
                                        uint8_t effect_variant,
                                        void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    // TODO: Implement visual identification (e.g., blink LED)
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type,
                                          uint16_t endpoint_id,
                                          uint32_t cluster_id,
                                          uint32_t attribute_id,
                                          esp_matter_attr_val_t *val,
                                          void *priv_data)
{
    esp_err_t err = ESP_OK;

    if (type == PRE_UPDATE) {
        app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
        err = app_driver_attribute_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
    }

    return err;
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "==================================");
    ESP_LOGI(TAG, "TRELAY - Matter-over-Thread Relay Controller");
    ESP_LOGI(TAG, "Version: %s", PROJECT_VER);
    ESP_LOGI(TAG, "==================================");

    /* Initialize the ESP NVS layer */
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Initialize configuration */
    err = tled_config_init();
    ESP_ERROR_CHECK(err);

    const tled_config_t *config = tled_config_get();

    /* Check if first boot - use defaults and save */
    if (!tled_config_is_configured()) {
        ESP_LOGI(TAG, "First boot detected - using default config");
        ESP_LOGI(TAG, "To change settings: hold BOOT button for 5s during startup");

        // Save the default config so next boot isn't "first boot"
        tled_config_save();
    }

    // TODO: BLE config mode on button hold (needs to be done before Matter starts BLE)
    // For now, just log if button is held
    if (is_button_held(3000)) {
        ESP_LOGW(TAG, "Button held - BLE config not yet implemented");
        ESP_LOGW(TAG, "Use nRF Connect to configure via BLE characteristics");
    }

    ESP_LOGI(TAG, "Using config: relay on GPIO%d", config->gpio_pin);

    /* Initialize drivers */
    app_driver_handle_t relay_handle = app_driver_light_init();
    ABORT_APP_ON_FAILURE(relay_handle != NULL, ESP_LOGE(TAG, "Failed to initialize relay driver"));

    app_driver_handle_t button_handle = app_driver_button_init();
    ABORT_APP_ON_FAILURE(button_handle != NULL, ESP_LOGE(TAG, "Failed to initialize button driver"));
    app_reset_button_register(button_handle);

    /* Set custom device info provider BEFORE creating Matter node */
    /* This must be done before node::create() so BasicInformation cluster gets our values */
    tled::set_device_info_provider();

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    /* Create relay endpoint based on configured device type */
    const tled_config_t *relay_cfg = tled_config_get();

    if (relay_cfg->device_type == DEVICE_TYPE_DOOR_LOCK) {
        door_lock::config_t dl_config;
        dl_config.door_lock.lock_state = nullable<uint8_t>(static_cast<uint8_t>(DlLockState::kLocked));
        dl_config.door_lock.lock_type  = chip::to_underlying(chip::app::Clusters::DoorLock::DlLockType::kDeadLatch);
        dl_config.door_lock.actuator_enabled = true; // CHIP's InitEndpoint forces this anyway
        // Explicitly advertise Normal operating mode so controllers (e.g. Apple Home)
        // don't receive UnsupportedAttribute (0x86) when reading attribute 0x0023/0x0024.
        // Normal (0x00) means remote lock/unlock is always permitted.
        // SupportedOperatingModes bitmap: bit 0 = Normal only (0x0001).
        dl_config.door_lock.operating_mode = static_cast<uint8_t>(chip::app::Clusters::DoorLock::OperatingModeEnum::kNormal);
        dl_config.door_lock.supported_operating_modes = 0x0001; // Normal only

        endpoint_t *endpoint = door_lock::create(node, &dl_config, ENDPOINT_FLAG_NONE, relay_handle);
        ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create door lock endpoint"));

        relay_endpoint_id = endpoint::get_id(endpoint);
        ESP_LOGI(TAG, "Door Lock endpoint created with endpoint_id %d", relay_endpoint_id);

        // Add the optional AutoRelockTime attribute (Matter 1.3, DoorLock cluster, attr 0x0023).
        // Value comes from NVS-backed config (default: CONFIG_TRELAY_AUTO_RELOCK_TIME_S).
        // Clamped to [1, 10] s in app_driver_attribute_update (PRE_UPDATE on AutoRelockTime).
        // The CHIP door-lock-server reads this attribute after every successful Unlock command
        // and schedules its own auto-relock timer, so no custom FreeRTOS timer is needed.
        uint32_t auto_relock_s = relay_cfg->auto_revert_s;
        if (auto_relock_s < 1 || auto_relock_s > 10) {
            ESP_LOGW(TAG, "auto_revert_s %lu out of [1,10], clamping to 3",
                     (unsigned long)auto_relock_s);
            auto_relock_s = 3;
        }
        cluster_t *dl_cluster = cluster::get(endpoint, DoorLock::Id);
        cluster::door_lock::attribute::create_auto_relock_time(dl_cluster, auto_relock_s);
        ESP_LOGI(TAG, "AutoRelockTime set to %lu s", (unsigned long)auto_relock_s);
    } else {
        /* Default: Generic On/Off Plug-in Unit (switch/outlet) */
        on_off_plug_in_unit::config_t relay_config;
        relay_config.on_off.on_off = TLED_DEFAULT_POWER;
        relay_config.on_off_lighting.start_up_on_off = nullptr;

        endpoint_t *endpoint = on_off_plug_in_unit::create(node, &relay_config, ENDPOINT_FLAG_NONE, relay_handle);
        ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create relay endpoint"));

        relay_endpoint_id = endpoint::get_id(endpoint);
        ESP_LOGI(TAG, "On/Off Plug-in Unit endpoint created with endpoint_id %d", relay_endpoint_id);
    }

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t ot_config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&ot_config);
#endif

    /* Start Matter */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    ESP_LOGI(TAG, "Matter started successfully");

    /* Print commissioning QR code */
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));

    /* Apply default relay settings (restore from NVS) */
    app_driver_light_set_defaults(relay_endpoint_id);

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#endif

    /* Initialize serial configuration interface */
    serial_config_init();

    /* Initialize health monitoring (watchdog, heap, thermal) */
    monitoring_init();

    ESP_LOGI(TAG, "TRELAY initialization complete. Waiting for commissioning...");
    ESP_LOGI(TAG, "Serial config available - connect via USB and type 'help'");

    /* Main loop - just idle, all work done in callbacks */
    while (true) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}
