#include <stdio.h>
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "tembed.h"
#include "apa102.h"
#include "math.h"

static const char *TAG="leds";

// Set the number of LEDs to control.
const uint16_t ledCount = CONFIG_APA102_LED_COUNT;

// We define "power" in this sketch to be the product of the
// 8-bit color channel value and the 5-bit brightness register.
// The maximum possible power is 255 * 31 (7905).
const uint16_t maxPower = 255 * 31;

// The power we want to use on the first LED is 1, which
// corresponds to the dimmest possible white.
const uint16_t minPower = 1;

// This function sends a white color with the specified power,
// which should be between 0 and 7905.
void sendWhite(const apa102_t *apa102,uint16_t power)
{
    // Choose the lowest possible 5-bit brightness that will work.
    uint8_t brightness5Bit = 1;
    while(brightness5Bit * 255 < power && brightness5Bit < 31)
    {
        brightness5Bit++;
    }

    // Uncomment this line to simulate an LED strip that does not
    // have the extra 5-bit brightness register.  You will notice
    // that roughly the first third of the LED strip turns off
    // because the brightness8Bit equals zero.
    //brightness = 31;

    // Set brightness8Bit to be power divided by brightness5Bit,
    // rounded to the nearest whole number.
    uint8_t brightness8Bit = (power + (brightness5Bit / 2)) / brightness5Bit;

    // Send the white color to the LED strip.  At this point,
    // brightness8Bit multiplied by brightness5Bit should be
    // approximately equal to power.
    apa102_sendColor(apa102,brightness8Bit, brightness8Bit, brightness8Bit, brightness5Bit);
}

void leds(tembed_t tembed) {
    ESP_LOGI(TAG, "LEDS");
// Calculate what the ratio between the powers of consecutive
// LEDs needs to be in order to reach the max power on the last
// LED of the strip.
    float multiplier = pow(maxPower / minPower, 1.0 / (ledCount - 1));
    apa102_startFrame(&tembed->leds);
    float power = minPower;
    for(uint16_t i = 0; i < ledCount; i++)
    {
        sendWhite(&tembed->leds,power);
        power = power * multiplier;
    }
    apa102_endFrame(&tembed->leds,ledCount);
}
