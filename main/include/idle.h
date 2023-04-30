#pragma once

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

// Time of the last user interaction
extern volatile int64_t last_action;

// Macro to update the time of the last action (since boot in micro-seconds)
// All UI action handlers should invoke this as first line
static inline void ACTION(void) {last_action = esp_timer_get_time();}

#define MICRO_PER_SECOND 1000000LL
#ifndef FAST_IDLE
#define IDLE_TIME_uS (5 * 60 * MICRO_PER_SECOND)
#else
#define IDLE_TIME_uS (30 * MICRO_PER_SECOND)
#endif
