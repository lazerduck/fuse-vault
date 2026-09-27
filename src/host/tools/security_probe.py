#!/usr/bin/env python3
"""RP2354 security bring-up: snapshots, fixed-allocation enrollment and tests.
OTP mutation requires a debug-enrollment build and exact device identity. No raw-memory/lock programming API.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
import time
from board_bench import SerialTransport


class Probe:
    def __init__(self, transport, timeout=120):
        self.transport, self.timeout = transport, timeout

    def transact(self, command, expected):
        if len(command) > 95 or any(ord(c) < 32 or ord(c) > 126 for c in command):
            raise ValueError('Invalid diagnostic command')
        deadline = time.monotonic() + self.timeout
        self.transport.write_all(command.encode('ascii') + b'\n', deadline)
        response = bytearray()
        while len(response) < 24576:
            byte = self.transport.read_exact(1, deadline)
            if byte == b'\n':
                value = json.loads(response)
                if not isinstance(value, dict) or value.get('command') != expected:
                    raise RuntimeError(f'Unexpected response to {expected}')
                if value.get('ok') is not True:
                    raise RuntimeError(json.dumps(value, sort_keys=True))
                return value
            response.extend(byte)
        raise RuntimeError('Oversized reply; reconnect before continuing')


def validate_snapshot(s):
    if not isinstance(s, dict) or s.get('schema') != 1 or s.get('command') != 'otp' or s.get('ok') is not True:
        raise ValueError('Expected an OTP snapshot with schema 1')
    if not isinstance(s.get('device_id'), str) or not s['device_id']:
        raise ValueError('Missing device identity')
    pages = s.get('pages')
    if not isinstance(pages, list) or len(pages) != 64:
        raise ValueError('Snapshot must have all 64 pages')
    for i, p in enumerate(pages):
        if not isinstance(p, dict) or p.get('page') != i:
            raise ValueError('Pages must be unique, ordered and complete')
        for key in ('blank', 'programmed', 'all_ones', 'unreadable'):
            if type(p.get(key)) is not int or not 0 <= p[key] <= 64:
                raise ValueError(f'Invalid page count {key}')
        if sum(p[k] for k in ('blank', 'programmed', 'all_ones', 'unreadable')) != 64:
            raise ValueError('Page counts must total 64')
        if type(p.get('software_lock')) is not int or not 0 <= p['software_lock'] <= 15:
            raise ValueError('Invalid software lock')
        if type(p.get('read_error')) is not int:
            raise ValueError('Missing read status')
        for key in ('lock_raw', 'lock_status'):
            if not isinstance(p.get(key), list) or len(p[key]) != 2 or any(type(v) is not int for v in p[key]):
                raise ValueError(f'Invalid {key}')
        if any(not 0 <= v <= 0xffffff for v in p['lock_raw']):
            raise ValueError('Lock row outside 24 bits')
    return s


def locks(page):
    def vote(v):
        return ((v & (v >> 8)) | (v & (v >> 16)) | ((v >> 8) & (v >> 16))) & 255
    raw = page['lock_raw']
    values = [vote(v) if status == 0 else None for v, status in zip(raw, page['lock_status'])]
    names = ('read-write', 'read-only', 'reserved', 'inaccessible')
    lock0, lock1 = values
    result = {'software_secure': names[page['software_lock'] & 3],
              'software_nonsecure': names[(page['software_lock'] >> 2) & 3]}
    if lock0 is not None:
        result.update(write_key=lock0 & 7, read_key=(lock0 >> 3) & 7,
                      no_key_inaccessible=bool(lock0 & 64))
    if lock1 is not None:
        result.update(secure=names[lock1 & 3], nonsecure=names[(lock1 >> 2) & 3],
                      bootloader=names[(lock1 >> 4) & 3])
    result['redundant_copies_agree'] = [None if status else (v & 255) == ((v >> 8) & 255) == ((v >> 16) & 255)
                                          for v, status in zip(raw, page['lock_status'])]
    return result


def compare(before, after):
    validate_snapshot(before); validate_snapshot(after)
    if before['device_id'] != after['device_id']:
        raise ValueError('Refusing to compare different devices')
    changes = []
    for a, b in zip(before['pages'], after['pages']):
        changed = {k: {'before': a[k], 'after': b[k]} for k in
                   ('blank', 'programmed', 'all_ones', 'unreadable', 'read_error', 'lock_raw', 'lock_status', 'software_lock')
                   if a[k] != b[k]}
        if changed:
            changes.append({'page': a['page'], 'changes': changed,
                            'locks_before': locks(a), 'locks_after': locks(b)})
    return {'device_id': before['device_id'], 'changed_pages': changes,
            'note': 'Occupancy is aggregate. Same-category data changes are not detected; unreadable does not mean blank or erased.'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='Security-debug cafe:4022 CDC device')
    sub = parser.add_subparsers(dest='action', required=True)
    sub.add_parser('info'); sub.add_parser('rng'); sub.add_parser('state')
    for name in ('prepare-flash', 'provision', 'destroy', 'create'):
        action = sub.add_parser(name)
        action.add_argument('--confirm-device', required=True, help='Exact device ID; operation changes OTP/flash or SD')
        if name == 'create': action.add_argument('--erase-sd', action='store_true', required=True)
    for name in ('check', 'change'):
        action = sub.add_parser(name)
        action.add_argument('--credential', choices=('original', 'replacement'), default='original', help='Public debug credential selector')
    sub.add_parser('wrong', help='Charge ONE real failed attempt; the final attempt destroys the enrollment')
    snap = sub.add_parser('snapshot'); snap.add_argument('--out', type=Path, required=True)
    diff = sub.add_parser('diff'); diff.add_argument('before', type=Path); diff.add_argument('after', type=Path)
    kdf = sub.add_parser('kdf'); kdf.add_argument('--iterations', type=int, default=1000)
    vault = sub.add_parser('vault-test'); vault.add_argument('--erase-sd', action='store_true', required=True)
    args = parser.parse_args(argv)
    if args.action == 'diff':
        print(json.dumps(compare(json.loads(args.before.read_text()), json.loads(args.after.read_text())), indent=2));return
    if not args.port:
        parser.error('--port is required for device operations')
    if args.action == 'kdf' and not 1 <= args.iterations <= 1000000:
        parser.error('iterations must be 1..1000000; production calibration is not yet fixed')
    transport = SerialTransport(args.port, product=0x4022)
    try:
        probe = Probe(transport)
        info = probe.transact('INFO', 'info')
        if info.get('protocol') != 1 or type(info.get('otp_writes')) is not bool:
            raise RuntimeError('Unexpected firmware capabilities')
        if args.action in ('prepare-flash', 'provision', 'destroy', 'create', 'check', 'change', 'wrong'):
            if info.get('debug_enrollment') is not True or info.get('persistent_authority') is not True:
                raise RuntimeError('Persistent enrollment debug commands are not enabled in this firmware')
        if args.action in ('prepare-flash', 'provision', 'destroy', 'create') and args.confirm_device != info.get('device_id'):
            raise RuntimeError('Confirmation does not match the connected device; no mutation sent')
        if args.action == 'info':
            result = info
        elif args.action == 'state':
            if info.get('persistent_authority') is not True:
                raise RuntimeError('Flash/OTP authority is not available in this firmware')
            result = probe.transact('STATE', 'state')
        elif args.action in ('prepare-flash', 'provision', 'destroy', 'create'):
            command, expected = {'prepare-flash': ('PREPARE_FLASH', 'prepare-flash'), 'provision': ('PROVISION', 'provision'), 'destroy': ('DESTROY', 'destroy'),
                                 'create': ('CREATE_ERASE_SD', 'persistent')}[args.action]
            result = probe.transact(command + ' ' + args.confirm_device, expected)
        elif args.action in ('check', 'change'):
            result = probe.transact(args.action.upper() + ' ' + args.credential.upper(), 'persistent')
        elif args.action == 'wrong':
            result = probe.transact('WRONG', 'persistent')
        elif args.action == 'snapshot':
            if info.get('otp_inspection') is not True:
                raise RuntimeError('OTP inspection was compiled out of this firmware')
            result = validate_snapshot(probe.transact('OTP', 'otp'))
            if result['device_id'] != info['device_id']:
                raise RuntimeError('Device identity changed during snapshot')
            result.update(captured_at=datetime.now(timezone.utc).isoformat(), firmware=info)
            # Exclusive create preserves the baseline instead of silently replacing it.
            with args.out.open('x') as f:
                json.dump(result, f, indent=2); f.write('\n')
        elif args.action == 'rng':
            result = probe.transact('RNG', 'rng')
        elif args.action == 'kdf':
            result = probe.transact(f'KDF {args.iterations}', 'kdf')
            if result['unlock_us'] > 0:
                result['estimated_iterations_for_1_5s'] = max(1, min(1000000, round(args.iterations * 1500000 / result['unlock_us'])))
                result['calibration_note'] = 'Estimate only: remeasure suggested count before choosing production limits.'
        else:
            result = probe.transact('VAULT ERASE_SD', 'vault')
        print(json.dumps(result, indent=2))
    finally:
        transport.close()


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        print(f'error: {error}', file=sys.stderr)
        sys.exit(1)
