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
#include "esp_gattc_api.h"

#include "gatt_profile.h"
#include "ble_cache.h"

static const char* TAG="ble_cache";

// This list is expected to be short so a linear search is acceptable
struct ble_cache_entry ble_cache[BLE_CACHE_MAX];
int device_list_idx;

void ble_cache_start_scan() {
    // Reset all the cached devices to unseen
    for(int idx=0;idx<device_list_idx;idx++) {
        ble_cache[idx].visible=false;
    }
}

void ble_cache_add(const esp_ble_gap_cb_param_t *scan_result, const uint8_t *adv_name, uint8_t adv_name_len) {
    for(int idx=0;idx<device_list_idx;idx++) {
        if(memcmp(&ble_cache[idx].bda, scan_result->scan_rst.bda, 6)==0) {
            // Already discovered this one
            ble_cache[idx].visible=true;
            return;
        }
    }
    if(device_list_idx == BLE_CACHE_MAX) return; // Cache full
    memcpy(&ble_cache[device_list_idx].bda, scan_result->scan_rst.bda, 6);
    ble_cache[device_list_idx].visible=true;
    ble_cache[device_list_idx].connecting=false;
    ble_cache[device_list_idx].connected=false;
    ble_cache[device_list_idx].failed=false;
    ble_cache[device_list_idx].name_failed=false;
    if(adv_name_len>0) {
        ble_cache[device_list_idx].name=strndup((const char *)adv_name, adv_name_len);
        if(ble_cache[device_list_idx].name==NULL) {
            ESP_LOGE(TAG,"BLE Name cache allocation failed");
            ble_cache[device_list_idx].name_failed=true;
        }
    } else {
        // Device did not send name in advertised advanced data
        ble_cache[device_list_idx].name=NULL;
    }
    device_list_idx++;
}

extern void ble_cache_update_name(esp_bd_addr_t remote_bda, uint8_t* name, uint8_t name_len) {
    for(int idx=0;idx<device_list_idx;idx++) {
        if(memcmp(&ble_cache[idx].bda, remote_bda, 6)==0) {
            ble_cache[device_list_idx].connecting=false;
            if(ble_cache[device_list_idx].name) {
                free(ble_cache[device_list_idx].name);
                ble_cache[device_list_idx].name=NULL;
            }
            if(name_len>0) {
                ble_cache[device_list_idx].name=strndup((const char *)name, name_len);
                if(ble_cache[device_list_idx].name==NULL) {
                    ESP_LOGE(TAG,"BLE Name cache allocation failed");
                    ble_cache[device_list_idx].name_failed=true;
                }
            }
            return;
        }
    }
}

int ble_cache_get_size() {
    return device_list_idx;
}

// Compact the cache by removing obsolete entries
void ble_cache_purge() {
    for(int idx=0;idx<device_list_idx;idx++) {
        if(ble_cache[idx].visible) continue;
        if(idx<device_list_idx-1) {
            // Copy the rest of the list down over this obsolete entry
            memcpy(&ble_cache[idx], &ble_cache[idx+1], sizeof(struct ble_cache_entry) * (device_list_idx - idx));
        }
        device_list_idx--;
    }
}

void ble_cache_dump() {
    for(int idx=0;idx<device_list_idx;idx++) {
        esp_log_buffer_hex(TAG, ble_cache[idx].bda, 6);
        if(ble_cache[idx].name!=NULL) {
            ESP_LOGI(TAG, "%s", ble_cache[idx].name);
        } else {
            ESP_LOGI(TAG, "");
        }
    }
}

void ble_cache_connect_from_unconnected() {
    // Attempt to connect to the next BLE device which hasn't already failed to connect

    ESP_LOGI(TAG,"Connecting");
    for(int idx=0;idx<device_list_idx;idx++) {
        //esp_log_buffer_hex(TAG, ble_cache[idx].bda, 6);
        if(ble_cache[idx].failed || ble_cache[idx].name_failed) continue;
        if(ble_cache[idx].connecting) return; // Already trying to connect
        ESP_LOGI(TAG,"Connecting to %d", idx);
        esp_log_buffer_hex(TAG, ble_cache[idx].bda, 6);
        ble_cache[idx].connecting=true;
        esp_ble_gattc_open(gl_profile_tab[PROFILE_A_APP_ID].gattc_if, ble_cache[idx].bda, ble_cache[idx].bda_type, true);
        break;
    }
}

void ble_cache_connect() {
    if(gl_profile_tab[PROFILE_A_APP_ID].open) {
        // Disconnect any existing connection to our currently selected target device
        esp_ble_gap_disconnect(gl_profile_tab[PROFILE_A_APP_ID].remote_bda);
    } else {
        ble_cache_connect_from_unconnected();
    }
}

void ble_cache_connect_failed() {
    // Attempt to connect to the next BLE device which hasn't already failed to connect
    ESP_LOGI(TAG,"Connecting");
    for(int idx=0;idx<device_list_idx;idx++) {
        if(!ble_cache[idx].connecting) continue; // Find the connection we were attempting
        // Mark the connection as no longer connecting
        ble_cache[idx].connecting=false;
        // Check to see if we got the name before the connection failed
        if(ble_cache[idx].name == NULL) {
            // No name, so mark this entry as failed
            ble_cache[idx].name_failed=true;
        }
        if(gl_profile_tab[PROFILE_A_APP_ID].open) continue; // Open was successful
        // Open failed, so mark this device as failed
        ESP_LOGI(TAG,"Connection to %d failed", idx);
        esp_log_buffer_hex(TAG, ble_cache[idx].bda, 6);
        // Mark this entry as failed
        ble_cache[idx].failed=true;
        break;
    }
}
