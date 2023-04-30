/*
 * ESP32 T-Embed Application Shell
 *
 * This code is a demonstration of the capabilities of the ESP32 T-Embed
 *
 * It is based on and includes code from the ESP-IDF v5.0, ESP examples,
 * T-Embed examples from Lilygo and the LVGL tutorial and examples.
 *
 * "We stand on the shoulders of Giants"
 */
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_err.h"
#include "esp_log.h"
#include "iot_button.h"
#include "iot_knob.h"
#include "tembed.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "tembed_lvgl.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "scr.h"
#include "app_event.h"
#include "esp_sntp.h"
#include "idle.h"
#include "esp_console.h"
#include "cmd_system.h"
#include "cmd_wifi.h"
#include "cmd_nvs.h"
#include "nvs_flash.h"

// Bluetooth support
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#include "gatt_profile.h"

static const char *TAG="tembed";

#define PROFILE_A_APP_ID 0

tembed_t tembed;
gui_t *gui;
extern panel_t *main_scr_init(gui_t *gui);

// TODO: Move these to tembed.c
extern sdmmc_card_t *sdcard_init();
extern void leds(tembed_t tembed);

panel_t *active_scr;
sdmmc_card_t *card;

extern void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
extern void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

ESP_EVENT_DEFINE_BASE(APP_EVENT);

esp_timer_handle_t periodic_timer;

static void periodic_timer_callback(void* arg)
{
    ESP_ERROR_CHECK(esp_event_post_to(app_event_loop, APP_EVENT, APP_EVENT_TICK, NULL, 0, (TickType_t)100));
}

// Called when the APP_EVENT_SHUTDOWN is fired
static void idle_watchdog(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG,"Entering sleep mode");
    gui_sleep(gui);
    gui_free(gui);

    ESP_LOGI(TAG, "Shutdown periphs");
    // TODO: What other services need shutdown here?
    ESP_ERROR_CHECK(tembed->goto_sleep(tembed));
    if(card) {
        ESP_LOGI(TAG,"SDCARD");
        ESP_ERROR_CHECK(esp_vfs_fat_sdcard_unmount("/sdcard", card));
        // sdmmc_host_deinit(); // Is this needed?
    }
    ESP_LOGI(TAG, "Periphs Done");

    // Stop and cleanup the timer
    ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
    ESP_ERROR_CHECK(esp_timer_delete(periodic_timer));

    // Set an wakeup pin
    ESP_ERROR_CHECK(gpio_reset_pin(ESP_DEEP_SLEEP_WAKE_PIN));
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(ESP_DEEP_SLEEP_WAKE_PIN, ESP_DEEP_SLEEP_WAKE_ACTIVE));

    // Configure pullup/downs via RTCIO to tie wakeup pins to inactive level during deepsleep.
    // EXT0 resides in the same power domain (RTC_PERIPH) as the RTC IO pullup/downs.
    // No need to keep that power domain explicitly, unlike EXT1.
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(ESP_DEEP_SLEEP_WAKE_PIN));
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(ESP_DEEP_SLEEP_WAKE_PIN));

    ESP_LOGI(TAG, "Shutdown");
    // This function does not return
    esp_deep_sleep_start();
}

void sdcard_init_task( void *pvParameters )
{
    card=sdcard_init(); // TODO: Move this to tembed.c
    if(card) {
        ESP_ERROR_CHECK(esp_event_post_to(app_event_loop, APP_EVENT, APP_EVENT_SDCARD_INIT, NULL, 0, (TickType_t)100));
    }
    /* Tasks must not attempt to return from their implementing
       function or otherwise exit.  In newer FreeRTOS port
       attempting to do so will result in an configASSERT() being
       called if it is defined.  If it is necessary for a task to
       exit then have the task call vTaskDelete( NULL ) to ensure
       its exit is clean. */
    vTaskDelete( NULL );
}

static void init_bt() {
    ESP_LOGI(TAG, "Init BT");
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(esp_gap_cb));
    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(esp_gattc_cb));
    ESP_ERROR_CHECK(esp_ble_gattc_app_register(PROFILE_A_APP_ID));
    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(500));
    ESP_LOGI(TAG, "BT Done");
}

static void chip_info()
{
    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "This is %s chip with %d CPU core(s), WiFi%s%s, ",
             CONFIG_IDF_TARGET,
             chip_info.cores,
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    unsigned major_rev = chip_info.revision / 100
        ;
    unsigned minor_rev = chip_info.revision % 100;
    ESP_LOGI(TAG, "silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        ESP_LOGE(TAG,"Get flash size failed");
        return;
    }

    ESP_LOGI(TAG, "%uMB %s flash", flash_size / (1024 * 1024),
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    ESP_LOGI(TAG, "Minimum free heap size: %d bytes", esp_get_minimum_free_heap_size());

    ESP_LOGI(TAG, "GPIO can wakeup RTC: %s",esp_sleep_is_valid_wakeup_gpio(ESP_DEEP_SLEEP_WAKE_PIN) ? "true" : "false");
}

esp_event_loop_handle_t app_event_loop;

static void wifi_scan_start(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    sidebar_wifi_state(gui->sidebar, WIFI_SCANNING);
}

static void wifi_active(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    sidebar_wifi_state(gui->sidebar, WIFI_ACTIVE);
}

void app_main(void)
{
    ACTION(); // Reset the idle watchdog

    ESP_LOGI(TAG,"Hello T-Embed!");

    // Do these as early as possible to try to avoid race conditions
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    // Set timezone to UTC-6. Note posix reverses the sign for some reason
    setenv("TZ", "UTC+6", 1);
    tzset();

    chip_info();

    // Do this early to allocate large buffers before fragmentation.
    // TODO: Can we do this using static?
    tembed_lvgl_alloc();

    // Initialize NVS (used to persist WiFi settings)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create the application event loop
    esp_event_loop_args_t event_loop_args = {
        .queue_size = 5,
        .task_name = NULL
    };
    ESP_ERROR_CHECK(esp_event_loop_create(&event_loop_args, &app_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop, APP_EVENT, APP_EVENT_SHUTDOWN, idle_watchdog, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop, APP_EVENT, APP_EVENT_WIFI_SCAN, wifi_scan_start, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop, APP_EVENT, APP_EVENT_WIFI_ACTIVE, wifi_active, NULL));

    // Initialize the T-Embed
    tembed = tembed_init(notify_lvgl_flush_ready, &lvgl_disp_drv);

    // Turn on the LEDs (just a demo)
    leds(tembed);

    // Configure LVGL to use the 1.7" LCD on the T-Embed
    tembed_lvgl_init(tembed);

    ESP_LOGI(TAG, "Display App Shell");
    gui = gui_init(tembed);
    active_scr = main_scr_init(gui);
    gui_set_panel(gui, active_scr);

#if CONFIG_TEMBED_INIT_WIFI
    esp_err_t res=esp_wifi_connect();
    switch(res) {
        // If we get this error, the stored SSID isn't valid
        // In that case, we ignore the error, leave the device
        // unconnected and wait for the user to pick a new device
        // TODO: Set the sidebar state correctly as a visual cue
    case ESP_ERR_WIFI_SSID:
        sidebar_wifi_state(gui->sidebar, WIFI_UNCONFIGURED);
        break;
    default: {
        ESP_ERROR_CHECK(res); // For all other errors let ERROR_CHECK handle it
        sidebar_wifi_state(gui->sidebar, WIFI_ACTIVE);
    }
    }
#endif

    // Setup a timer to tick once per second
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &periodic_timer_callback,
        .name = "tick"
    };
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 1000000));

    // Begin initializing the SD card in the background
    xTaskCreate(sdcard_init_task,"sdcard_init", 4096, NULL, 1, NULL);

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    repl_config.prompt = ">";
    repl_config.max_cmdline_length = 80;
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));

    register_system();
#ifdef CONFIG_TEMBED_INIT_WIFI
    register_wifi();
#endif
    register_nvs();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    init_bt();

    // Main loop - this is exited if the idle timer runs out and the system enters deep sleep
    while (1) {
        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        // vTaskDelay(pdMS_TO_TICKS(10)); - Removed as we use the event loop
        LOCK_GUI;
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        lv_timer_handler();
        UNLOCK_GUI;
        ESP_ERROR_CHECK(esp_event_loop_run(app_event_loop, pdMS_TO_TICKS(10)));
    }
}
