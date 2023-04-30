#pragma once

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"

// This flag is used to enable magic number checking on the usr_data
// when the scr struct is passed to a callback
#define STRUCT_MAGIC

#ifdef STRUCT_MAGIC
typedef struct magic {
    uint16_t magic;
} magic_t;

#define STRUCT_MAGIC_NUM 0xF3
#define STRUCT_MAKE_MAGIC(m) ((m << 8) | STRUCT_MAGIC_NUM)
#define STRUCT_GET_MAGIC(m) (m & 0xFF)
inline void STRUCT_CHECK_MAGIC(void *v, uint16_t m, const char *tag, const char *e) {
    if(v) {
        if(!((magic_t *)v)->magic) {
            ESP_LOGE(tag,"Uninitalized or use after free: (0x%04x) %s",((magic_t *)v)->magic,e);
            ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
        }
        if(((magic_t*)v)->magic!=(m)) {
            ESP_LOGE(tag,"Invalid magic: (0x%04x)!=(0x%04x) %s",((magic_t *)v)->magic,m,e);
            ESP_ERROR_CHECK(ESP_ERR_INVALID_VERSION);
        }
    } else {
        ESP_LOGE(tag,"Null pointer: %s",e);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
    }
}
#define MAGIC_FIELD uint16_t magic
inline void STRUCT_INIT_MAGIC(void *v, uint16_t m) { (((magic_t*)v)->magic=m); }

// Call this before freeing a struct to trigger asserts on use after free
inline void STRUCT_INVALIDATE(void *v) {((magic_t *)v)->magic = 0x0000;}
#else
#define MAGIC_FIELD
inline void STRUCT_CHECK_MAGIC(void *v, uint16_t m, const char *tag,const char *e) {}
inline void STRUCT_INVALIDATE(void *v) {}
inline void STRUCT_INIT_MAGIC(void *v, uint16_t m) {}
#endif
