#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"

#include "iot_button.h"
#include "lvgl.h"
#include "tembed.h"
#include "scr.h"

static const char *TAG="col_scr";

#ifdef STRUCT_MAGIC
#define COL_SCR_MAGIC STRUCT_MAKE_MAGIC(0xF3)
#endif

extern panel_t *main_scr_init();

typedef struct col_scr {
    panel_t scr;
} col_scr_t;

static void col_menu_click_cb(void *arg, void *data);

static void col_reg_handlers(col_scr_t *col) {
    STRUCT_CHECK_MAGIC(col, COL_SCR_MAGIC, TAG, "reg_handlers");
    // Register the event handlers for the knob for this screen
    ESP_ERROR_CHECK(iot_button_register_cb(tembed->dial.btn, BUTTON_SINGLE_CLICK, col_menu_click_cb, col));
    col->scr.handlers_installed=true;
}

static void col_unreg_handlers(col_scr_t *col) {
    STRUCT_CHECK_MAGIC(col, COL_SCR_MAGIC, TAG, "unreg_handlers");
    ESP_ERROR_CHECK(iot_button_unregister_cb(tembed->dial.btn,BUTTON_SINGLE_CLICK));
    col->scr.handlers_installed=false;
}

static void col_free(panel_t *data) {
    col_scr_t *col = (col_scr_t *)data;
    STRUCT_CHECK_MAGIC(col, COL_SCR_MAGIC, TAG, "free");

    ESP_LOGI(TAG,"Free");

    if(col->scr.handlers_installed) {
        col_unreg_handlers(col);
    }
    lv_obj_del(col->scr.lv_root); // Free of this object frees children too

    STRUCT_INVALIDATE(col);
    free(col);

    ESP_LOGI(TAG,"Free done");
}

static esp_err_t col_sleep(panel_t *data) {
    assert(data);
    col_scr_t *col = (col_scr_t *)data;
    STRUCT_CHECK_MAGIC(col, COL_SCR_MAGIC, TAG, "sleep");

    ESP_LOGI(TAG, "sleep");

    if(col->scr.handlers_installed) {
        col_unreg_handlers(col);
    }

    return ESP_OK;
}

// Handle a selection on the col menu
static void col_menu_click_cb(void *arg, void *data)
{
    ACTION();
    assert(data);
    col_scr_t *col = (col_scr_t *)data;
    STRUCT_CHECK_MAGIC(col, COL_SCR_MAGIC, TAG, "click");

    LOCK_GUI;

    if(col->scr.handlers_installed) {
        col_unreg_handlers(col);
    }

    active_scr=main_scr_init();
    gui_set_panel(gui, active_scr);

    UNLOCK_GUI;

}

static const lv_style_const_prop_t col_style_props[] = {
    LV_STYLE_CONST_BG_COLOR(black), // Black
    LV_STYLE_CONST_BG_OPA(LV_OPA_COVER), // Opaque background
    LV_STYLE_CONST_TEXT_COLOR(white), // White
    LV_STYLE_CONST_TEXT_ALIGN(LV_TEXT_ALIGN_CENTER),
    LV_STYLE_CONST_TEXT_FONT(&lv_font_montserrat_18),
    {.prop=0,.value={.num=0}}
};
LV_STYLE_CONST_INIT(col_style, col_style_props);

static void col_lv_init(panel_t *panel, lv_obj_t *parent)
{
    col_scr_t *col = (col_scr_t *)panel;
    STRUCT_CHECK_MAGIC(col, COL_SCR_MAGIC, TAG, "lv_init");

    LOCK_GUI;

    // Set the menu title
    gui_set_menu_title((char *)"Color Test");

    // Create a container with ROW flex direction
    col->scr.lv_root = lv_obj_create(parent);

    lv_obj_t * cont_rows = col->scr.lv_root;
    lv_obj_set_size(cont_rows, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(cont_rows, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(cont_rows, (lv_style_t *)&col_style, LV_PART_MAIN);
    lv_obj_set_flex_align(cont_rows, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t * cont_row = lv_obj_create(cont_rows);
    lv_obj_set_size(cont_row, lv_pct(100), 75);
    lv_obj_set_flex_flow(cont_row, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_row, (lv_style_t *)&col_style, LV_PART_MAIN);
    lv_obj_set_flex_align(cont_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /*Add items to the row*/
    {
        lv_obj_t * label = lv_label_create(cont_row);
        lv_label_set_text_static(label, "RED");
        lv_obj_set_style_bg_color(label, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(label, LV_OPA_COVER, LV_PART_MAIN);
    }

    {
        lv_obj_t * label = lv_label_create(cont_row);
        lv_label_set_text_static(label, "GREEN");
        lv_obj_set_style_bg_color(label, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(label, LV_OPA_COVER, LV_PART_MAIN);
    }

    {
        lv_obj_t * label = lv_label_create(cont_row);
        lv_label_set_text_static(label, "BLUE");
        lv_obj_set_style_bg_color(label, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(label, LV_OPA_COVER, LV_PART_MAIN);
    }

    lv_obj_t * lbl = lv_label_create(cont_rows);
    lv_label_set_text_static(lbl, "Press button");
    lv_obj_set_width(lbl, lv_pct(100));

    col_reg_handlers(col);

    UNLOCK_GUI;
}

// Select and display the col menu screen
panel_t *col_scr_init() {
    ESP_LOGI(TAG,"Init");
    col_scr_t *col = calloc(1, sizeof(col_scr_t));
    STRUCT_INIT_MAGIC(col, COL_SCR_MAGIC);
    col->scr.free = col_free;
    col->scr.goto_sleep = col_sleep;
    col->scr.create_content = col_lv_init;

    ESP_LOGI(TAG,"Done");
    return (panel_t *)col;
}
