#!/usr/bin/env python3
"""Laptop-controlled USB -> encryption -> SD -> decryption -> USB benchmark."""
import argparse
from datetime import datetime, timezone
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

MAGIC, VERSION = 0x33425646, 5
INFO, CONFIG, WRITE, READ, END = range(1, 6)
AUTH_CONFIG = 6
WRITE_TOKEN = 0x45524153
REQUEST = struct.Struct('<8I')
RESPONSE = struct.Struct('<6I4Q2I4Q2I')
MAX_BYTES = 32768
ALGORITHMS = {'aes': 1, 'camellia': 2}


def stack_code(text):
    if text == 'raw':
        return 0
    parts = text.split(',')
    if not 1 <= len(parts) <= 4 or any(p not in ALGORITHMS for p in parts):
        raise argparse.ArgumentTypeError('Use raw or 1–4 comma-separated aes/camellia layers')
    return sum(ALGORITHMS[p] << (8*i) for i, p in enumerate(parts))


def is_bench_device(path, product=0x4021):
    path = Path(path).resolve()
    for parent in (path, *path.parents):
        if (parent/'idVendor').exists() and (parent/'idProduct').exists():
            return ((parent/'idVendor').read_text().strip() == 'cafe' and
                    (parent/'idProduct').read_text().strip() == f'{product:04x}')
    return False


class SerialTransport:
    """Bounded nonblocking I/O; a timeout fails the run, never retries a write."""
    def __init__(self, path, product=0x4021):
        port = Path(path).resolve()
        if not is_bench_device(Path('/sys/class/tty')/port.name/'device', product):
            raise RuntimeError(f'Expected cafe:{product:04x} firmware CDC port')
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.previous = None
        try:
            # Keep other serial clients from interleaving commands/test payloads.
            fcntl.ioctl(self.fd, termios.TIOCEXCL)
            self.previous = termios.tcgetattr(self.fd)
            settings = termios.tcgetattr(self.fd)
            settings[0] = settings[1] = settings[3] = 0
            settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL | termios.HUPCL
            settings[4] = settings[5] = termios.B115200
            settings[6][termios.VMIN] = 0
            settings[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, settings)
            termios.tcflush(self.fd, termios.TCIOFLUSH)
        except Exception:
            self.close()
            raise

    def _wait(self, write, deadline):
        remaining = deadline-time.monotonic()
        if remaining <= 0:
            raise TimeoutError('USB transaction timed out; no retry. Reconnect before another run.')
        readable, writable, _ = select.select([] if write else [self.fd],
                                             [self.fd] if write else [], [], remaining)
        if not (readable or writable):
            raise TimeoutError('USB transaction timed out; no retry. Reconnect before another run.')

    def write_all(self, data, deadline):
        view = memoryview(data)
        while view:
            self._wait(True, deadline)
            try:
                count = os.write(self.fd, view)
            except BlockingIOError:
                continue
            if count == 0:
                raise RuntimeError('USB disconnected during write')
            view = view[count:]

    def read_exact(self, length, deadline):
        data = bytearray()
        while len(data) < length:
            self._wait(False, deadline)
            try:
                chunk = os.read(self.fd, length-len(data))
            except BlockingIOError:
                continue
            if not chunk:
                raise RuntimeError('USB disconnected during read')
            data.extend(chunk)
        return bytes(data)

    def close(self):
        try:
            fcntl.ioctl(self.fd, termios.TIOCMBIC, struct.pack('I', termios.TIOCM_DTR))
            if self.previous is not None:
                termios.tcsetattr(self.fd, termios.TCSANOW, self.previous)
            fcntl.ioctl(self.fd, termios.TIOCNXCL)
        except OSError:
            pass
        finally:
            os.close(self.fd)


class BenchClient:
    def __init__(self, transport, timeout=30):
        self.transport = transport
        self.timeout = timeout
        self.sequence = 0

    def transact(self, op, lba=0, blocks=0, algorithms=0, payload=b''):
        if op in (READ, WRITE) and not 1 <= blocks <= 64:
            raise ValueError('Batch must contain 1–64 sectors')
        if len(payload) != (blocks*512 if op == WRITE else 0):
            raise ValueError('Invalid payload length')
        self.sequence += 1
        header = REQUEST.pack(MAGIC, VERSION, op, self.sequence, lba, blocks, len(payload), algorithms)
        start = time.monotonic()
        deadline = start+self.timeout
        self.transport.write_all(header, deadline)
        if payload:
            self.transport.write_all(payload, deadline)
        prefix = self.transport.read_exact(8, deadline)
        if struct.unpack('<2I', prefix) != (MAGIC, VERSION):
            raise RuntimeError('Firmware protocol mismatch: flash protocol 5 firmware and reconnect')
        values = RESPONSE.unpack(prefix + self.transport.read_exact(RESPONSE.size-8, deadline))
        magic, version, response_op, sequence, status, length, crypto, sd, setup, capacity, cpu, sd_clock, hmac, metadata, meta_reads, meta_writes, backend, self_test = values
        if (magic, version, response_op, sequence) != (MAGIC, VERSION, op | 0x80000000, self.sequence):
            raise RuntimeError('Mismatched response framing/version/sequence; reconnect')
        if backend not in (0, 1) or self_test != 1:
            raise RuntimeError('Board HMAC backend/self-test did not validate')
        expected = blocks*512 if op == READ and status == 0 else 0
        if length != expected or length > MAX_BYTES:
            raise RuntimeError('Invalid response payload length; reconnect')
        if status:
            raise RuntimeError(f'Board status {status} for operation {op}, LBA {lba}, blocks {blocks}')
        data = self.transport.read_exact(length, deadline) if length else b''
        return {'sequence': sequence, 'crypto_us': crypto, 'sd_us': sd,
                'setup_us': setup, 'capacity_blocks': capacity,
                'hmac_backend': 'pico-sha256-cpu-fed' if backend else 'software-sha256',
                'hmac_self_test': True, 'hmac_us': hmac, 'metadata_us': metadata,
                'metadata_read_blocks': meta_reads, 'metadata_write_blocks': meta_writes,
                'cpu_hz': cpu, 'sd_hz_requested': sd_clock,
                'transaction_seconds': time.monotonic()-start}, data


def run_case(client, stack, batch_bytes, plaintext, log_batches=False, case=None, integrity=False):
    """Works with real firmware or the test peer; all comparison is on laptop."""
    if not plaintext or len(plaintext) % 512 or len(plaintext) > 4*1024*1024:
        raise ValueError('Test data must contain 1–8192 whole sectors')
    if batch_bytes % 512 or not 512 <= batch_bytes <= MAX_BYTES:
        raise ValueError('Batch must be 512–32768 bytes in whole sectors')
    if case is None:
        case = {}
    case.update(integrity=integrity, stack=stack, batch_bytes=batch_bytes, total_bytes=len(plaintext), verified=False)
    case['physical_bytes'] = len(plaintext) + (((len(plaintext)//512+14)//15)*512 if integrity else 0)
    case['configuration'], _ = client.transact(AUTH_CONFIG if integrity else CONFIG, WRITE_TOKEN, len(plaintext)//512, stack_code(stack))
    for op, name in ((WRITE, 'write'), (READ, 'read')):
        phase = {'completed_bytes': 0, 'requests': 0, 'crypto_us': 0, 'sd_us': 0,
                 'hmac_us': 0, 'metadata_us': 0, 'metadata_read_blocks': 0,
                 'metadata_write_blocks': 0, 'transaction_seconds': 0.0}
        case[name] = phase
        if log_batches:
            phase['batches'] = []
        phase_start = time.monotonic()
        for offset in range(0, len(plaintext), batch_bytes):
            block = plaintext[offset:offset+batch_bytes]
            row, returned = client.transact(op, offset//512, len(block)//512,
                                           payload=block if op == WRITE else b'')
            if op == READ and returned != block:
                raise RuntimeError(f'Data verification failed at byte offset {offset}')
            phase['completed_bytes'] += len(block)
            phase['requests'] += 1
            for key in ('crypto_us', 'sd_us', 'hmac_us', 'metadata_us', 'metadata_read_blocks', 'metadata_write_blocks', 'transaction_seconds'):
                phase[key] += row[key]
            if log_batches:
                phase['batches'].append(dict(lba=offset//512, bytes=len(block), **row))
        phase['wall_seconds'] = time.monotonic()-phase_start
        phase['kib_per_second'] = len(plaintext)/1024/phase['transaction_seconds']
        phase['wall_kib_per_second'] = len(plaintext)/1024/phase['wall_seconds']
    case['end'], _ = client.transact(END)
    case['verified'] = True
    return case


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--label', default='', help='Board/card/run description for the log')
    parser.add_argument('--firmware', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--info-only', action='store_true')
    parser.add_argument('--allow-scratch-write', action='store_true',
                        help='Overwrite test data plus metadata at the start of a disposable SD card')
    parser.add_argument('--stacks', nargs='+', default=['raw', 'aes', 'camellia', 'aes,camellia', 'camellia,aes'])
    parser.add_argument('--batch-bytes', nargs='+', type=int, default=[512,4096,8192,16384,32768])
    parser.add_argument('--total-bytes', type=int, default=4*1024*1024)
    parser.add_argument('--seed', default='fuse-vault-v3-public-test-data')
    parser.add_argument('--integrity', action='store_true', help='Enable HMAC and packed metadata (formats metadata per configuration)')
    parser.add_argument('--log-batches', action='store_true')
    args = parser.parse_args()
    if not args.info_only and not args.allow_scratch_write:
        parser.error('Use --info-only, or explicitly --allow-scratch-write on a disposable card')
    if args.info_only and args.allow_scratch_write:
        parser.error('--info-only does not take --allow-scratch-write')
    if not 512 <= args.total_bytes <= 4*1024*1024 or args.total_bytes % 512:
        parser.error('--total-bytes must be a multiple of 512 up to 4194304')
    if any(n<512 or n>32768 or n%512 for n in args.batch_bytes):
        parser.error('Batch sizes must be multiples of 512 from 512 to 32768')
    for stack in args.stacks:
        try:
            stack_code(stack)
        except argparse.ArgumentTypeError as exc:
            parser.error(str(exc))
    report = {'protocol': VERSION, 'complete': False, 'results': [], 'host': platform.platform(),
              'started_utc': datetime.now(timezone.utc).isoformat(), 'port': args.port,
              'integrity': args.integrity, 'seed': args.seed, 'label': args.label, 'scratch_write_authorized': args.allow_scratch_write}
    transport = None
    def save():
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2)+'\n')
    try:
        if args.firmware:
            report['firmware_sha256'] = hashlib.sha256(args.firmware.read_bytes()).hexdigest()
        transport = SerialTransport(args.port)
        client = BenchClient(transport)
        report['info'], _ = client.transact(INFO)
        if not args.info_only:
            # Generate test bytes before any timed operation. Reuse across configurations.
            plaintext = hashlib.shake_256(args.seed.encode()).digest(args.total_bytes)
            for stack in args.stacks:
                for batch in args.batch_bytes:
                    case = {}
                    report['results'].append(case)
                    run_case(client, stack, batch, plaintext, args.log_batches, case, args.integrity)
                    print(f'{stack:20s} {batch:5d} B: write {case["write"]["kib_per_second"]:.1f} KiB/s, '
                          f'read {case["read"]["kib_per_second"]:.1f} KiB/s; verified', flush=True)
                    save()
        else:
            print(json.dumps(report['info'], indent=2))
        report['complete'] = True
    except (OSError, ValueError, RuntimeError, KeyboardInterrupt) as exc:
        report['error'] = str(exc) or 'Interrupted'
    finally:
        if transport is not None:
            transport.close()
        save()
    if not report['complete']:
        parser.exit(1, report.get('error', 'Incomplete')+'\n')


if __name__ == '__main__':
    main()
