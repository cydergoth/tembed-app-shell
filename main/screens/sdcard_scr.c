#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"

#include "iot_button.h"
#include "lvgl.h"
#include "tembed.h"
#include "scr.h"
#include "dirent.h"

static const char *TAG="sdcard_scr";
#ifdef STRUCT_MAGIC
#define SDCARD_SCR_MAGIC STRUCT_MAKE_MAGIC(0xF3)
#endif

extern panel_t *main_scr_init();

typedef struct sdcard_scr {
    panel_t scr;
} sdcard_scr_t;

static void sdcard_menu_click_cb(void *arg, void *data);

static void sdcard_reg_handlers(sdcard_scr_t *col) {
    STRUCT_CHECK_MAGIC(col, SDCARD_SCR_MAGIC, TAG, "reg_handlers");
    // Register the event handlers for the knob for this screen
    ESP_ERROR_CHECK(iot_button_register_cb(tembed->dial.btn, BUTTON_SINGLE_CLICK, sdcard_menu_click_cb, col));
    col->scr.handlers_installed=true;
}

static void sdcard_unreg_handlers(sdcard_scr_t *col) {
    STRUCT_CHECK_MAGIC(col, SDCARD_SCR_MAGIC, TAG, "unreg_handlers");
    ESP_ERROR_CHECK(iot_button_unregister_cb(tembed->dial.btn,BUTTON_SINGLE_CLICK));
    col->scr.handlers_installed=false;
}

static void sdcard_free(panel_t *data) {
    sdcard_scr_t *col = (sdcard_scr_t *)data;
    STRUCT_CHECK_MAGIC(col, SDCARD_SCR_MAGIC, TAG, "free");

    ESP_LOGI(TAG,"Free");

    if(col->scr.handlers_installed) {
        sdcard_unreg_handlers(col);
    }
    lv_obj_del(col->scr.lv_root); // Free of this object frees children too

    STRUCT_INVALIDATE(col);
    free(col);

    ESP_LOGI(TAG,"Free done");
}

static esp_err_t sdcard_sleep(panel_t *data) {
    sdcard_scr_t *col = (sdcard_scr_t *)data;
    STRUCT_CHECK_MAGIC(col, SDCARD_SCR_MAGIC, TAG, "sleep");

    ESP_LOGI(TAG, "sleep");

    if(col->scr.handlers_installed) {
        sdcard_unreg_handlers(col);
    }

    return ESP_OK;
}

// Handle a selection on the col menu
static void sdcard_menu_click_cb(void *arg, void *data)
{
    ACTION();
    sdcard_scr_t *col = (sdcard_scr_t *)data;
    STRUCT_CHECK_MAGIC(col, SDCARD_SCR_MAGIC, TAG, "click");

    LOCK_GUI;

    if(col->scr.handlers_installed) {
        sdcard_unreg_handlers(col);
    }

    active_scr=main_scr_init();
    gui_set_panel(gui, active_scr);

    UNLOCK_GUI;

}

static const lv_style_const_prop_t sdcard_style_props[] = {
    LV_STYLE_CONST_BG_COLOR(black), // Black
    LV_STYLE_CONST_BG_OPA(LV_OPA_COVER), // Opaque background
    LV_STYLE_CONST_TEXT_COLOR(white), // White
    LV_STYLE_CONST_TEXT_ALIGN(LV_TEXT_ALIGN_CENTER),
    LV_STYLE_CONST_TEXT_FONT(&lv_font_montserrat_18),
    {.prop=0,.value={.num=0}}
};
LV_STYLE_CONST_INIT(sdcard_style, sdcard_style_props);

static void sdcard_lv_init(panel_t *panel, lv_obj_t *parent)
{
    sdcard_scr_t *sdcard = (sdcard_scr_t *)panel;
    STRUCT_CHECK_MAGIC(sdcard, SDCARD_SCR_MAGIC, TAG, "lv_init");

    LOCK_GUI;

    // Set the menu title
    gui_set_menu_title((char *)"SD/TF Card Directory");

    // Create a container with ROW flex direction
    sdcard->scr.lv_root = lv_obj_create(parent);

    lv_obj_t * cont_row = sdcard->scr.lv_root;
    lv_obj_set_size(cont_row, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(cont_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(cont_row, (lv_style_t *)&sdcard_style, LV_PART_MAIN);
    lv_obj_set_flex_align(cont_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    DIR *root=opendir("/sdcard/");
    struct dirent *entry;

    while((entry=readdir(root)))
    {
        ESP_LOGI(TAG, "file %s", entry->d_name);
        /*Add items to the column*/
        lv_obj_t * label = lv_label_create(cont_row);
        lv_label_set_text(label, entry->d_name);
        lv_obj_set_style_bg_opa(label, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(label, black, LV_PART_MAIN);
    }

    closedir(root);

    sdcard_reg_handlers(sdcard);

    UNLOCK_GUI;
}

// Select and display the col menu screen
panel_t *sdcard_scr_init() {
    ESP_LOGI(TAG,"Init");
    sdcard_scr_t *sdcard = calloc(1, sizeof(sdcard_scr_t));
    STRUCT_INIT_MAGIC(sdcard, SDCARD_SCR_MAGIC);
    sdcard->scr.free = sdcard_free;
    sdcard->scr.goto_sleep = sdcard_sleep;
    sdcard->scr.create_content = sdcard_lv_init;

    ESP_LOGI(TAG,"Done");
    return (panel_t *)sdcard;
}
