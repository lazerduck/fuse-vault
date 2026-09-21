#!/usr/bin/env python3
"""Run the real TinyUSB task body against self-requeuing deferred MSC events."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', type=Path, required=True)
parser.add_argument('--firmware-build', type=Path, required=True)
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]


def body(path):
    text = path.read_text()
    start = text.index('void tud_task_ext(')
    opening = text.index('{', start)
    level = 1
    end = opening + 1
    while level:
        if text[end] == '{':
            level += 1
        elif text[end] == '}':
            level -= 1
        end += 1
    return text[start:end]


with tempfile.TemporaryDirectory(prefix='fv-usb-task-') as tmp:
    for fixed, source in ((False, args.sdk / 'lib/tinyusb/src/device/usbd.c'),
                          (True, args.firmware_build / 'usbd_budget.c')):
        include = Path(tmp) / 'task.inc'
        include.write_text(body(source))
        binary = Path(tmp) / 'test'
        cmd = ['cc', '-std=c11', '-g', '-DCFG_TUSB_MCU=OPT_MCU_NRF5X',
               '-DFV_USB_MSC=1', '-DFV_USB_FIDO=1',
               f'-DFV_USB_TASK_BODY="{include}"',
               '-I' + str(root / 'firmware/security'),
               '-I' + str(args.sdk.resolve() / 'lib/tinyusb/src'),
               str(root / 'tests/usb_task_budget_tests.c'), '-o', str(binary)]
        if args.sanitize:
            cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
        subprocess.run(cmd, check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5)
        assert result.returncode == (0 if fixed else 42), result.stderr
        print(('FIXED: ' if fixed else 'ORIGINAL BUG REPRODUCED: ') +
              (result.stdout or result.stderr).strip())
