#!/usr/bin/env python3
"""Monitor physical switches on an FV_DEBUG_BUTTONS build; sends only BUTTONS."""
import argparse
import time
from datetime import datetime

from board_bench import SerialTransport
from device_ui import port_for
from security_probe import Probe

BUTTONS = (('SW5', 'Up', 10), ('SW6', 'Left', 11), ('SW7', 'Down', 12),
           ('SW8', 'Right', 13), ('SW9', 'Select', 14), ('SW4', 'Back', 15))


def describe(frame):
    if (frame.get('schema') != 1 or frame.get('diagnostic_only') is not True
            or type(frame.get('raw_pressed')) is not int
            or not 0 <= frame['raw_pressed'] <= 63
            or type(frame.get('stable_pressed')) is not int
            or not 0 <= frame['stable_pressed'] <= 63
            or not isinstance(frame.get('presses'), list)
            or len(frame['presses']) != 6
            or any(type(n) is not int or not 0 <= n <= 0xffffffff
                   for n in frame['presses'])):
        raise ValueError('Expected button diagnostic firmware schema 1')
    return ' | '.join(
        f'{sw}/{name}/GP{pin}: '
        f'{"LOW" if frame["raw_pressed"] & (1 << i) else "HIGH"} '
        f'{"PRESSED" if frame["stable_pressed"] & (1 << i) else "released"} '
        f'count={frame["presses"][i]}'
        for i, (sw, name, pin) in enumerate(BUTTONS))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', required=True, help='USB device serial')
    parser.add_argument('--seconds', type=float, default=120, help='Monitor duration (default 120)')
    parser.add_argument('--once', action='store_true', help='Read one snapshot')
    parser.add_argument('--live', action='store_true', help='Refresh a compact terminal table')
    args = parser.parse_args()
    if not 0 < args.seconds <= 3600:
        parser.error('--seconds must be between 0 and 3600')
    transport = SerialTransport(str(port_for(args.device)), product=0x4022)
    try:
        probe = Probe(transport, timeout=5)
        print('Release all switches first, then press each for at least half a second.', flush=True)
        print('Idle = HIGH/released; held = LOW/PRESSED. Counts are since boot, including '
              'an input held low at boot. UI button actions are disabled.', flush=True)
        previous = None
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            frame = probe.transact('BUTTONS', 'buttons')
            line = describe(frame)
            if line != previous:
                if args.live:
                    print('\033[2J\033[H', end='')
                    print('FUSE VAULT — LIVE BUTTON MONITOR\n')
                    print('\n'.join(line.split(' | ')))
                    print('\nHIGH = released. LOW = electrically pressed. Counts since boot.')
                    print('UI button actions disabled. Ctrl+C stops the monitor.', flush=True)
                else:
                    print(datetime.now().isoformat(timespec='milliseconds'), line, flush=True)
                previous = line
            if args.once:
                break
            time.sleep(0.05)
    except KeyboardInterrupt:
        pass
    finally:
        transport.close()


if __name__ == '__main__':
    main()
