from pathlib import Path
import hashlib
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from board_bench import (BenchClient, SerialTransport, MAGIC, VERSION, RESPONSE,
                         INFO, READ, WRITE, run_case, stack_code, is_bench_device)
PEER = sys.argv.pop(1) if len(sys.argv)>1 else None

class ResponseTransport:
    def __init__(self, response):
        self.data = bytearray(response)
        self.writes = []
    def write_all(self, data, deadline):
        self.writes.append(bytes(data))
    def read_exact(self, count, deadline):
        result = bytes(self.data[:count]); del self.data[:count]
        if len(result)!=count:
            raise RuntimeError('Disconnected')
        return result

def response(op=INFO, seq=1, status=0, length=0, version=VERSION, backend=0, self_test=1):
    return RESPONSE.pack(MAGIC, version, op|0x80000000, seq, status, length, 1, 2, 3, 8192, 150000000, 25000000, 0, 0, 0, 0, backend, self_test)

class ProtocolTests(unittest.TestCase):
    def test_response_validation(self):
        for reply in [response(seq=2), response(version=2), response(status=3),
                      response(length=32769), response(op=READ), response(backend=7), response(self_test=0)]:
            with self.assertRaises(RuntimeError):
                BenchClient(ResponseTransport(reply)).transact(INFO)
        row, data = BenchClient(ResponseTransport(response())).transact(INFO)
        self.assertEqual(row['sd_us'], 2)
        self.assertEqual(data, b'')

    def test_invalid_payload(self):
        client = BenchClient(ResponseTransport(b''))
        for size in [0, 511, 513]:
            with self.assertRaises(ValueError):
                client.transact(WRITE, blocks=1, payload=b'x'*size)
        with self.assertRaises(ValueError):
            client.transact(READ, blocks=65)

    def test_partial_transport(self):
        port = SerialTransport.__new__(SerialTransport)
        port.fd = 7
        with patch.object(port, '_wait'), patch('board_bench.os.read', side_effect=[b'ab', b'c', b'd']):
            self.assertEqual(port.read_exact(4, 100), b'abcd')
        with patch.object(port, '_wait'), patch('board_bench.os.write', side_effect=[2, 1, 1]) as write:
            port.write_all(b'abcd', 100)
            self.assertEqual(write.call_count, 3)
        with patch('board_bench.time.monotonic', return_value=101):
            with self.assertRaises(TimeoutError):
                port._wait(False,100)

    def test_device_identity_and_stack(self):
        self.assertEqual(stack_code('aes,camellia'), 0x0201)
        self.assertEqual(stack_code('camellia,aes'), 0x0102)
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)
            (p/'idVendor').write_text('cafe\n'); (p/'idProduct').write_text('4021\n')
            self.assertTrue(is_bench_device(p/'child'))
            (p/'idProduct').write_text('4020\n')
            self.assertFalse(is_bench_device(p/'child'))

@unittest.skipUnless(PEER, 'Pass path to benchmark_peer for end-to-end test')
class EngineIntegration(unittest.TestCase):
    def setUp(self):
        self.process = subprocess.Popen([PEER], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        process=self.process
        class Transport:
            def write_all(self, data, deadline):
                process.stdin.write(data); process.stdin.flush()
            def read_exact(self, count, deadline):
                data=process.stdout.read(count)
                if len(data)!=count: raise RuntimeError('Peer disconnected')
                return data
        self.client=BenchClient(Transport())
    def tearDown(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=5)
        finally:
            if self.process.poll() is None:
                self.process.kill(); self.process.wait()
            self.process.stdout.close()
        self.assertEqual(self.process.returncode,0)
    def test_real_engine_file_backed_matrix(self):
        plain=hashlib.shake_256(b'test').digest(65*512)  # partial last batch
        for stack in ['raw','aes','camellia','aes,camellia','camellia,aes','aes,camellia,aes']:
            for batch in [512,4096,32768]:
                case=run_case(self.client, stack, batch, plain)
                self.assertTrue(case['verified'])
                self.assertEqual(case['read']['completed_bytes'],len(plain))
                authenticated=run_case(self.client,stack,batch,plain,integrity=True)
                self.assertTrue(authenticated['verified'])
                self.assertGreater(authenticated['write']['metadata_write_blocks'],0)
    def test_verification_detects_corruption(self):
        original=self.client.transact
        def damaged(op,*args,**kwargs):
            row,data=original(op,*args,**kwargs)
            if op==READ: data=bytes([data[0]^1])+data[1:]
            return row,data
        self.client.transact=damaged
        with self.assertRaisesRegex(RuntimeError,'verification failed'):
            run_case(self.client,'aes',512,b'x'*512)

if __name__=='__main__': unittest.main()
