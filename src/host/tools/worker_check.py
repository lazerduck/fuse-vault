#!/usr/bin/env python3
"""Read-only V2 worker check. Sends INFO and STATE only; never START or provisioning."""
import argparse
import json
from pathlib import Path
import sys

from board_bench import SerialTransport
from security_probe import Probe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', required=True, help='Expected USB device serial')
    expected_state = parser.add_mutually_exclusive_group()
    expected_state.add_argument('--expect-unprovisioned', action='store_true')
    expected_state.add_argument('--expect-empty', action='store_true',
                                help='Require first enrollment after a restart, before vault creation')
    args = parser.parse_args()
    port = Path('/dev/serial/by-id') / (
        f'usb-Fuse_Vault_Fuse_Vault_SECURITY_DEBUG_{args.device}-if00')
    transport = None
    try:
        if not port.exists():
            raise RuntimeError('Expected V2 USB device is absent (or still in BOOTSEL)')
        transport = SerialTransport(str(port), product=0x4022)
        probe = Probe(transport, timeout=5)
        info = probe.transact('INFO', 'info')
        if info.get('device_id') != args.device or info.get('protocol') != 1:
            raise RuntimeError('Unexpected device identity or protocol')
        state = probe.transact('STATE', 'state')
        print(json.dumps({'info': info, 'state': state}, indent=2), flush=True)
        if args.expect_unprovisioned:
            expected = {'open_result': 1, 'boot_recovery': 1,
                        'root_blank': 1, 'tokens_blank': 1, 'flash_blank': 1}
            if any(state.get(k) != v for k, v in expected.items()):
                raise RuntimeError('Worker responds, but state is not the expected blank/unprovisioned state')
            print('PASS: V2 USB and worker respond; enrollment is blank/unprovisioned.')
        elif args.expect_empty:
            expected = {'open_result': 0, 'boot_recovery': 0, 'journal_result': 0,
                        'status': 1, 'token_slot': 0, 'root_blank': 0,
                        'tokens_blank': 0, 'flash_blank': 0, 'attempts': 0,
                        'pending': False}
            if any(state.get(k) != v for k, v in expected.items()):
                raise RuntimeError('Worker responds, but first enrollment is not in the expected empty state after restart')
            print('PASS: V2 worker ready after restart; first enrollment exists and no vault is created.')
        elif state.get('open_result', -1) < 0 or state.get('boot_recovery', -1) < 0:
            raise RuntimeError('Worker responds, but enrollment/recovery reports an error')
        else:
            print('PASS: V2 USB and worker respond; enrollment/recovery reports no error.')
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1
    finally:
        if transport is not None:
            transport.close()


if __name__ == '__main__':
    sys.exit(main())
