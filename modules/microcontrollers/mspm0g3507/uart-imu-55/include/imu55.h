#ifndef IMU55_H
#define IMU55_H

#include <stdint.h>

#define IMU55_FRAME_SIZE 11u
#define IMU55_DEFAULT_ACCEL_RANGE_G 16.0f
#define IMU55_DEFAULT_GYRO_RANGE_DPS 2000.0f
#define IMU55_VALID_ACCEL 0x01u
#define IMU55_VALID_GYRO 0x02u
#define IMU55_VALID_ANGLES 0x04u
#define IMU55_VALID_TEMPERATURE 0x08u
#define IMU55_VALID_VERSION_RAW 0x10u

typedef enum {
    IMU55_INVALID_ARGUMENT = -2,
    IMU55_BAD_CHECKSUM = -1,
    IMU55_NONE = 0,
    IMU55_SAMPLE = 1,
    IMU55_UNSUPPORTED_TYPE = 2
} imu55_event_t;

typedef struct {
    uint8_t type;
    uint8_t valid_mask;
    float accel_mg[3];
    float gyro_dps[3];
    float angles_deg[3]; /* Roll, pitch, yaw. */
    float temperature_c;
    uint16_t version_raw;
} imu55_sample_t;

typedef struct {
    uint8_t buffer[IMU55_FRAME_SIZE];
    uint8_t used;
    float accel_mg_per_lsb;
    float gyro_dps_per_lsb;
} imu55_parser_t;

/* Returns 0, or -1 for null parser/nonfinite or nonpositive ranges.
 * Successful initialization also discards partial frames. */
int imu55_init(imu55_parser_t *parser, float accel_range_g, float gyro_range_dps);

/* Writes a fresh per-frame event only on IMU55_SAMPLE. Objects must not overlap. */
imu55_event_t imu55_feed_byte(imu55_parser_t *parser, uint8_t byte,
                             imu55_sample_t *sample);

#endif
