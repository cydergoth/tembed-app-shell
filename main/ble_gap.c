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
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#include "ble_cache.h"
#include "gatt_profile.h"

static const char *TAG="ble_gap";

// Bluetooth Generic Access Profile (GAP)
// These functions handle the low level BLE connection: scan, connect etc.

// Bluetooth GAP profile definitions for the name lookup service
#define REMOTE_SERVICE_UUID        0x1800
#define REMOTE_NOTIFY_CHAR_UUID    0xFF01

// Scanning parameters have been setup, start the scan
static inline void scan_param_set_complete() {
    // Setup the name cache for a scan
    ble_cache_start_scan();
    //the unit of the duration is seconds
    uint32_t duration = 30; // Use zero here for continuous scanning
    esp_ble_gap_start_scanning(duration);
}

// Scan start complete event to indicate scan start successfully or failed
static inline void scan_start_complete(esp_ble_gap_cb_param_t *param) {
    if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "scan start failed, error status = %x", param->scan_start_cmpl.status);
    } else {
        ESP_LOGD(TAG, "scan start success");
    }
}

int connect_in_progress=false;

// Get the name from the advanced parameters if available
// Return: NULL if name is not found, set adv_name_len = 0
static inline uint8_t* get_name(const esp_ble_gap_cb_param_t *scan_result, uint8_t *adv_name_len) {
    uint8_t *adv_name=NULL;
    ESP_LOGD(TAG, "searched Adv Data Len %d, Scan Response Len %d", scan_result->scan_rst.adv_data_len, scan_result->scan_rst.scan_rsp_len);
    adv_name = esp_ble_resolve_adv_data((uint8_t *)scan_result->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, adv_name_len);
    if(esp_log_level_get(TAG) > ESP_LOG_DEBUG) {
        ESP_LOGD(TAG, "searched Device Name Len %d", *adv_name_len);
        esp_log_buffer_char(TAG, adv_name, *adv_name_len);
        ESP_LOGD(TAG, "\n");
    }
    return adv_name;
}

// Handle the event for a device seen in the scan
// Note: Devices discovered here may be duplicates
static inline void discover_device(const esp_ble_gap_cb_param_t *scan_result) {
    uint8_t *adv_name = NULL;
    uint8_t adv_name_len = 0;

    // Decode the new device information
    if (esp_log_level_get(TAG) >= ESP_LOG_DEBUG ) {
        esp_log_buffer_hex(TAG, scan_result->scan_rst.bda, 6);
    }

    adv_name = get_name(scan_result, &adv_name_len);

    if (adv_name != NULL) {
        ble_cache_add(scan_result, adv_name, adv_name_len);
    } else {
        ble_cache_add(scan_result, (const uint8_t *)"", 0);
    }

    ESP_LOGD(TAG, "Discovered %d",device_list_idx);
}

// GAP Layer has discovered a device
static inline void scan_result(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;

    switch (scan_result->scan_rst.search_evt) {
    case ESP_GAP_SEARCH_INQ_RES_EVT: discover_device(scan_result); break;
    case ESP_GAP_SEARCH_INQ_CMPL_EVT:
    case ESP_GAP_SEARCH_SEARCH_CANCEL_CMPL_EVT:
        ESP_LOGI(TAG, "BLE Scan complete or cancelled");
        ble_cache_purge();
        // The entire scan has finished
        ble_cache_dump();
        ble_cache_connect();
        break;
    default:
        break;
    }
}

// Bluetooth GAP callback which handles GAP state events
void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    ACTION();
    switch (event) {

    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: scan_param_set_complete(); break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT: scan_start_complete(param); break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: scan_result(event, param); break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(TAG, "scan stop failed, error status = %x", param->scan_stop_cmpl.status);
            break;
        }
        ESP_LOGI(TAG, "stop scan successfully");
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(TAG, "adv stop failed, error status = %x", param->adv_stop_cmpl.status);
            break;
        }
        ESP_LOGI(TAG, "stop adv successfully");
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                 param->update_conn_params.status,
                 param->update_conn_params.min_int,
                 param->update_conn_params.max_int,
                 param->update_conn_params.conn_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}
