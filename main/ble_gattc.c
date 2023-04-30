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

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"

#include "gatt_profile.h"
#include "ble_cache.h"

static const char *TAG="ble_gattc";

#define PROFILE_A_APP_ID 0

// GAP NAME SERVICE
#define REMOTE_SERVICE_UUID        0x1800
// GAP NAME CHARACTERISTIC
#define REMOTE_NAME_CHAR_UUID      0x2A00
#define PROFILE_NUM      1
#define PROFILE_A_APP_ID 0
#define INVALID_HANDLE   0

static bool get_server = false;

/* Declare static functions */
void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

extern void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

static esp_bt_uuid_t remote_filter_service_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = REMOTE_SERVICE_UUID,},
};

static esp_bt_uuid_t remote_filter_char_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = REMOTE_NAME_CHAR_UUID,},
};

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};

/* One gatt-based profile one app_id and one gattc_if, this array will store the gattc_if returned by ESP_GATTS_REG_EVT */
struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gattc_cb = gattc_profile_event_handler,
        .gattc_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
        .open = false,
    },
};

static inline void reg() {
    ESP_LOGI(TAG, "REG_EVT");
    esp_err_t scan_ret = esp_ble_gap_set_scan_params(&ble_scan_params);
    if (scan_ret){
        ESP_LOGE(TAG, "set scan params error, error code = %x", scan_ret);
    }
}

static inline void connect(esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *p_data) {
    ESP_LOGI(TAG, "ESP_GATTC_CONNECT_EVT conn_id %d, if %d", p_data->connect.conn_id, gattc_if);
    gl_profile_tab[PROFILE_A_APP_ID].conn_id = p_data->connect.conn_id;
    memcpy(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, p_data->connect.remote_bda, sizeof(esp_bd_addr_t));
    ESP_LOGI(TAG, "REMOTE BDA:");
    esp_log_buffer_hex(TAG, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, sizeof(esp_bd_addr_t));
    esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req (gattc_if, p_data->connect.conn_id);
    if (mtu_ret){
        ESP_LOGE(TAG, "config MTU error, error code = %x", mtu_ret);
    }
}

// Handle a BLE Event which reports the value of a characteristic. This is _the_
// event we care about when trying to read values from a BLE connection
static inline void read(esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *p_data) {
    ESP_LOGI(TAG, "Read");
    if(p_data->read.status==0) {
        ESP_LOG_BUFFER_CHAR(TAG, p_data->read.value, p_data->read.value_len);
        ble_cache_update_name(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, p_data->read.value, p_data->read.value_len);
        ble_cache_connect();
    } else {
        ESP_LOGE(TAG, "Read error %d", p_data->read.status);
    }
}

static inline void service_found(esp_ble_gattc_cb_param_t *p_data) {
    ESP_LOGD(TAG, "SEARCH RES: conn_id = %x is primary service %d", p_data->search_res.conn_id, p_data->search_res.is_primary);
    ESP_LOGD(TAG, "start handle %d end handle %d current handle value %d", p_data->search_res.start_handle, p_data->search_res.end_handle, p_data->search_res.srvc_id.inst_id);
    if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 && p_data->search_res.srvc_id.uuid.uuid.uuid16 == REMOTE_SERVICE_UUID) {
        ESP_LOGD(TAG, "service found");
        get_server = true;
        gl_profile_tab[PROFILE_A_APP_ID].service_start_handle = p_data->search_res.start_handle;
        gl_profile_tab[PROFILE_A_APP_ID].service_end_handle = p_data->search_res.end_handle;
        ESP_LOGD(TAG, "UUID16: %x", p_data->search_res.srvc_id.uuid.uuid.uuid16);
    }
}

static inline void open(esp_ble_gattc_cb_param_t *p_data) {
    if (p_data->open.status != ESP_GATT_OK){
        ESP_LOGE(TAG, "open failed, status %d", p_data->open.status);
        return;
    }
    gl_profile_tab[PROFILE_A_APP_ID].open=true;
    get_server=false;
    ESP_LOGD(TAG, "open success");
}

// Get the device name from the GATT GAP service
static inline void get_name(esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *p_data) {
    uint16_t count = 0;
    esp_gatt_status_t status = esp_ble_gattc_get_attr_count( gattc_if,
                                                             p_data->search_cmpl.conn_id,
                                                             ESP_GATT_DB_CHARACTERISTIC,
                                                             gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                             gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                             INVALID_HANDLE,
                                                             &count);
    if (status != ESP_GATT_OK){
        ESP_LOGE(TAG, "esp_ble_gattc_get_attr_count error %d", status);
    }

    if (count > 0) {
        static esp_gattc_char_elem_t *char_elem_result   = NULL;

        char_elem_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
        if (!char_elem_result) {
            ESP_LOGE(TAG, "gattc no mem");
        } else {
            status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                     p_data->search_cmpl.conn_id,
                                                     gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                     gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                     remote_filter_char_uuid,
                                                     char_elem_result,
                                                     &count);
            if (status != ESP_GATT_OK) {
                ESP_LOGE(TAG, "esp_ble_gattc_get_char_by_uuid error");
            }
            ESP_LOGI(TAG, "Got name characteristic %d", count);
            /*  Every service have only one char in our 'ESP_GATTS_DEMO' demo, so we used first 'char_elem_result' */
            if (count > 0 && (char_elem_result[0].properties & ESP_GATT_CHAR_PROP_BIT_READ)){
                gl_profile_tab[PROFILE_A_APP_ID].char_handle = char_elem_result[0].char_handle;
                ESP_ERROR_CHECK(esp_ble_gattc_read_char(gattc_if, p_data->search_cmpl.conn_id, gl_profile_tab[PROFILE_A_APP_ID].char_handle, ESP_GATT_AUTH_REQ_NONE));
                //    esp_ble_gattc_register_for_notify (gattc_if, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, char_elem_result[0].char_handle);
            }
        }
        /* free char_elem_result */
        free(char_elem_result);
    } else {
        ESP_LOGE(TAG, "no char found");
    }
}

// Handle the service search complete event.
static inline void search_cmpl(esp_gatt_if_t gattc_if,esp_ble_gattc_cb_param_t *p_data) {
    if (p_data->search_cmpl.status != ESP_GATT_OK){
        ESP_LOGE(TAG, "search service failed, error status = %x", p_data->search_cmpl.status);
        return;
    }

    if(p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_REMOTE_DEVICE) {
        ESP_LOGD(TAG, "Get service information from remote device");
    } else if (p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_NVS_FLASH) {
        ESP_LOGD(TAG, "Get service information from flash");
    } else {
        ESP_LOGD(TAG, "unknown service source");
    }
    ESP_LOGD(TAG, "ESP_GATTC_SEARCH_CMPL_EVT");

    // If we found the service, invoke it to get the device name
    if (get_server) get_name(gattc_if, p_data);
}

static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    ACTION();
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

    switch (event) {
    case ESP_GATTC_REG_EVT: reg(); break;
    case ESP_GATTC_CONNECT_EVT: connect(gattc_if, p_data); break;
    case ESP_GATTC_READ_CHAR_EVT: read(gattc_if, p_data); break;
    case ESP_GATTC_OPEN_EVT: open(p_data); break;
    case ESP_GATTC_DIS_SRVC_CMPL_EVT:
        if (param->dis_srvc_cmpl.status != ESP_GATT_OK){
            ESP_LOGE(TAG, "discover service failed, status %d", param->dis_srvc_cmpl.status);
            break;
        }
        ESP_LOGI(TAG, "discover service complete conn_id %d", param->dis_srvc_cmpl.conn_id);
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, &remote_filter_service_uuid);
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        if (param->cfg_mtu.status != ESP_GATT_OK){
            ESP_LOGE(TAG,"config mtu failed, error status = %x", param->cfg_mtu.status);
        }
        ESP_LOGD(TAG, "ESP_GATTC_CFG_MTU_EVT, Status %d, MTU %d, conn_id %d", param->cfg_mtu.status, param->cfg_mtu.mtu, param->cfg_mtu.conn_id);
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, &remote_filter_service_uuid);
        break;
    case ESP_GATTC_SEARCH_RES_EVT: service_found(p_data); break;
    case ESP_GATTC_SEARCH_CMPL_EVT: search_cmpl(gattc_if, p_data); break;
    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGI(TAG, "ESP_GATTC_DISCONNECT_EVT, reason = %d", p_data->disconnect.reason);
        ble_cache_connect_failed();
        gl_profile_tab[PROFILE_A_APP_ID].open=false;
        ble_cache_connect_from_unconnected();
        break;
    default:
        break;
    }
}

// This is the main BLE GATT Client event handler. It dispatches to the various Application Profiles
void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    /* If event is register event, store the gattc_if for each profile */
    if (event == ESP_GATTC_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gattc_if = gattc_if;
        } else {
            ESP_LOGI(TAG, "reg app failed, app_id %04x, status %d",
                     param->reg.app_id,
                     param->reg.status);
            return;
        }
    }

    /* If the gattc_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gattc_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                gattc_if == gl_profile_tab[idx].gattc_if) {
                if (gl_profile_tab[idx].gattc_cb) {
                    gl_profile_tab[idx].gattc_cb(event, gattc_if, param);
                }
            }
        }
    } while (0); // FIXME: hmm. why is this in a while (false) loop?
}
