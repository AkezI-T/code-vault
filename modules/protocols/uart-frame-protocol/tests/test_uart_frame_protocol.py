"""Unit tests for the generic UART frame transport protocol.

Run from the module directory::

    python -m unittest discover -s tests -v

The golden frames below are byte-exact fixtures; the C header carries the same
frames so both implementations are checked against one wire image.
"""

import os
import struct
import sys
import unittest

sys.path.insert(
    0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src")
)

import uart_frame_protocol as ufp  # noqa: E402


def _hex(text):
    return bytes.fromhex(text.replace(" ", ""))


# (wire bytes, TYPE, SEQ, PAYLOAD, description)
GOLDEN_FRAMES = (
    (
        "AA 01 06 00 00 00 00 00 B8 0B F3",
        ufp.TYPE_AIM_TARGET,
        0,
        _hex("00 00 00 00 B8 0B"),
        "G1 AIM_TARGET dx=0 dy=0 area=3000",
    ),
    (
        "AA 01 06 01 0A 00 EC FF 94 11 82",
        ufp.TYPE_AIM_TARGET,
        1,
        _hex("0A 00 EC FF 94 11"),
        "G2 AIM_TARGET dx=+10 dy=-20 area=4500",
    ),
    (
        "AA 01 06 05 00 00 00 00 00 00 8F",
        ufp.TYPE_AIM_TARGET,
        5,
        _hex("00 00 00 00 00 00"),
        "G3 AIM_TARGET all zero",
    ),
    (
        "AA 00 01 2A 00 69",
        ufp.TYPE_ACK,
        0x2A,
        _hex("00"),
        "G4 ACK for SEQ=0x2A, status=OK",
    ),
    (
        "AA 03 03 10 01 19 00 43",
        ufp.TYPE_CMD,
        16,
        _hex("01 19 00"),
        "G5 CMD cmd=1 value=25",
    ),
    (
        "AA 11 02 00 FD 00 4B",
        ufp.TYPE_TEMP,
        0,
        _hex("FD 00"),
        "G6 TEMP temp=253 (25.3 degC)",
    ),
    (
        "AA 02 06 00 00 00 00 00 96 00 31",
        ufp.TYPE_IMU_DATA,
        0,
        _hex("00 00 00 00 96 00"),
        "G7 IMU_DATA gx=0 gy=0 gz=150 (1.50 deg/s)",
    ),
)


class Crc8Test(unittest.TestCase):
    def test_check_vector(self):
        self.assertEqual(ufp.crc8(b"123456789"), 0xF4)

    def test_empty_input(self):
        self.assertEqual(ufp.crc8(b""), 0x00)

    def test_sof_byte_mixes_into_crc(self):
        # 0xAA appears in the golden frames; the CRC must not be confused by it.
        self.assertEqual(ufp.crc8(b"\xAA"), 0x5F)
        self.assertEqual(ufp.crc8(b"\xAA\x00"), 0x9A)


class GoldenFrameTest(unittest.TestCase):
    def test_pack_matches_wire_bytes(self):
        for wire, frame_type, seq, payload, note in GOLDEN_FRAMES:
            with self.subTest(note):
                self.assertEqual(ufp.pack(frame_type, seq, payload), _hex(wire))

    def test_unpack_reads_wire_bytes(self):
        for wire, frame_type, seq, payload, note in GOLDEN_FRAMES:
            with self.subTest(note):
                frame = ufp.unpack(_hex(wire))
                self.assertEqual(frame.type, frame_type)
                self.assertEqual(frame.seq, seq)
                self.assertEqual(frame.payload, payload)

    def test_round_trip(self):
        for wire, frame_type, seq, payload, note in GOLDEN_FRAMES:
            with self.subTest(note):
                self.assertEqual(ufp.pack(*_fields(ufp.unpack(_hex(wire)))), _hex(wire))

    def test_aim_target_payload_is_little_endian(self):
        # The codec stays payload-agnostic; the registry schema drives this.
        dx, dy, area = struct.unpack("<hhH", ufp.unpack(_hex(GOLDEN_FRAMES[0][0])).payload)
        self.assertEqual((dx, dy, area), (0, 0, 3000))
        dx, dy, area = struct.unpack("<hhH", ufp.unpack(_hex(GOLDEN_FRAMES[1][0])).payload)
        self.assertEqual((dx, dy, area), (10, -20, 4500))

    def test_aim_target_built_from_fields_matches_wire_bytes(self):
        payload = struct.pack("<hhH", 10, -20, 4500)
        self.assertEqual(ufp.pack(ufp.TYPE_AIM_TARGET, 1, payload), _hex(GOLDEN_FRAMES[1][0]))


def _fields(frame):
    return frame.type, frame.seq, frame.payload


class BinarySafetyTest(unittest.TestCase):
    def test_payload_containing_sof_byte(self):
        payload = bytes((0xAA, 0x01, 0xAA, 0xAA, 0x00))
        wire = ufp.pack(ufp.TYPE_CMD, 0x07, payload)
        self.assertEqual(wire[4:9], payload)
        frame = ufp.unpack(wire)
        self.assertEqual(frame.payload, payload)

    def test_decoder_is_not_fooled_by_sof_inside_payload(self):
        payload = bytes((0xAA, 0x55, 0xAA, 0xAA, 0xAA, 0xAA))
        wire = ufp.pack(ufp.TYPE_CMD, 0x09, payload)
        decoder = ufp.Decoder()
        frames = decoder.feed_bytes(wire)
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].payload, payload)

    def test_maximum_payload(self):
        payload = bytes(range(ufp.MAX_PAYLOAD))
        wire = ufp.pack(ufp.TYPE_CMD, 0xFF, payload)
        self.assertEqual(len(wire), ufp.MAX_FRAME_LEN)
        self.assertEqual(ufp.unpack(wire).payload, payload)

    def test_empty_payload(self):
        wire = ufp.pack(ufp.TYPE_IMU_DATA, 3)
        self.assertEqual(wire, _hex("AA 02 00 03 BC"))
        self.assertEqual(ufp.unpack(wire).payload, b"")


class UnpackRejectionTest(unittest.TestCase):
    def setUp(self):
        self.wire = _hex(GOLDEN_FRAMES[0][0])

    def test_bad_sof(self):
        broken = b"\x00" + self.wire[1:]
        with self.assertRaises(ufp.UartFrameProtocolError):
            ufp.unpack(broken)

    def test_short_buffer(self):
        with self.assertRaises(ufp.UartFrameProtocolError):
            ufp.unpack(self.wire[:-2])

    def test_empty_buffer(self):
        with self.assertRaises(ufp.UartFrameProtocolError):
            ufp.unpack(b"")

    def test_len_disagreement_extra_byte(self):
        with self.assertRaises(ufp.UartFrameProtocolError):
            ufp.unpack(self.wire + b"\x00")

    def test_len_disagreement_missing_crc(self):
        with self.assertRaises(ufp.UartFrameProtocolError):
            ufp.unpack(self.wire[:-1])

    def test_bad_crc(self):
        broken = self.wire[:-1] + bytes((self.wire[-1] ^ 0xFF,))
        with self.assertRaises(ufp.UartFrameProtocolError):
            ufp.unpack(broken)


class PackRejectionTest(unittest.TestCase):
    def test_type_out_of_range(self):
        with self.assertRaises(ValueError):
            ufp.pack(0x100, 0)

    def test_seq_out_of_range(self):
        with self.assertRaises(ValueError):
            ufp.pack(ufp.TYPE_CMD, 256)

    def test_payload_too_long(self):
        with self.assertRaises(ValueError):
            ufp.pack(ufp.TYPE_CMD, 0, bytes(ufp.MAX_PAYLOAD + 1))


class DecoderTest(unittest.TestCase):
    def test_sequential_frames(self):
        decoder = ufp.Decoder()
        stream = b"".join(_hex(wire) for wire, _, _, _, _ in GOLDEN_FRAMES)
        frames = decoder.feed_bytes(stream)
        self.assertEqual(
            [(f.type, f.seq, f.payload) for f in frames],
            [(t, s, p) for _, t, s, p, _ in GOLDEN_FRAMES],
        )

    def test_noise_before_frame_is_dropped(self):
        decoder = ufp.Decoder()
        frames = decoder.feed_bytes(b"\x00\x55\xBB" + _hex(GOLDEN_FRAMES[3][0]))
        self.assertEqual([(f.type, f.seq) for f in frames], [(ufp.TYPE_ACK, 0x2A)])

    def test_bad_crc_is_dropped_and_stream_recovers(self):
        decoder = ufp.Decoder()
        broken = _hex(GOLDEN_FRAMES[0][0])[:-1] + b"\x00"
        self.assertEqual(decoder.feed_bytes(broken), [])
        good = _hex(GOLDEN_FRAMES[1][0])
        frames = decoder.feed_bytes(good)
        self.assertEqual([(f.type, f.seq) for f in frames], [(ufp.TYPE_AIM_TARGET, 1)])

    def test_truncated_frame_needs_timeout_reset(self):
        decoder = ufp.Decoder()
        self.assertEqual(decoder.feed_bytes(_hex(GOLDEN_FRAMES[0][0])[:-3]), [])
        decoder.reset()
        frames = decoder.feed_bytes(_hex(GOLDEN_FRAMES[2][0]))
        self.assertEqual([(f.type, f.seq) for f in frames], [(ufp.TYPE_AIM_TARGET, 5)])

    def test_sof_inside_garbage_body_needs_timeout_reset(self):
        # A lone 0xAA starts a header whose LEN swallows the next frame. The
        # idle timeout is what recovers; that is why reset() is mandatory.
        decoder = ufp.Decoder()
        self.assertEqual(decoder.feed_bytes(b"\xAA" + _hex(GOLDEN_FRAMES[0][0])), [])
        decoder.reset()
        frames = decoder.feed_bytes(_hex(GOLDEN_FRAMES[0][0]))
        self.assertEqual(len(frames), 1)


class RegistryTest(unittest.TestCase):
    def test_ack_flags_follow_reliability(self):
        self.assertTrue(ufp.MESSAGE_REGISTRY[ufp.TYPE_CMD].needs_ack)
        self.assertFalse(ufp.MESSAGE_REGISTRY[ufp.TYPE_AIM_TARGET].needs_ack)
        self.assertFalse(ufp.MESSAGE_REGISTRY[ufp.TYPE_IMU_DATA].needs_ack)
        self.assertFalse(ufp.MESSAGE_REGISTRY[ufp.TYPE_ACK].needs_ack)

    def test_ack_payload_is_status_byte(self):
        spec = ufp.MESSAGE_REGISTRY[ufp.TYPE_ACK]
        self.assertEqual(spec.payload_len, 1)
        frame = ufp.unpack(_hex(GOLDEN_FRAMES[3][0]))
        self.assertEqual(frame.payload, b"\x00")

    def test_aim_target_is_six_bytes_example_message(self):
        spec = ufp.MESSAGE_REGISTRY[ufp.TYPE_AIM_TARGET]
        self.assertEqual(spec.payload_len, 6)
        self.assertIn("example", spec.schema)

    def test_every_golden_type_is_registered(self):
        for _wire, frame_type, _seq, _payload, _note in GOLDEN_FRAMES:
            self.assertIn(frame_type, ufp.MESSAGE_REGISTRY)

    def test_frame_constants(self):
        self.assertEqual(ufp.SOF, 0xAA)
        self.assertEqual(ufp.HEADER_LEN, 4)
        self.assertEqual(ufp.MAX_PAYLOAD, 255)
        self.assertEqual(ufp.MAX_FRAME_LEN, 260)


class TempExampleTest(unittest.TestCase):
    def test_temp_is_two_byte_example_message(self):
        wire = _hex("AA 11 02 00 FD 00 4B")
        # The codec stays payload-agnostic; the registry schema defines temp.
        self.assertEqual(ufp.pack(ufp.TYPE_TEMP, 0, struct.pack("<h", 253)), wire)

        frame = ufp.unpack(wire)
        self.assertEqual(frame.type, ufp.TYPE_TEMP)
        self.assertEqual(frame.seq, 0)
        (temp,) = struct.unpack("<h", frame.payload)
        self.assertEqual(temp, 253)  # 253 * 0.1 = 25.3 degC

        spec = ufp.MESSAGE_REGISTRY[ufp.TYPE_TEMP]
        self.assertEqual(spec.name, "TEMP")
        self.assertEqual(spec.payload_len, 2)
        self.assertFalse(spec.needs_ack)
        self.assertIn("example", spec.schema)


class ImuExampleTest(unittest.TestCase):
    def test_imu_is_six_byte_angular_rate(self):
        wire = _hex("AA 02 06 00 00 00 00 00 96 00 31")
        payload = struct.pack("<hhh", 0, 0, 150)  # 0.01 deg/s per LSB
        self.assertEqual(ufp.pack(ufp.TYPE_IMU_DATA, 0, payload), wire)

        frame = ufp.unpack(wire)
        self.assertEqual(frame.type, ufp.TYPE_IMU_DATA)
        self.assertEqual(struct.unpack("<hhh", frame.payload), (0, 0, 150))

        spec = ufp.MESSAGE_REGISTRY[ufp.TYPE_IMU_DATA]
        self.assertEqual(spec.payload_len, 6)
        self.assertFalse(spec.needs_ack)
        self.assertIn("0.01", spec.schema)


if __name__ == "__main__":
    unittest.main()
