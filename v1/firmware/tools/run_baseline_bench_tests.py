#!/usr/bin/env python3
"""Protocol checks without a physical board; destructive commands are never retried."""
import json
import unittest
from unittest.mock import patch
from run_baseline_bench import transact, find_port


class ProtocolTests(unittest.TestCase):
    def exchange(self, response):
        with patch('run_baseline_bench.os.write', return_value=9) as write, \
             patch('run_baseline_bench.select.select', return_value=([7], [], [])), \
             patch('run_baseline_bench.os.read', side_effect=[*response, b'']):
            rows = transact(7, 'ERASE-SD', timeout=1)
            return rows, [call.args[1] for call in write.call_args_list]

    def test_fragmented_results_and_single_command(self):
        rows, seen = self.exchange([b'{"ok":tr', b'ue}\n{"done":true}\n'])
        self.assertEqual(rows, [{'ok': True}, {'done': True}])
        self.assertEqual(seen, [b'ERASE-SD\n'])

    def test_disconnect_is_not_success(self):
        with self.assertRaisesRegex(RuntimeError, 'disconnected'):
            self.exchange([b'{"ok":true}\n'])

    def test_invalid_json(self):
        with self.assertRaises(json.JSONDecodeError):
            self.exchange([b'wrong firmware\n'])

    def test_wrong_response_type(self):
        with self.assertRaisesRegex(RuntimeError, 'Invalid'):
            self.exchange([b'[]\n'])

    def test_no_board_does_not_select_arbitrary_port(self):
        with patch('run_baseline_bench.Path.glob', return_value=[]):
            with self.assertRaisesRegex(RuntimeError, 'found 0'):
                find_port()
            with self.assertRaisesRegex(RuntimeError, 'not the expected'):
                find_port('/dev/ttyACM0')

    def test_short_destructive_command_is_not_retried(self):
        with patch('run_baseline_bench.os.write', return_value=3) as write:
            with self.assertRaisesRegex(RuntimeError, 'Short command'):
                transact(7, 'ERASE-SD', timeout=1)
            self.assertEqual(write.call_count, 1)

    def test_regular_file_cannot_be_a_ram_test_target(self):
        from bench_ram_usb import validate_device
        with self.assertRaisesRegex(RuntimeError, 'block device'):
            validate_device(__file__)


if __name__ == '__main__':
    unittest.main()
