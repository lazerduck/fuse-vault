#!/usr/bin/env python3
"""Development USB test: creates one test passkey and verifies a real signature.
Never initializes/resets the device and never enters or approves device secrets.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
from threading import Event, Timer

from fido2.ctap2 import Ctap2
from fido2.hid import CtapHidDevice
from fido2.webauthn import AttestedCredentialData


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['info', 'register', 'login'])
    parser.add_argument('--credential', type=Path, default=Path('fido-test-credential.json'))
    parser.add_argument('--device', help='Board serial number or exact hidraw path')
    args = parser.parse_args()
    devices = []
    for device in CtapHidDevice.list_devices():
        descriptor = device.descriptor
        if descriptor.vid == 0xcafe and descriptor.pid == 0x4022 and (
                not args.device or os.fsdecode(descriptor.path) == args.device or
                descriptor.serial_number == args.device):
            devices.append(device)
        else:
            device.close()
    if len(devices) != 1:
        for device in devices:
            device.close()
        parser.error(f'Expected one accessible Fuse Vault FIDO interface; found {len(devices)}. Check hidraw permissions or use --device.')
    device = devices[0]
    try:
        ctap = Ctap2(device)
        if args.action == 'info':
            print(json.dumps({'versions': ctap.info.versions, 'options': ctap.info.options,
                              'aaguid': str(ctap.info.aaguid)}, indent=2))
            return
        cancel = Event()
        timer = Timer(120, cancel.set)
        timer.start()
        try:
            digest = hashlib.sha256(os.urandom(32)).digest()
            print('Use the device screen to unlock/verify, then approve or reject. Ctrl-C cancels.')
            def keepalive(status):
                print(f'Waiting: {status.name}', flush=True)
            if args.action == 'register':
                if args.credential.exists():
                    parser.error('Credential file already exists; choose a new --credential path or use login.')
                # Unique reserved test RP avoids accumulating indistinguishable accounts.
                rp = os.urandom(8).hex() + '.fuse-vault.test'
                result = ctap.make_credential(digest, {'id': rp, 'name': 'Fuse Vault local test'},
                    {'id': os.urandom(16), 'name': 'Local test'}, [{'type': 'public-key', 'alg': -7}],
                    options={'rk': True, 'uv': True}, event=cancel, on_keepalive=keepalive)
                data = result.auth_data
                credential = data.credential_data
                assert result.fmt == 'packed' and 'x5c' not in result.att_stmt
                credential.public_key.verify(bytes(data) + digest, result.att_stmt['sig'])
                check(data, rp)
                with args.credential.open('x') as out:
                    json.dump({'rp': rp, 'credential': bytes(credential).hex()}, out, indent=2)
                print(f'Registration verified. Public test record saved to {args.credential}.')
            else:
                record = json.loads(args.credential.read_text())
                rp = record['rp']
                credential = AttestedCredentialData(bytes.fromhex(record['credential']))
                result = ctap.get_assertion(rp, digest,
                    allow_list=[{'type': 'public-key', 'id': credential.credential_id}],
                    options={'uv': True}, event=cancel, on_keepalive=keepalive)
                assert result.credential['id'] == credential.credential_id
                credential.public_key.verify(bytes(result.auth_data) + digest, result.signature)
                check(result.auth_data, rp)
                print('Login signature, RP, UV/UP flags and zero counter verified.')
        except KeyboardInterrupt:
            cancel.set()
            raise
        finally:
            timer.cancel()
    finally:
        device.close()


def check(data, rp):
    assert data.rp_id_hash == hashlib.sha256(rp.encode()).digest()
    assert data.is_user_present() and data.is_user_verified() and data.counter == 0


if __name__ == '__main__':
    main()
