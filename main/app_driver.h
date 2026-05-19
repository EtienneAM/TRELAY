/*
 * TRELAY - Matter-over-Thread Relay Controller
 * Relay Driver Interface
 */

#pragma once

#include <esp_err.h>
#include <esp_matter.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle for the driver
 */
typedef void *app_driver_handle_t;

/**
 * @brief Initialize the relay driver
 *
 * @return Driver handle, or NULL on failure
 */
app_driver_handle_t app_driver_light_init(void);

/**
 * @brief Initialize the button driver
 *
 * @return Driver handle, or NULL on failure
 */
app_driver_handle_t app_driver_button_init(void);

/**
 * @brief Set the relay power state
 *
 * @param handle Driver handle
 * @param power true = on (relay energized), false = off
 * @return ESP_OK on success
 */
esp_err_t app_driver_light_set_power(app_driver_handle_t handle, bool power);

/**
 * @brief Get the current relay power state
 *
 * @param handle Driver handle
 * @return true if relay is on, false if off
 */
bool app_driver_light_get_power(app_driver_handle_t handle);

/**
 * @brief Handle Matter attribute updates
 *
 * Called by the Matter stack when attributes change.
 *
 * @param driver_handle Driver handle
 * @param endpoint_id Matter endpoint ID
 * @param cluster_id Matter cluster ID
 * @param attribute_id Matter attribute ID
 * @param val New attribute value
 * @return ESP_OK on success
 */
esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle,
                                       uint16_t endpoint_id,
                                       uint32_t cluster_id,
                                       uint32_t attribute_id,
                                       esp_matter_attr_val_t *val);

/**
 * @brief Apply default settings from NVS to the relay hardware
 *
 * @param endpoint_id Matter endpoint ID
 * @return ESP_OK on success
 */
esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id);

#ifdef __cplusplus
}
#endif
