#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"

#include "iot_button.h"
#include "iot_knob.h"
#include "lvgl.h"
#include "tembed.h"

static const char *TAG "image_scr"

// https://github.com/espressif/esp-iot-solution/issues/245
#define BAD_KNOB_USR_DATA

extern unsigned char *img_logo;
extern void main_scr(tembed_t tembed);

typedef struct image_scr {
#ifdef BAD_KNOB_USR_DATA
    uint16_t magic;
#endif
    lv_obj_t *scr;
    lv_img_dsc_t image_data;
    tembed_t tembed;
} *image_scr_t;

// Handle a click on the image
static void image_click_cb(void *arg, void *data)
{
    ACTION;
    assert(data);
    image_scr_t image = (image_scr_t)data;
#ifdef BAD_KNOB_USR_DATA
    assert(image->magic==0xF2F3);
#endif
    ESP_ERROR_CHECK(iot_button_unregister_cb((button_handle_t)arg,BUTTON_SINGLE_CLICK));

    main_scr(image->tembed);

    lv_obj_del(image->scr); // Also deletes all the children
    free(image);
}

// Select and display the main menu screen
void image_scr(tembed_t tembed) {
    image_scr_t image = cmalloc(1, sizeof(struct image_scr));
    ESP_LOGI(TAG,"Init");
#ifdef BAD_KNOB_USR_DATA
    image->magic=0xF2F3;
#endif
    image->tembed = tembed;

    image->image_data.header.h=170;
    image->image_data.header.w=320;
    image->image_data.header.always_zero=0;
    image->image_data.header.cf=LV_IMG_CF_RGB565;
    image->image_data.data=img_logo;
    image->image_data.data_size=108800;

    // Top level object for this screen
    // Uses a column flow layout
    image->scr = lv_obj_create(NULL);
    lv_obj_set_width(image->scr, 320);
    lv_obj_set_height(image->scr, 170);
    lv_obj_set_style_bg_color(image->scr, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);

    lv_obj_t *img = lv_img_create(image->scr);
    // lv_obj_set_width(img, 320);
    // lv_obj_set_height(img, 170);
    lv_img_set_src(img, &image->image_data);

    // Register the event handlers for the knob for this screen
    ESP_ERROR_CHECK(iot_button_register_cb(tembed->dial.btn, BUTTON_SINGLE_CLICK, image_click_cb, image));

    // Display the screen
    lv_scr_load(image->scr);

    ESP_LOGI(TAG,"Done");
}
