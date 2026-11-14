"""Portable codec for the generic UART frame transport protocol.

One frame on the wire::

    SOF(0xAA) | TYPE(1) | LEN(1) | SEQ(1) | PAYLOAD(LEN bytes) | CRC8(1)

LEN counts payload bytes only. The length field delimits the frame, so no byte
is escaped and the payload may contain 0xAA. Multi-byte fields inside a payload
are little-endian; the single-byte header fields have no byte order. CRC8
covers SOF through the last payload byte and excludes the CRC byte itself.

CRC parameters are CRC-8/SMBus: polynomial 0x07, init 0x00, MSB-first, no
reflection, no final XOR. Check vector: the ASCII string "123456789" yields
0xF4.

`pack` and `unpack` never interpret payload bytes. Message layouts live in
MESSAGE_REGISTRY, so adding a message type is a table entry, not a codec change.

Python 3.7+; standard library only.
"""

from dataclasses import dataclass
from typing import Dict, List, Optional

__all__ = [
    "SOF",
    "HEADER_LEN",
    "CRC_LEN",
    "MAX_PAYLOAD",
    "MAX_FRAME_LEN",
    "TYPE_ACK",
    "TYPE_AIM_TARGET",
    "TYPE_IMU_DATA",
    "TYPE_CMD",
    "MessageSpec",
    "MESSAGE_REGISTRY",
    "UartFrameProtocolError",
    "Frame",
    "crc8",
    "pack",
    "unpack",
    "Decoder",
]

SOF = 0xAA
HEADER_LEN = 4
CRC_LEN = 1
MAX_PAYLOAD = 255
MAX_FRAME_LEN = HEADER_LEN + MAX_PAYLOAD + CRC_LEN

TYPE_ACK = 0x00
TYPE_AIM_TARGET = 0x01
TYPE_IMU_DATA = 0x02
TYPE_CMD = 0x03


@dataclass(frozen=True)
class MessageSpec:
    """Registry entry describing what a TYPE code carries.

    `payload_len` is the fixed payload size in bytes, or None for a variable
    or not-yet-defined payload. `needs_ack` marks reliable messages; the
    stream types do not request ACK. The codec itself ignores all of this.
    """

    type_id: int
    name: str
    needs_ack: bool
    payload_len: Optional[int]
    schema: str


#: Message registry. Adding a message type is one entry here; the transport
#: layer (pack/unpack/Decoder) does not change.
MESSAGE_REGISTRY: Dict[int, MessageSpec] = {
    TYPE_ACK: MessageSpec(
        type_id=TYPE_ACK,
        name="ACK",
        needs_ack=False,
        payload_len=1,
        schema="status(1B), 0x00 = OK; SEQ carries the acknowledged frame SEQ",
    ),
    TYPE_AIM_TARGET: MessageSpec(
        type_id=TYPE_AIM_TARGET,
        name="AIM_TARGET",
        needs_ack=False,
        payload_len=6,
        schema="dx(int16 LE), dy(int16 LE), area(uint16 LE); "
               "example message, dx/dy are offsets from the image center",
    ),
    TYPE_IMU_DATA: MessageSpec(
        type_id=TYPE_IMU_DATA,
        name="IMU_DATA",
        needs_ack=False,
        payload_len=None,
        schema="reserved, undefined",
    ),
    TYPE_CMD: MessageSpec(
        type_id=TYPE_CMD,
        name="CMD",
        needs_ack=True,
        payload_len=None,
        schema="reserved, undefined; example cmd(1B) + value(uint16 LE)",
    ),
}


class UartFrameProtocolError(ValueError):
    """Raised when a byte string is not a structurally valid frame."""


@dataclass(frozen=True)
class Frame:
    """A decoded frame. `payload` holds the raw bytes exactly as received."""

    type: int
    seq: int
    payload: bytes


def crc8(data: bytes) -> int:
    """Return the CRC-8/SMBus of `data`.

    >>> crc8(b"123456789")
    244
    """

    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def pack(frame_type: int, seq: int, payload: bytes = b"") -> bytes:
    """Serialize one frame.

    `frame_type` and `seq` must fit one byte. `payload` may be at most
    MAX_PAYLOAD bytes and is copied verbatim, so it may contain 0xAA.
    """

    payload = bytes(payload)
    if not 0 <= frame_type <= 0xFF:
        raise ValueError("frame_type must be in 0..255, got %r" % (frame_type,))
    if not 0 <= seq <= 0xFF:
        raise ValueError("seq must be in 0..255, got %r" % (seq,))
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload is %d bytes, limit is %d" % (len(payload), MAX_PAYLOAD))

    body = bytes((SOF, frame_type, len(payload), seq)) + payload
    return body + bytes((crc8(body),))


def unpack(frame: bytes) -> Frame:
    """Validate one complete frame and return it.

    Raises UartFrameProtocolError when the SOF is missing, LEN disagrees with
    the buffer length, or the CRC does not match. The payload is returned
    verbatim; its layout is the message registry's concern.
    """

    data = bytes(frame)
    if len(data) < HEADER_LEN + CRC_LEN:
        raise UartFrameProtocolError(
            "frame is %d bytes, minimum is %d" % (len(data), HEADER_LEN + CRC_LEN)
        )
    if data[0] != SOF:
        raise UartFrameProtocolError("bad SOF 0x%02X" % (data[0],))

    payload_len = data[2]
    expected_len = HEADER_LEN + payload_len + CRC_LEN
    if len(data) != expected_len:
        raise UartFrameProtocolError(
            "LEN %d implies %d bytes, got %d" % (payload_len, expected_len, len(data))
        )
    if crc8(data[:-1]) != data[-1]:
        raise UartFrameProtocolError(
            "bad CRC 0x%02X, expected 0x%02X" % (data[-1], crc8(data[:-1]))
        )

    return Frame(
        type=data[1],
        seq=data[3],
        payload=data[HEADER_LEN:HEADER_LEN + payload_len],
    )


class Decoder:
    """Byte-fed receive state machine, mirroring the C decoder.

    Feed bytes as they arrive from the UART. Each completed frame with a valid
    CRC comes back from `feed` (and appears in the list from `feed_bytes`).
    Bytes outside a frame, bad CRCs and truncated frames are dropped without
    raising, so the caller can keep feeding a noisy stream.

    `reset` is the idle-timeout hook: call it when no byte arrived for longer
    than the configured interval so a half-received frame cannot wedge the
    decoder.
    """

    IDLE = 0
    HEADER = 1
    BODY = 2

    def __init__(self) -> None:
        self._state = self.IDLE
        self._header = bytearray()
        self._payload = bytearray()
        self._payload_len = 0

    def reset(self) -> None:
        """Drop any partial frame and start hunting for SOF again."""

        self._state = self.IDLE
        self._header.clear()
        self._payload.clear()
        self._payload_len = 0

    def feed(self, byte: int) -> Optional[Frame]:
        """Feed one received byte; return a Frame only on completion."""

        if not 0 <= byte <= 0xFF:
            raise ValueError("byte must be in 0..255, got %r" % (byte,))

        if self._state == self.IDLE:
            if byte == SOF:
                self._state = self.HEADER
                self._header.clear()
            return None

        if self._state == self.HEADER:
            self._header.append(byte)
            if len(self._header) < 3:
                return None
            self._payload_len = self._header[1]
            self._payload.clear()
            self._state = self.BODY
            return None

        if len(self._payload) < self._payload_len:
            self._payload.append(byte)
            return None

        frame_type = self._header[0]
        seq = self._header[2]
        payload = bytes(self._payload)
        expected_crc = crc8(bytes((SOF,)) + bytes(self._header) + payload)
        self.reset()
        if expected_crc != byte:
            return None
        return Frame(type=frame_type, seq=seq, payload=payload)

    def feed_bytes(self, data: bytes) -> List[Frame]:
        """Feed a chunk; return every frame completed inside it, in order."""

        frames: List[Frame] = []
        for byte in bytes(data):
            frame = self.feed(byte)
            if frame is not None:
                frames.append(frame)
        return frames
