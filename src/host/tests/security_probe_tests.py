import copy
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
from contextlib import redirect_stdout
from io import StringIO
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from security_probe import Probe, compare, locks, validate_snapshot, main


def snapshot():
    return {'schema': 1, 'command': 'otp', 'ok': True, 'device_id': 'example', 'pages': [
        {'page': i, 'blank': 64, 'programmed': 0, 'all_ones': 0, 'unreadable': 0,
         'read_error': 0, 'lock_raw': [0, 0], 'lock_status': [0, 0], 'software_lock': 0}
        for i in range(64)]}


class Transport:
    def __init__(self, output):
        self.output = bytearray(output); self.sent = []
    def close(self): pass
    def write_all(self, data, deadline): self.sent.append(data)
    def read_exact(self, n, deadline):
        if len(self.output) < n: raise TimeoutError()
        out = self.output[:n];del self.output[:n];return out


class Tests(unittest.TestCase):
    def test_diff(self):
        before = snapshot();after = copy.deepcopy(before)
        after['pages'][20].update(blank=63, all_ones=1, lock_raw=[0, 0x030303])
        delta = compare(before, after)['changed_pages']
        self.assertEqual(len(delta), 1);self.assertEqual(delta[0]['page'], 20)
        self.assertEqual(delta[0]['locks_after']['secure'], 'inaccessible')
        after['pages'][20].update(blank=0, all_ones=0, unreadable=64, read_error=-4, lock_status=[-4, -4])
        self.assertNotIn('secure', locks(after['pages'][20]))
        self.assertEqual(compare(before, before)['changed_pages'], [])
    def test_malformed(self):
        s = snapshot();s['device_id'] = 'different'
        with self.assertRaises(ValueError): compare(snapshot(), s)
        for mutation in (lambda s: s['pages'].pop(), lambda s: s['pages'][0].update(blank=65),
                         lambda s: s['pages'][0].update(page=1), lambda s: s['pages'][0].update(lock_raw=[0]),
                         lambda s: s.update(schema=2)):
            s = snapshot();mutation(s)
            with self.assertRaises(ValueError): validate_snapshot(s)
    def test_redundancy(self):
        s = snapshot()['pages'][0];s['lock_raw'][1] = 0x000303
        self.assertEqual(locks(s)['secure'], 'inaccessible')
        self.assertFalse(locks(s)['redundant_copies_agree'][1])
    def test_enrollment_guards(self):
        info = {'command': 'info', 'ok': True, 'protocol': 1, 'device_id': 'example',
                'otp_writes': True, 'persistent_authority': True, 'debug_enrollment': True}
        for action in ('prepare-flash', 'provision', 'destroy', 'create'):
            args = ['--port', 'fake', action, '--confirm-device', 'wrong']
            if action == 'create': args += ['--erase-sd']
            t = Transport(json.dumps(info).encode() + b'\n')
            with patch('security_probe.SerialTransport', return_value=t), self.assertRaises(RuntimeError): main(args)
            self.assertEqual(t.sent, [b'INFO\n'])
        t = Transport(json.dumps(info).encode() + b'\n' + b'{"command":"provision","ok":true}\n')
        with patch('security_probe.SerialTransport', return_value=t), redirect_stdout(StringIO()):
            main(['--port', 'fake', 'provision', '--confirm-device', 'example'])
        self.assertEqual(t.sent, [b'INFO\n', b'PROVISION example\n'])
        info['debug_enrollment'] = False
        t = Transport(json.dumps(info).encode() + b'\n')
        with patch('security_probe.SerialTransport', return_value=t), self.assertRaises(RuntimeError):
            main(['--port', 'fake', 'provision', '--confirm-device', 'example'])
        self.assertEqual(t.sent, [b'INFO\n'])
    def test_framing(self):
        t = Transport(json.dumps(snapshot()).encode() + b'\n');p = Probe(t)
        self.assertEqual(p.transact('OTP', 'otp'), snapshot());self.assertEqual(t.sent, [b'OTP\n'])
        for output in (b'{"command":"info","ok":true}\n', b'{"command":"otp","ok":false}\n', b'x'*24576):
            with self.assertRaises(RuntimeError): Probe(Transport(output)).transact('OTP', 'otp')
        with self.assertRaises(ValueError): p.transact('OTP\nBAD', 'otp')
        with self.assertRaises(TimeoutError): Probe(Transport(b'')).transact('OTP', 'otp')


if __name__ == '__main__': unittest.main()
