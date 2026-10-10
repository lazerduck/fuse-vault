"""Independent FIDO client exercises the actual bounded encrypted journal backend."""
import hashlib
import sys
from fido_engine_reference_tests import (Device, Ctap2, CtapError, ClientPin,
    PinProtocolV1, PinProtocolV2, CredentialManagement, exercise)


def capacity(executable):
    dev = Device(executable)
    try:
        ctap = Ctap2(dev)
        challenge = hashlib.sha256(b'512 journal capacity').digest()
        keys = []
        for i in range(512):
            if i % 64 == 0: print(f"Registering {i}/512", flush=True)
            result = ctap.make_credential(challenge,
                {'id': f'site-{i}.example'},
                {'id': i.to_bytes(64, 'big'), 'name': f'user-{i}'.ljust(100, 'x'),
                 'displayName': f'Account {i}'.ljust(100, 'y')},
                [{'type': 'public-key', 'alg': -7}], options={'rk': True, 'uv': True})
            keys.append(result.auth_data.credential_data)
        try:
            ctap.make_credential(challenge, {'id': 'overflow.example'},
                {'id': b'overflow', 'name': 'overflow'},
                [{'type': 'public-key', 'alg': -7}], options={'rk': True, 'uv': True})
        except CtapError as error:
            assert error.code == CtapError.ERR.KEY_STORE_FULL
        else:
            raise AssertionError('513th resident credential accepted')
        dev.control('reopen')
        ctap = Ctap2(dev)
        for i in [0, 32, 255, 256, 511]:
            assertion = ctap.get_assertion(f'site-{i}.example', challenge,
                allow_list=[{'type': 'public-key', 'id': keys[i].credential_id}],
                options={'uv': True})
            keys[i].public_key.verify(bytes(assertion.auth_data) + challenge, assertion.signature)
        pin = ClientPin(ctap, PinProtocolV2())
        token = pin.get_uv_token(ClientPin.PERMISSION.CREDENTIAL_MGMT)
        management = CredentialManagement(ctap, pin.protocol, token)
        assert management.get_metadata()[CredentialManagement.RESULT.EXISTING_CRED_COUNT] == 512
        assert len(management.enumerate_rps()) == 512
        assert len(management.enumerate_creds(hashlib.sha256(b'site-511.example').digest())) == 1
        management.delete_cred({'type': 'public-key', 'id': keys[256].credential_id})
        ctap.make_credential(challenge, {'id': 'replacement.example'},
            {'id': b'replacement', 'name': 'replacement'},
            [{'type': 'public-key', 'alg': -7}], options={'rk': True, 'uv': True})
        # Reset is destructive initialization, not a 512-credential journal transaction.
        dev.control('time 1')
        ctap.reset()
        dev.control('reopen')
        ctap = Ctap2(dev)
        pin = ClientPin(ctap, PinProtocolV2())
        token = pin.get_uv_token(ClientPin.PERMISSION.CREDENTIAL_MGMT)
        assert CredentialManagement(ctap, pin.protocol, token).get_metadata()[CredentialManagement.RESULT.EXISTING_CRED_COUNT] == 0
        print('512 resident credentials: reopen, independent signatures, delete/reuse and reset passed')
    finally:
        dev.close()


def migration(executable):
    dev = Device(executable)
    try:
        dev.control('make-legacy')
        ctap = Ctap2(dev)
        challenge = hashlib.sha256(b'migration').digest()
        keys = []
        for resident in (True, False):
            result = ctap.make_credential(challenge, {'id': 'migration.example'},
                {'id': bytes([resident]), 'name': 'existing account'},
                [{'type': 'public-key', 'alg': -7}], options={'rk': resident, 'uv': True})
            keys.append(result.auth_data.credential_data)
        dev.control('migrate')
        dev.control('reopen')
        ctap = Ctap2(dev)
        for key in keys:
            assertion = ctap.get_assertion('migration.example', challenge,
                allow_list=[{'type': 'public-key', 'id': key.credential_id}], options={'uv': True})
            key.public_key.verify(bytes(assertion.auth_data)+challenge, assertion.signature)
        print('Automatic snapshot migration preserves resident and nonresident signatures', flush=True)
    finally:
        dev.close()


if __name__ == '__main__':
    for protocol in (PinProtocolV1, PinProtocolV2):
        exercise(sys.argv[1], protocol, True)
    migration(sys.argv[1])
    capacity(sys.argv[1])
