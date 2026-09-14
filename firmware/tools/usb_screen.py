#!/usr/bin/env python3
"""Linux GTK viewer for the bench-only Fuse Vault USB framebuffer protocol."""
import argparse
import glob
import os
from pathlib import Path
import re
import struct
import termios
import time
import tty
import fcntl
import gi

gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, GdkPixbuf, GLib

MAGIC = b'FVD1'
FRAME_BYTES = 160 * 80 * 2
PACKET_BYTES = 32 + FRAME_BYTES


def find_device():
    for name in glob.glob('/sys/class/tty/ttyACM*'):
        for parent in (Path(name) / 'device').resolve().parents:
            try:
                if ((parent / 'idVendor').read_text().strip() == 'cafe' and
                        (parent / 'idProduct').read_text().strip() == '4013'):
                    return '/dev/' + Path(name).name
            except OSError:
                pass
    return None


def extract_packets(buffer):
    """Incremental decoder; tolerate partial reads and stale connection bytes."""
    packets = []
    while True:
        start = buffer.find(MAGIC)
        if start < 0:
            del buffer[:-3]
            break
        del buffer[:start]
        if len(buffer) < 32:
            break
        header = struct.unpack_from('<8I', buffer)
        if header[1] != FRAME_BYTES:
            del buffer[0]
            continue
        if len(buffer) < PACKET_BYTES:
            break
        packets.append((header, bytes(buffer[32:PACKET_BYTES])))
        del buffer[:PACKET_BYTES]
    return packets


def rgb565_to_rgb(data):
    rgb = bytearray(160 * 80 * 3)
    for i, (pixel,) in enumerate(struct.iter_unpack('<H', data)):
        r, g, b = (pixel >> 11) & 31, (pixel >> 5) & 63, pixel & 31
        rgb[3*i:3*i+3] = bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4),
                              (b << 3) | (b >> 2)))
    return bytes(rgb)


class Viewer(Gtk.Window):
    def __init__(self, port):
        super().__init__(title='Fuse Vault — USB screen debug')
        self.port = port
        self.fd = None
        self.buffer = bytearray()
        self.last_request = 0
        self.last_frame = 0
        self.pending = False
        self.next_connect = 0
        self.states = re.findall(r'\bFV_STATE_[A-Z_]+',
            (Path(__file__).resolve().parents[1] / 'include/fuse_vault/app.h').read_text())
        self.states = list(dict.fromkeys(self.states))
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        box.set_border_width(18)
        self.add(box)
        warning = Gtk.Label(label='BENCH FIRMWARE • Leave the physical screen unplugged\n'
                                 'This window displays secrets shown on the device. Use test data.')
        box.pack_start(warning, False, False, 0)
        self.screen = Gtk.Image()
        self.screen.set_size_request(640, 320)
        box.pack_start(self.screen, True, True, 0)
        self.status = Gtk.Label(label='Waiting for the headless firmware…')
        self.status.set_line_wrap(True)
        self.status.set_selectable(True)
        box.pack_start(self.status, False, False, 0)
        self.details = Gtk.Label(label='Use the physical direction, OK and Back buttons on the board.')
        self.details.set_selectable(True)
        self.details.set_line_wrap(True)
        self.details.set_max_width_chars(85)
        box.pack_start(self.details, False, False, 0)
        self.connect('destroy', self.close)
        GLib.timeout_add(5, self.tick)
        self.show_all()

    def close(self, *_):
        if self.fd is not None:
            os.close(self.fd)
        Gtk.main_quit()

    def disconnect(self, reason):
        if self.fd is not None:
            os.close(self.fd)
        self.fd = None
        self.buffer.clear()
        self.pending = False
        self.screen.clear()
        self.details.set_text('Use the physical buttons. Reconnect USB after a connector fault.')
        self.status.set_text(reason)
        self.next_connect = time.monotonic() + 1

    def tick(self):
        now = time.monotonic()
        if self.fd is None:
            if now < self.next_connect:
                return True
            path = self.port or find_device()
            self.next_connect = now + 1
            if not path:
                return True
            try:
                self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
                tty.setraw(self.fd)
                attrs = termios.tcgetattr(self.fd)
                attrs[4] = attrs[5] = termios.B115200
                attrs[2] |= termios.CLOCAL | termios.CREAD
                termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
                termios.tcflush(self.fd, termios.TCIOFLUSH)
                fcntl.ioctl(self.fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_DTR))
                self.last_request = 0
                self.last_frame = now
                self.status.set_text('Connected to ' + path + ' — waiting for firmware')
            except OSError as exc:
                self.disconnect(f'Cannot open {path}: {exc}')
                return True
        try:
            # Drain the tty rather than reading a single (~4 KiB) chunk per
            # GUI tick. Bound work so a noisy device cannot starve GTK.
            for _ in range(16):
                try:
                    chunk = os.read(self.fd, 65536)
                    if not chunk:
                        self.disconnect('USB disconnected; waiting to reconnect…')
                        return True
                    self.buffer.extend(chunk)
                except BlockingIOError:
                    break
            for header, pixels in extract_packets(self.buffer):
                self.pending = False
                self.last_frame = now
                pixbuf = GdkPixbuf.Pixbuf.new_from_bytes(GLib.Bytes.new(rgb565_to_rgb(pixels)),
                    GdkPixbuf.Colorspace.RGB, False, 8, 160, 80, 160 * 3)
                self.screen.set_from_pixbuf(pixbuf.scale_simple(640, 320, GdkPixbuf.InterpType.NEAREST))
                _, _, sequence, state, buttons, flags, recovery, uptime = header
                state_name = self.states[state] if state < len(self.states) else f'State {state}'
                self.status.set_text(f'{state_name} • frame {sequence} • uptime {uptime / 1000:.1f}s'
                                     f' • buttons 0x{buttons:02x}')
                names = ('buttons', 'flash', 'OTP interface', 'SD driver', 'USB', 'connector', 'services')
                checks = ', '.join(f'{name}: {"OK" if flags & (1 << i) else "FAIL"}'
                                   for i, name in enumerate(names))
                recovery_name = {0: 'ready', 1: 'waiting for media', 2: 'failed'}.get(recovery, str(recovery))
                self.details.set_text(checks + '\nBoot recovery: ' + recovery_name +
                    '\nSD driver OK does not mean a card is present or readable.')
            if now - self.last_frame > 2:
                self.status.set_text('Waiting for firmware — it may be busy with SD or password derivation.')
            # Retry only after a generous interval. Late frames remain valid;
            # firmware ignores duplicate requests while transmitting a snapshot.
            if (not self.pending and now - self.last_request > .05) or now - self.last_request > 5:
                os.write(self.fd, b'f')
                self.last_request = now
                self.pending = True
        except OSError as exc:
            self.disconnect('USB connection lost: ' + str(exc))
        return True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='Optional serial device, e.g. /dev/ttyACM0; otherwise auto-detect')
    args = parser.parse_args()
    Viewer(args.port)
    Gtk.main()
