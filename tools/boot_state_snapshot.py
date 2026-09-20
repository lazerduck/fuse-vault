#!/usr/bin/env python3
"""Read-only BOOTSEL snapshot; excludes application OTP secrets. No reset/write."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial', required=True)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--picotool', default='/tmp/fv-picotool-usb/picotool')
    a = p.parse_args()
    if a.out.exists():
        p.error('Output already exists; preserve prior snapshots')
    locks = [f'PAGE{page}_LOCK{lock}' for page in [0, 1, 2, *range(16, 25), 59, 60]
             for lock in (0, 1)]
    checks = [
        ('info', ['info', '-a', '--debug']),
        ('partitions', ['partition', 'info']),
        ('boot_otp_raw', ['otp', 'get', '-r', '-n', *[hex(row) for row in range(0x40, 0xc0)]]),
        ('otp_locks', ['otp', 'get', '-r', '-n', *locks]),
    ]
    result = {'serial_requested': a.serial,
              'utc': datetime.now(timezone.utc).isoformat(), 'reads': {}}
    for name, args in checks:
        command = [a.picotool, *args, '--ser', a.serial]
        try:
            r = subprocess.run(command, capture_output=True, text=True, timeout=20)
            entry = {'command': command, 'returncode': r.returncode,
                     'stdout': r.stdout, 'stderr': r.stderr}
        except subprocess.TimeoutExpired:
            entry = {'command': command, 'error': '20-second timeout'}
        result['reads'][name] = entry
        print(f'{name}: {entry.get("returncode", entry.get("error"))}', flush=True)
        # No reason to repeat USB access if initial identity cannot be read.
        if name == 'info' and entry.get('returncode') != 0:
            break
    a.out.write_text(json.dumps(result, indent=2) + '\n')
    if any(e.get('returncode') != 0 for e in result['reads'].values()):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
