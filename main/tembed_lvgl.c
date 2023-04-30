#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"
#include "tembed.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "tembed_lvgl.h"
#include "assert.h"
#include "idle.h"
#include "scr.h"
#include "app_event.h"
#include "esp_console.h"

static const char *TAG="lvgl";

// Mutex to lock lvgl widget tree
SemaphoreHandle_t gui_mutex = NULL;

// LVGL update interval
#define LVGL_TICK_PERIOD_MS 2
#define LVGL_BUFFER_LINES 34

lv_obj_t * lv_blank;
static bool lvgl_init_done = false;

esp_timer_handle_t lvgl_tick_timer = NULL;

static int snapshot(int argc, char **argv) {
    LOCK_GUI;
    lv_img_dsc_t *snap=lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
    UNLOCK_GUI;

    ESP_LOGI(TAG, "Snapshot %dx%d", snap->header.w, snap->header.h);
    // Output image
    for(int x=0;x<snap->header.w;x++) {
        for(int y=0;y<snap->header.h;y++) {
            lv_color_t col=lv_img_buf_get_px_color(snap, x, y, lv_color_black());
            printf("%04x ",col.full);
        }
    }
    printf("\n");

    lv_snapshot_free(snap);

    return 0;
}

static void register_cmd_snapshot(void)
{
    const esp_console_cmd_t cmd = {
        .command = "snap",
        .help = "Take a snapshot of the screen",
        .hint = NULL,
        .func = &snapshot,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    if(!lvgl_init_done) return false;
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    ESP_LOGD(TAG, "flush");
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // copy a buffer's content to a specific area of the display
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map));
    ESP_LOGD(TAG, "flush done");
}

/* Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. */
static void lvgl_port_update_callback(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;

    switch (drv->rotated) {
    case LV_DISP_ROT_NONE:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
        break;
    case LV_DISP_ROT_90:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
        break;
    case LV_DISP_ROT_180:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
        break;
    case LV_DISP_ROT_270:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, false);
        break;
    }
}

// ISR ISR ISR ISR ISR
static void increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(LVGL_TICK_PERIOD_MS);

    int64_t now = esp_timer_get_time();

    if(last_action == 0) {
        // Initialize
        last_action = now;
        return;
    }

    if((now - last_action) > IDLE_TIME_uS) {
        last_action=now; // Prevent repeat firing
        ESP_LOGD(TAG, "Firing shutdown event");
        // Trigger the deep sleep shutdown
        ESP_ERROR_CHECK(esp_event_post_to(app_event_loop, APP_EVENT, APP_EVENT_SHUTDOWN, NULL, 0, (TickType_t)100));
    }
}

static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
lv_disp_drv_t lvgl_disp_drv;      // contains callback functions

lv_color_t *buf1=NULL;
lv_color_t *buf2=NULL;

void tembed_lvgl_alloc(void) {
    // Call this early to allocate the large LVGL buffers before DMA memory becomes fragmented
    heap_caps_print_heap_info(MALLOC_CAP_DMA | MALLOC_CAP_32BIT);

    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    buf1 = heap_caps_malloc(320 * LVGL_BUFFER_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
    assert(buf1);
    buf2 = heap_caps_malloc(320 * LVGL_BUFFER_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
    assert(buf2);
    ESP_LOGI(TAG, "Buffers allocated 2 x %d", 320 * LVGL_BUFFER_LINES * sizeof(lv_color_t));

    heap_caps_print_heap_info(MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
}

lv_disp_t *tembed_lvgl_init(tembed_t tembed) {
    if(!buf1) {
        ESP_LOGE(TAG, "Call alloc first!");
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
    gui_mutex = xSemaphoreCreateRecursiveMutex();

    ESP_LOGI(TAG, "Init");
    LOCK_GUI;

    lv_init();

    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, 320 * LVGL_BUFFER_LINES);

    // heap_caps_print_heap_info(MALLOC_CAP_DMA);

    ESP_LOGI(TAG, "Register display");

    lv_disp_drv_init(&lvgl_disp_drv);
    lvgl_disp_drv.hor_res = TEMBED_LCD_H_RES;
    lvgl_disp_drv.ver_res = TEMBED_LCD_V_RES;
    lvgl_disp_drv.flush_cb = lvgl_flush_cb;
    lvgl_disp_drv.drv_update_cb = lvgl_port_update_callback;
    lvgl_disp_drv.draw_buf = &disp_buf;
    lvgl_disp_drv.user_data = tembed->lcd;

    lv_disp_t *disp = lv_disp_drv_register(&lvgl_disp_drv);
    assert(disp);

    ESP_LOGI(TAG, "Tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    // Ensure the coordiate systems align with the physical display
    lv_disp_set_rotation(disp, LV_DISP_ROT_270);

    lv_blank = lv_obj_create(NULL);
    lv_obj_set_width(lv_blank, 320);
    lv_obj_set_height(lv_blank, 170);

    lv_scr_load(lv_blank);

    register_cmd_snapshot();

    lvgl_init_done = true;

    UNLOCK_GUI;

    ESP_LOGI(TAG, "Init done");
    return disp;
}
