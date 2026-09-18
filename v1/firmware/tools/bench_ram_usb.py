#!/usr/bin/env python3
"""Linux direct-I/O test restricted to the cafe:4014 128 KiB benchmark RAM disk."""
import argparse
import json
import mmap
import os
from pathlib import Path
import stat
import time
from run_baseline_bench import is_bench_device


def validate_device(path, integrated=False):
    node = Path(path).resolve()
    if not stat.S_ISBLK(node.stat().st_mode):
        raise RuntimeError('Expected a block device')
    sys = Path('/sys/class/block') / node.name
    if (sys / 'partition').exists() or not is_bench_device(
            sys / 'device', '4013' if integrated else '4014'):
        raise RuntimeError('Not the standalone benchmark USB RAM disk')
    if int((sys / 'size').read_text()) != 256:
        raise RuntimeError('Benchmark RAM disk must be exactly 128 KiB')
    return node


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', required=True)
    parser.add_argument('--integrated', action='store_true',
                        help='Accept headless 4013 PID, still requiring the exact 128 KiB whole RAM disk')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = dict(version=1, complete=False, results=[])
    fd = None
    try:
        node = validate_device(args.device, args.integrated)
        fd = os.open(node, os.O_RDWR | os.O_DIRECT | os.O_EXCL)
        # The descriptor must still refer to the validated device if hotplug raced open.
        if os.fstat(fd).st_rdev != node.stat().st_rdev:
            raise RuntimeError('Device changed during open')
        validate_device(node, args.integrated)
        report['device'] = str(node)
        report['usb_serial'] = next(
            ((p / 'serial').read_text().strip()
             for p in (Path('/sys/class/block') / node.name / 'device').resolve().parents
             if (p / 'idVendor').exists() and (p / 'serial').exists()), '')
        for size in (512, 4096, 16384):
            with mmap.mmap(-1, size) as buf:
                expected = bytes((i * 17 + 23) & 255 for i in range(size))
                buf[:] = expected
                # 8 MiB per direction; every syscall bypasses the host page cache.
                operations = 8 * 1024 * 1024 // size
                for name in ('write', 'read'):
                    start = time.monotonic()
                    for i in range(operations):
                        offset = (i * size) % (128 * 1024)
                        n = (os.pwritev(fd, [buf], offset) if name == 'write'
                             else os.preadv(fd, [buf], offset))
                        if n != size:
                            raise RuntimeError('Short RAM disk I/O')
                    if name == 'write':
                        os.fsync(fd)
                    elapsed = time.monotonic() - start
                    row = dict(operation=name, request_bytes=size,
                               bytes=operations * size, seconds=elapsed,
                               kib_s=operations * size / elapsed / 1024)
                    report['results'].append(row)
                    print(json.dumps(row), flush=True)
                # Full RAM disk correctness check outside timed measurements.
                for offset in range(0, 128 * 1024, size):
                    if os.preadv(fd, [buf], offset) != size or buf[:] != expected:
                        raise RuntimeError('RAM disk data verification failed')
        report['complete'] = True
    except (OSError, RuntimeError, ValueError, StopIteration, KeyboardInterrupt) as exc:
        report['error'] = str(exc) or 'Interrupted'
    finally:
        if fd is not None:
            os.close(fd)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    if not report['complete']:
        parser.exit(1, report.get('error', 'Incomplete') + '\n')


if __name__ == '__main__':
    main()
