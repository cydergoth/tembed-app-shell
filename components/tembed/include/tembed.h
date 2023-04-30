#pragma once

#include "sdkconfig.h"
#include "esp_err.h"

#ifdef CONFIG_TEMBED_INIT_LCD
#include "hal/spi_types.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "st7789.h"
#include "esp_sntp.h"
#include "esp_wifi.h"

// from https://github.com/Xinyuan-LilyGO/T-Embed
#define TEMBED_LCD_H_RES              170
#define TEMBED_LCD_V_RES              320
#endif

#ifdef CONFIG_TEMBED_INIT_LEDS
#include "apa102.h"
#endif

#ifdef CONFIG_TEMBED_INIT_DIAL
#include "iot_button.h"
#include "iot_knob.h"
#endif

#include "esp_wifi.h"

#define ESP_DEEP_SLEEP_WAKE_PIN CONFIG_TEMBED_DIAL_BUTTON_IO_NUM
#define ESP_DEEP_SLEEP_WAKE_ACTIVE CONFIG_TEMBED_DIAL_BUTTON_ACTIVE_LEVEL

typedef struct tembed {

    esp_err_t (*goto_sleep)(struct tembed *tembed);
#ifdef CONFIG_TEMBED_INIT_LCD
    esp_lcd_panel_handle_t lcd;
#endif
#ifdef CONFIG_TEMBED_INIT_LEDS
    apa102_t leds;
#endif
#ifdef CONFIG_TEMBED_INIT_DIAL
    struct dial {
        button_handle_t btn;
        knob_handle_t knob;
    } dial;
#endif
    esp_netif_t *netif;
} *tembed_t;

extern tembed_t tembed_init(
#ifdef CONFIG_TEMBED_INIT_LCD
    esp_lcd_panel_io_color_trans_done_cb_t notify_color_trans_done, void *user_data
#endif
    );
