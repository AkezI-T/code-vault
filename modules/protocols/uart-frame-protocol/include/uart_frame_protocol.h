/*
 * Generic UART frame transport: framing, CRC8 and a byte-fed decoder.
 *
 * One frame on the wire:
 *
 *   +-----+------+-----+-----+---------------------+------+
 *   | SOF | TYPE | LEN | SEQ | PAYLOAD (LEN bytes) | CRC8 |
 *   | AA  | 1 B  | 1 B | 1 B | 0..255 B            | 1 B  |
 *   +-----+------+-----+-----+---------------------+------+
 *
 * SOF is always 0xAA. LEN counts payload bytes only; the length field
 * delimits the frame, so payload bytes are never escaped and may themselves
 * be 0xAA. Multi-byte fields inside a payload are little-endian. The
 * single-byte header fields have no byte order. CRC8 covers SOF through the
 * last payload byte and excludes the CRC byte itself.
 *
 * CRC parameters: CRC-8/SMBus - polynomial 0x07, init 0x00, MSB-first, no
 * input/output reflection, no final XOR. Check vector: the ASCII string
 * "123456789" yields 0xF4.
 *
 * This header is the transport layer only. It moves TYPE/LEN/SEQ/PAYLOAD and
 * validates the CRC without interpreting payload bytes. Payload layout lives
 * in the message registry in README.md, so adding a message type is a
 * registry change rather than a change here.
 *
 * Golden frames (byte-exact, mirrored by tests/test_uart_frame_protocol.py):
 *   G1  AA 01 06 00 00 00 00 00 B8 0B F3   AIM_TARGET dx=0  dy=0   area=3000, SEQ=0
 *   G2  AA 01 06 01 0A 00 EC FF 94 11 82   AIM_TARGET dx=+10 dy=-20 area=4500, SEQ=1
 *   G3  AA 01 06 05 00 00 00 00 00 00 8F   AIM_TARGET all zero,          SEQ=5
 *   G4  AA 00 01 2A 00 69                  ACK for SEQ=0x2A, status=OK
 *   G5  AA 03 03 10 01 19 00 43            CMD cmd=1 value=25,           SEQ=16
 *   G6  AA 11 02 00 FD 00 4B               TEMP temp=253 (25.3 degC), SEQ=0
 *   G7  AA 02 06 00 00 00 00 00 96 00 31   IMU_DATA gx=0 gy=0 gz=150, SEQ=0
 *
 * C99, header-only, depends on <stdint.h> and <stddef.h> only.
 */

#ifndef CODE_VAULT_UART_FRAME_PROTOCOL_H
#define CODE_VAULT_UART_FRAME_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Frame layout constants. */
#define UFP_SOF 0xAAu
#define UFP_HEADER_LEN 4u
#define UFP_CRC_LEN 1u
#define UFP_PAYLOAD_MAX 255u
#define UFP_FRAME_MAX (UFP_HEADER_LEN + UFP_PAYLOAD_MAX + UFP_CRC_LEN)

/* Message registry type codes. ACK is reserved; the rest are documented in
 * README.md. Unknown codes still frame and validate normally. */
#define UFP_TYPE_ACK 0x00u
#define UFP_TYPE_AIM_TARGET 0x01u
#define UFP_TYPE_IMU_DATA 0x02u
#define UFP_TYPE_CMD 0x03u
#define UFP_TYPE_TEMP 0x11u

/* Return codes. All public calls return UFP_OK (0) on success. */
#define UFP_OK 0
#define UFP_ERR_ARG (-1)      /* NULL pointer or value outside its domain   */
#define UFP_ERR_CAPACITY (-2) /* output buffer cannot hold the whole frame  */
#define UFP_ERR_SOF (-3)      /* frame does not start with 0xAA             */
#define UFP_ERR_LENGTH (-4)   /* LEN disagrees with the supplied frame size */
#define UFP_ERR_CRC (-5)      /* CRC byte does not match the frame body     */

typedef enum {
    UFP_DECODER_IDLE = 0,   /* hunting for SOF                            */
    UFP_DECODER_HEADER = 1, /* SOF seen, collecting TYPE/LEN/SEQ          */
    UFP_DECODER_BODY = 2    /* collecting LEN payload bytes then CRC      */
} ufp_decoder_state_t;

/* A validated or packed frame. `payload` aliases the caller's buffer and
 * stays valid only as long as that buffer does. */
typedef struct {
    uint8_t type;
    uint8_t seq;
    const uint8_t *payload;
    uint8_t payload_len;
} ufp_frame_t;

/* Byte-fed decoder for a UART receive interrupt. One struct per port. */
typedef struct {
    uint8_t state;             /* ufp_decoder_state_t */
    uint8_t header[3];         /* TYPE, LEN, SEQ in arrival order */
    uint8_t header_len;
    uint8_t body_len;          /* payload bytes collected so far */
    uint8_t payload[UFP_PAYLOAD_MAX];
    ufp_frame_t frame;         /* valid once ufp_decoder_feed returns 1 */
} ufp_decoder_t;

/* CRC-8/SMBus update helper: feeds bytes into an already started CRC. */
static inline uint8_t ufp_crc8_update(uint8_t crc, const uint8_t *data, size_t len)
{
    size_t index;
    if (data == NULL) {
        return crc;
    }
    for (index = 0; index < len; ++index) {
        uint8_t bit;
        crc = (uint8_t)(crc ^ data[index]);
        for (bit = 0; bit < 8u; ++bit) {
            if ((crc & 0x80u) != 0u) {
                crc = (uint8_t)((uint8_t)(crc << 1) ^ 0x07u);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* CRC-8/SMBus over `len` bytes; ufp_crc8("123456789", 9) == 0xF4. */
static inline uint8_t ufp_crc8(const uint8_t *data, size_t len)
{
    return ufp_crc8_update(0x00u, data, len);
}

/* Serializes one frame into `out`. `payload_len` must fit LEN (<= 255) and
 * `out_capacity` must be at least 4 + payload_len + 1. Writes the frame size
 * to *out_len on success. Golden frames G1..G5 at the top of this file are the
 * byte images this function must reproduce. */
static inline int ufp_pack(uint8_t type, uint8_t seq, const uint8_t *payload,
                           size_t payload_len, uint8_t *out,
                           size_t out_capacity, size_t *out_len)
{
    size_t index;
    size_t body_len;
    size_t total_len;

    if (out == NULL || out_len == NULL) {
        return UFP_ERR_ARG;
    }
    if (payload_len > (size_t)UFP_PAYLOAD_MAX) {
        return UFP_ERR_ARG;
    }
    if (payload_len > 0u && payload == NULL) {
        return UFP_ERR_ARG;
    }
    total_len = (size_t)UFP_HEADER_LEN + payload_len + (size_t)UFP_CRC_LEN;
    if (out_capacity < total_len) {
        return UFP_ERR_CAPACITY;
    }

    out[0] = (uint8_t)UFP_SOF;
    out[1] = type;
    out[2] = (uint8_t)payload_len;
    out[3] = seq;
    for (index = 0; index < payload_len; ++index) {
        out[(size_t)UFP_HEADER_LEN + index] = payload[index];
    }

    body_len = (size_t)UFP_HEADER_LEN + payload_len;
    out[body_len] = ufp_crc8(out, body_len);
    *out_len = body_len + (size_t)UFP_CRC_LEN;
    return UFP_OK;
}

/* Validates and decodes one complete frame. Rejects a missing SOF, a LEN that
 * disagrees with `frame_len`, and a bad CRC. On UFP_OK the payload fields of
 * *out_frame alias `frame`. */
static inline int ufp_unpack(const uint8_t *frame, size_t frame_len,
                             ufp_frame_t *out_frame)
{
    size_t payload_len;
    size_t crc_index;
    int status;

    if (frame == NULL || out_frame == NULL) {
        return UFP_ERR_ARG;
    }
    if (frame_len < (size_t)UFP_HEADER_LEN + (size_t)UFP_CRC_LEN) {
        return UFP_ERR_LENGTH;
    }
    if (frame[0] != (uint8_t)UFP_SOF) {
        return UFP_ERR_SOF;
    }

    payload_len = (size_t)frame[2];
    if (frame_len != (size_t)UFP_HEADER_LEN + payload_len + (size_t)UFP_CRC_LEN) {
        return UFP_ERR_LENGTH;
    }

    crc_index = (size_t)UFP_HEADER_LEN + payload_len;
    status = UFP_OK;
    if (ufp_crc8(frame, crc_index) != frame[crc_index]) {
        status = UFP_ERR_CRC;
    }
    if (status != UFP_OK) {
        return status;
    }

    out_frame->type = frame[1];
    out_frame->seq = frame[3];
    out_frame->payload_len = (uint8_t)payload_len;
    out_frame->payload = (payload_len > 0u) ? (frame + UFP_HEADER_LEN) : NULL;
    return UFP_OK;
}

/* Prepares a decoder for use. */
static inline void ufp_decoder_init(ufp_decoder_t *decoder)
{
    if (decoder == NULL) {
        return;
    }
    decoder->state = (uint8_t)UFP_DECODER_IDLE;
    decoder->header_len = 0u;
    decoder->body_len = 0u;
    decoder->frame.type = 0u;
    decoder->frame.seq = 0u;
    decoder->frame.payload = NULL;
    decoder->frame.payload_len = 0u;
}

/* Drops any partially received frame and returns to hunting for SOF. Call it
 * from the idle-timeout hook so a truncated frame cannot wedge the decoder. */
static inline void ufp_decoder_reset(ufp_decoder_t *decoder)
{
    ufp_decoder_init(decoder);
}

/* Feeds one received byte. Returns 1 after a frame with a valid CRC completes,
 * otherwise 0. On 1 the completed frame is in decoder->frame and its payload
 * aliases decoder->payload until the next call. Feeding the G1..G5 byte
 * sequences from the top of this file reproduces each frame exactly. */
static inline int ufp_decoder_feed(ufp_decoder_t *decoder, uint8_t byte)
{
    if (decoder == NULL) {
        return 0;
    }

    switch ((ufp_decoder_state_t)decoder->state) {
    case UFP_DECODER_IDLE:
        if (byte == (uint8_t)UFP_SOF) {
            decoder->state = (uint8_t)UFP_DECODER_HEADER;
            decoder->header_len = 0u;
        }
        return 0;

    case UFP_DECODER_HEADER:
        decoder->header[decoder->header_len] = byte;
        ++decoder->header_len;
        if (decoder->header_len < 3u) {
            return 0;
        }
        decoder->body_len = 0u;
        decoder->state = (uint8_t)UFP_DECODER_BODY;
        return 0;

    case UFP_DECODER_BODY: {
        uint8_t payload_len = decoder->header[1];
        if (decoder->body_len < payload_len) {
            decoder->payload[decoder->body_len] = byte;
            ++decoder->body_len;
            return 0;
        }
        /* LEN payload bytes are in place, so this byte is the CRC. */
        {
            const uint8_t sof = (uint8_t)UFP_SOF;
            uint8_t crc;
            crc = ufp_crc8_update(0x00u, &sof, 1u);
            crc = ufp_crc8_update(crc, decoder->header, 3u);
            crc = ufp_crc8_update(crc, decoder->payload, (size_t)payload_len);

            decoder->state = (uint8_t)UFP_DECODER_IDLE;
            decoder->header_len = 0u;
            decoder->body_len = 0u;
            if (crc != byte) {
                return 0;
            }
            decoder->frame.type = decoder->header[0];
            decoder->frame.seq = decoder->header[2];
            decoder->frame.payload_len = payload_len;
            decoder->frame.payload = (payload_len > 0u) ? decoder->payload : NULL;
            return 1;
        }
    }

    default:
        /* Corrupted state byte: recover instead of looping forever. */
        decoder->state = (uint8_t)UFP_DECODER_IDLE;
        decoder->header_len = 0u;
        decoder->body_len = 0u;
        return 0;
    }
}

#endif
