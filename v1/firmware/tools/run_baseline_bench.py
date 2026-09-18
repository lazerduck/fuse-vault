#!/usr/bin/env python3
"""Run the standalone baseline image (USB cafe:4014), saving JSON evidence."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import select
import struct
import termios
import time
import tty


def is_bench_device(sys_path, product='4014'):
    for parent in (sys_path.resolve(), *sys_path.resolve().parents):
        try:
            if ((parent / 'idVendor').read_text().strip() == 'cafe' and
                    (parent / 'idProduct').read_text().strip() == product):
                return True
        except OSError:
            pass
    return False


def find_port(requested=None, integrated=False):
    ports = ['/dev/' + p.name for p in Path('/sys/class/tty').glob('ttyACM*')
             if is_bench_device(p / 'device', '4013' if integrated else '4014')]
    if requested:
        resolved = str(Path(requested).resolve())
        if resolved not in ports:
            raise RuntimeError('Selected port is not the expected benchmark firmware USB identity')
        return resolved
    if len(ports) != 1:
        raise RuntimeError(f'Expected one benchmark board, found {len(ports)}; connect one or use --port')
    return ports[0]


def transact(fd, command, timeout=300):
    # Never retry a command: ERASE-SD is a destructive operation.
    payload = (command + '\n').encode('ascii')
    if os.write(fd, payload) != len(payload):
        raise RuntimeError('Short command write; power-cycle before retrying')
    deadline = time.monotonic() + timeout
    pending = bytearray()
    rows = []
    while time.monotonic() < deadline:
        if not select.select([fd], [], [], 1)[0]:
            continue
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            continue
        if not chunk:
            raise RuntimeError('Board disconnected before completion')
        pending.extend(chunk)
        if len(pending) > 65536:
            raise RuntimeError('Oversized benchmark response')
        while b'\n' in pending:
            line, _, remainder = pending.partition(b'\n')
            pending = bytearray(remainder)
            row = json.loads(line)
            if not isinstance(row, dict):
                raise RuntimeError('Invalid benchmark response')
            rows.append(row)
            print(json.dumps(row), flush=True)
            if row.get('done'):
                return rows
    raise RuntimeError('Benchmark timed out; partial results are incomplete; no command retried')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--integrated', action='store_true',
                        help='Use baseline commands inside the existing headless firmware (4013)')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--label', required=True, help='Board/card identifier and test notes')
    parser.add_argument('--erase-sd', action='store_true',
                        help='DESTROY first 4 MiB of inserted SD, including partition/vault headers')
    parser.add_argument('--ram-usb', action='store_true', help='Expose a 128 KiB RAM disk after tests')
    parser.add_argument('--info-only', action='store_true',
                        help='Send no SD test commands (integrated firmware still performs its normal boot checks)')
    parser.add_argument('--uf2', type=Path, help='Flashed image, recorded by SHA-256 (not flashed by this tool)')
    args = parser.parse_args()
    if args.info_only and (args.erase_sd or args.ram_usb):
        parser.error('--info-only cannot be combined with --erase-sd or --ram-usb')
    report = dict(version=1, label=args.label, host=platform.platform(),
                  started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                  erase_sd=args.erase_sd, runs=[], complete=False)
    fd = None
    try:
        if args.uf2:
            report['uf2_sha256'] = hashlib.sha256(args.uf2.read_bytes()).hexdigest()
        report['port'] = find_port(args.port, args.integrated)
        fd = os.open(report['port'], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        tty.setraw(fd)
        fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_DTR))
        termios.tcflush(fd, termios.TCIFLUSH)
        if args.integrated:
            rows = transact(fd, 'B', timeout=10)
            report['runs'].append(dict(command='B', rows=rows))
            if not any(r.get('bench_mode') is True for r in rows):
                raise RuntimeError('Firmware did not enter baseline mode; no tests sent')
        commands = ['INFO'] if args.info_only else ['INFO', 'CRYPTO', 'INIT-SD', 'INFO', 'READ-SD']
        if args.erase_sd:
            print('DESTRUCTIVE TEST: first 4 MiB of the board SD will be overwritten.', flush=True)
            commands.append('ERASE-SD')
        if args.ram_usb:
            commands.append('RAM-USB')
        for command in commands:
            start = time.monotonic()
            rows = transact(fd, command)
            report['runs'].append(dict(command=command, wall_seconds=time.monotonic()-start, rows=rows))
            if any('error' in r or r.get('ok') is False for r in rows):
                raise RuntimeError(f'{command} failed; stopping benchmark')
        report['complete'] = True
    except (OSError, RuntimeError, ValueError, KeyboardInterrupt) as exc:
        report['error'] = str(exc) or 'Interrupted'
    finally:
        if fd is not None:
            os.close(fd)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Saved {args.output}')
    if not report['complete']:
        parser.exit(1, report.get('error', 'Incomplete') + '\n')


if __name__ == '__main__':
    main()
