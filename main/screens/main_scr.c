#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "magic.h"
#include "iot_button.h"
#include "iot_knob.h"
#include "lvgl.h"
#include "tembed.h"
#include "scr.h"
#include "idle.h"
#include "app_event.h"

// Identifiers for this screen
static const char *TAG="main_scr";

#ifdef STRUCT_MAGIC
#define MAIN_SCR_MAGIC STRUCT_MAKE_MAGIC(0xF0)
#endif

#define MAIN_MENU_SETTINGS 0
#define MAIN_MENU_IMAGE 3
#define MAIN_MENU_SDCARD 2
#define MAIN_MENU_COLS 1
#define MAIN_MENU_MAX MAIN_MENU_SDCARD

extern panel_t *settings_scr_init();
extern panel_t *col_scr_init();
extern panel_t *sdcard_scr_init();

typedef struct main_scr {
    panel_t scr; // Common screen state
    lv_obj_t *lvnd_menu;
    lv_obj_t *lvnd_widgets[MAIN_MENU_MAX+1];
    lv_obj_t *lvnd_network;
    lv_obj_t *lvnd_clock; // Clock
    esp_event_handler_instance_t ip_handler;
    esp_event_handler_instance_t tick_handler;
    int16_t current;
} main_scr_t;


static void main_unreg_handlers(main_scr_t *main);

static void main_free(panel_t *data) {
    main_scr_t *main = (main_scr_t *)data;
    STRUCT_CHECK_MAGIC(main, MAIN_SCR_MAGIC, TAG, "free");
    ESP_LOGI(TAG,"Free");

    if(main->scr.handlers_installed) {
        main_unreg_handlers(main);
    }
    lv_obj_del(main->scr.lv_root); // Free of this object frees children too

    STRUCT_INVALIDATE(main);
    free(main);

    ESP_LOGI(TAG,"Free done");
}

// Handle a selection on the main menu
static void main_menu_click_cb(void *arg, void *data)
{
    ACTION();
    ESP_LOGI(TAG,"Click");

    main_scr_t *main = (main_scr_t *)data;
    STRUCT_CHECK_MAGIC(main, MAIN_SCR_MAGIC, TAG, "click");

    LOCK_GUI;

    // Degregister our event handlers
    ESP_LOGI(TAG,"Deregister");

    if(main->scr.handlers_installed) {
        main_unreg_handlers(main);
    }

    ESP_LOGI(TAG,"Screen %d", main->current);
    switch(main->current) {
    case MAIN_MENU_SETTINGS:
        active_scr = settings_scr_init();
        gui_set_panel(gui, active_scr);
        break;
    case MAIN_MENU_COLS:
        active_scr = col_scr_init();
        gui_set_panel(gui, active_scr);
        break;
    case MAIN_MENU_SDCARD:
        active_scr = sdcard_scr_init();
        gui_set_panel(gui, active_scr);
        break;
    default: assert(false); // Panic
    }

    UNLOCK_GUI;

}

static void main_menu_knob_left_cb(void *arg, void *data)
{
    ACTION();

    main_scr_t *main = (main_scr_t *)data;
#ifdef STRUCT_MAGIC
#ifdef BAD_KNOB_USR_DATA
    if(main->scr.magic!=MAIN_SCR_MAGIC) {
        // Knob library is buggy
        void** usr_data = (void**)data;
        main = (main_scr_t *)usr_data[KNOB_LEFT];
    }
#endif
    STRUCT_CHECK_MAGIC(main, MAIN_SCR_MAGIC, TAG, "left");
#endif
    ESP_LOGD(TAG,"left");

    LOCK_GUI;

    lv_obj_clear_state(main->lvnd_widgets[main->current], LV_STATE_FOCUSED);
    main->current--;
    if(main->current < 0) main->current=MAIN_MENU_MAX;
    lv_obj_add_state(main->lvnd_widgets[main->current], LV_STATE_FOCUSED);

    UNLOCK_GUI;
    ESP_LOGD(TAG,"done");
}

static void main_menu_knob_right_cb(void *arg, void *data)
{
    ACTION();

    main_scr_t *main = (main_scr_t *)data;
#ifdef STRUCT_MAGIC
#ifdef BAD_KNOB_USR_DATA
    if(main->scr.magic!=MAIN_SCR_MAGIC) {
        // Knob library is buggy
        void **usr_data = (void**)data;
        main = (main_scr_t *)usr_data[KNOB_RIGHT];
    }
#endif
    STRUCT_CHECK_MAGIC(main, MAIN_SCR_MAGIC, TAG, "right");
#endif

    ESP_LOGD(TAG,"right");

    LOCK_GUI;

    lv_obj_clear_state(main->lvnd_widgets[main->current], LV_STATE_FOCUSED);
    main->current++;
    if(main->current > MAIN_MENU_MAX) main->current=0;
    lv_obj_add_state(main->lvnd_widgets[main->current], LV_STATE_FOCUSED);

    UNLOCK_GUI;

    ESP_LOGD(TAG,"done");
}

static void set_ip_label(lv_obj_t *label, const esp_netif_ip_info_t *ip_info) {
    // TODO: Show "Connected" and SSID here
    ESP_LOGI(TAG,"IP: %d.%d.%d.%d", \
             (ip_info->ip.addr) & 0xFF, \
             (ip_info->ip.addr >> 8) & 0xFF, \
             (ip_info->ip.addr >> 16) & 0xFF, \
             (ip_info->ip.addr >> 24) & 0xFF \
        );
    LOCK_GUI;
    lv_label_set_text_fmt(label, LV_SYMBOL_WIFI " %d.%d.%d.%d", \
                          (ip_info->ip.addr) & 0xFF, \
                          (ip_info->ip.addr >> 8) & 0xFF, \
                          (ip_info->ip.addr >> 16) & 0xFF, \
                          (ip_info->ip.addr >> 24) & 0xFF \
        );
    UNLOCK_GUI;
}

static void got_ip_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG,"Got IP address");

    main_scr_t *main = (main_scr_t *)event_handler_arg;
    STRUCT_CHECK_MAGIC(main, MAIN_SCR_MAGIC, TAG, "ip_event");
    assert(event_base==IP_EVENT);
    assert(event_id==IP_EVENT_STA_GOT_IP);
    ip_event_got_ip_t *ip_event=(ip_event_got_ip_t *)event_data;
    esp_netif_ip_info_t *ip_info=&ip_event->ip_info;
    set_ip_label(main->lvnd_network, ip_info);
}

static void main_menu_knob_event(void *arg, void *data) {
    ACTION();
    knob_event_t event=iot_knob_get_event((knob_handle_t)arg);
    ESP_LOGI(TAG,"Got event %d", event);
}

static void tick_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    main_scr_t *main = (main_scr_t *)event_handler_arg;
    STRUCT_CHECK_MAGIC(main, MAIN_SCR_MAGIC, TAG, "tick");
    assert(event_base==APP_EVENT);
    assert(event_id==APP_EVENT_TICK);
    time_t now;
    char strftime_buf[64];
    struct tm timeinfo;

    time(&now);

    localtime_r(&now, &timeinfo);
     strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    // ESP_LOGI(TAG, "The current date/time in CST is: %s", strftime_buf);

    LOCK_GUI;
    lv_label_set_text(main->lvnd_clock, strftime_buf);
    UNLOCK_GUI;
}

static void main_reg_handlers(main_scr_t *main) {
    ESP_LOGI(TAG, "reg");
    STRUCT_CHECK_MAGIC(main, MAIN_SCR_MAGIC, TAG, "reg");

    // Register the event handlers for the knob for this screen
    ESP_ERROR_CHECK(iot_button_register_cb(tembed->dial.btn, BUTTON_SINGLE_CLICK, main_menu_click_cb, main));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_LEFT, main_menu_knob_left_cb, main));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_RIGHT, main_menu_knob_right_cb, main));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_ZERO, main_menu_knob_event, main));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_H_LIM, main_menu_knob_event, main));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_L_LIM, main_menu_knob_event, main));

    // Register an event handler for the IP Address changing
    if(tembed->netif) {
        ESP_LOGI(TAG, "Reg ip_event");
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, got_ip_event_handler, main, &main->ip_handler));
    }

    // Register the clock update handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop, APP_EVENT, APP_EVENT_TICK, tick_event_handler, main, &main->tick_handler));

    main->scr.handlers_installed = true;
}

// Unregister all the pending event handlers for this screen
static void main_unreg_handlers(main_scr_t *main) {
    ESP_LOGI(TAG, "unreg");
    STRUCT_CHECK_MAGIC(main, MAIN_SCR_MAGIC, TAG, "unreg");

    ESP_ERROR_CHECK(iot_button_unregister_cb(tembed->dial.btn, BUTTON_SINGLE_CLICK));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_LEFT));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_RIGHT));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_ZERO));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_H_LIM));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_L_LIM));

    if(tembed->netif) {
        ESP_LOGI(TAG, "Unreg ip_event");
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, main->ip_handler));
    }

    // Unregister the clock update handler
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister_with(app_event_loop, APP_EVENT, APP_EVENT_TICK, main->tick_handler));

    main->scr.handlers_installed = false;
}

static const lv_style_const_prop_t menu_style_props[] = {
    LV_STYLE_CONST_TEXT_FONT(&lv_font_montserrat_22),
    {.prop=0,.value={.num=0}}
};
static LV_STYLE_CONST_INIT(menu_style, menu_style_props);

static const lv_style_const_prop_t focus_style_props[] = {
    LV_STYLE_CONST_OUTLINE_COLOR(blue),
    LV_STYLE_CONST_OUTLINE_WIDTH(2),
    LV_STYLE_CONST_OUTLINE_OPA(LV_OPA_COVER),
    {.prop=0,.value={.num=0}}
};
static LV_STYLE_CONST_INIT(focus_style, focus_style_props);

static void main_lv_init(panel_t *panel, lv_obj_t *parent) {
    main_scr_t * main = (main_scr_t *)panel;
    STRUCT_CHECK_MAGIC(main, MAIN_SCR_MAGIC, TAG, "click");
    LOCK_GUI;

    gui_set_menu_title((char *)"Main Menu");

    // Main content panel
    main->scr.lv_root  = lv_obj_create(parent);
    lv_obj_t *content= main->scr.lv_root;
    lv_obj_center(content);
    lv_obj_set_size(content, lv_pct(100), lv_pct(100));
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

    main->lvnd_menu = lv_obj_create(content);
    lv_obj_set_width(main->lvnd_menu, lv_pct(100));
    lv_obj_set_flex_grow(main->lvnd_menu, 1);
    lv_obj_set_layout(main->lvnd_menu, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(main->lvnd_menu, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(main->lvnd_menu, LV_FLEX_ALIGN_SPACE_EVENLY, 0, 0);

    /*Add items to the row*/
    main->lvnd_widgets[MAIN_MENU_SETTINGS] = lv_label_create(main->lvnd_menu);
    lv_label_set_text_static(main->lvnd_widgets[MAIN_MENU_SETTINGS], LV_SYMBOL_SETTINGS);
    lv_obj_add_style(main->lvnd_widgets[MAIN_MENU_SETTINGS], (lv_style_t *)&menu_style, LV_PART_MAIN);
    lv_obj_add_style(main->lvnd_widgets[MAIN_MENU_SETTINGS], (lv_style_t *)&focus_style, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_state(main->lvnd_widgets[MAIN_MENU_SETTINGS], LV_STATE_FOCUSED);

    main->lvnd_widgets[MAIN_MENU_COLS] = lv_label_create(main->lvnd_menu);
    lv_label_set_text_static(main->lvnd_widgets[MAIN_MENU_COLS], LV_SYMBOL_EYE_OPEN);
    lv_obj_add_style(main->lvnd_widgets[MAIN_MENU_COLS], (lv_style_t *)&menu_style, LV_PART_MAIN);
    lv_obj_add_style(main->lvnd_widgets[MAIN_MENU_COLS], (lv_style_t *)&focus_style, LV_PART_MAIN | LV_STATE_FOCUSED);

    main->lvnd_widgets[MAIN_MENU_SDCARD] = lv_label_create(main->lvnd_menu);
    lv_label_set_text_static(main->lvnd_widgets[MAIN_MENU_SDCARD], LV_SYMBOL_SD_CARD);
    lv_obj_add_style(main->lvnd_widgets[MAIN_MENU_SDCARD], (lv_style_t *)&menu_style, LV_PART_MAIN);
    lv_obj_add_style(main->lvnd_widgets[MAIN_MENU_SDCARD], (lv_style_t *)&focus_style, LV_PART_MAIN | LV_STATE_FOCUSED);

    // Create a widget to show the time
    main->lvnd_clock = lv_label_create(content);
    lv_obj_set_width(main->lvnd_clock, lv_pct(100));
    lv_obj_add_style(main->lvnd_clock, (lv_style_t *)&menu_style, LV_PART_MAIN);
    lv_obj_set_style_text_align(main->lvnd_clock, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_label_set_long_mode(main->lvnd_clock, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text_static(main->lvnd_clock, "--:--:--");

    // Create a widget to show the network state of the device
    main->lvnd_network = lv_label_create(content);
    lv_obj_set_width(main->lvnd_network, lv_pct(100));
    lv_obj_add_style(main->lvnd_network, (lv_style_t *)&menu_style, LV_PART_MAIN);
    lv_obj_set_style_text_align(main->lvnd_network, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_label_set_long_mode(main->lvnd_network, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // TODO: Handle not configured && provisioning cases here
    if(tembed->netif) {
        // Network sub-system is configured
        if(!esp_netif_is_netif_up(tembed->netif)) {
            // But there is no network
            lv_label_set_text_static(main->lvnd_network, LV_SYMBOL_WIFI " No WiFi connection");
        } else {
            // Show an IP if we already have one
            esp_netif_ip_info_t ip_info;
            ESP_ERROR_CHECK(esp_netif_get_ip_info(tembed->netif, &ip_info));
            set_ip_label(main->lvnd_network, &ip_info);
        }
    } else {
        lv_label_set_text_static(main->lvnd_network, LV_SYMBOL_WIFI " No network");
    }

    main_reg_handlers(main);

    UNLOCK_GUI;
}

esp_err_t main_sleep(panel_t *data) {
    assert(data);
    main_scr_t *main = (main_scr_t *)data;
    STRUCT_CHECK_MAGIC(main, MAIN_SCR_MAGIC, TAG, "sleep");

    ESP_LOGI(TAG, "sleep");
    if(main->scr.handlers_installed) {
        main_unreg_handlers(main);
    }

    return ESP_OK;
}

// Select and display the main menu screen
panel_t *main_scr_init() {
    ESP_LOGI(TAG,"Init");

    main_scr_t *main = calloc(1, sizeof(main_scr_t));
    STRUCT_INIT_MAGIC(main, MAIN_SCR_MAGIC);
    main->scr.free = main_free;
    main->scr.goto_sleep = main_sleep;
    main->scr.create_content = main_lv_init;
    main->current=MAIN_MENU_SETTINGS;

    ESP_LOGI(TAG,"Done");
    return (panel_t *)main;
}
