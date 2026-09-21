"""Independent python-fido2 client against V2's actual C engine (not USB HID).
The child process uses test-only RAM snapshots and simulated physical UI callbacks.
"""
import hashlib
import random
import subprocess
import sys
from contextlib import contextmanager

from fido2.ctap import CtapDevice, CtapError
from fido2.ctap2 import Ctap2, ClientPin, CredentialManagement
from fido2.ctap2.pin import PinProtocolV1, PinProtocolV2
from fido2.hid import CTAPHID


class Device(CtapDevice):
    capabilities = 4

    @classmethod
    def list_devices(cls):
        return []

    def __init__(self, executable):
        self.proc = subprocess.Popen([executable, '--wire'], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, text=True)

    def exchange(self, line):
        self.proc.stdin.write(line + '\n')
        self.proc.stdin.flush()
        response = self.proc.stdout.readline()
        assert response, 'engine exited unexpectedly'
        return response.strip()

    def call(self, cmd, data=b'', event=None, on_keepalive=None):
        assert cmd == CTAPHID.CBOR
        return bytes.fromhex(self.exchange(data.hex()))

    def control(self, command):
        assert self.exchange(command) == 'ok'

    def close(self):
        self.proc.stdin.close()
        try:
            assert self.proc.wait(timeout=10) == 0
        finally:
            if self.proc.poll() is None:
                self.proc.kill()
                self.proc.wait()
            self.proc.stdout.close()


@contextmanager
def rejected(*codes):
    try:
        yield
    except CtapError as exc:
        assert exc.code in codes, (exc, codes)
    else:
        raise AssertionError('operation unexpectedly accepted')


def exercise(executable, protocol_type, encrypted=False):
    dev = Device(executable)
    try:
        ctap = Ctap2(dev)
        assert ctap.info.versions == ['FIDO_2_0', 'FIDO_2_1']
        assert ctap.info.extensions == []
        assert ctap.info.algorithms == [{'alg': -7, 'type': 'public-key'}]
        assert ctap.info.options['rk'] and ctap.info.options['uv']
        assert ctap.info.options['alwaysUv'] and 'clientPin' not in ctap.info.options
        pin = ClientPin(ctap, protocol_type())
        assert pin.get_uv_retries() == 10
        with rejected(CtapError.ERR.INVALID_SUBCOMMAND):
            ctap.client_pin(pin.protocol.VERSION, 3)

        rp = 'example.com'
        rp_entity = {'id': rp, 'name': 'Example'}
        params = [{'type': 'public-key', 'alg': -7}]
        challenge = hashlib.sha256(b'register').digest()

        def register(name, resident=True, **kwargs):
            return ctap.make_credential(challenge, rp_entity,
                {'id': name.encode(), 'name': name, 'displayName': name}, params,
                options={'rk': resident, 'uv': True}, **kwargs)

        dev.control('uv-deny')
        with rejected(CtapError.ERR.UV_BLOCKED):
            register('denied-uv')
        with rejected(CtapError.ERR.UV_BLOCKED):
            pin.get_uv_token(ClientPin.PERMISSION.MAKE_CREDENTIAL, rp)
        dev.control('uv-allow')
        dev.control('deny')
        before = dev.exchange('commits')
        with rejected(CtapError.ERR.OPERATION_DENIED):
            register('denied-presence')
        assert dev.exchange('commits') == before
        dev.control('allow')

        dev.control('cancel')
        before = dev.exchange('commits')
        with rejected(CtapError.ERR.KEEPALIVE_CANCEL):
            register('already-cancelled')
        assert dev.exchange('commits') == before
        dev.control('uncancel')

        # Direct UV (no PIN/token) still requires presence and verifies signatures.
        result = register('Alice')
        assert result.fmt == 'packed'
        data = result.auth_data
        credential = data.credential_data
        assert data.rp_id_hash == hashlib.sha256(rp.encode()).digest()
        assert data.is_user_present() and data.is_user_verified() and data.counter == 0
        credential.public_key.verify(bytes(data) + challenge, result.att_stmt['sig'])
        descriptor = {'type': 'public-key', 'id': credential.credential_id}
        with rejected(CtapError.ERR.CREDENTIAL_EXCLUDED):
            register('Alice', exclude_list=[descriptor])
        nonresident = register('Bob', False).auth_data.credential_data
        bob = {'type': 'public-key', 'id': nonresident.credential_id}

        dev.control('reopen')
        if encrypted:
            dev.control('change-credential')
        ctap = Ctap2(dev)
        pin = ClientPin(ctap, protocol_type())
        login_hash = hashlib.sha256(b'login').digest()
        before = dev.exchange('commits')
        for allow_list, cred in ((None, credential), ([bob], nonresident)):
            assertion = ctap.get_assertion(rp, login_hash, allow_list=allow_list,
                                          options={'uv': True})
            assert assertion.credential['id'] == cred.credential_id
            assert assertion.auth_data.counter == 0
            assert assertion.auth_data.is_user_present() and assertion.auth_data.is_user_verified()
            cred.public_key.verify(bytes(assertion.auth_data) + login_hash, assertion.signature)
        assert dev.exchange('commits') == before, 'assertion performed an unnecessary persistent write'
        with rejected(CtapError.ERR.NO_CREDENTIALS):
            ctap.get_assertion('other.example', login_hash, allow_list=[descriptor], options={'uv': True})
        dev.control('deny')
        with rejected(CtapError.ERR.OPERATION_DENIED):
            ctap.get_assertion(rp, login_hash, options={'uv': True})
        dev.control('allow')

        # Scoped built-in UV tokens use authenticatorClientPIN but no user PIN.
        token = pin.get_uv_token(ClientPin.PERMISSION.MAKE_CREDENTIAL, rp)
        auth = pin.protocol.authenticate(token, challenge)
        register('Carol', pin_uv_param=auth, pin_uv_protocol=pin.protocol.VERSION)
        token = pin.get_uv_token(ClientPin.PERMISSION.GET_ASSERTION, rp)
        assertion = ctap.get_assertion(rp, login_hash, allow_list=[descriptor],
            pin_uv_param=pin.protocol.authenticate(token, login_hash),
            pin_uv_protocol=pin.protocol.VERSION)
        credential.public_key.verify(bytes(assertion.auth_data) + login_hash, assertion.signature)
        with rejected(CtapError.ERR.PIN_AUTH_INVALID):
            register('wrong-permission', pin_uv_param=pin.protocol.authenticate(token, challenge),
                     pin_uv_protocol=pin.protocol.VERSION)
        with rejected(CtapError.ERR.PIN_AUTH_INVALID):
            ctap.get_assertion('other.example', login_hash,
                pin_uv_param=pin.protocol.authenticate(token, login_hash),
                pin_uv_protocol=pin.protocol.VERSION)
        dev.control('time 600002')
        with rejected(CtapError.ERR.PIN_AUTH_INVALID):
            ctap.get_assertion(rp, login_hash,
                pin_uv_param=pin.protocol.authenticate(token, login_hash),
                pin_uv_protocol=pin.protocol.VERSION)
        dev.control('reopen')
        with rejected(CtapError.ERR.PIN_AUTH_INVALID):
            ctap.get_assertion(rp, login_hash,
                pin_uv_param=pin.protocol.authenticate(token, login_hash),
                pin_uv_protocol=pin.protocol.VERSION)
        ctap = Ctap2(dev)
        pin = ClientPin(ctap, protocol_type())
        token = pin.get_uv_token(ClientPin.PERMISSION.CREDENTIAL_MGMT)
        management = CredentialManagement(ctap, pin.protocol, token)
        key = CredentialManagement.RESULT.EXISTING_CRED_COUNT
        assert management.get_metadata()[key] == 2
        assert len(management.enumerate_rps()) == 1
        assert len(management.enumerate_creds(hashlib.sha256(rp.encode()).digest())) == 2
        management.delete_cred(descriptor)
        assert management.get_metadata()[key] == 1
        dev.control('reopen')
        with rejected(CtapError.ERR.NO_CREDENTIALS):
            ctap.get_assertion(rp, login_hash, allow_list=[descriptor], options={'uv': True})

        dev.control('fail-commit')
        with rejected(CtapError.ERR.PROCESSING):
            register('failed-write')
        with rejected(CtapError.ERR.PROCESSING):
            ctap.get_info()
        dev.control('reopen')
        pin = ClientPin(ctap, protocol_type())
        token = pin.get_uv_token(ClientPin.PERMISSION.CREDENTIAL_MGMT)
        assert CredentialManagement(ctap, pin.protocol, token).get_metadata()[key] == 1

        dev.control('deny')
        with rejected(CtapError.ERR.OPERATION_DENIED):
            ctap.reset()
        dev.control('allow')
        ctap.reset()
        dev.control('reopen')
        with rejected(CtapError.ERR.NO_CREDENTIALS):
            ctap.get_assertion(rp, login_hash, allow_list=[bob], options={'uv': True})
        assert 'clientPin' not in ctap.get_info().options
        dev.control('time 620003')
        with rejected(CtapError.ERR.NOT_ALLOWED):
            ctap.reset()

        dev.control('fail-rng')
        with rejected(CtapError.ERR.PROCESSING):
            register('failed-rng')
        dev.control('reopen')
        dev.control('cancel')
        with rejected(CtapError.ERR.KEEPALIVE_CANCEL):
            ctap.get_info()
        dev.control('uncancel')
        malformed = random.Random(451)
        for _ in range(1000):
            request = bytes([malformed.choice([1, 2, 6, 8, 10])])
            request += malformed.randbytes(malformed.randrange(0, 128))
            assert dev.call(CTAPHID.CBOR, request)
        assert dev.call(CTAPHID.CBOR, b'\x04' * 2049)[0] == CtapError.ERR.INVALID_LENGTH
        print(f'UV protocol {pin.protocol.VERSION}: signatures, resident/nonresident, scope, '
              'denial, zero counters, reopen, deletion/reset, RNG/commit faults and malformed CBOR passed')
    finally:
        dev.close()


def capacity(executable):
    """A full store must preserve previously committed credentials after reopen."""
    dev = Device(executable)
    try:
        ctap = Ctap2(dev)
        challenge = hashlib.sha256(b'capacity').digest()
        first = None
        for count in range(200):
            try:
                result = ctap.make_credential(challenge, {'id': 'capacity.example'},
                    {'id': count.to_bytes(4, 'big'), 'name': f'user-{count}'.ljust(100, 'x')},
                    [{'type': 'public-key', 'alg': -7}], options={'rk': True, 'uv': True})
                if first is None:
                    first = result.auth_data.credential_data
            except CtapError as exc:
                assert exc.code == CtapError.ERR.KEY_STORE_FULL, exc
                break
        else:
            raise AssertionError('capacity fixture did not fill 64 KiB store')
        assert first is not None and count > 1
        dev.control('reopen')
        ctap = Ctap2(dev)
        assertion = ctap.get_assertion('capacity.example', challenge,
            allow_list=[{'type': 'public-key', 'id': first.credential_id}], options={'uv': True})
        first.public_key.verify(bytes(assertion.auth_data) + challenge, assertion.signature)
        pin = ClientPin(ctap, PinProtocolV2())
        token = pin.get_uv_token(ClientPin.PERMISSION.CREDENTIAL_MGMT)
        metadata = CredentialManagement(ctap, pin.protocol, token).get_metadata()
        assert metadata[CredentialManagement.RESULT.EXISTING_CRED_COUNT] == count
        print(f'Capacity: clean full-store failure after {count} fixture records; existing key survived')
    finally:
        dev.close()


if __name__ == '__main__':
    for protocol_type in (PinProtocolV1, PinProtocolV2):
        exercise(sys.argv[1], protocol_type, '--encrypted' in sys.argv[2:])
    capacity(sys.argv[1])
