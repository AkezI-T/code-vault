#ifndef CODE_VAULT_SSD1306_H
#define CODE_VAULT_SSD1306_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SSD1306_OK = 0,
    SSD1306_ERR_ARGUMENT = -1,
    SSD1306_ERR_STATE = -2
};

/* Synchronous: consume all bytes before return. Return 0 or a positive error. */
typedef int (*ssd1306_write_fn)(void *context, bool is_data,
                                const uint8_t *bytes, size_t length);

typedef struct {
    ssd1306_write_fn write;
    void *context;
    uint8_t height;
    uint8_t contrast;
    bool inverted;
    bool rotated;
} ssd1306_config_t;

/* Caller-owned storage. Fields are private to the implementation after init. */
typedef struct {
    ssd1306_config_t config;
    uint16_t column;
    uint8_t page;
    bool initialized;
    bool positioned;
} ssd1306_t;

/* The caller powers/resets the controller and waits for readiness before init. */
int ssd1306_init(ssd1306_t *display, const ssd1306_config_t *config);
int ssd1306_set_position(ssd1306_t *display, uint8_t column, uint8_t page);
int ssd1306_write_data(ssd1306_t *display, const uint8_t *bytes, size_t length);
int ssd1306_clear(ssd1306_t *display);
int ssd1306_set_inverted(ssd1306_t *display, bool inverted);
int ssd1306_set_display(ssd1306_t *display, bool enabled);
int ssd1306_set_orientation(ssd1306_t *display, bool rotated);
/* Page-major, one vertical 8-pixel column per byte; no clipping or blending. */
int ssd1306_draw_bitmap(ssd1306_t *display, uint8_t column, uint8_t page,
                        uint8_t width, uint8_t page_count,
                        const uint8_t *bitmap, size_t length);

#ifdef __cplusplus
}
#endif
#endif
