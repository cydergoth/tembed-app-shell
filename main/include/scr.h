// Common core for all the screens

#pragma once

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "magic.h"
#include "esp_err.h"
#include "tembed.h"
#include "idle.h"

// https://github.com/espressif/esp-iot-solution/issues/245
#define BAD_KNOB_USR_DATA

#define CONVERT_888RGB_TO_565BGR(r, g, b) ((r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11))

typedef struct gui gui_t;

// Sidebar indicators. A gui consists of a title, a sidebar and a Panel
typedef struct sidebar {
    MAGIC_FIELD;
    gui_t *scr; // Owning screen
    lv_obj_t *lv_root; // Root object
    lv_obj_t *wifi; // Wifi state indicator
    lv_obj_t *bt; // Bluetooth state indicator
    lv_obj_t *sdcard; // Scard state indicator
    lv_obj_t *battery;
    bool wifi_scanning; // Wifi scanning animation is running
    bool handlers_installed;
    esp_event_handler_instance_t ip_handler;
    esp_event_handler_instance_t wifi_handler;
    esp_event_handler_instance_t sdcard_handler;
} sidebar_t;

typedef struct panel panel_t;

typedef void (*panel_free_func)(panel_t *panel);

// Callback for when sleep starting
typedef esp_err_t (*sleep_cb_t)(panel_t *panel);

// Common structure for all embedded content of screens
// The panel contains the main content for the screen
// and may be swapped independently of the whole screen
typedef struct panel {
    MAGIC_FIELD;
    void (*create_content)(panel_t *panel, lv_obj_t *parent);
    panel_free_func free;
    sleep_cb_t goto_sleep; // Called when the sleep code is requesting enter sleep
    bool handlers_installed;
    lv_obj_t *lv_root;
} panel_t;


// Top level structure of the GUI - contains title, sidebar and the panel host
typedef struct gui {
    MAGIC_FIELD;
    lv_obj_t *lv_root; // Root lv_obj for the screen

    // LV children shortcuts, don't call obj_del on these as they are owned by the lv_root
    lv_obj_t *lvnd_title;
    lv_obj_t *lvnd_content;

    // Child components
    panel_t *panel;
    sidebar_t *sidebar;
} gui_t;

// LV Obj used when there is no content to display (keeps pointers populated)
extern lv_obj_t *lv_blank;

// The currently activated screen
extern panel_t *active_scr;

typedef enum {
    WIFI_UNCONFIGURED,
    WIFI_SCANNING,
    WIFI_DISABLED,
    WIFI_ACTIVE,
    WIFI_DISCONNECTED
} wifi_state_t;

void sidebar_wifi_state(sidebar_t *sidebar, wifi_state_t state);

// Mutex to protect the all the GUI state. Take this before
// updating any GUI state
extern SemaphoreHandle_t gui_mutex;

extern gui_t *gui_init();
extern esp_err_t gui_sleep(gui_t *gui);
extern void gui_free(gui_t *gui);
extern void gui_set_panel(gui_t *gui, panel_t *panel);

extern sidebar_t *sidebar_init(gui_t *gui);
extern esp_err_t sidebar_sleep(sidebar_t *sidebar);
extern void sidebar_free(sidebar_t *sidebar);

extern esp_err_t panel_sleep(panel_t *panel);
extern void panel_free(panel_t *panel);
extern void panel_create_content(panel_t *panel, lv_obj_t *parent);

#define GUI_LOCKS
#ifdef GUI_LOCKS
#define LOCK_GUI assert(xSemaphoreTakeRecursive(gui_mutex, (TickType_t)100)==pdTRUE)
#define UNLOCK_GUI xSemaphoreGiveRecursive(gui_mutex)
#else
#define LOCK_GUI
#define UNLOCK_GUI
#endif
static const lv_color16_t grey={.full=CONVERT_888RGB_TO_565BGR(0xC0,0xC0,0xC0)};
static const lv_color16_t white={.full=0xFFFF};
static const lv_color16_t black={.full=0x0000};
static const lv_color16_t blue={.full=CONVERT_888RGB_TO_565BGR(0x00,0x00,0xFF)};
static const lv_color16_t red ={.full=CONVERT_888RGB_TO_565BGR(0xFF,0x00,0x00)};

extern gui_t *gui;
extern tembed_t tembed;

extern void gui_set_menu_title(char *title);
