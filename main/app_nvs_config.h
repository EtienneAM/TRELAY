/*
 * TRELAY - Matter-over-Thread Relay Controller
 * NVS Configuration Management
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

// Power-on behavior options
typedef enum {
    POWER_ON_RESTORE = 0,   // Restore last state (default)
    POWER_ON_ON = 1,        // Always turn on
    POWER_ON_OFF = 2,       // Always stay off
} tled_power_on_t;

// Configuration structure
typedef struct {
    uint8_t gpio_pin;           // Relay GPIO pin
    uint8_t power_on_behavior;  // Power-on behavior (tled_power_on_t)
    char device_name[32];       // Custom device name
    uint8_t config_version;     // Config version for migration
    bool configured;            // True if config has been set
} tled_config_t;

// Default configuration values from Kconfig (menuconfig)
#include <sdkconfig.h>

#define TLED_DEFAULT_GPIO_PIN       CONFIG_TRELAY_GPIO_PIN
#define TLED_DEFAULT_DEVICE_NAME    "TRELAY"
#define TLED_DEFAULT_POWER_ON       POWER_ON_RESTORE
#define TLED_CONFIG_VERSION         1

/**
 * @brief Initialize the config module
 *
 * Loads config from NVS if available, otherwise uses defaults.
 *
 * @return ESP_OK on success
 */
esp_err_t tled_config_init(void);

/**
 * @brief Get pointer to current configuration (read-only)
 *
 * @return Pointer to config structure
 */
const tled_config_t* tled_config_get(void);

/**
 * @brief Get mutable pointer to current configuration
 *
 * Use for modifying config values. Call tled_config_save() to persist.
 *
 * @return Mutable pointer to config structure
 */
tled_config_t* tled_config_get_mutable(void);

/**
 * @brief Check if device has been configured
 *
 * @return true if config exists in NVS, false if first boot
 */
bool tled_config_is_configured(void);

/**
 * @brief Save current configuration to NVS
 *
 * @return ESP_OK on success
 */
esp_err_t tled_config_save(void);

/**
 * @brief Reset configuration to factory defaults
 *
 * Does not save to NVS until tled_config_save() is called.
 */
void tled_config_reset_to_defaults(void);

/**
 * @brief Validate a GPIO pin for relay output
 *
 * @param gpio_pin GPIO pin number
 * @return true if pin is valid
 */
bool tled_config_validate_gpio(uint8_t gpio_pin);

// Alias for reset function
#define tled_config_reset() tled_config_reset_to_defaults()