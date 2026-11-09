#include "ssd1306.h"

static int send_bytes(ssd1306_t *display, bool is_data,
                       const uint8_t *bytes, size_t length)
{
    int error = display->config.write(display->config.context, is_data,
                                       bytes, length);
    if (error != SSD1306_OK) {
        /* A partial command/data transaction cannot be rolled back. */
        display->initialized = false;
        display->positioned = false;
    }
    return error;
}

static int check_display(const ssd1306_t *display)
{
    if (display == NULL) {
        return SSD1306_ERR_ARGUMENT;
    }
    return display->initialized ? SSD1306_OK : SSD1306_ERR_STATE;
}

int ssd1306_init(ssd1306_t *display, const ssd1306_config_t *config)
{
    if (display == NULL || config == NULL || config->write == NULL ||
        (config->height != 32 && config->height != 64)) {
        return SSD1306_ERR_ARGUMENT;
    }
    /* Copy first so config may point at display->config during recovery. */
    const ssd1306_config_t copy = *config;
    *display = (ssd1306_t){.config = copy};
    const uint8_t commands[] = {
        0xAE, 0x00, 0x10, 0x40,
        0x81, copy.contrast,
        copy.rotated ? 0xA0 : 0xA1,
        copy.rotated ? 0xC0 : 0xC8,
        copy.inverted ? 0xA7 : 0xA6,
        0xA8, (uint8_t)(copy.height - 1U),
        0xD3, 0x00, 0xD5, 0x80, 0xD9, 0xF1,
        0xDA, copy.height == 32 ? 0x02 : 0x12,
        0xDB, 0x20,
        0x20, 0x02,
        0x8D, 0x14, 0xA4
    };
    int error = send_bytes(display, false, commands, sizeof(commands));
    if (error != SSD1306_OK) {
        return error;
    }
    display->initialized = true;
    error = ssd1306_clear(display);
    if (error != SSD1306_OK) {
        return error;
    }
    return ssd1306_set_display(display, true);
}

int ssd1306_set_position(ssd1306_t *display, uint8_t column, uint8_t page)
{
    int error = check_display(display);
    if (error != SSD1306_OK) {
        return error;
    }
    if (column >= 128 || page >= display->config.height / 8U) {
        return SSD1306_ERR_ARGUMENT;
    }
    const uint8_t commands[] = {
        (uint8_t)(0xB0U | page),
        (uint8_t)(0x10U | (column >> 4)),
        (uint8_t)(column & 0x0FU)
    };
    error = send_bytes(display, false, commands, sizeof(commands));
    if (error == SSD1306_OK) {
        display->column = column;
        display->page = page;
        display->positioned = true;
    }
    return error;
}

int ssd1306_write_data(ssd1306_t *display, const uint8_t *bytes, size_t length)
{
    int error = check_display(display);
    if (error != SSD1306_OK) {
        return error;
    }
    if (length == 0) {
        return SSD1306_OK;
    }
    if (!display->positioned) {
        return SSD1306_ERR_STATE;
    }
    if (bytes == NULL || length > 128U - display->column) {
        return SSD1306_ERR_ARGUMENT;
    }
    error = send_bytes(display, true, bytes, length);
    if (error == SSD1306_OK) {
        display->column = (uint16_t)(display->column + length);
    }
    return error;
}

int ssd1306_clear(ssd1306_t *display)
{
    int error = check_display(display);
    if (error != SSD1306_OK) {
        return error;
    }
    const uint8_t blank[128] = {0};
    for (uint8_t page = 0; page < display->config.height / 8U; ++page) {
        error = ssd1306_set_position(display, 0, page);
        if (error != SSD1306_OK) {
            return error;
        }
        error = ssd1306_write_data(display, blank, sizeof(blank));
        if (error != SSD1306_OK) {
            return error;
        }
    }
    return SSD1306_OK;
}

int ssd1306_set_inverted(ssd1306_t *display, bool inverted)
{
    int error = check_display(display);
    if (error != SSD1306_OK) {
        return error;
    }
    const uint8_t command = inverted ? 0xA7 : 0xA6;
    error = send_bytes(display, false, &command, 1);
    if (error == SSD1306_OK) {
        display->config.inverted = inverted;
    }
    return error;
}

int ssd1306_set_display(ssd1306_t *display, bool enabled)
{
    int error = check_display(display);
    if (error != SSD1306_OK) {
        return error;
    }
    const uint8_t on[] = {0x8D, 0x14, 0xAF};
    const uint8_t off[] = {0xAE, 0x8D, 0x10};
    return send_bytes(display, false, enabled ? on : off, sizeof(on));
}

int ssd1306_set_orientation(ssd1306_t *display, bool rotated)
{
    int error = check_display(display);
    if (error != SSD1306_OK) {
        return error;
    }
    const uint8_t commands[] = {
        rotated ? 0xC0 : 0xC8,
        rotated ? 0xA0 : 0xA1
    };
    error = send_bytes(display, false, commands, sizeof(commands));
    if (error == SSD1306_OK) {
        display->config.rotated = rotated;
    }
    return error;
}

int ssd1306_draw_bitmap(ssd1306_t *display, uint8_t column, uint8_t page,
                        uint8_t width, uint8_t page_count,
                        const uint8_t *bitmap, size_t length)
{
    int error = check_display(display);
    if (error != SSD1306_OK) {
        return error;
    }
    if (bitmap == NULL || width == 0 || page_count == 0 || column >= 128 ||
        width > 128U - column || page >= display->config.height / 8U ||
        page_count > display->config.height / 8U - page ||
        length < (size_t)width * page_count) {
        return SSD1306_ERR_ARGUMENT;
    }
    for (uint8_t row = 0; row < page_count; ++row) {
        error = ssd1306_set_position(display, column, (uint8_t)(page + row));
        if (error != SSD1306_OK) {
            return error;
        }
        error = ssd1306_write_data(display, bitmap + (size_t)row * width, width);
        if (error != SSD1306_OK) {
            return error;
        }
    }
    return SSD1306_OK;
}
