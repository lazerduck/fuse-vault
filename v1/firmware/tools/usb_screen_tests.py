"""Protocol tests do not need USB hardware or a graphical display."""
import struct
import unittest
from usb_screen import extract_packets, rgb565_to_rgb, FRAME_BYTES, MetricsRequests, TIMING_NAMES

class DecoderTests(unittest.TestCase):
    def packet(self):
        return struct.pack('<8I', 0x31445646, FRAME_BYTES, 7, 2, 0, 127, 0, 1234) + b'\x00\xf8' * (160 * 80)

    def test_fragmented_and_coalesced(self):
        packet = self.packet()
        buffer = bytearray(b'old connection bytes')
        result = []
        for offset in range(0, len(packet), 11):
            buffer.extend(packet[offset:offset+11])
            result.extend(extract_packets(buffer))
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0][0][2], 7)
        self.assertEqual(result[0][1], packet[32:])
        buffer.extend(packet * 2)
        self.assertEqual(len(extract_packets(buffer)), 2)
        self.assertFalse(buffer)

    def test_invalid_length_resync(self):
        buffer = bytearray(b'FVD1' + struct.pack('<7I', 0xffffffff, 0, 0, 0, 0, 0, 0) + self.packet())
        self.assertEqual(len(extract_packets(buffer)), 1)

    def test_extended_timings(self):
        packet = bytearray(self.packet())
        struct.pack_into('<I', packet, 4, FRAME_BYTES + 80)
        timings = struct.pack('<4I', 2, 6000, 0, 4000) * 5
        packet.extend(timings)
        result = extract_packets(packet)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0][1][FRAME_BYTES:], timings)
        self.assertFalse(packet)

    def test_detailed_timings_fragmented(self):
        packet = bytearray(self.packet())
        timings = struct.pack('<4I', 3, 9000, 0, 5000) * len(TIMING_NAMES)
        struct.pack_into('<I', packet, 4, FRAME_BYTES + len(timings))
        packet.extend(timings)
        buffer = bytearray(packet[:-1])
        self.assertEqual(extract_packets(buffer), [])
        buffer.extend(packet[-1:])
        result = extract_packets(buffer)
        self.assertEqual(result[0][1][FRAME_BYTES:], timings)
        self.assertFalse(buffer)

    def test_slow_storage_does_not_disable_confirmed_metrics(self):
        requests = MetricsRequests()
        self.assertEqual(requests.command(0, False), b'hg')
        requests.received(True)
        self.assertEqual(requests.command(6, True), b'ihg')
        requests.received(False)  # A late legacy reply cannot undo detection.
        self.assertEqual(requests.command(12, True), b'ihg')
        self.assertEqual(requests.command(13, False), b'ihg')

    def test_legacy_fallback_reprobes(self):
        requests = MetricsRequests()
        self.assertEqual(requests.command(0, False), b'hg')
        self.assertEqual(requests.command(6, True), b'f')
        self.assertEqual(requests.command(7, False), b'f')
        self.assertEqual(requests.command(11, False), b'hg')
        requests.received(True)
        self.assertEqual(requests.command(17, True), b'ihg')

    def test_timings_only_then_full_screen(self):
        timings = struct.pack('<4I', 9, 10000, 0, 3000) * len(TIMING_NAMES)
        packet = struct.pack('<8I', 0x31445646, len(timings), 7, 2, 0, 127, 0, 1234) + timings
        buffer = bytearray(packet[:-3])
        self.assertEqual(extract_packets(buffer), [])
        buffer.extend(packet[-3:] + self.packet())
        result = extract_packets(buffer)
        self.assertEqual(len(result), 2)
        self.assertEqual(result[0][1], timings)
        self.assertEqual(len(result[1][1]), FRAME_BYTES)
        self.assertFalse(buffer)
        self.assertEqual(MetricsRequests().command(20, False), b'hg')

    def test_rgb565_primaries(self):
        data = struct.pack('<4H', 0xf800, 0x07e0, 0x001f, 0xffff) + bytes(FRAME_BYTES - 8)
        self.assertEqual(rgb565_to_rgb(data)[:12], bytes([255,0,0,0,255,0,0,0,255,255,255,255]))

if __name__ == '__main__':
    unittest.main()
