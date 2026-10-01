#!/usr/bin/env python3
"""V2 device framebuffer viewer and debug D-pad. No host-side setup logic."""
import argparse
import json
from pathlib import Path
import queue
import threading
import time

from board_bench import SerialTransport
from security_probe import Probe
from storage_session import disks_for_device, mounted_partitions


def pixels(frame):
    if (frame.get('width'), frame.get('height'), frame.get('format')) != (160, 80, 'rgb332'):
        raise ValueError('Unsupported device framebuffer')
    data = bytes.fromhex(frame['pixels'])
    if len(data) != 12800:
        raise ValueError('Invalid framebuffer length')
    return data


def rgb(value):
    r, g, b = value >> 5, (value >> 2) & 7, value & 3
    return (r << 5 | r << 2 | r >> 1, g << 5 | g << 2 | g >> 1, b * 85)


def rgb_bytes(data):
    return bytes(channel for value in data for channel in rgb(value))


def port_for(serial):
    return Path('/dev/serial/by-id') / f'usb-Fuse_Vault_Fuse_Vault_SECURITY_DEBUG_{serial}-if00'


def ram_text(frame):
    ram = frame.get('ram')
    if ram is None:
        return 'RAM telemetry unavailable — update the device firmware'
    return (f"RAM {ram['total_bytes'] / 1024:.0f} KiB · "
            f"Fixed incl. stacks {ram['fixed_bytes'] / 1024:.1f} KiB\n"
            f"Heap reserved {ram['heap_reserved_bytes'] / 1024:.1f} KiB "
            f"(peak {ram['heap_peak_reserved_bytes'] / 1024:.1f}) · "
            f"Uncommitted {ram['uncommitted_bytes'] / 1024:.1f} KiB")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', required=True)
    parser.add_argument('--snapshot', type=Path, help='Save one device screen as PPM and exit')
    args = parser.parse_args()
    if args.snapshot:
        transport = SerialTransport(str(port_for(args.device)), product=0x4022)
        try:
            frame = Probe(transport, timeout=5).transact('SCREEN', 'screen')
            args.snapshot.write_bytes(b'P6\n160 80\n255\n' + rgb_bytes(pixels(frame)))
            print(json.dumps({k: v for k, v in frame.items() if k != 'pixels'}))
        finally:
            transport.close()
        return

    import gi
    gi.require_version('Gtk', '3.0')
    gi.require_foreign('cairo')
    from gi.repository import Gtk, Gdk, GLib
    keys = queue.Queue(maxsize=16)
    stopped = threading.Event()
    window = Gtk.Window(title='Fuse Vault — device screen (DEBUG)')
    window.set_border_width(16)
    window.set_default_size(700, 510)
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
    window.add(box)
    area = Gtk.DrawingArea()
    area.set_size_request(640, 320)
    box.pack_start(area, True, True, 0)
    status = Gtk.Label(label='Connecting to ' + args.device)
    status.set_line_wrap(True)
    box.pack_start(status, False, False, 0)
    memory = Gtk.Label(label='RAM: waiting for device')
    memory.set_line_wrap(True)
    memory.set_tooltip_text('Fixed includes reserved stacks and the full FIDO image. '
                           'Heap reserved includes reusable freed allocations; it is not live usage. '
                           'Peak reservation is tracked since boot, including between screen updates. '
                           'Uncommitted excludes reusable space inside the heap and does not '
                           'guarantee any single allocation will succeed. Stack usage is not measured.')
    box.pack_start(memory, False, False, 0)
    screen = bytearray(12800)
    notice_until = [0.0]

    def draw(widget, context):
        scale = min(widget.get_allocated_width() / 160, widget.get_allocated_height() / 80)
        context.set_source_rgb(0.03, 0.09, 0.1)
        context.paint()
        context.translate((widget.get_allocated_width() - 160 * scale) / 2,
                          (widget.get_allocated_height() - 80 * scale) / 2)
        context.scale(scale, scale)
        for bit, value in enumerate(screen):
            r, g, b = rgb(value)
            context.set_source_rgb(r / 255, g / 255, b / 255)
            context.rectangle(bit % 160, bit // 160, 1, 1)
            context.fill()
    area.connect('draw', draw)

    allow_mounted = [False]

    def send(key):
        # The host can see mounts; the physical device cannot. Protect debug use.
        if key == 'SELECT' and not allow_mounted[0] and mounted_partitions(disks_for_device(args.device)):
            notice_until[0] = time.monotonic() + 5
            status.set_text('Unmount the vault filesystem before selecting an action.')
            return
        try:
            keys.put_nowait(key)
        except queue.Full:
            status.set_text('Input queue full; wait for the device.')

    replay = Gtk.Button(label='Replay startup animation')
    replay.connect('clicked', lambda *_: send('__STARTUP'))
    box.pack_start(replay, False, False, 0)
    row = Gtk.Box(spacing=8)
    for name in ('UP', 'DOWN', 'LEFT', 'RIGHT', 'SELECT', 'BACK'):
        button = Gtk.Button(label=name.title())
        button.set_can_focus(False)
        button.connect('clicked', lambda _button, key=name: send(key))
        row.pack_start(button, True, True, 0)
    box.pack_start(row, False, False, 0)
    box.pack_start(Gtk.Label(label='Arrows = directions • Enter = Select • Backspace/Esc = Back\n'
                            'Development input only. Unmount before locking or changing settings.'), False, False, 0)
    keymap = {Gdk.KEY_Up: 'UP', Gdk.KEY_Down: 'DOWN', Gdk.KEY_Left: 'LEFT',
              Gdk.KEY_Right: 'RIGHT', Gdk.KEY_Return: 'SELECT',
              Gdk.KEY_BackSpace: 'BACK', Gdk.KEY_Escape: 'BACK'}
    held = set()

    def key_press(_window, event):
        if event.keyval in keymap:
            if event.keyval not in held:
                held.add(event.keyval)
                send(keymap[event.keyval])
            return True
        return False
    window.connect('key-press-event', key_press)
    window.connect('key-release-event', lambda _w, e: held.discard(e.keyval))
    window.connect('focus-out-event', lambda *_: held.clear())

    def update(frame):
        screen[:] = pixels(frame)
        memory.set_text(ram_text(frame))
        allow_mounted[0] = frame.get("allow_mounted", False)
        if time.monotonic() >= notice_until[0]:
            status.set_text('Device busy — keep power connected' if frame['busy'] else 'Connected: ' + args.device)
        area.queue_draw()
        return False

    def clear_keys():
        while True:
            try:
                keys.get_nowait()
            except queue.Empty:
                return

    def worker():
        while not stopped.is_set():
            transport = None
            try:
                transport = SerialTransport(str(port_for(args.device)), product=0x4022)
                probe = Probe(transport, timeout=5)
                clear_keys()  # Never replay input after reconnect.
                input_id = None
                while not stopped.is_set():
                    frame = probe.transact('SCREEN', 'screen')
                    pixels(frame)
                    if input_id != frame.get("input_id", 0):
                        clear_keys()
                        input_id = frame.get("input_id", 0)
                    GLib.idle_add(update, frame)
                    if frame['busy']:
                        clear_keys()
                    else:
                        try:
                            key = keys.get(timeout=0.15)
                        except queue.Empty:
                            continue
                        if key == '__STARTUP':
                            # Fetch before playback so USB latency cannot stretch the animation.
                            frames = [probe.transact(f'STARTUP {i}', 'screen') for i in range(10)]
                            for startup_frame in frames:
                                pixels(startup_frame)
                            started = time.monotonic()
                            for i, startup_frame in enumerate(frames):
                                if stopped.is_set():
                                    break
                                GLib.idle_add(update, startup_frame)
                                stopped.wait(max(0, started + (i + 1) * 0.05 - time.monotonic()))
                            clear_keys()
                            continue
                        probe.transact(f'KEY {input_id} {key}', 'key')  # Never retry uncertain key presses.
                    stopped.wait(0.1)
            except (OSError, RuntimeError, ValueError, KeyError) as error:
                clear_keys()
                GLib.idle_add(memory.set_text, 'RAM: disconnected — waiting for device')
                GLib.idle_add(status.set_text, 'Disconnected: ' + str(error))
            finally:
                if transport is not None:
                    try:
                        transport.close()
                    except OSError:
                        pass
            stopped.wait(1)

    def close(*_):
        stopped.set()
        Gtk.main_quit()
    window.connect('destroy', close)
    threading.Thread(target=worker, daemon=True).start()
    window.show_all()
    Gtk.main()


if __name__ == '__main__':
    main()
