"""Exercise actual Pico FIDO handlers with python-fido2 as an independent client.
Requires python-fido2 and cryptography; pass the host engine harness executable.
"""
import hashlib
import random
import subprocess
import sys
from fido2.ctap import CtapDevice, CtapError
from fido2.ctap2 import Ctap2, ClientPin, CredentialManagement
from fido2.hid import CTAPHID
from fido2.ctap2.pin import PinProtocolV1, PinProtocolV2

class Device(CtapDevice):
    capabilities = 4
    @classmethod
    def list_devices(cls):
        return []
    def __init__(self, executable):
        self.proc = subprocess.Popen([executable, '--wire'], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, text=True)
    def call(self, cmd, data=b'', event=None, on_keepalive=None):
        assert cmd == CTAPHID.CBOR
        self.proc.stdin.write(data.hex() + '\n'); self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        assert line, 'engine exited unexpectedly'
        return bytes.fromhex(line.strip())
    def control(self, command):
        self.proc.stdin.write(command + "\n"); self.proc.stdin.flush()
        assert self.proc.stdout.readline().strip() == "ok"
    def reopen(self):
        self.control("reopen")
    def close(self):
        self.proc.stdin.close()
        assert self.proc.wait(timeout=10) == 0

def main(executable, protocol_type):
    dev = Device(executable)
    try:
        ctap = Ctap2(dev)
        assert ctap.info.options['rk'] and not ctap.info.options['clientPin']
        pin = ClientPin(ctap, protocol_type())
        pin.set_pin('983746')
        assert Ctap2(dev).info.options['clientPin']
        rp = 'example.com'
        challenge = hashlib.sha256(b'register').digest()
        token = pin.get_pin_token('983746', ClientPin.PERMISSION.MAKE_CREDENTIAL, rp)
        auth = pin.protocol.authenticate(token, challenge)
        result = ctap.make_credential(challenge, {'id': rp, 'name': 'Example'},
            {'id': b'alice', 'name': 'Alice', 'displayName': 'Alice'},
            [{'type': 'public-key', 'alg': -7}], options={'rk': True},
            pin_uv_param=auth, pin_uv_protocol=pin.protocol.VERSION)
        assert result.fmt == 'packed'
        data = result.auth_data
        assert data.rp_id_hash == hashlib.sha256(rp.encode()).digest()
        assert data.is_user_present() and data.is_user_verified()
        credential = data.credential_data
        credential.public_key.verify(bytes(data)+challenge, result.att_stmt['sig'])
        dev.reopen()
        ctap = Ctap2(dev); pin = ClientPin(ctap, protocol_type())
        challenge = hashlib.sha256(b'login').digest()
        token = pin.get_pin_token('983746', ClientPin.PERMISSION.GET_ASSERTION, rp)
        assertion = ctap.get_assertion(rp,challenge,
            pin_uv_param=pin.protocol.authenticate(token,challenge),pin_uv_protocol=pin.protocol.VERSION)
        assert assertion.credential['id'] == credential.credential_id
        assert assertion.auth_data.is_user_present() and assertion.auth_data.is_user_verified()
        credential.public_key.verify(bytes(assertion.auth_data)+challenge,assertion.signature)
        assert assertion.auth_data.counter > data.counter
        try:
            pin.get_pin_token('wrong-pin', ClientPin.PERMISSION.GET_ASSERTION,rp)
            raise AssertionError('wrong PIN accepted')
        except CtapError as e:
            assert e.code == CtapError.ERR.PIN_INVALID, e
        retries = pin.get_pin_retries()[0]
        dev.reopen()
        ctap = Ctap2(dev); pin = ClientPin(ctap, protocol_type())
        assert pin.get_pin_retries()[0] == retries
        token = pin.get_pin_token('983746', ClientPin.PERMISSION.CREDENTIAL_MGMT)
        management = CredentialManagement(ctap, pin.protocol, token)
        assert management.get_metadata()[CredentialManagement.RESULT.EXISTING_CRED_COUNT] == 1
        rps = management.enumerate_rps()
        assert len(rps) == 1
        creds = management.enumerate_creds(hashlib.sha256(rp.encode()).digest())
        assert len(creds) == 1
        management.delete_cred({'type': 'public-key', 'id': credential.credential_id})
        assert management.get_metadata()[CredentialManagement.RESULT.EXISTING_CRED_COUNT] == 0
        dev.control('deny')
        try:
            ctap.reset()
            raise AssertionError('reset without presence accepted')
        except CtapError as e:
            assert e.code == CtapError.ERR.OPERATION_DENIED, e
        dev.control('allow')
        ctap.reset()
        dev.reopen()
        ctap = Ctap2(dev)
        assert not ctap.info.options['clientPin']
        dev.control('fail-commit')
        try:
            ClientPin(ctap, protocol_type()).set_pin('234567')
            raise AssertionError('PIN success reported despite failed durable commit')
        except CtapError as e:
            assert e.code == CtapError.ERR.PROCESSING, e
        dev.reopen()
        assert not Ctap2(dev).info.options['clientPin']
        malformed = random.Random(451)
        for _ in range(1000):
            request = bytes([malformed.choice([1, 2, 6, 8, 10])])
            request += malformed.randbytes(malformed.randrange(0, 128))
            assert dev.call(CTAPHID.CBOR, request)
        print('Independent client: PIN setup, resident registration, attestation signature, '
              'snapshot reopen, assertion signature, persistent PIN retries, credential deletion, '
              'reset presence, commit-failure rejection and malformed requests passed.')
    finally:
        dev.close()
if __name__ == '__main__':
    for protocol_type in (PinProtocolV1, PinProtocolV2):
        main(sys.argv[1], protocol_type)
