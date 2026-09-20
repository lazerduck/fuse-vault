#!/usr/bin/env python3
"""Control the USB-storage debug session. Uses public test credentials only."""
import argparse
import json
from pathlib import Path
import sys

from board_bench import SerialTransport
from security_probe import Probe


def disks_for_device(serial):
    disks = []
    for block in Path('/sys/class/block').glob('*'):
        if (block / 'partition').exists():
            continue
        for parent in (block / 'device').resolve().parents:
            try:
                if ((parent / 'idVendor').read_text().strip() == 'cafe' and
                    (parent / 'idProduct').read_text().strip() == '4022' and
                    (parent / 'serial').read_text().strip() == serial):
                    disks.append('/dev/' + block.name)
                    break
            except OSError:
                continue
    return disks


def mounted_partitions(disks):
    numbers = set()
    for disk in disks:
        block = Path('/sys/class/block') / Path(disk).name
        for node in [block, *block.glob('*')]:
            try:
                numbers.add((node / 'dev').read_text().strip())
            except OSError:
                pass
    return [line.split()[4] for line in Path('/proc/self/mountinfo').read_text().splitlines()
            if line.split()[2] in numbers]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--device', required=True)
    p.add_argument('action', choices=('status', 'unlock', 'lock'))
    p.add_argument('--credential', choices=('original', 'replacement'), default='original')
    a = p.parse_args()
    transport = None
    try:
        disks = disks_for_device(a.device)
        if a.action == 'lock':
            mounts = mounted_partitions(disks)
            if mounts:
                raise RuntimeError('Unmount/eject the filesystem before locking: ' + ', '.join(mounts))
        port = Path('/dev/serial/by-id') / f'usb-Fuse_Vault_Fuse_Vault_SECURITY_DEBUG_{a.device}-if00'
        if not port.exists():
            raise RuntimeError('Expected USB-storage firmware port is absent')
        transport = SerialTransport(str(port), product=0x4022)
        probe = Probe(transport, timeout=15)
        info = probe.transact('INFO', 'info')
        if info.get('device_id') != a.device or info.get('usb_msc') is not True or info.get('debug_session') is not True:
            raise RuntimeError('Wrong device or USB-storage debug firmware is not installed')
        command = {'status': 'MEDIA', 'lock': 'LOCK', 'unlock': 'UNLOCK ' + a.credential.upper()}[a.action]
        state = probe.transact(command, 'media')
        if a.action != 'status' and state.get('unlocked') != (a.action == 'unlock'):
            raise RuntimeError('Unexpected session state')
        print(json.dumps({'device': a.device, 'media': state,
                          'linux_disks': disks_for_device(a.device)}, indent=2))
        print('PASS: vault ' + ('unlocked' if state['unlocked'] else 'locked'))
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1
    finally:
        if transport is not None:
            transport.close()


if __name__ == '__main__':
    sys.exit(main())
