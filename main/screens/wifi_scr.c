#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "magic.h"
#include "ctype.h"

#include "iot_button.h"
#include "iot_knob.h"
#include "lvgl.h"
#include "tembed.h"
#include "esp_wifi.h"
#include "scr.h"
#include "idle.h"
#include "app_event.h"

#ifdef CONFIG_TEMBED_INIT_WIFI

#define WIFI_PW_LEN 64

// FIXME: There is a race condition if this is called while the WiFi is attempting to connect!

// Identifiers for this screen
static const char *TAG="wifi_scr";
#ifdef STRUCT_MAGIC
#define WIFI_SCR_MAGIC STRUCT_MAKE_MAGIC(0xF1)
#endif

// First two characters are placeholders for special commands like NEW_LINE and BACKSPACE
#define CMD_NEWLINE 0
#define CMD_BACKSPACE 1
const char *valid_chars="XXabcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_+=~`[]{}|\\:;\"'<>,.?/";

extern panel_t *main_scr_init();
extern wifi_ap_record_t *wifi_scan(uint16_t *ap_count_out);


static void wifi_ssid_click_cb(void *arg, void *data);

typedef enum int16_t {
    SCAN = 0,
    SELECT_AP,
    ENTER_PW
} wifi_state;

typedef struct wifi_scr {
    panel_t scr;
    lv_obj_t *ap_label;
    lv_obj_t *lvnd_password;
    lv_style_t menu_style; // TODO: Move this to static data
    uint16_t ap_count;
    int16_t ap_index;
    wifi_ap_record_t *aps;
    wifi_state state;
    uint8_t current_char;
    uint8_t current_pw_index;
    char password[WIFI_PW_LEN];
} wifi_scr_t;

static void wifi_unreg_handlers(wifi_scr_t *wifi);
static void wifi_reg_handlers(wifi_scr_t *wifi);

static void wifi_free(panel_t *data) {
    ESP_LOGI(TAG, "Free");

    wifi_scr_t *wifi = (wifi_scr_t *)data;
    STRUCT_CHECK_MAGIC(wifi, WIFI_SCR_MAGIC, TAG, "free");

    if(wifi->scr.handlers_installed) {
        wifi_unreg_handlers(wifi);
    }
    lv_obj_del(wifi->scr.lv_root);
    lv_style_reset(&wifi->menu_style);

    if(wifi->ap_count) {
       free(wifi->aps);
    }

    STRUCT_INVALIDATE(wifi);
    free(wifi);

    ESP_LOGI(TAG, "Free done");
}

void wifi_connect_task(void *pvParameters) {
    ESP_ERROR_CHECK(esp_wifi_connect());
    /* Tasks must not attempt to return from their implementing
       function or otherwise exit.  In newer FreeRTOS port
       attempting to do so will result in an configASSERT() being
       called if it is defined.  If it is necessary for a task to
       exit then have the task call vTaskDelete( NULL ) to ensure
       its exit is clean. */
    vTaskDelete( NULL );
}

static void display_pw(wifi_scr_t *wifi) {
    STRUCT_CHECK_MAGIC(wifi, WIFI_SCR_MAGIC, TAG, "display_pw");
    switch(wifi->state) {
    case SCAN: break;
    case SELECT_AP: lv_label_set_text(wifi->lvnd_password, "Select AP"); break;
    case ENTER_PW: {
        char pw_buffer[WIFI_PW_LEN + 3]; // Extra 2 for the unicode symbols
        memset(pw_buffer, 0, sizeof(pw_buffer));
        strncpy(pw_buffer, wifi->password, WIFI_PW_LEN);
        switch(wifi->current_char) {
        case CMD_NEWLINE: strcpy(&pw_buffer[wifi->current_pw_index], LV_SYMBOL_NEW_LINE); break;
        case CMD_BACKSPACE: strcpy(&pw_buffer[wifi->current_pw_index], LV_SYMBOL_BACKSPACE); break;
        default: {
            // Add pending character
            pw_buffer[wifi->current_pw_index]=valid_chars[wifi->current_char];
            break;
        }
        }
        lv_label_set_text(wifi->lvnd_password, pw_buffer);
        break;
    }
    }
}

// Handle a selection of a wifi ssid
static void wifi_ssid_click_cb(void *arg, void *data)
{
    ACTION();

    wifi_scr_t * wifi = (wifi_scr_t *)data;
    STRUCT_CHECK_MAGIC(wifi, WIFI_SCR_MAGIC, TAG, "click");

    LOCK_GUI;

    switch(wifi->state) {
    case ENTER_PW: {
        switch(wifi->current_char) {
        case CMD_NEWLINE: {
            // Enter (complete)
            if(wifi->scr.handlers_installed) {
                wifi_unreg_handlers(wifi);
            }

            wifi_config_t conf;
            memset(&conf,0,sizeof(conf));
            memcpy(conf.sta.ssid,wifi->aps[wifi->ap_index].ssid,sizeof(conf.sta.ssid));
            strncpy((char*)conf.sta.password,wifi->password, sizeof(conf.sta.password));
            conf.sta.scan_method = WIFI_FAST_SCAN;
            conf.sta.channel = wifi->aps[wifi->ap_index].primary;
            conf.sta.pmf_cfg.required = false;
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &conf));

            xTaskCreate(wifi_connect_task,"wifi_connect", 4096, NULL, 1, NULL);

            active_scr = main_scr_init();
            gui_set_panel(gui, active_scr);
            break;
        }
        case CMD_BACKSPACE: {
            // Backspace
            if(wifi->current_pw_index > 0 ) {
                wifi->password[wifi->current_pw_index] = 0;
                wifi->current_pw_index --;
            }
            display_pw(wifi);
            break;
        }
        default:
            {
                // Append character and advance
                if(wifi->current_pw_index < WIFI_PW_LEN - 2 ) {
                    wifi->password[wifi->current_pw_index]=valid_chars[wifi->current_char];
                    wifi->current_pw_index++;
                }
            }
        display_pw(wifi);
        break;
    }
        break;
    }
    case SCAN: wifi->state = SELECT_AP; display_pw(wifi); break;
    case SELECT_AP: wifi->state = ENTER_PW; display_pw(wifi); break;
    }

    UNLOCK_GUI;

}

static void wifi_ssid_knob_left_cb(void *arg, void *data)
{
    ACTION();
    wifi_scr_t * wifi = (wifi_scr_t *)data;
#ifdef STRUCT_MAGIC
#ifdef BAD_KNOB_USR_DATA
    if(wifi->scr.magic!=WIFI_SCR_MAGIC) {
        // Knob library is buggy
        void** usr_data = (void**)data;
        wifi = (wifi_scr_t *)usr_data[KNOB_LEFT];
    }
#endif
    STRUCT_CHECK_MAGIC(wifi, WIFI_SCR_MAGIC, TAG, "left");
#endif

    LOCK_GUI;

    switch(wifi->state) {
    case SCAN: break;
    case SELECT_AP: {
        wifi->ap_index--;
        if(wifi->ap_index < 0) wifi->ap_index=wifi->ap_count - 1;
        lv_label_set_text(wifi->ap_label, (const char *)wifi->aps[wifi->ap_index].ssid);
        break;
    }
    case ENTER_PW: {
        if(wifi->current_char == 0) {
            wifi->current_char = strlen(valid_chars) - 1;
        } else {
            wifi->current_char--;
        }
        display_pw(wifi);
        break;
    }
    }

    UNLOCK_GUI;
}

static void wifi_ssid_knob_right_cb(void *arg, void *data)
{
    ACTION();

    wifi_scr_t * wifi = (wifi_scr_t *)data;
#ifdef STRUCT_MAGIC
#ifdef BAD_KNOB_USR_DATA
    if(wifi->scr.magic!=WIFI_SCR_MAGIC) {
        // Knob library is buggy
        void** usr_data = (void**)data;
        wifi = (wifi_scr_t *)usr_data[KNOB_RIGHT];
    }
#endif
    STRUCT_CHECK_MAGIC(wifi, WIFI_SCR_MAGIC, TAG, "right");
#endif

    LOCK_GUI;

    switch(wifi->state) {
    case SCAN: break;
    case SELECT_AP: {
        wifi->ap_index++;
        if(wifi->ap_index >= wifi->ap_count) wifi->ap_index=0;
        lv_label_set_text(wifi->ap_label, (const char *)wifi->aps[wifi->ap_index].ssid);
        break;
    }
    case ENTER_PW: {
        if(wifi->current_char == (strlen(valid_chars)-1)) {
            wifi->current_char = 0;
        } else {
            wifi->current_char++;
        }
        display_pw(wifi);
        break;
    }
    }

    UNLOCK_GUI;
}

void wifi_scan_task( void *pvParameters )
{
    ACTION();

    ESP_LOGI(TAG,"Scanning task");

    wifi_scr_t * wifi = (wifi_scr_t *)pvParameters;
    STRUCT_CHECK_MAGIC(wifi, WIFI_SCR_MAGIC, TAG, "scan");

    if(wifi->ap_count) {
        wifi->ap_count=0;
        free(wifi->aps);
    }

    ESP_ERROR_CHECK(esp_event_post_to(app_event_loop, APP_EVENT, APP_EVENT_WIFI_SCAN, NULL, 0, (TickType_t)100));
    wifi->aps=wifi_scan(&wifi->ap_count);
    ESP_ERROR_CHECK(esp_event_post_to(app_event_loop, APP_EVENT, APP_EVENT_WIFI_SCAN_DONE, NULL, 0, (TickType_t)100));

    if(wifi->ap_count) {
        lv_label_set_text(wifi->ap_label, (const char *)wifi->aps[0].ssid);
    } else {
        lv_label_set_text_static(wifi->ap_label,"NO WIFI FOUND!");
    }

    wifi->state=SELECT_AP;
    display_pw(wifi);

    wifi_reg_handlers(wifi);

    /* Tasks must not attempt to return from their implementing
       function or otherwise exit.  In newer FreeRTOS port
       attempting to do so will result in an configASSERT() being
       called if it is defined.  If it is necessary for a task to
       exit then have the task call vTaskDelete( NULL ) to ensure
       its exit is clean. */
    vTaskDelete( NULL );
}

void wifi_reg_handlers(wifi_scr_t * wifi) {
    STRUCT_CHECK_MAGIC(wifi, WIFI_SCR_MAGIC, TAG, "reg");
    // Register the event handlers for the knob for this screen
    ESP_ERROR_CHECK(iot_button_register_cb(tembed->dial.btn, BUTTON_SINGLE_CLICK, wifi_ssid_click_cb, wifi));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_LEFT, wifi_ssid_knob_left_cb, wifi));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_RIGHT, wifi_ssid_knob_right_cb, wifi));
    wifi->scr.handlers_installed=true;
}

void wifi_unreg_handlers(wifi_scr_t * wifi) {
    STRUCT_CHECK_MAGIC(wifi, WIFI_SCR_MAGIC, TAG, "unreg");
    ESP_ERROR_CHECK(iot_button_unregister_cb(tembed->dial.btn,BUTTON_SINGLE_CLICK));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_LEFT));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_RIGHT));
    wifi->scr.handlers_installed=false;
}

esp_err_t wifi_sleep(panel_t *data) {
    ESP_LOGI(TAG, "Wifi sleep");

    wifi_scr_t * wifi = (wifi_scr_t *)data;
    STRUCT_CHECK_MAGIC(wifi, WIFI_SCR_MAGIC, TAG, "sleep");

    if(wifi->scr.handlers_installed) {
        wifi_unreg_handlers(wifi);
    }

    return ESP_OK;
}

static void wifi_lv_init(panel_t *panel, lv_obj_t *parent) {
    ESP_LOGI(TAG, "lv_init");
    // Top level object for this screen
    // Uses a column flow layout

    wifi_scr_t * wifi = (wifi_scr_t *)panel;
    STRUCT_CHECK_MAGIC(wifi, WIFI_SCR_MAGIC, TAG, "lv_init");

    LOCK_GUI;

    gui_set_menu_title((char *)LV_SYMBOL_WIFI " WiFi");

    lv_style_init(&wifi->menu_style);
    lv_style_set_height(&wifi->menu_style, 40);
    lv_style_set_text_color(&wifi->menu_style, lv_color_white());
    lv_style_set_text_align(&wifi->menu_style, LV_TEXT_ALIGN_LEFT);
    lv_style_set_text_font(&wifi->menu_style, &lv_font_montserrat_22);

    wifi->scr.lv_root = lv_obj_create(parent);

    lv_obj_t *col = wifi->scr.lv_root;
    lv_obj_set_size(col, lv_pct(100), lv_pct(100));
    lv_obj_set_layout(col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    wifi->ap_label = lv_label_create(col);
    lv_obj_set_width(col, lv_pct(100));
    lv_label_set_long_mode(wifi->ap_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(wifi->ap_label, "Scanning ...");
    lv_obj_add_style(wifi->ap_label, &wifi->menu_style, LV_PART_MAIN);
    lv_obj_set_style_pad_left(wifi->ap_label, 5, LV_PART_MAIN);

    wifi->lvnd_password = lv_label_create(col);
    lv_obj_set_width(col, lv_pct(100));
    lv_label_set_long_mode(wifi->lvnd_password, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text_static(wifi->lvnd_password, "");
    lv_obj_add_style(wifi->lvnd_password, &wifi->menu_style, LV_PART_MAIN);
    lv_obj_set_style_pad_left(wifi->lvnd_password, 5, LV_PART_MAIN);

    ESP_LOGI(TAG,"Scanning");
    wifi->state=SCAN;
    xTaskCreate(wifi_scan_task,"wifi_scan", 3278, wifi, 1, NULL);

    UNLOCK_GUI;

    ESP_LOGI(TAG,"Done");
}


// Create the WiFi panel metadata
panel_t *wifi_scr_init() {
    ESP_LOGI(TAG,"Init");

    wifi_scr_t *wifi = calloc(1, sizeof(wifi_scr_t));
    STRUCT_INIT_MAGIC(wifi, WIFI_SCR_MAGIC);

    wifi->scr.free = wifi_free;
    wifi->scr.goto_sleep = wifi_sleep;
    wifi->scr.create_content = wifi_lv_init;

    wifi->ap_index=0;
    wifi->ap_count=0;

    return (panel_t *)wifi;
}

#endif
