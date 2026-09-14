"""Protocol tests do not need USB hardware or a graphical display."""
import struct
import unittest
from usb_screen import extract_packets, rgb565_to_rgb, FRAME_BYTES

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

    def test_rgb565_primaries(self):
        data = struct.pack('<4H', 0xf800, 0x07e0, 0x001f, 0xffff) + bytes(FRAME_BYTES - 8)
        self.assertEqual(rgb565_to_rgb(data)[:12], bytes([255,0,0,0,255,0,0,0,255,255,255,255]))

if __name__ == '__main__':
    unittest.main()
