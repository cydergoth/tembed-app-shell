#include <stdlib.h>
#include "lvgl.h"
#include "magic.h"
#include "scr.h"
#include "app_event.h"

// Identifiers for this screen
static const char *TAG="sidebar";
#ifdef STRUCT_MAGIC
#define SIDEBAR_MAGIC STRUCT_MAKE_MAGIC(0xE0)
#endif

// For red and blue, you are passed a value in the range 0-255 which
// you need to map to the range 0-31. You can do this by dividing by 8
// (shifting right by 3 is equivalent to integer dividing by 8) The
// remaining 5-bits then need to be shifted into their correct
// position (for 565bgr, red is already in the correct position in the
// lowest 5 bits, blue needs to be shifted left 11 places) Green is
// similar, but because you have 6 bits available (0-63) you need only
// divide the source 8-bit value by 4 (or shift right by 2) and then
// shift the result into the correct position.

static const lv_style_const_prop_t sidebar_panel_style_props[] = {
    LV_STYLE_CONST_WIDTH(35),
    LV_STYLE_CONST_TEXT_COLOR(grey),
    LV_STYLE_CONST_TEXT_ALIGN(LV_TEXT_ALIGN_CENTER),
    LV_STYLE_CONST_TEXT_FONT(&lv_font_montserrat_18),
    LV_STYLE_CONST_BORDER_COLOR(grey),
    LV_STYLE_CONST_BORDER_SIDE(LV_BORDER_SIDE_RIGHT),
    LV_STYLE_CONST_BORDER_OPA(LV_OPA_COVER),
    LV_STYLE_CONST_BORDER_WIDTH(2),
    LV_STYLE_CONST_WIDTH(40),
    LV_STYLE_CONST_HEIGHT(170),
    {.prop=0,.value={.num=0}}
};
LV_STYLE_CONST_INIT(sidebar_panel_style, sidebar_panel_style_props);

static const lv_style_const_prop_t sidebar_style_props[] = {
    LV_STYLE_CONST_WIDTH(30),
    LV_STYLE_CONST_TEXT_COLOR(grey),
    LV_STYLE_CONST_TEXT_ALIGN(LV_TEXT_ALIGN_CENTER),
    LV_STYLE_CONST_TEXT_FONT(&lv_font_montserrat_18),
    {.prop=0,.value={.num=0}}
};
LV_STYLE_CONST_INIT(sidebar_style, sidebar_style_props);

static const lv_style_const_prop_t sidebar_style_active_props[] = {
    LV_STYLE_CONST_TEXT_COLOR(white),
    {.prop=0,.value={.num=0}}
};
LV_STYLE_CONST_INIT(sidebar_style_active, sidebar_style_active_props);

static const lv_style_const_prop_t sidebar_style_error_props[] = {
    LV_STYLE_CONST_TEXT_COLOR(red),
    {.prop=0,.value={.num=0}}
};
LV_STYLE_CONST_INIT(sidebar_style_error, sidebar_style_error_props);

void set_text_color(void *obj, long int val) {
    lv_color_t c = lv_color_make((255 * val) / 100,(255 * val) / 100,(255 * val) / 100);
    lv_obj_set_style_text_color(obj, c, LV_PART_MAIN);
}

static void got_ip_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG,"Got IP address");

    sidebar_t *sidebar = (sidebar_t *)event_handler_arg;
    STRUCT_CHECK_MAGIC(sidebar, SIDEBAR_MAGIC, TAG, "ip_event");
    ESP_LOGI(TAG, "Checking event");
    assert(event_base==IP_EVENT);
    assert(event_id==IP_EVENT_STA_GOT_IP);
    sidebar_wifi_state(sidebar, WIFI_ACTIVE);
}


static void wifi_disconnected_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "Wifi disconnected");

    sidebar_t *sidebar = (sidebar_t *)event_handler_arg;
    STRUCT_CHECK_MAGIC(sidebar, SIDEBAR_MAGIC, TAG, "wifi_event");
    assert(event_base==WIFI_EVENT);
    assert(event_id==WIFI_EVENT_STA_DISCONNECTED);
    sidebar_wifi_state(sidebar, WIFI_DISCONNECTED);

    /* esp_err_t res=esp_wifi_connect(); */
    /* switch(res) { */
    /*     // If we get this error, the stored SSID isn't valid */
    /*     // In that case, we ignore the error, leave the device */
    /*     // unconnected and wait for the user to pick a new device */
    /* case ESP_ERR_WIFI_SSID: */
    /*     sidebar_wifi_state(sidebar, WIFI_UNCONFIGURED); */
    /*     break; */
    /* default: { */
    /*     ESP_ERROR_CHECK(res); // For all other errors let ERROR_CHECK handle it */
    /*     sidebar_wifi_state(sidebar, WIFI_ACTIVE); */
    /* } */
    /* } */
}

static void sdcard_init_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "sccard init");
    sidebar_t *sidebar = (sidebar_t *)event_handler_arg;
    STRUCT_CHECK_MAGIC(sidebar, SIDEBAR_MAGIC, TAG, "sdcard_init");
    assert(event_base == APP_EVENT);
    assert(event_id == APP_EVENT_SDCARD_INIT);
    lv_obj_add_state(sidebar->sdcard, LV_PART_MAIN | LV_STATE_CHECKED);
}

static void sidebar_reg_handlers(sidebar_t *sidebar) {
    STRUCT_CHECK_MAGIC(sidebar, SIDEBAR_MAGIC, TAG, "reg");
    if(tembed->netif) {
        // Register an event handler for the IP Address changing
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, got_ip_event_handler, sidebar, &sidebar->ip_handler));
        // Register a handler to update the status icon if the WiFi disconnects
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_disconnected_event_handler, sidebar, &sidebar->wifi_handler));
        sidebar->handlers_installed=true;
    }
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop, APP_EVENT, APP_EVENT_SDCARD_INIT, sdcard_init_handler, sidebar, &sidebar->sdcard_handler));
}

static void sidebar_unreg_handlers(sidebar_t *sidebar) {
    STRUCT_CHECK_MAGIC(sidebar, SIDEBAR_MAGIC, TAG, "unreg");
    if(tembed->netif) {
        // Register an event handler for the IP Address changing
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, sidebar->ip_handler));
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, sidebar->wifi_handler));
        sidebar->handlers_installed=false;
    }
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister_with(app_event_loop, APP_EVENT, APP_EVENT_SDCARD_INIT, sidebar->sdcard_handler));
}


void sidebar_free(sidebar_t *sidebar) {
    STRUCT_CHECK_MAGIC(sidebar, SIDEBAR_MAGIC, TAG, "free");

    LOCK_GUI;

    if(sidebar->handlers_installed) {
        sidebar_unreg_handlers(sidebar);
    }

    if(sidebar->wifi_scanning) {
        lv_anim_del(sidebar->wifi, set_text_color);
    }
    lv_obj_del(sidebar->lv_root); // Also destroys children

    STRUCT_INVALIDATE(sidebar);
    free(sidebar);

    UNLOCK_GUI;
}

esp_err_t sidebar_sleep(sidebar_t *sidebar) {
    ESP_LOGI(TAG, "sleep");
    STRUCT_CHECK_MAGIC(sidebar, SIDEBAR_MAGIC, TAG, "sleep");
    if(sidebar->handlers_installed) {
        sidebar_unreg_handlers(sidebar);
    }
    return ESP_OK;
}

sidebar_t *sidebar_init(gui_t *gui) {
    sidebar_t *sidebar = calloc(1, sizeof(struct sidebar));
    STRUCT_INIT_MAGIC(sidebar, SIDEBAR_MAGIC);

    LOCK_GUI;

    sidebar->scr = gui;

    sidebar->lv_root = lv_obj_create(gui->lv_root);
    lv_obj_add_style(sidebar->lv_root, (lv_style_t *)&sidebar_panel_style, LV_PART_MAIN);
    lv_obj_set_layout(sidebar->lv_root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(sidebar->lv_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sidebar->lv_root, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    sidebar->wifi = lv_label_create(sidebar->lv_root);
    lv_obj_add_style(sidebar->wifi, (lv_style_t *)&sidebar_style, LV_PART_MAIN);
    lv_obj_add_style(sidebar->wifi, (lv_style_t *)&sidebar_style_active, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_add_style(sidebar->wifi, (lv_style_t *)&sidebar_style_error, LV_PART_MAIN | LV_STATE_USER_1);
    lv_label_set_text_static(sidebar->wifi, LV_SYMBOL_WIFI);

    sidebar->bt = lv_label_create(sidebar->lv_root);
    lv_obj_add_style(sidebar->bt, (lv_style_t *)&sidebar_style, LV_PART_MAIN);
    lv_obj_add_style(sidebar->bt, (lv_style_t *)&sidebar_style_active, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_label_set_text_static(sidebar->bt, LV_SYMBOL_BLUETOOTH);

    sidebar->sdcard = lv_label_create(sidebar->lv_root);
    lv_obj_add_style(sidebar->sdcard, (lv_style_t *)&sidebar_style, LV_PART_MAIN);
    lv_obj_add_style(sidebar->sdcard, (lv_style_t *)&sidebar_style_active, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_label_set_text_static(sidebar->sdcard, LV_SYMBOL_SD_CARD);

    sidebar->battery = lv_label_create(sidebar->lv_root);
    lv_obj_add_style(sidebar->battery, (lv_style_t *)&sidebar_style, LV_PART_MAIN);
    lv_obj_add_style(sidebar->battery, (lv_style_t *)&sidebar_style_active, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_label_set_text_static(sidebar->battery, LV_SYMBOL_BATTERY_EMPTY);

    sidebar_reg_handlers(sidebar);

    UNLOCK_GUI;

    return sidebar;
}

lv_anim_t wifi_scanning;

// TODO: Make WiFi application state a proper state machine
void sidebar_wifi_state(sidebar_t *sidebar, wifi_state_t state) {
    STRUCT_CHECK_MAGIC(sidebar, SIDEBAR_MAGIC, TAG, "wifi_state");

    ESP_LOGI(TAG, "Wifi State Change");

    LOCK_GUI;

    if(sidebar->wifi_scanning) {
        if (state == WIFI_SCANNING) {
            // Same state, no change
            UNLOCK_GUI;
            return;
        }
        lv_anim_del(sidebar->wifi, set_text_color);
        sidebar->wifi_scanning=false;
    }

    lv_obj_clear_state(sidebar->wifi, LV_PART_MAIN | LV_STATE_ANY); // TODO: Verify this does what we think it does

    switch(state)
    {
    case WIFI_DISCONNECTED: lv_obj_add_state(sidebar->wifi, LV_PART_MAIN | LV_STATE_USER_2); break;
    case WIFI_UNCONFIGURED: lv_obj_add_state(sidebar->wifi, LV_PART_MAIN | LV_STATE_USER_1); break;
    case WIFI_DISABLED: lv_obj_add_state(sidebar->wifi, LV_PART_MAIN | LV_STATE_DISABLED); break;
    case WIFI_ACTIVE: lv_obj_add_state(sidebar->wifi, LV_PART_MAIN | LV_STATE_CHECKED); break;
    case WIFI_SCANNING: {
        lv_anim_init(&wifi_scanning);
        lv_anim_set_exec_cb(&wifi_scanning, (lv_anim_exec_xcb_t) set_text_color);
        lv_anim_set_var(&wifi_scanning, sidebar->wifi);
        lv_anim_set_delay(&wifi_scanning, 50);
        /*Length of the animation [ms]*/
        lv_anim_set_time(&wifi_scanning, 2000);
        /*Set start and end values. E.g. 0, 150*/
        lv_anim_set_values(&wifi_scanning, 0, 100);
        lv_anim_set_playback_delay(&wifi_scanning, 25);
        lv_anim_set_playback_time(&wifi_scanning, 2000);
        lv_anim_set_repeat_count(&wifi_scanning, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&wifi_scanning, lv_anim_path_ease_in_out);
        lv_anim_start(&wifi_scanning);
        sidebar->wifi_scanning=true;
        break;
    }
    };

    UNLOCK_GUI;
}
