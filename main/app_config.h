/*
 * TRELAY - Matter-over-Thread Relay Controller
 * Configuration constants
 */

#pragma once

#include <sdkconfig.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include "esp_openthread_types.h"
#endif

// Hardware Configuration - DFRobot Beetle ESP32-C6
#define TLED_BOOT_BUTTON_GPIO   9       // Boot button

// Relay default GPIO (from Kconfig)
#define TRELAY_DEFAULT_GPIO_PIN CONFIG_TRELAY_GPIO_PIN

// Matter default values
#define TLED_DEFAULT_POWER      false

// Device identification
#define TLED_DEVICE_NAME        "TRELAY"
#define TLED_VENDOR_NAME        "TLED Project"

// OpenThread configuration for ESP32-C6 native radio
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()                                           \
    {                                                                                   \
        .radio_mode = RADIO_MODE_NATIVE,                                                \
    }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()                                            \
    {                                                                                   \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,                              \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                                            \
    {                                                                                   \
        .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, \
    }
#endif
