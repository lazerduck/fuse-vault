#!/usr/bin/env python3
"""Read heartbeats and ping the minimal USB startup firmware (Linux)."""
import argparse
import fcntl
import os
from pathlib import Path
import select
import struct
import termios
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', help='/dev/serial/by-id/... or /dev/ttyACM...')
    args = parser.parse_args()
    port = Path(args.port).resolve()
    device = (Path('/sys/class/tty') / port.name / 'device').resolve()
    if not any((p / 'product').exists() and
               (p / 'product').read_text().strip() == 'Fuse Vault ALIVE'
               for p in device.parents):
        parser.error('Expected the Fuse Vault ALIVE USB device')
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    previous = None
    try:
        fcntl.ioctl(fd, termios.TIOCEXCL)
        previous = termios.tcgetattr(fd)
        settings = termios.tcgetattr(fd)
        settings[0] = settings[1] = settings[3] = 0
        settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        settings[4] = settings[5] = termios.B115200
        settings[6][termios.VMIN] = settings[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, settings)
        fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_DTR))
        termios.tcflush(fd, termios.TCIFLUSH)
        os.write(fd, b'?')
        deadline = time.monotonic() + 8
        data = b''
        pong = False
        beats = []
        while time.monotonic() < deadline:
            if not select.select([fd], [], [], max(0, deadline - time.monotonic()))[0]:
                break
            chunk = os.read(fd, 1024)
            if not chunk:
                raise RuntimeError('Device disconnected')
            data += chunk
            while b'\n' in data:
                line, data = data.split(b'\n', 1)
                line = line.decode('ascii', errors='replace').strip()
                print(line, flush=True)
                parts = line.split()
                if len(parts) == 3 and parts[0] == 'FV_ALIVE_V1' and parts[2].isdigit():
                    if parts[1] == 'PONG':
                        pong = True
                    elif parts[1] == 'AWAKE':
                        beats.append(int(parts[2]))
                if pong and len(beats) >= 2 and beats[-1] > beats[-2]:
                    print('PASS: ping answered and heartbeat uptime advanced')
                    return
            if len(data) > 4096:
                raise RuntimeError('Unexpected response length')
        raise RuntimeError('Timed out waiting for PONG and two advancing heartbeats')
    finally:
        if previous is not None:
            termios.tcsetattr(fd, termios.TCSANOW, previous)
        os.close(fd)


if __name__ == '__main__':
    main()
