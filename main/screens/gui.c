#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"

#include "iot_button.h"
#include "iot_knob.h"
#include "lvgl.h"
#include "tembed.h"
#include "scr.h"
#include "idle.h"

// Identifiers for this screen
static const char *TAG="gui";
#ifdef STRUCT_MAGIC
#define GUI_MAGIC STRUCT_MAKE_MAGIC(0xD1)
#endif

static const lv_style_const_prop_t gui_style_props[] = {
    LV_STYLE_CONST_BG_COLOR(black), // Black
    LV_STYLE_CONST_BG_OPA(LV_OPA_COVER), // Opaque background
    LV_STYLE_CONST_TEXT_COLOR(white), // White
    LV_STYLE_CONST_TEXT_ALIGN(LV_TEXT_ALIGN_CENTER),
    LV_STYLE_CONST_TEXT_FONT(&lv_font_montserrat_18),
    {.prop=0,.value={.num=0}}
};
LV_STYLE_CONST_INIT(gui_style, gui_style_props);

void gui_set_menu_title(char *title)
{
    lv_label_set_text(gui->lvnd_title, title);
}

void gui_free(gui_t *gui) {
    ESP_LOGI(TAG,"Free");
    STRUCT_CHECK_MAGIC(gui, GUI_MAGIC, TAG, "Free");

    // Disable sleep whilst we do stuff which is not an ACTION

    // GUI Tree critical region
    LOCK_GUI;

    // Cleanly deconstruct the GUI
    if(gui->sidebar) {
        sidebar_free(gui->sidebar);
        gui->sidebar=NULL;
    } else {
        ESP_LOGW(TAG, "No sidebar?");
    }
    if(gui->panel) {
        panel_free(gui->panel);
        gui->panel=NULL;
    }
    // del of this object deletes children too. As we delete the
    // components above, that should now only be objects directly
    // owned by this one
    lv_obj_del(gui->lv_root);

    STRUCT_INVALIDATE(gui);
    free(gui);

    UNLOCK_GUI;

    ESP_LOGI(TAG, "Free done");
}

// Initialize the LV widget tree for this GUI component
static void gui_lv_init(gui_t *gui) {

    // Only called from gui_init so doesn't need to lock GUI

    // Top level object for this gui
    // Uses a column flow layout
    gui->lv_root = lv_obj_create(NULL);
    lv_obj_t *root = gui->lv_root;
    // TODO: Probably shouldn't hardcode disp size here
    lv_obj_set_width(root, 320);
    lv_obj_set_height(root, 170);

    // Set the default style for all the gui elements.
    // Individual elements may override this
    lv_obj_add_style(root, (lv_style_t *)&gui_style, LV_PART_MAIN);

    // The screen lays out the components with the sidebar on the left
    lv_obj_set_layout(root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW); // Set to LV_FLEX_FLOW_ROW_REVERSE to put sidebar on the right

    // Sidebar to show the status icons
    gui->sidebar=sidebar_init(gui);

    // Gui content panel for title and content
    lv_obj_t *panel = lv_obj_create(root);
    lv_obj_set_flex_grow(panel, 1);
    // lv_obj_set_width(panel, lv_pct(100));
    lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

    gui->lvnd_title = lv_label_create(panel);
    lv_obj_t *title = gui->lvnd_title;
    // lv_obj_set_width(title, lv_pct(100)); // Maybe needed, maybe not
    lv_obj_center(title);
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text_static(title, "T-Embed"); // Placeholder
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);

    // Set a border to separate the title from the rest
    lv_obj_set_style_border_width(title, 2, LV_PART_MAIN);
    lv_obj_set_style_border_opa(title, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(title, grey, LV_PART_MAIN);
    lv_obj_set_style_border_side(title, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);

    // Create an object to host the real content
    gui->lvnd_content = lv_obj_create(panel);
    lv_obj_t *content = gui->lvnd_content;
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_SPACE_EVENLY, 0, 0);
}

// Request the GUI disconnect all events (call gui_free to destroy all
// active objects - not strictly needed for deep sleep but cleaner and
// useful for shallow sleep)
esp_err_t gui_sleep(gui_t *gui) {
    STRUCT_CHECK_MAGIC(gui, GUI_MAGIC, TAG, "Sleep");

    ESP_LOGI(TAG, "sleep");

    // Don't need to NO_SLEEP here as the sleep callback is in effect already
    LOCK_GUI;

    if(gui->panel) {
        esp_err_t res=panel_sleep(gui->panel);
        if(res!=ESP_OK) {
            UNLOCK_GUI;
           return res;
       }
    }

    ESP_LOGI(TAG, "Sidebar sleep");
    if(gui->sidebar) {
        esp_err_t res=sidebar_sleep(gui->sidebar);
        if(res!=ESP_OK) {
            UNLOCK_GUI;
            return res;
        }
    } else {
        ESP_LOGW(TAG, "No sidebar?");
    }

    UNLOCK_GUI;

    return ESP_OK;
}

esp_err_t panel_sleep(panel_t *panel) {
    ESP_LOGI(TAG, "panel sleep");

    return panel->goto_sleep(panel);
}

void panel_free(panel_t *panel) {
    if(panel) {
        panel->free(panel);
    }
}

void gui_set_panel(gui_t *gui, panel_t *panel) {
    STRUCT_CHECK_MAGIC(gui, GUI_MAGIC, TAG, "set panel");
    ESP_LOGI(TAG, "set panel");

    LOCK_GUI;

    if(gui->panel) {
        // Disconnect _and free_ all resources associated with the current panel
        panel_free(gui->panel);
    }
    gui->panel=panel;
    gui->panel->create_content(panel, gui->lvnd_content);

    UNLOCK_GUI;

    ESP_LOGI(TAG, "done");
}

// Select and display the gui menu screen
gui_t *gui_init() {
    ESP_LOGI(TAG,"Init");

    LOCK_GUI;

    gui_t *gui = calloc(1, sizeof(gui_t));
    STRUCT_INIT_MAGIC(gui, GUI_MAGIC);

    // Init the LV widget tree for the screen
    gui_lv_init(gui);

    // Display the screen
    lv_scr_load(gui->lv_root);

    UNLOCK_GUI;

    ESP_LOGI(TAG,"Done");
    return gui;
}
