#!/usr/bin/env python3
"""Core0-only diagnostics for the explicitly enabled FV_DEBUG_STARTUP image.
START starts normal authority recovery, including any pending destruction policy.
BOOT only reads startup progress and USB sense pins; neither command unlocks.
"""
import argparse
import json
from board_bench import SerialTransport
from security_probe import Probe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('action', choices=('boot', 'start', 'reboot'))
    args = parser.parse_args()
    transport = SerialTransport(args.port, product=0x4022)
    try:
        print(json.dumps(Probe(transport, timeout=5).transact(args.action.upper(), 'boot'), indent=2))
    finally:
        transport.close()


if __name__ == '__main__':
    main()
