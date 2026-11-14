# uart-frame-protocol

[简体中文](README.md) | English

A generic UART frame transport: a fixed 4-byte header, a variable payload and a 1-byte CRC8. Length-delimited and binary-safe, with a byte-fed receive state machine. It targets small messages between an MCU and a host (target coordinates, IMU data, commands) and does not require both ends to share a language or a chip.

The transport layer only packs TYPE/LEN/SEQ/PAYLOAD into a frame, validates the CRC and splits it back into fields. Payload meaning lives in the message registry below, so adding a message type is one registry row and no transport change.

## Requirements

- Chip: any MCU with a UART. The C implementation depends on `stdint.h` and `stddef.h` only, with no platform, OS or dynamic memory.
- Framework and toolchain: a C99 compiler (verified with arm-none-eabi-gcc 16.1.0 on Cortex-M3 and host gcc 16.1.0); Python 3.7 or newer (standard library only).
- Dependencies: none. No other module in this repository is required.

## Frame format

A frame has six fields and is 5 + LEN bytes long:

```
+------+------+------+------+---------------------+------+
| SOF  | TYPE | LEN  | SEQ  | PAYLOAD (LEN bytes) | CRC8 |
| 0xAA | 1 B  | 1 B  | 1 B  | 0 .. 255 B          | 1 B  |
+------+------+------+------+---------------------+------+
offset 0    1      2      3     4 .. 3+LEN           4+LEN
```

| Field | Length | Byte order | Value | Notes |
| --- | --- | --- | --- | --- |
| SOF | 1 B | none (single byte) | fixed `0xAA` | Frame start. The receiver drops non-`0xAA` bytes in IDLE |
| TYPE | 1 B | none (single byte) | `0x00` .. `0xFF` | Message type, see the registry; `0x00` is reserved for ACK |
| LEN | 1 B | none (single byte) | `0` .. `255` | Counts payload bytes only; the sole delimiting field |
| SEQ | 1 B | none (single byte) | `0` .. `255`, wrapping | Sender adds 1 per frame; in an ACK it holds the acknowledged SEQ |
| PAYLOAD | LEN B | little-endian for multi-byte fields | any byte | Binary safe: no escaping, `0xAA` may appear inside |
| CRC8 | 1 B | none (single byte) | `0x00` .. `0xFF` | Covers SOF through the last payload byte, excluding itself |

Delimiting is by length: after LEN arrives, the receiver counts LEN payload bytes and then one CRC byte. `0xAA` therefore means "frame start" only in IDLE; a `0xAA` inside the payload is never mistaken for a new frame.

Maximum frame size = 4 + 255 + 1 = 260 bytes.

## CRC8 parameters and check vector

| Parameter | Value |
| --- | --- |
| Algorithm | CRC-8/SMBus |
| Polynomial | `0x07` (x^8 + x^2 + x + 1) |
| Initial value | `0x00` |
| Input reflection | no (MSB-first) |
| Output reflection | no |
| Final XOR | `0x00` |
| Coverage | `SOF` through the last `PAYLOAD` byte (4 + LEN bytes) |
| Check vector | ASCII `"123456789"` gives `0xF4` |

Both ends use a bitwise algorithm, so no lookup table is needed; C and Python agree byte for byte.

## Receive state machine

```
       non-0xAA dropped
   +-------------------+
   v                   |
 [IDLE] --0xAA--> [HEADER] --TYPE/LEN/SEQ--> [BODY] --LEN+1 bytes--> validate
   ^                   |                        |                        |
   |                   +------- idle timeout ----+                        |
   +-------------- CRC fail dropped / frame delivered --------------------+
```

| Current state | Input | Action | Next state |
| --- | --- | --- | --- |
| IDLE | byte == `0xAA` | Mark frame start | HEADER |
| IDLE | any other byte | Drop | IDLE |
| HEADER | bytes 1..3 | Store TYPE, LEN, SEQ in order | BODY after the third byte |
| BODY | first LEN bytes | Store into the payload buffer | BODY |
| BODY | byte number LEN+1 | Compare with locally computed CRC | Match: deliver frame, back to IDLE; mismatch: drop the frame, back to IDLE |
| any | idle longer than the configured timeout | Drop the partial frame | IDLE |

The timeout prevents a wedged decoder: if the line delivers a single `0xAA`, the decoder sits in BODY waiting for bytes that never come. The module owns no timer, so the caller detects idle time and calls `ufp_decoder_reset()` (Python: `Decoder.reset()`) to return to IDLE. The timeout is an application setting; keep it at least three byte times.

## ACK mechanism

`TYPE = 0x00` is reserved for ACK. Its `SEQ` field carries the SEQ of the frame being acknowledged and its payload is exactly one status byte:

```
AA 00 01 <acknowledged SEQ> <status> <CRC8>     LEN = 1, status 0x00 = OK
```

Which messages need an ACK is a per-type registry flag, not a transport rule:

- Streaming messages (`AIM_TARGET`, `IMU_DATA`) need no ACK; the next frame supersedes a lost one.
- Reliable messages (`CMD`) need an ACK.
- ACK itself is never acknowledged.

The transport layer supplies the flag and carries the status back; it does not implement retransmission, retry timers or duplicate caches. The sender uses `needs_ack` to decide whether to wait for an ACK and what a timeout means, and the application decides what a non-zero status implies.

## SEQ, loss, duplication and reordering

The sender adds 1 to SEQ for every frame it emits, wrapping 0..255. The receiver compares the incoming SEQ with the value it expects:

- equal to the expected value: in order, advance the expectation.
- ahead of it by d (mod 256): d frames were lost.
- equal to the previously received SEQ: duplicate.
- backward within a 128-frame window: reordered.

The transport layer carries SEQ but does not track per-link receive state, so this comparison belongs to the application. That choice lets one link carry several independent sequence spaces.

## LEN extension headroom

This version fixes LEN at one byte, so the payload is at most 255 bytes. The reserved path to larger payloads:

- Treat bit 7 (`0x80`) of `TYPE` as an extension flag. When set, LEN becomes a 2-byte little-endian length (0..65535) and the frame grows by one byte; when clear, the 1-byte LEN of this version applies and older nodes keep working.
- A version byte is the alternative, but it shifts every field of every frame, so the `TYPE` bit is preferred.

This version does not implement the extension: bit 7 of `TYPE` has no special meaning and neither end decodes extended frames. The transport layer does not consult the registry, so an unregistered TYPE is framed and validated normally and handed to the application, which should drop it as unknown.

## Message registry

The registry is data, not code branches. Adding a message type is one row; the transport layer stays unchanged.

| TYPE | Name | Needs ACK | Payload length | Payload schema | Notes |
| --- | --- | --- | --- | --- | --- |
| `0x00` | ACK | no (never acknowledged) | 1 B | `status(1B)`, `0x00` = OK | SEQ holds the acknowledged frame's SEQ |
| `0x01` | AIM_TARGET | no (streaming) | 6 B | `dx(int16 LE), dy(int16 LE), area(uint16 LE)` | Coordinate semantics are **TBD**: relative offset or absolute, to be decided when needed |
| `0x02` | IMU_DATA | no (streaming) | reserved | undefined | To be defined from the sensor output |
| `0x03` | CMD | yes (reliable) | reserved | undefined; example `cmd(1B) + value(uint16 LE)` | Requires an ACK |

**AIM_TARGET coordinate semantics are marked TBD.** Only the 6-byte layout is agreed (two int16 coordinates plus a uint16 area or scale, all little-endian); whether `dx`/`dy` are relative offsets or absolute coordinates is undecided, so consumers must not assume either interpretation.

Where that table lives in code:

- C: the `UFP_TYPE_*` constants in `include/uart_frame_protocol.h`.
- Python: the `TYPE_*` constants and `MESSAGE_REGISTRY` in `src/uart_frame_protocol.py` (name, ACK flag, fixed payload length, schema text).

The codec never reads the registry: `pack`/`unpack` and the C `ufp_pack`/`ufp_unpack` treat every TYPE alike and only frame and validate.

### What each column means

- **TYPE**: the message id, 0..255. `0x00` is reserved for ACK; the rest are assigned as needed. Only this one byte goes on the wire, never the name.
- **Name**: a human label that appears only in docs and code constants.
- **Needs ACK**: whether a lost message should be retransmitted. Streaming data (coordinates, IMU) is "no" — the next frame supersedes a lost one; commands are "yes".
- **Payload length**: the fixed payload size in bytes, or "reserved" when it varies or is undefined.
- **Payload schema**: the name, type and byte order of every payload field. This column is what actually defines the payload layout.
- **Notes**: extra conventions, such as what an ACK's SEQ holds.

### Adding a message

Adding a message only touches the registry; the frame format, CRC and state machine stay unchanged. A heartbeat `PING` as an example:

1. Pick an unused TYPE, say `0x10`.
2. Decide three things: the name `PING`, whether it needs an ACK (a heartbeat usually does not) and the payload layout (empty here, so `LEN=0`).
3. In C, add `#define UFP_TYPE_PING 0x10` to `uart_frame_protocol.h`.
4. In Python, add `TYPE_PING = 0x10` and a `MessageSpec(type_id=0x10, name="PING", needs_ack=False, payload_len=0, schema="no payload")` entry to `MESSAGE_REGISTRY`.
5. Handle the payload on each end per the schema. The transport and codec need no changes.

## Wiring and configuration

The UART side needs the usual three wires: TX to the peer's RX, RX to the peer's TX, and a common ground, at the level both chips expect (commonly 3.3 V). The protocol fixes no pins and uses no hardware flow control.

| Setting | Type | Unit and range | Default | Required | Notes |
| --- | --- | --- | --- | --- | --- |
| Baud rate | `uint32_t` | typically 9600 .. 921600 | example 115200 | yes | Must match at both ends; the UART peripheral sets the ceiling |
| Data bits / parity / stop bits | — | 8 / none / 1 | 8N1 | yes | The frame already carries CRC8, so a parity bit is usually unnecessary |
| Idle timeout | `uint32_t` | ms, at least three byte times | example 20 ms | yes | On expiry, reset the decoder and drop the partial frame |
| Initial TX SEQ | `uint8_t` | `0` .. `255` | `0` | yes | One counter per direction, plus 1 per frame with wrap |
| Timeout poll period | — | ms, typically 1 .. 10 | example 5 ms | no | Periodic task checking the last receive timestamp |

## Integration steps

C side (header-only, no build configuration):

1. Copy `include/uart_frame_protocol.h` into the project and compile with at least `-std=c99`. No extra `.c` file or link step is needed.
2. Declare one `ufp_decoder_t` per UART port (about 264 bytes) and call `ufp_decoder_init()` at startup.
3. In the UART receive interrupt or callback, call `ufp_decoder_feed()` for every byte. When it returns 1, `decoder.frame` holds the complete frame; copy it into a queue instead of doing slow work in the interrupt.
4. Keep a `uint8_t` SEQ counter on the sending side, build the payload, call `ufp_pack()` and hand the bytes to the platform UART write.
5. Call `ufp_decoder_reset()` when the idle check sees a silent line.

Python side:

1. Add `src/uart_frame_protocol.py` to the project (or to `sys.path`); there are no third-party dependencies.
2. Send with `pack()`; decode whole buffers with `unpack()` and streamed or fragmented input with `Decoder`.
3. Call `Decoder.reset()` after a long gap without bytes.

## Minimal example

C: send one AIM_TARGET frame and decode bytes in the receive interrupt.

```c
#include <stdint.h>
#include "uart_frame_protocol.h"

/* Provided by the platform adapter; replace with the project's UART write. */
int board_uart_write(const uint8_t *data, size_t len);
void board_handle_cmd(uint8_t cmd, uint16_t value);

static ufp_decoder_t g_decoder;
static uint8_t g_tx_seq;

void link_init(void)
{
    ufp_decoder_init(&g_decoder);
    g_tx_seq = 0u;
}

/* Call after the line has been silent; drops a partial frame. */
void link_idle_timeout(void)
{
    ufp_decoder_reset(&g_decoder);
}

static void put_u16le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

/* Payload layout comes from the registry: dx(int16 LE) + dy(int16 LE) + area(uint16 LE). */
int link_send_aim_target(int16_t dx, int16_t dy, uint16_t area)
{
    uint8_t payload[6];
    uint8_t frame[UFP_FRAME_MAX];
    size_t frame_len = 0u;
    int status;

    put_u16le(&payload[0], (uint16_t)dx);
    put_u16le(&payload[2], (uint16_t)dy);
    put_u16le(&payload[4], area);

    status = ufp_pack(UFP_TYPE_AIM_TARGET, g_tx_seq, payload, sizeof(payload),
                      frame, sizeof(frame), &frame_len);
    if (status != UFP_OK) {
        return status;
    }
    g_tx_seq = (uint8_t)(g_tx_seq + 1u); /* wraps 0..255 */

    /* Replace board_uart_write() with the platform UART write. */
    return board_uart_write(frame, frame_len) == (int)frame_len ? UFP_OK : -1;
}

static void on_frame(uint8_t type, uint8_t seq, const uint8_t *payload, uint8_t payload_len);

/* Called once per byte from the UART receive interrupt. */
void link_rx_byte(uint8_t byte)
{
    if (ufp_decoder_feed(&g_decoder, byte) == 1) {
        on_frame(g_decoder.frame.type, g_decoder.frame.seq,
                 g_decoder.frame.payload, g_decoder.frame.payload_len);
    }
}

static void on_frame(uint8_t type, uint8_t seq, const uint8_t *payload, uint8_t payload_len)
{
    if (type == UFP_TYPE_CMD && payload_len == 3u) {
        /* Example schema from the registry: cmd(1B) + value(uint16 LE). */
        uint8_t cmd = payload[0];
        uint16_t value = (uint16_t)((uint16_t)payload[1] | ((uint16_t)payload[2] << 8));
        uint8_t ack_status = 0x00u;
        uint8_t ack[UFP_FRAME_MAX];
        size_t ack_len = 0u;

        board_handle_cmd(cmd, value);

        /* The ACK SEQ holds the acknowledged frame's SEQ; payload is one status byte. */
        if (ufp_pack(UFP_TYPE_ACK, seq, &ack_status, 1u, ack, sizeof(ack), &ack_len) == UFP_OK) {
            board_uart_write(ack, ack_len);
        }
    }
}
```

Python: encode golden frame G2 and decode it back as a stream.

```python
import struct

import uart_frame_protocol as ufp

seq = 1  # G2's SEQ
decoder = ufp.Decoder()


def encode_aim_target(dx, dy, area):
    global seq
    frame = ufp.pack(ufp.TYPE_AIM_TARGET, seq, struct.pack("<hhH", dx, dy, area))
    seq = (seq + 1) % 256
    return frame


# Fragments must reassemble, so feed three bytes at a time on purpose.
stream = encode_aim_target(10, -20, 4500)
for start in range(0, len(stream), 3):
    for frame in decoder.feed_bytes(stream[start:start + 3]):
        spec = ufp.MESSAGE_REGISTRY[frame.type]
        print(spec.name, frame.seq, struct.unpack("<hhH", frame.payload))
# AIM_TARGET 1 (10, -20, 4500)
```

## API reference

### ufp_crc8

**Signature**

```c
uint8_t ufp_crc8(const uint8_t *data, size_t len);
```

**Purpose and preconditions**

Computes CRC-8/SMBus. No initialization is required and it may be called from any context.

**Parameters**

| Parameter | Type and direction | Unit, range and default | Notes |
| --- | --- | --- | --- |
| `data` | `const uint8_t *`; input | length given by `len` | Bytes to check; `NULL` counts as zero bytes and returns `0x00` |
| `len` | `size_t`; input | `0` .. `SIZE_MAX` | Byte count |

**Return value and errors**

Returns the CRC (`0x00` .. `0xFF`). No error code.

**Call constraints**

No global state, reentrant, interrupt-safe; runtime is linear in `len`.

**Example**

```c
static const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
uint8_t crc = ufp_crc8(check, sizeof(check)); /* 0xF4 */
```

### ufp_crc8_update

**Signature**

```c
uint8_t ufp_crc8_update(uint8_t crc, const uint8_t *data, size_t len);
```

**Purpose and preconditions**

Continues a CRC over more bytes, for fragmented computation. Start with `0x00`.

**Parameters**

| Parameter | Type and direction | Unit, range and default | Notes |
| --- | --- | --- | --- |
| `crc` | `uint8_t`; input | `0x00` .. `0xFF` | Previous CRC result; `0x00` on the first call |
| `data` | `const uint8_t *`; input | length given by `len` | Bytes to fold in; `NULL` returns `crc` unchanged |
| `len` | `size_t`; input | `0` .. `SIZE_MAX` | Byte count |

**Return value and errors**

Returns the updated CRC. No error code.

**Call constraints**

No global state, reentrant.

**Example**

```c
uint8_t crc = ufp_crc8_update(0x00u, header, 4u);
crc = ufp_crc8_update(crc, payload, payload_len);
```

### ufp_pack

**Signature**

```c
int ufp_pack(uint8_t type, uint8_t seq, const uint8_t *payload,
             size_t payload_len, uint8_t *out,
             size_t out_capacity, size_t *out_len);
```

**Purpose and preconditions**

Serializes type, sequence and payload into one frame and appends the CRC. Payload bytes are never interpreted.

**Parameters**

| Parameter | Type and direction | Unit, range and default | Notes |
| --- | --- | --- | --- |
| `type` | `uint8_t`; input | `0x00` .. `0xFF` | Message type |
| `seq` | `uint8_t`; input | `0x00` .. `0xFF` | Frame sequence, owned by the caller |
| `payload` | `const uint8_t *`; input | `NULL` only when `payload_len == 0` | Payload bytes, `0xAA` allowed |
| `payload_len` | `size_t`; input | `0` .. `255` | Payload size; above 255 returns `UFP_ERR_ARG` |
| `out` | `uint8_t *`; output | capacity at least `5 + payload_len` | Frame output buffer |
| `out_capacity` | `size_t`; input | `0` .. `SIZE_MAX` | Capacity of `out` in bytes |
| `out_len` | `size_t *`; output | points to a `size_t` | Receives the frame size on success |

**Return value and errors**

`UFP_OK` (0) on success; `UFP_ERR_ARG` (-1) for a null pointer or an oversized payload; `UFP_ERR_CAPACITY` (-2) when the buffer cannot hold the frame. On failure `*out_len` is left untouched and the caller should discard the frame.

**Call constraints**

No global state, reentrant, interrupt-safe. The output buffer belongs to the caller.

**Example**

```c
uint8_t payload[6] = {0};
uint8_t frame[UFP_FRAME_MAX];
size_t frame_len = 0u;
if (ufp_pack(UFP_TYPE_AIM_TARGET, 0u, payload, sizeof(payload),
             frame, sizeof(frame), &frame_len) == UFP_OK) {
    /* frame_len == 11 and the bytes are golden frame G1 */
}
```

### ufp_unpack

**Signature**

```c
int ufp_unpack(const uint8_t *frame, size_t frame_len, ufp_frame_t *out_frame);
```

**Purpose and preconditions**

Validates and splits one complete frame: checks the SOF, that LEN agrees with the buffer length, and the CRC.

**Parameters**

| Parameter | Type and direction | Unit, range and default | Notes |
| --- | --- | --- | --- |
| `frame` | `const uint8_t *`; input | at least 5 bytes | Complete frame buffer |
| `frame_len` | `size_t`; input | `0` .. `260` | Bytes in the buffer; must equal `5 + LEN` |
| `out_frame` | `ufp_frame_t *`; output | non-null | Receives type/seq/payload/payload_len on success |

**Return value and errors**

`UFP_OK` on success; `UFP_ERR_ARG` (-1) for null pointers; `UFP_ERR_LENGTH` (-4) when the buffer is too short or disagrees with LEN; `UFP_ERR_SOF` (-3) when the first byte is not `0xAA`; `UFP_ERR_CRC` (-5) on a checksum failure. On failure `*out_frame` is undefined and should be discarded.

**Call constraints**

No global state, reentrant. On success `out_frame->payload` points inside the caller's `frame` buffer and stays valid only while that buffer lives; a zero-length payload yields `NULL`.

**Example**

```c
ufp_frame_t frame;
if (ufp_unpack(rx_buffer, rx_len, &frame) == UFP_OK) {
    if (frame.type == UFP_TYPE_ACK && frame.payload_len == 1u) {
        uint8_t status = frame.payload[0];
    }
}
```

### ufp_decoder_init

**Signature**

```c
void ufp_decoder_init(ufp_decoder_t *decoder);
```

**Purpose and preconditions**

Puts the decoder in IDLE and clears any partial frame. Call once before receiving; a `NULL` pointer returns immediately.

**Parameters**

| Parameter | Type and direction | Unit, range and default | Notes |
| --- | --- | --- | --- |
| `decoder` | `ufp_decoder_t *`; input/output | may be null | Decoder instance, one per UART port |

**Return value and errors**

No return value.

**Call constraints**

Not reentrant (writes `decoder`). Call before the receive interrupt starts so it cannot race `ufp_decoder_feed`.

**Example**

```c
static ufp_decoder_t decoder;

void link_init(void)
{
    ufp_decoder_init(&decoder);
}
```

### ufp_decoder_reset

**Signature**

```c
void ufp_decoder_reset(ufp_decoder_t *decoder);
```

**Purpose and preconditions**

Drops the current partial frame and returns to IDLE; this is the idle-timeout entry point. Equivalent to `ufp_decoder_init`.

**Parameters**

| Parameter | Type and direction | Unit, range and default | Notes |
| --- | --- | --- | --- |
| `decoder` | `ufp_decoder_t *`; input/output | may be null | Decoder instance |

**Return value and errors**

No return value.

**Call constraints**

Not reentrant. Call only after the line is known to be idle (past the configured timeout), otherwise it discards a valid frame in progress.

**Example**

```c
if (now_ms - last_rx_ms > LINK_IDLE_TIMEOUT_MS) {
    ufp_decoder_reset(&decoder);
    last_rx_ms = now_ms;
}
```

### ufp_decoder_feed

**Signature**

```c
int ufp_decoder_feed(ufp_decoder_t *decoder, uint8_t byte);
```

**Purpose and preconditions**

Feeds one received byte into the state machine. Call it byte by byte, in wire order.

**Parameters**

| Parameter | Type and direction | Unit, range and default | Notes |
| --- | --- | --- | --- |
| `decoder` | `ufp_decoder_t *`; input/output | non-null | Initialized decoder |
| `byte` | `uint8_t`; input | `0x00` .. `0xFF` | One received byte |

**Return value and errors**

Returns 1 when a frame passed the CRC and is complete, with the result in `decoder->frame`; returns 0 while the frame is incomplete or when it was dropped for a CRC error or noise. No error code.

**Call constraints**

Not reentrant: one instance may only be fed from one context. `decoder->frame.payload` points into the decoder's internal buffer and is overwritten by the next frame, so copy it out during the call that returned 1. The cost is independent of the byte count, so this may run inside the UART interrupt.

**Example**

```c
void link_rx_byte(uint8_t byte)
{
    if (ufp_decoder_feed(&decoder, byte) == 1) {
        queue_push(&rx_queue, decoder.frame.type, decoder.frame.seq,
                   decoder.frame.payload, decoder.frame.payload_len);
    }
}
```

### ufp_frame_t

**Signature**

```c
typedef struct {
    uint8_t type;
    uint8_t seq;
    const uint8_t *payload;
    uint8_t payload_len;
} ufp_frame_t;
```

**Fields**

| Field | Type | Unit, range and default | Notes |
| --- | --- | --- | --- |
| `type` | `uint8_t` | `0x00` .. `0xFF` | Message type |
| `seq` | `uint8_t` | `0x00` .. `0xFF` | Frame sequence |
| `payload` | `const uint8_t *` | `NULL` when the length is 0 | Payload pointer; lifetime follows its source (input frame or decoder) |
| `payload_len` | `uint8_t` | `0` .. `255` | Payload size |

**Call constraints**

A `ufp_unpack` result points into the caller's frame buffer; a `ufp_decoder_feed` result points into the decoder's buffer. Neither allocates memory.

### ufp_decoder_t

**Signature**

```c
typedef struct {
    uint8_t state;
    uint8_t header[3];
    uint8_t header_len;
    uint8_t body_len;
    uint8_t payload[UFP_PAYLOAD_MAX];
    ufp_frame_t frame;
} ufp_decoder_t;
```

**Fields**

| Field | Type | Notes |
| --- | --- | --- |
| `state` | `uint8_t` | Current state, one of `ufp_decoder_state_t` |
| `header[3]` | `uint8_t[3]` | Received TYPE, LEN, SEQ |
| `header_len` | `uint8_t` | Header bytes received (0 .. 3) |
| `body_len` | `uint8_t` | Payload bytes received so far |
| `payload[255]` | `uint8_t[255]` | Payload buffer |
| `frame` | `ufp_frame_t` | Last decoded frame, valid when a call returned 1 |

**Call constraints**

The decoder owns the struct contents; callers only read `frame`. It is about 264 bytes, so allocate one per port outside the task stack.

### Python: crc8 / pack / unpack

```python
def crc8(data: bytes) -> int
def pack(frame_type: int, seq: int, payload: bytes = b"") -> bytes
def unpack(frame: bytes) -> Frame
```

`crc8` returns `0` .. `255`. `pack` returns the complete frame and raises `ValueError` when `frame_type` or `seq` is outside `0` .. `255` or the payload exceeds 255 bytes. `unpack` returns a `Frame` and raises `UartFrameProtocolError` (a `ValueError` subclass) on a bad SOF, a length that disagrees with LEN, or a CRC failure. All three are pure functions with no shared state.

| Parameter | Type and direction | Unit, range and default | Notes |
| --- | --- | --- | --- |
| `data` | `bytes`; input | any length | Bytes to check |
| `frame_type` | `int`; input | `0` .. `255` | Message type |
| `seq` | `int`; input | `0` .. `255` | Frame sequence |
| `payload` | `bytes`; input | `0` .. `255` bytes, default `b""` | Payload, `0xAA` allowed |
| `frame` | `bytes`; input | at least 5 bytes | Complete frame |

```python
import uart_frame_protocol as ufp

wire = ufp.pack(ufp.TYPE_ACK, 0x2A, b"\x00")
assert wire == bytes.fromhex("AA00012A0069")
frame = ufp.unpack(wire)
assert (frame.type, frame.seq, frame.payload) == (ufp.TYPE_ACK, 0x2A, b"\x00")
```

### Python: Frame

```python
@dataclass(frozen=True)
class Frame:
    type: int
    seq: int
    payload: bytes
```

Immutable data class. `payload` holds the raw bytes; the registry explains them.

### Python: MessageSpec and MESSAGE_REGISTRY

```python
@dataclass(frozen=True)
class MessageSpec:
    type_id: int
    name: str
    needs_ack: bool
    payload_len: Optional[int]
    schema: str

MESSAGE_REGISTRY: Dict[int, MessageSpec]
```

The registry is keyed by TYPE. `payload_len` is the fixed payload size, or `None` for a reserved or variable payload; `needs_ack` marks reliable messages; `schema` is a human-readable field description. The codec never reads this table; it serves the semantic layer and the tests.

### Python: Decoder

```python
class Decoder:
    def reset(self) -> None
    def feed(self, byte: int) -> Optional[Frame]
    def feed_bytes(self, data: bytes) -> List[Frame]
```

A byte-fed state machine matching the C `ufp_decoder_feed`: noise, CRC errors and partial frames are dropped without raising. `feed` takes one byte and returns a `Frame` when one completes, otherwise `None`; `feed_bytes` returns every frame completed in a chunk, in arrival order. `reset` is the idle-timeout entry point. A `byte` outside `0` .. `255` raises `ValueError`.

| Parameter | Type and direction | Unit, range and default | Notes |
| --- | --- | --- | --- |
| `byte` | `int`; input | `0` .. `255` | One received byte |
| `data` | `bytes`; input | any length | A chunk of received bytes, may be fragmented |

```python
decoder = ufp.Decoder()
frames = decoder.feed_bytes(b"\x00\x55\xBB" + ufp.pack(ufp.TYPE_IMU_DATA, 3))
assert len(frames) == 1
decoder.reset()
```

## Build and checks

Python unit tests, including byte-exact pack/unpack/CRC assertions for the five golden frames:

```bash
cd modules/protocols/uart-frame-protocol
python -m unittest discover -s tests -v
```

The C side only needs a compile check (header-only, no separate translation unit):

```bash
cd modules/protocols/uart-frame-protocol
gcc -std=c99 -Wall -Wextra -pedantic -Werror -I include -c your_file.c -o your_file.o
```

Golden frames shared by both implementations:

| # | Bytes | Meaning |
| --- | --- | --- |
| G1 | `AA 01 06 00 00 00 00 00 B8 0B F3` | AIM_TARGET dx=0 dy=0 area=3000, SEQ=0 |
| G2 | `AA 01 06 01 0A 00 EC FF 94 11 82` | AIM_TARGET dx=+10 dy=-20 area=4500, SEQ=1 |
| G3 | `AA 01 06 05 00 00 00 00 00 00 8F` | AIM_TARGET all zero, SEQ=5 |
| G4 | `AA 00 01 2A 00 69` | ACK for SEQ=0x2A, status=OK |
| G5 | `AA 03 03 10 01 19 00 43` | CMD cmd=1 value=25, SEQ=16 |

## Limits and resources

- The payload tops out at 255 bytes and a frame at 260 bytes because LEN is one byte; larger payloads need the LEN extension, which this version does not implement.
- No escaping, fragmentation, flow control or automatic retransmission. An ACK is a flag plus a status byte; retransmission, duplicate suppression and timeout policy belong to the application.
- No multi-link management: one decoder instance per UART port, and the application partitions sequence spaces.
- The idle timeout is not implemented in the protocol layer; the caller must run a timer and call reset, otherwise a partial frame holds the decoder indefinitely.
- CRC8 detects errors on short frames in a low-noise link only; no bit-error-rate analysis was done, so noisy links should add application-level confirmation or a stronger checksum.
- No allocation and no I/O: port reads and writes stay in the platform adapter.
- AIM_TARGET `dx`/`dy` coordinate semantics (relative offset vs absolute) are TBD; the decision updates the registry and the consumers only.
- No third-party code or resources; both the C and Python implementations were written here.
