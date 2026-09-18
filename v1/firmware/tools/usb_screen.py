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
TIMING_NAMES = ('SD sector read', 'SD sector write', 'SD sync', 'Vault sector read',
                'Vault sector write', 'Select existing copies (includes SD/crypto)',
                'Read-back verification (includes SD/crypto)', 'Record nonce (KMAC)',
                'Layer IV (KMAC)', 'AES-XTS (includes key setup)', 'ChaCha20',
                'Ascon encrypt', 'Ascon decrypt')


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
        if header[1] not in (FRAME_BYTES, FRAME_BYTES + 80, FRAME_BYTES + len(TIMING_NAMES) * 16, len(TIMING_NAMES) * 16):
            del buffer[0]
            continue
        packet_size = 32 + header[1]
        if len(buffer) < packet_size:
            break
        packets.append((header, bytes(buffer[32:packet_size])))
        del buffer[:packet_size]
    return packets


def rgb565_to_rgb(data):
    rgb = bytearray(160 * 80 * 3)
    for i, (pixel,) in enumerate(struct.iter_unpack('<H', data)):
        r, g, b = (pixel >> 11) & 31, (pixel >> 5) & 63, pixel & 31
        rgb[3*i:3*i+3] = bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4),
                              (b << 3) | (b >> 2)))
    return bytes(rgb)


class MetricsRequests:
    """A slow USB reply is not evidence that profiling is unsupported."""
    def __init__(self):
        self.confirmed = False
        self.prefer_profile = True
        self.last_probe = float('-inf')

    def received(self, extended):
        if extended:
            self.confirmed = True
            self.prefer_profile = True

    def command(self, now, timed_out):
        if timed_out and not self.confirmed:
            self.prefer_profile = False
        if self.confirmed or self.prefer_profile or now - self.last_probe >= 10:
            self.last_probe = now
            return b'ihg' if self.confirmed else b'hg'  # First response supplies a full screen.
        return b'f'


class Viewer(Gtk.Window):
    def __init__(self, port):
        super().__init__(title='Fuse Vault — USB screen debug')
        self.port = port
        self.fd = None
        self.buffer = bytearray()
        self.last_request = 0
        self.last_frame = 0
        self.last_metrics = None
        self.metrics_text = "Waiting for storage timings…"
        self.pending = False
        self.metrics_requests = MetricsRequests()
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
        self.performance = Gtk.Label(label="Storage timings will appear with profiling firmware.")
        self.performance.set_selectable(True)
        self.performance.set_line_wrap(True)
        self.performance.set_max_width_chars(85)
        timings_scroll = Gtk.ScrolledWindow()
        timings_scroll.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        timings_scroll.set_min_content_height(180)
        timings_scroll.add(self.performance)
        box.pack_start(timings_scroll, True, True, 0)
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
        self.last_metrics = None
        self.performance.set_text("Disconnected — no current storage timings.")
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
                fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                tty.setraw(self.fd)
                attrs = termios.tcgetattr(self.fd)
                attrs[4] = attrs[5] = termios.B115200
                attrs[2] |= termios.CLOCAL | termios.CREAD
                termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
                termios.tcflush(self.fd, termios.TCIOFLUSH)
                fcntl.ioctl(self.fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_DTR))
                self.last_request = 0
                self.metrics_requests = MetricsRequests()
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
                timings_only = len(pixels) == len(TIMING_NAMES) * 16
                timing_data = pixels if timings_only else pixels[FRAME_BYTES:]
                extended = bool(timing_data)
                self.metrics_requests.received(extended)
                if extended:
                    names = TIMING_NAMES
                    if header[5] & 128:
                        names = ('SD read request', 'SD write request') + TIMING_NAMES[2:]
                    lines = []
                    for name, (count, lo, hi, maximum) in zip(names, struct.iter_unpack('<4I', timing_data)):
                        total = lo + (hi << 32)
                        average = total / count / 1000 if count else 0
                        lines.append(f'{name}: {count} calls, mean {average:.2f} ms, max {maximum / 1000:.2f} ms')
                    self.last_metrics = now
                    self.metrics_text = 'Since boot (nested timings overlap; do not add):\n' + '\n'.join(lines)
                if not timings_only:
                    pixbuf = GdkPixbuf.Pixbuf.new_from_bytes(GLib.Bytes.new(rgb565_to_rgb(pixels[:FRAME_BYTES])),
                        GdkPixbuf.Colorspace.RGB, False, 8, 160, 80, 160 * 3)
                    self.screen.set_from_pixbuf(pixbuf.scale_simple(640, 320, GdkPixbuf.InterpType.NEAREST))
                _, _, sequence, state, buttons, flags, recovery, uptime = header
                state_name = self.states[state] if state < len(self.states) else f'State {state}'
                self.status.set_text(f'{state_name} • frame {sequence} • uptime {uptime / 1000:.1f}s'
                                     f' • buttons 0x{buttons:02x}'
                                     + (' • 4-bit SD / 25 MHz' if flags & 128 else ' • SPI SD / 8 MHz'))
                names = ('buttons', 'flash', 'OTP interface', 'SD driver', 'USB', 'connector', 'services')
                checks = ', '.join(f'{name}: {"OK" if flags & (1 << i) else "FAIL"}'
                                   for i, name in enumerate(names))
                recovery_name = {0: 'ready', 1: 'waiting for media', 2: 'failed'}.get(recovery, str(recovery))
                self.details.set_text(checks + '\nBoot recovery: ' + recovery_name +
                    '\nSD driver OK does not mean a card is present or readable.')
            if self.last_metrics is not None:
                age = now - self.last_metrics
                freshness = f'Timings stale — last received {age:.1f}s ago\n' if age > 2 else ''
                self.performance.set_text(freshness + self.metrics_text)
            else:
                self.performance.set_text('Waiting for profiling reply — timing support not yet confirmed.')
            if now - self.last_frame > 2:
                self.status.set_text('Waiting for firmware — it may be busy with SD or password derivation.')
            # Retry only after a generous interval. Late frames remain valid;
            # firmware ignores duplicate requests while transmitting a snapshot.
            if (not self.pending and now - self.last_request > .05) or now - self.last_request > 5:
                os.write(self.fd, self.metrics_requests.command(now, self.pending))
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
