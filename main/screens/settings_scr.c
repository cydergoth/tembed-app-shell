#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "magic.h"
#include "iot_button.h"
#include "iot_knob.h"
#include "lvgl.h"
#include "tembed.h"
#include "scr.h"
#include "idle.h"

// Identifiers for this screen
static const char *TAG="settings_scr";

#ifdef STRUCT_MAGIC
#define SETTINGS_SCR_MAGIC STRUCT_MAKE_MAGIC(0xF7)
#endif

#define SETTINGS_MENU_HOME 0
#define SETTINGS_MENU_WIFI_SCAN 1
#define SETTINGS_MENU_SMART 2
#define SETTINGS_MENU_MAX SETTINGS_MENU_SMART

extern panel_t *wifi_scr_init();
extern panel_t *main_scr_init();
extern panel_t *smart_scr_init();

typedef struct settings_scr {
    panel_t scr; // Common screen state
    lv_obj_t *lvnd_menu;
    lv_obj_t *lvnd_widgets[SETTINGS_MENU_MAX+1];
    lv_obj_t *lvnd_network;
    esp_event_handler_instance_t ip_handler;
    int16_t current;
} settings_scr_t;


static void settings_unreg_handlers(settings_scr_t *settings);

static void settings_free(panel_t *data) {
    settings_scr_t *settings = (settings_scr_t *)data;
    STRUCT_CHECK_MAGIC(settings, SETTINGS_SCR_MAGIC, TAG, "free");
    ESP_LOGI(TAG,"Free");

    if(settings->scr.handlers_installed) {
        settings_unreg_handlers(settings);
    }
    lv_obj_del(settings->scr.lv_root); // Free of this object frees children too

    STRUCT_INVALIDATE(settings);
    free(settings);

    ESP_LOGI(TAG,"Free done");
}

// Handle a selection on the settings menu
static void settings_menu_click_cb(void *arg, void *data)
{
    ACTION();
    assert(data);
    settings_scr_t *settings = (settings_scr_t *)data;
    ESP_LOGI(TAG,"Click");
    STRUCT_CHECK_MAGIC(settings, SETTINGS_SCR_MAGIC, TAG, "click");

    LOCK_GUI;

    // Degregister our event handlers
    ESP_LOGI(TAG,"Deregister");

    if(settings->scr.handlers_installed) {
        settings_unreg_handlers(settings);
    }

    ESP_LOGI(TAG,"Screen %d", settings->current);
    switch(settings->current) {
    case SETTINGS_MENU_HOME:
        active_scr = main_scr_init();
        gui_set_panel(gui, active_scr);
        break;
//    case SETTINGS_MENU_IMAGE:
//        image_scr(settings->scr.tembed);
//        break;
#ifdef CONFIG_TEMBED_INIT_WIFI
    case SETTINGS_MENU_WIFI_SCAN:
        active_scr = wifi_scr_init();
        gui_set_panel(gui, active_scr);
        break;
#endif
    case SETTINGS_MENU_SMART:
        active_scr = smart_scr_init();
        gui_set_panel(gui, active_scr);
        break;
    default: assert(false); // Panic
    }

    UNLOCK_GUI;
}

static void settings_menu_knob_left_cb(void *arg, void *data)
{
    ACTION();
//    assert(data);
    settings_scr_t *settings = (settings_scr_t *)data;
#ifdef STRUCT_MAGIC
#ifdef BAD_KNOB_USR_DATA
    if(settings->scr.magic!=SETTINGS_SCR_MAGIC) {
        // Knob library is buggy
        void** usr_data = (void**)data;
        settings = (settings_scr_t *)usr_data[KNOB_LEFT];
    }
#endif
    STRUCT_CHECK_MAGIC(settings, SETTINGS_SCR_MAGIC, TAG, "left");
#endif
    ESP_LOGD(TAG,"left");

    LOCK_GUI;

    lv_obj_clear_state(settings->lvnd_widgets[settings->current], LV_STATE_FOCUSED);
    settings->current--;
    if(settings->current < 0) settings->current=SETTINGS_MENU_MAX;
    lv_obj_add_state(settings->lvnd_widgets[settings->current], LV_STATE_FOCUSED);

    UNLOCK_GUI;
    ESP_LOGD(TAG,"done");
}

static void settings_menu_knob_right_cb(void *arg, void *data)
{
    ACTION();

    settings_scr_t *settings = (settings_scr_t *)data;
#ifdef STRUCT_MAGIC
#ifdef BAD_KNOB_USR_DATA
    if(settings->scr.magic!=SETTINGS_SCR_MAGIC) {
        // Knob library is buggy
        void **usr_data = (void**)data;
        settings = (settings_scr_t *)usr_data[KNOB_RIGHT];
    }
#endif
    STRUCT_CHECK_MAGIC(settings, SETTINGS_SCR_MAGIC, TAG, "right");
#endif

    ESP_LOGD(TAG,"right");

    LOCK_GUI;

    lv_obj_clear_state(settings->lvnd_widgets[settings->current], LV_STATE_FOCUSED);
    settings->current++;
    if(settings->current > SETTINGS_MENU_MAX) settings->current=0;
    lv_obj_add_state(settings->lvnd_widgets[settings->current], LV_STATE_FOCUSED);

    UNLOCK_GUI;

    ESP_LOGD(TAG,"done");
}

static void settings_menu_knob_event(void *arg, void *data) {
    ACTION();
    knob_event_t event=iot_knob_get_event((knob_handle_t)arg);
    ESP_LOGI(TAG,"Got event %d", event);
}

static void settings_reg_handlers(settings_scr_t *settings) {
    STRUCT_CHECK_MAGIC(settings, SETTINGS_SCR_MAGIC, TAG, "reg");

    // Register the event handlers for the knob for this screen
    ESP_ERROR_CHECK(iot_button_register_cb(tembed->dial.btn, BUTTON_SINGLE_CLICK, settings_menu_click_cb, settings));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_LEFT, settings_menu_knob_left_cb, settings));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_RIGHT, settings_menu_knob_right_cb, settings));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_ZERO, settings_menu_knob_event, settings));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_H_LIM, settings_menu_knob_event, settings));
    ESP_ERROR_CHECK(iot_knob_register_cb(tembed->dial.knob, KNOB_L_LIM, settings_menu_knob_event, settings));

    settings->scr.handlers_installed = true;
}

// Unregister all the pending event handlers for this screen
static void settings_unreg_handlers(settings_scr_t *settings) {
    STRUCT_CHECK_MAGIC(settings, SETTINGS_SCR_MAGIC, TAG, "unreg");
    ESP_ERROR_CHECK(iot_button_unregister_cb(tembed->dial.btn, BUTTON_SINGLE_CLICK));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_LEFT));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_RIGHT));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_ZERO));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_H_LIM));
    ESP_ERROR_CHECK(iot_knob_unregister_cb(tembed->dial.knob, KNOB_L_LIM));

    settings->scr.handlers_installed = false;
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

static void settings_lv_init(panel_t *panel, lv_obj_t *parent) {
    settings_scr_t * settings = (settings_scr_t *)panel;
    STRUCT_CHECK_MAGIC(settings, SETTINGS_SCR_MAGIC, TAG, "click");
    LOCK_GUI;

    gui_set_menu_title((char *)"Settings Menu");

    // Settings content panel
    settings->scr.lv_root  = lv_obj_create(parent);
    lv_obj_t *content= settings->scr.lv_root;
    lv_obj_center(content);
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_height(content, lv_pct(100));
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

    settings->lvnd_menu = lv_obj_create(content);
    lv_obj_set_width(settings->lvnd_menu, lv_pct(100));
    lv_obj_set_flex_grow(settings->lvnd_menu, 1);
    lv_obj_set_layout(settings->lvnd_menu, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(settings->lvnd_menu, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(settings->lvnd_menu, LV_FLEX_ALIGN_SPACE_EVENLY, 0, 0);

    /*Add items to the row*/
    settings->lvnd_widgets[SETTINGS_MENU_HOME] = lv_label_create(settings->lvnd_menu);
    lv_label_set_text_static(settings->lvnd_widgets[SETTINGS_MENU_HOME], LV_SYMBOL_HOME);
    lv_obj_add_style(settings->lvnd_widgets[SETTINGS_MENU_HOME], (lv_style_t *)&menu_style, LV_PART_MAIN);
    lv_obj_add_style(settings->lvnd_widgets[SETTINGS_MENU_HOME], (lv_style_t *)&focus_style, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_state(settings->lvnd_widgets[SETTINGS_MENU_HOME], LV_STATE_FOCUSED);

#ifdef CONFIG_TEMBED_INIT_WIFI
    settings->lvnd_widgets[SETTINGS_MENU_WIFI_SCAN] = lv_label_create(settings->lvnd_menu);
    lv_label_set_text_static(settings->lvnd_widgets[SETTINGS_MENU_WIFI_SCAN], LV_SYMBOL_WIFI " SCAN");
    lv_obj_add_style(settings->lvnd_widgets[SETTINGS_MENU_WIFI_SCAN], (lv_style_t *)&menu_style, LV_PART_MAIN);
    lv_obj_add_style(settings->lvnd_widgets[SETTINGS_MENU_WIFI_SCAN], (lv_style_t *)&focus_style, LV_PART_MAIN | LV_STATE_FOCUSED);
#endif

    settings->lvnd_widgets[SETTINGS_MENU_SMART] = lv_label_create(settings->lvnd_menu);
    lv_label_set_text_static(settings->lvnd_widgets[SETTINGS_MENU_SMART], LV_SYMBOL_WIFI " SMART");
    lv_obj_add_style(settings->lvnd_widgets[SETTINGS_MENU_SMART], (lv_style_t *)&menu_style, LV_PART_MAIN);
    lv_obj_add_style(settings->lvnd_widgets[SETTINGS_MENU_SMART], (lv_style_t *)&focus_style, LV_PART_MAIN | LV_STATE_FOCUSED);

    settings_reg_handlers(settings);

    UNLOCK_GUI;
}

esp_err_t settings_sleep(panel_t *data) {
    assert(data);
    settings_scr_t *settings = (settings_scr_t *)data;
    STRUCT_CHECK_MAGIC(settings, SETTINGS_SCR_MAGIC, TAG, "sleep");

    ESP_LOGI(TAG, "sleep");
    if(settings->scr.handlers_installed) {
        settings_unreg_handlers(settings);
    }

    return ESP_OK;
}

// Select and display the settings menu screen
panel_t *settings_scr_init() {
    ESP_LOGI(TAG,"Init");

    settings_scr_t *settings = calloc(1, sizeof(settings_scr_t));
    STRUCT_INIT_MAGIC(settings, SETTINGS_SCR_MAGIC);
    settings->scr.free = settings_free;
    settings->scr.goto_sleep = settings_sleep;
    settings->scr.create_content = settings_lv_init;
    settings->current=SETTINGS_MENU_HOME;

    ESP_LOGI(TAG,"Done");
    return (panel_t *)settings;
}
