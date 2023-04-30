#pragma once

/*
 * ESP32 T-Embed Application Shell
 *
 * This code is a demonstration of the capabilities of the ESP32 T-Embed
 *
 * It is based on and includes code from the ESP-IDF v5.0, ESP examples,
 * T-Embed examples from Lilygo and the LVGL tutorial and examples.
 *
 * "We stand on the shoulders of Giants"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "idle.h"
#include "nvs_flash.h"

// Bluetooth support
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"

#define BLE_CACHE_MAX 100 // Only scan 100 BT devices

struct ble_cache_entry {
    esp_bd_addr_t bda;
    char *name;
    esp_ble_addr_type_t bda_type;
    int visible:1; // Did we see this device in the latest scan?
    int connecting:1; // Are we currently attempting to connect to this device?
    int connected:1; // Is this device currently connected?
    int failed:1; // Did connection to this device fail?
    int name_failed:1; // Did name lookup fail?
};

extern struct ble_cache_entry ble_cache[BLE_CACHE_MAX];
extern int connecting;
extern int device_list_idx;
extern void ble_cache_start_scan();
extern void ble_cache_add(const esp_ble_gap_cb_param_t *scan_result, const uint8_t *adv_name, uint8_t adv_name_len);
extern int ble_cache_get_size();
extern void ble_cache_purge();
extern void ble_cache_dump();
extern void ble_cache_connect();
extern void ble_cache_connect_from_unconnected();
extern void ble_cache_connect_failed();
extern void ble_cache_update_name(esp_bd_addr_t remote_bda, uint8_t* name, uint8_t name_len);
