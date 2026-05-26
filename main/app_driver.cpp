/*
 * TRELAY - Matter-over-Thread Relay Controller
 * Relay Driver Implementation
 */

#include <esp_log.h>
#include <driver/gpio.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

#include <esp_matter.h>
#include <platform/CHIPDeviceLayer.h>
#include "app_driver.h"
#include "app_config.h"
#include "app_nvs_config.h"

#include <iot_button.h>
#include <button_gpio.h>

// NVS namespace and keys for state persistence
#define NVS_NAMESPACE "trelay_state"
#define NVS_KEY_POWER "power"

// Debounce timing for NVS saves (5 seconds)
#define NVS_SAVE_DEBOUNCE_MS 5000

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "trelay_driver";

// External reference to relay endpoint ID (defined in app_main.cpp)
extern uint16_t relay_endpoint_id;

// Driver state structure
typedef struct {
    bool power;
    uint8_t gpio_pin;
    bool active_high;           // true = relay on when GPIO high
    uint8_t power_on_behavior;
    uint8_t device_type;        // DEVICE_TYPE_ON_OFF or DEVICE_TYPE_DOOR_LOCK
    uint16_t auto_revert_s;     // Auto-revert delay in seconds (0=disabled)
    bool nvs_save_pending;
    TimerHandle_t nvs_save_timer;
    TimerHandle_t auto_revert_timer;
} relay_driver_t;

// Semaphore to trigger NVS writes from a background task.
// Timer callbacks and Matter callbacks signal this; the dedicated task does
// the actual flash write so the timer/CHIP threads never block on NVS.
static SemaphoreHandle_t s_nvs_save_sem = NULL;

// Static driver instance
static relay_driver_t s_relay_driver = {
    .power = false,
    .gpio_pin = TRELAY_DEFAULT_GPIO_PIN,
    .active_high = true,
    .power_on_behavior = POWER_ON_RESTORE,
    .device_type = DEVICE_TYPE_ON_OFF,
    .auto_revert_s = 0,
    .nvs_save_pending = false,
    .nvs_save_timer = NULL,
    .auto_revert_timer = NULL,
};

// --- GPIO control ---

static void relay_gpio_set(relay_driver_t *driver, bool on)
{
    int level = on ? (driver->active_high ? 1 : 0)
                   : (driver->active_high ? 0 : 1);
    gpio_set_level((gpio_num_t)driver->gpio_pin, level);
    ESP_LOGI(TAG, "Relay GPIO%d -> %s (level=%d)", driver->gpio_pin, on ? "ON" : "OFF", level);
}

// --- NVS ---

static esp_err_t do_save_state_to_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(handle, NVS_KEY_POWER, s_relay_driver.power ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set power: %s", esp_err_to_name(err));
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    s_relay_driver.nvs_save_pending = false;

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "State saved to NVS: power=%d", s_relay_driver.power);
    } else {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    }
    return err;
}

// Background task: waits on semaphore, performs the blocking NVS commit.
// Priority 2 keeps it below Thread/Matter (typically 5-8) and serial (2).
static void nvs_save_task(void *pvParameters)
{
    while (true) {
        if (xSemaphoreTake(s_nvs_save_sem, portMAX_DELAY) == pdTRUE) {
            do_save_state_to_nvs();
        }
        vTaskDelay(pdMS_TO_TICKS(1));  // yield before re-checking
    }
}

static void nvs_save_timer_callback(TimerHandle_t timer)
{
    // Do NOT write NVS here — timer task must not block on flash.
    // Signal the background task instead.
    if (s_nvs_save_sem) {
        xSemaphoreGive(s_nvs_save_sem);
    }
}

// Forward declaration (definition follows schedule_save_state_to_nvs below)
static void schedule_save_state_to_nvs(void);

// Auto-revert: turn relay off / re-lock after configured delay.
// Runs in FreeRTOS timer context — schedules the Matter attribute update
// on the CHIP stack thread.
static void auto_revert_timer_callback(TimerHandle_t timer)
{
    relay_driver_t *driver = &s_relay_driver;
    ESP_LOGI(TAG, "Auto-revert fired: returning to off/locked state");

    relay_gpio_set(driver, false);
    driver->power = false;
    // Signal background NVS task — never write flash from the timer task.
    schedule_save_state_to_nvs();

    chip::DeviceLayer::SystemLayer().ScheduleLambda([driver]() {
        if (driver->device_type == DEVICE_TYPE_DOOR_LOCK) {
            // LockState = 1 (Locked)
            esp_matter_attr_val_t val = esp_matter_uint8(1);
            attribute::update(relay_endpoint_id, DoorLock::Id,
                              DoorLock::Attributes::LockState::Id, &val);
        } else {
            esp_matter_attr_val_t val = esp_matter_bool(false);
            attribute::update(relay_endpoint_id, OnOff::Id,
                              OnOff::Attributes::OnOff::Id, &val);
        }
    });
}

static void schedule_save_state_to_nvs(void)
{
    s_relay_driver.nvs_save_pending = true;

    if (s_relay_driver.nvs_save_timer != NULL) {
        if (xTimerReset(s_relay_driver.nvs_save_timer, 0) != pdPASS) {
            ESP_LOGW(TAG, "Failed to reset NVS timer, signalling background task");
            if (s_nvs_save_sem) xSemaphoreGive(s_nvs_save_sem);
        }
    } else if (s_nvs_save_sem) {
        xSemaphoreGive(s_nvs_save_sem);
    } else {
        // Semaphore not yet created (very early boot) — direct write is acceptable.
        do_save_state_to_nvs();
    }
}

static esp_err_t load_state_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved state found, using defaults");
        return err;
    }

    uint8_t val;
    if (nvs_get_u8(handle, NVS_KEY_POWER, &val) == ESP_OK) {
        s_relay_driver.power = (val != 0);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "State loaded from NVS: power=%d", s_relay_driver.power);
    return ESP_OK;
}

// --- Button ---

static void app_driver_button_toggle_cb(void *button_handle, void *usr_data)
{
    ESP_LOGI(TAG, "Toggle button pressed");
    relay_driver_t *driver = &s_relay_driver;

    chip::DeviceLayer::SystemLayer().ScheduleLambda([driver]() {
        if (driver->device_type == DEVICE_TYPE_DOOR_LOCK) {
            // Toggle LockState: 1 (Locked) <-> 2 (Unlocked)
            attribute_t *attr = attribute::get(relay_endpoint_id,
                DoorLock::Id, DoorLock::Attributes::LockState::Id);
            if (attr == NULL) {
                ESP_LOGE(TAG, "Failed to get LockState attribute");
                return;
            }
            esp_matter_attr_val_t val = esp_matter_invalid(NULL);
            attribute::get_val(attr, &val);
            uint8_t new_state = (val.val.u8 == 2) ? 1 : 2; // flip Locked<->Unlocked
            esp_matter_attr_val_t new_val = esp_matter_uint8(new_state);
            attribute::update(relay_endpoint_id, DoorLock::Id,
                              DoorLock::Attributes::LockState::Id, &new_val);
        } else {
            attribute_t *attribute = attribute::get(relay_endpoint_id,
                OnOff::Id, OnOff::Attributes::OnOff::Id);
            if (attribute == NULL) {
                ESP_LOGE(TAG, "Failed to get OnOff attribute");
                return;
            }
            esp_matter_attr_val_t val = esp_matter_invalid(NULL);
            attribute::get_val(attribute, &val);
            val.val.b = !val.val.b;
            attribute::update(relay_endpoint_id, OnOff::Id,
                              OnOff::Attributes::OnOff::Id, &val);
        }
    });
}

// --- Public API ---

esp_err_t app_driver_light_set_power(app_driver_handle_t handle, bool power)
{
    relay_driver_t *driver = (relay_driver_t *)handle;
    if (driver == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    driver->power = power;
    relay_gpio_set(driver, power);
    schedule_save_state_to_nvs();

    // Manage auto-revert timer
    if (driver->auto_revert_timer != NULL) {
        if (power && driver->auto_revert_s > 0) {
            // Start (or restart) one-shot timer when turning on
            xTimerChangePeriod(driver->auto_revert_timer,
                               pdMS_TO_TICKS((uint32_t)driver->auto_revert_s * 1000), 0);
            xTimerStart(driver->auto_revert_timer, 0);
            ESP_LOGI(TAG, "Auto-revert in %d seconds", driver->auto_revert_s);
        } else {
            // Cancel any pending revert when explicitly turning off
            xTimerStop(driver->auto_revert_timer, 0);
        }
    }

    return ESP_OK;
}

bool app_driver_light_get_power(app_driver_handle_t handle)
{
    relay_driver_t *driver = (relay_driver_t *)handle;
    if (driver == NULL) {
        return false;
    }
    return driver->power;
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle,
                                       uint16_t endpoint_id,
                                       uint32_t cluster_id,
                                       uint32_t attribute_id,
                                       esp_matter_attr_val_t *val)
{
    relay_driver_t *driver = (relay_driver_t *)driver_handle;

    if (driver == NULL) {
        return ESP_OK;  // Not initialized yet -- ignore during boot
    }

    if (endpoint_id != relay_endpoint_id) {
        return ESP_OK;  // Not our endpoint
    }

    if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
        return app_driver_light_set_power(driver_handle, val->val.b);
    }

    // Door lock: LockState 1=Locked (relay off), 2=Unlocked (relay on)
    if (cluster_id == DoorLock::Id && attribute_id == DoorLock::Attributes::LockState::Id) {
        bool unlocked = (val->val.u8 == 2);
        return app_driver_light_set_power(driver_handle, unlocked);
    }

    return ESP_OK;
}

esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id)
{
    relay_driver_t *driver = &s_relay_driver;

    const tled_config_t *config = tled_config_get();
    uint8_t power_on_behavior = config->power_on_behavior;

    if (load_state_from_nvs() == ESP_OK) {
        if (driver->device_type == DEVICE_TYPE_DOOR_LOCK) {
            // Door lock always starts in locked (safe) state regardless of power-on setting
            driver->power = false;
            ESP_LOGI(TAG, "Door lock: starting in LOCKED state");
        } else {
            switch (power_on_behavior) {
                case POWER_ON_RESTORE:
                    break;
                case POWER_ON_ON:
                    driver->power = true;
                    ESP_LOGI(TAG, "Power-on behavior: forcing ON");
                    break;
                case POWER_ON_OFF:
                    driver->power = false;
                    ESP_LOGI(TAG, "Power-on behavior: forcing OFF");
                    break;
            }
        }
    }

    // Apply GPIO output immediately
    relay_gpio_set(driver, driver->power);

    // Sync the correct Matter attribute to match hardware state
    if (driver->device_type == DEVICE_TYPE_DOOR_LOCK) {
        // LockState: 1=Locked, 2=Unlocked
        esp_matter_attr_val_t val = esp_matter_uint8(driver->power ? 2 : 1);
        attribute::update(endpoint_id, DoorLock::Id,
                          DoorLock::Attributes::LockState::Id, &val);
    } else {
        esp_matter_attr_val_t val = esp_matter_bool(driver->power);
        attribute::update(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
    }

    ESP_LOGI(TAG, "Relay defaults applied: power=%d (type=%d, revert=%ds)",
             driver->power, driver->device_type, driver->auto_revert_s);
    return ESP_OK;
}

app_driver_handle_t app_driver_light_init(void)
{
    const tled_config_t *config = tled_config_get();
    s_relay_driver.gpio_pin = config->gpio_pin;
    s_relay_driver.power_on_behavior = config->power_on_behavior;
    s_relay_driver.device_type = config->device_type;
    s_relay_driver.auto_revert_s = config->auto_revert_s;

#ifdef CONFIG_TRELAY_ACTIVE_HIGH
    s_relay_driver.active_high = true;
#else
    s_relay_driver.active_high = false;
#endif

    ESP_LOGI(TAG, "Initializing relay driver on GPIO%d (active %s, type=%d, revert=%ds)",
             s_relay_driver.gpio_pin,
             s_relay_driver.active_high ? "HIGH" : "LOW",
             s_relay_driver.device_type,
             s_relay_driver.auto_revert_s);

    // Configure GPIO as output, default to relay-off state
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << s_relay_driver.gpio_pin);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure relay GPIO: %s", esp_err_to_name(err));
        return NULL;
    }

    // Start with relay off
    relay_gpio_set(&s_relay_driver, false);

    // Create NVS save timer
    s_relay_driver.nvs_save_timer = xTimerCreate(
        "nvs_save",
        pdMS_TO_TICKS(NVS_SAVE_DEBOUNCE_MS),
        pdFALSE,
        NULL,
        nvs_save_timer_callback
    );
    if (s_relay_driver.nvs_save_timer == NULL) {
        ESP_LOGW(TAG, "Failed to create NVS save timer");
    }

    // Create background NVS write task so timer/CHIP threads never block on flash.
    s_nvs_save_sem = xSemaphoreCreateBinary();
    if (s_nvs_save_sem != NULL) {
        BaseType_t nvs_ret = xTaskCreate(nvs_save_task, "nvs_save", 3072, NULL, 2, NULL);
        if (nvs_ret != pdPASS) {
            ESP_LOGW(TAG, "Failed to create NVS save task");
        }
    } else {
        ESP_LOGW(TAG, "Failed to create NVS save semaphore");
    }

    // Create auto-revert timer (one-shot, only when revert is configured)
    if (s_relay_driver.auto_revert_s > 0) {
        s_relay_driver.auto_revert_timer = xTimerCreate(
            "auto_revert",
            pdMS_TO_TICKS((uint32_t)s_relay_driver.auto_revert_s * 1000),
            pdFALSE,   // one-shot
            NULL,
            auto_revert_timer_callback
        );
        if (s_relay_driver.auto_revert_timer == NULL) {
            ESP_LOGW(TAG, "Failed to create auto-revert timer");
        } else {
            ESP_LOGI(TAG, "Auto-revert timer configured: %d seconds", s_relay_driver.auto_revert_s);
        }
    }

    ESP_LOGI(TAG, "Relay driver initialized");
    return (app_driver_handle_t)&s_relay_driver;
}

app_driver_handle_t app_driver_button_init(void)
{
    ESP_LOGI(TAG, "Initializing button driver on GPIO%d", TLED_BOOT_BUTTON_GPIO);

    button_handle_t handle = NULL;
    const button_config_t btn_cfg = {0};
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = TLED_BOOT_BUTTON_GPIO,
        .active_level = 0,
    };

    if (iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create button device");
        return NULL;
    }

    esp_err_t err = iot_button_register_cb(handle, BUTTON_PRESS_DOWN, NULL, app_driver_button_toggle_cb, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register button callback: %s", esp_err_to_name(err));
    }

    return (app_driver_handle_t)handle;
}