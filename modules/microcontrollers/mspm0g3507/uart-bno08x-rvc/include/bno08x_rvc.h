/* Source provenance and license status: see ../SOURCE.md. */
#ifndef BNO08X_RVC_H
#define BNO08X_RVC_H

#include <stdint.h>

#define BNO08X_RVC_FRAME_SIZE 19u

typedef enum {
    BNO08X_RVC_INVALID_ARGUMENT = -2,
    BNO08X_RVC_BAD_CHECKSUM = -1,
    BNO08X_RVC_NONE = 0,
    BNO08X_RVC_SAMPLE = 1
} bno08x_rvc_event_t;

typedef struct {
    uint8_t index;
    float yaw_deg;
    float pitch_deg;
    float roll_deg;
    float accel_mg[3];
    uint8_t reserved[3];
} bno08x_rvc_sample_t;

typedef struct {
    uint8_t buffer[BNO08X_RVC_FRAME_SIZE];
    uint8_t used;
} bno08x_rvc_parser_t;

/* Returns 0 on success, -1 for a null parser. Also discards partial frames. */
int bno08x_rvc_init(bno08x_rvc_parser_t *parser);

/* Writes sample only on BNO08X_RVC_SAMPLE. Objects must not overlap. */
bno08x_rvc_event_t bno08x_rvc_feed_byte(bno08x_rvc_parser_t *parser,
                                      uint8_t byte,
                                      bno08x_rvc_sample_t *sample);

#endif
