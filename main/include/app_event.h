#pragma once

#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(APP_EVENT);

typedef enum {
    APP_EVENT_SHUTDOWN,
    APP_EVENT_SDCARD_INIT,
    APP_EVENT_TICK, // Regular tick
    APP_EVENT_BLE_DEVICE, // New BLE device discovered
    APP_EVENT_WIFI_SCAN, // WiFi scanning
    APP_EVENT_WIFI_SCAN_DONE,
    APP_EVENT_WIFI_ACTIVE, // WiFi connected
} app_event_t;

extern esp_event_loop_handle_t app_event_loop;
