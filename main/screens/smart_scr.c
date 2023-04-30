/* Esptouch example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lvgl.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "scr.h"
#include "idle.h"

#ifdef STRUCT_MAGIC
#define SMART_SCR_MAGIC STRUCT_MAKE_MAGIC(0xD7)
#endif

// TODO: provide a way to cancel here
// TODO: handle sleep correctly

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t s_wifi_event_group;

typedef struct smart_scr {
    panel_t scr; // Common screen state
    lv_obj_t *lvnd_instruct;
    esp_event_handler_instance_t ip_handler;
} smart_scr_t;

static void smart_unreg_handlers(smart_scr_t *smart);

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const char *TAG = "smartconfig";

extern panel_t *main_scr_init();

static void smartconfig_task(void * parm);

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    ACTION();
    ESP_LOGI(TAG, "wifi_event");
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGE(TAG, "Unexpected start event");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };
        uint8_t rvd_data[33] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password); // TODO: DON'T DO THIS!
        if (evt->type == SC_TYPE_ESPTOUCH_V2) {
            ESP_ERROR_CHECK( esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)) );
            ESP_LOGI(TAG, "RVD_DATA:");
            for (int i=0; i<33; i++) {
                printf("%02x ", rvd_data[i]);
            }
            printf("\n");
        }

        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        esp_wifi_connect();
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

static void initialise_wifi(void)
{
    ACTION();
    s_wifi_event_group = xEventGroupCreate();

    // Default event loop is already started
    // FIXME: Deregister these on success
    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );

    sidebar_wifi_state(gui->sidebar, WIFI_SCANNING);
    xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 3, NULL);
}

esp_err_t smart_sleep(panel_t *data) {
    smart_scr_t *smart = (smart_scr_t *)data;
    STRUCT_CHECK_MAGIC(smart, SMART_SCR_MAGIC, TAG, "sleep");

    ESP_LOGI(TAG, "sleep");
    if(smart->scr.handlers_installed) {
        smart_unreg_handlers(smart);
    }

    return ESP_OK;
}

static void smartconfig_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
            active_scr = main_scr_init();
            gui_set_panel(gui, active_scr);
            continue;
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            break;
        }
    }
    vTaskDelete(NULL);
}

static void smart_free(panel_t *data) {
    smart_scr_t *smart = (smart_scr_t *)data;
    STRUCT_CHECK_MAGIC(smart, SMART_SCR_MAGIC, TAG, "free");
    ESP_LOGI(TAG,"Free");

    if(smart->scr.handlers_installed) {
        smart_unreg_handlers(smart);
    }
    lv_obj_del(smart->scr.lv_root); // Free of this object frees children too

    STRUCT_INVALIDATE(smart);
    free(smart);

    ESP_LOGI(TAG,"Free done");
}

// Handle a selection on the smart menu
static void smart_menu_click_cb(void *arg, void *data)
{
    ACTION();
    assert(data);
    smart_scr_t *smart = (smart_scr_t *)data;
    ESP_LOGI(TAG,"Click");
    STRUCT_CHECK_MAGIC(smart, SMART_SCR_MAGIC, TAG, "click");

    LOCK_GUI;

    // Degregister our event handlers
    ESP_LOGI(TAG,"Deregister");

    if(smart->scr.handlers_installed) {
        smart_unreg_handlers(smart);
    }

    // FIXME: Need to not do this if SMART is still running
    active_scr = main_scr_init();
    gui_set_panel(gui, active_scr);

    UNLOCK_GUI;

}

static void smart_menu_knob_left_cb(void *arg, void *data)
{
    ACTION();
}

static void smart_menu_knob_right_cb(void *arg, void *data)
{
    ACTION();
}

static void smart_menu_knob_event(void *arg, void *data) {
    ACTION();
    knob_event_t event=iot_knob_get_event((knob_handle_t)arg);
    ESP_LOGI(TAG,"Got event %d", event);
}

static void smart_reg_handlers(smart_scr_t *smart) {
    STRUCT_CHECK_MAGIC(smart, SMART_SCR_MAGIC, TAG, "reg");

    // Register the event handlers for the knob for this screen
    ESP_ERROR_CHECK(iot_button_register_cb(tembed->dial.btn, BUTTON_SINGLE_CLICK, smart_menu_click_cb, smart));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_LEFT, smart_menu_knob_left_cb, smart));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_RIGHT, smart_menu_knob_right_cb, smart));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_ZERO, smart_menu_knob_event, smart));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_H_LIM, smart_menu_knob_event, smart));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_L_LIM, smart_menu_knob_event, smart));

    smart->scr.handlers_installed = true;
}

// Unregister all the pending event handlers for this screen
static void smart_unreg_handlers(smart_scr_t *smart) {
    STRUCT_CHECK_MAGIC(smart, SMART_SCR_MAGIC, TAG, "unreg");
    ESP_ERROR_CHECK(iot_button_unregister_cb(tembed->dial.btn, BUTTON_SINGLE_CLICK));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_LEFT));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_RIGHT));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_ZERO));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_H_LIM));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_L_LIM));

    smart->scr.handlers_installed = false;
}

static void smart_lv_init(panel_t *panel, lv_obj_t *parent) {
    smart_scr_t * smart = (smart_scr_t *)panel;
    STRUCT_CHECK_MAGIC(smart, SMART_SCR_MAGIC, TAG, "click");
    LOCK_GUI;

    gui_set_menu_title((char *)"Smart Menu");

    // Smart content panel
    smart->scr.lv_root  = lv_obj_create(parent);
    lv_obj_t *content= smart->scr.lv_root;
    lv_obj_center(content);
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_height(content, lv_pct(100));
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

    smart->lvnd_instruct = lv_label_create(content);
    lv_obj_set_width(smart->lvnd_instruct, lv_pct(100));
    lv_obj_set_flex_grow(smart->lvnd_instruct, 1);
    lv_label_set_text_static(smart->lvnd_instruct, "Use smartphone app 'ESPTOUCH' to configure");

    smart_reg_handlers(smart);

    UNLOCK_GUI;
}


// Select and display the smart menu screen
panel_t *smart_scr_init() {
    ESP_LOGI(TAG,"Init");

    smart_scr_t *smart = calloc(1, sizeof(smart_scr_t));
    STRUCT_INIT_MAGIC(smart, SMART_SCR_MAGIC);
    smart->scr.free = smart_free;
    smart->scr.goto_sleep = smart_sleep;
    smart->scr.create_content = smart_lv_init;

    initialise_wifi();

    ESP_LOGI(TAG,"Done");
    return (panel_t *)smart;
}
