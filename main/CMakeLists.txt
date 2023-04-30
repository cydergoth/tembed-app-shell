idf_component_register(SRCS "tembed_main.c" "tembed_lvgl.c" "leds.c" "ble_gap.c" "ble_gattc.c" "ble_cache.c"
  "screens/main_scr.c"
  "screens/sidebar.c"
  "screens/gui.c"
  "screens/col_scr.c"
  "screens/sdcard_scr.c"
  "screens/wifi_scr.c"
  "screens/settings_scr.c"
  "screens/smart_scr.c"
  "idle.c"
  INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
