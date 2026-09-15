"""Independent client through runtime unlock, real encrypted media and CTAPHID."""
import hashlib
import sys
from fido2.ctap import CtapError
from fido2.ctap2 import Ctap2, ClientPin, CredentialManagement
from fido2.ctap2.pin import PinProtocolV1, PinProtocolV2
from fido_engine_reference_tests import Device


def rejected(code, operation):
    try:
        operation()
    except CtapError as error:
        assert error.code == code, error
    else:
        raise AssertionError(f"expected {code}")


def run(executable, protocol):
    device = Device(executable)
    try:
        ctap = Ctap2(device)
        assert ctap.info.options['uv'] and ctap.info.options['alwaysUv']
        assert 'clientPin' not in ctap.info.options
        rp = 'example.com'
        challenge = hashlib.sha256(b'create through encrypted device runtime').digest()
        pin = ClientPin(ctap, protocol())
        assert pin.get_uv_retries() == 10
        token = pin.get_uv_token(ClientPin.PERMISSION.MAKE_CREDENTIAL, rp)
        def register():
            return ctap.make_credential(challenge, {'id': rp}, {'id': b'alice', 'name': 'alice@example.com'},
                [{'type': 'public-key', 'alg': -7}], options={'rk': True},
                pin_uv_param=pin.protocol.authenticate(token, challenge),
                pin_uv_protocol=pin.protocol.VERSION)
        device.control('deny')
        rejected(CtapError.ERR.OPERATION_DENIED, register)
        device.control('allow')
        result = register()
        credential = result.auth_data.credential_data
        assert result.auth_data.is_user_verified() and result.auth_data.is_user_present()
        credential.public_key.verify(bytes(result.auth_data) + challenge, result.att_stmt['sig'])
        device.control('change-password')
        device.control('lock')
        rejected(CtapError.ERR.OTHER, lambda: ctap.get_info())
        device.reopen()
        ctap = Ctap2(device); pin = ClientPin(ctap, protocol())
        challenge = hashlib.sha256(b'assert through encrypted device runtime').digest()
        token = pin.get_uv_token(ClientPin.PERMISSION.GET_ASSERTION, rp)
        assertion = ctap.get_assertion(rp, challenge,
            pin_uv_param=pin.protocol.authenticate(token, challenge),
            pin_uv_protocol=pin.protocol.VERSION)
        assert assertion.credential['id'] == credential.credential_id
        assert assertion.auth_data.is_user_verified() and assertion.auth_data.is_user_present()
        assert assertion.auth_data.counter > result.auth_data.counter
        credential.public_key.verify(bytes(assertion.auth_data) + challenge, assertion.signature)
        rejected(CtapError.ERR.UV_BLOCKED, lambda: pin.get_uv_token(
            ClientPin.PERMISSION.GET_ASSERTION, 'different.example'))
        device.reopen(); ctap = Ctap2(device); pin = ClientPin(ctap, protocol())
        device.control('expire')
        rejected(CtapError.ERR.UV_BLOCKED, lambda: pin.get_uv_token(
            ClientPin.PERMISSION.GET_ASSERTION, rp))
        device.reopen(); ctap = Ctap2(device); pin = ClientPin(ctap, protocol())
        token = pin.get_uv_token(ClientPin.PERMISSION.CREDENTIAL_MGMT)
        management = CredentialManagement(ctap, pin.protocol, token)
        assert management.get_metadata()[CredentialManagement.RESULT.EXISTING_CRED_COUNT] == 1
        device.control('ui-select')
        device.control('check-list')
        rejected(CtapError.ERR.NOT_ALLOWED, lambda: ctap.get_info())
        # Opening confirmation and cancelling must leave the credential usable.
        device.control('ui-select'); device.control('ui-back')
        device.control('check-list')
        device.control('ui-back')
        assert management.get_metadata()[CredentialManagement.RESULT.EXISTING_CRED_COUNT] == 1
        device.control('ui-select'); device.control('ui-select')
        device.control('ui-right')
        device.control('check-empty')
        device.control('ui-back'); device.control('check-cleared')
        device.reopen(); ctap = Ctap2(device); pin = ClientPin(ctap, protocol())
        token = pin.get_uv_token(ClientPin.PERMISSION.CREDENTIAL_MGMT)
        management = CredentialManagement(ctap, pin.protocol, token)
        assert management.get_metadata()[CredentialManagement.RESULT.EXISTING_CRED_COUNT] == 0
        # Multiple credentials, failed deletion recovery, expiry and key clearing.
        device.reopen(); ctap = Ctap2(device)
        for user in ('alice', 'bob'):
            ctap.make_credential(challenge, {'id': rp},
                {'id': user.encode(), 'name': user + '@example.com'},
                [{'type': 'public-key', 'alg': -7}], options={'rk': True, 'uv': True})
        device.control('ui-select'); device.control('check-two')
        device.control('ui-down'); device.control('check-bob')
        device.control('ui-up'); device.control('ui-select')
        device.control('fail-commit'); device.control('ui-right')
        device.control('check-fault'); device.control('check-cleared')
        device.reopen(); device.control('ui-select'); device.control('check-two')
        device.control('expire'); device.control('ui-select'); device.control('ui-right')
        device.control('check-fault'); device.control('check-cleared')
        device.reopen(); device.control('ui-select'); device.control('check-two')
        device.control('lock'); device.control('check-cleared')
        device.reopen(); ctap = Ctap2(device)
        direct_hash = hashlib.sha256(b'direct built-in verification').digest()
        direct = ctap.make_credential(direct_hash, {'id': rp}, {'id': b'nonresident'},
            [{'type': 'public-key', 'alg': -7}], options={'uv': True})
        assert direct.auth_data.is_user_verified()
        old_id = direct.auth_data.credential_data.credential_id
        device.control('change-password')
        ctap = Ctap2(device)
        preserved = ctap.get_assertion(rp, direct_hash,
            allow_list=[{'type': 'public-key', 'id': old_id}], options={'uv': True})
        direct.auth_data.credential_data.public_key.verify(
            bytes(preserved.auth_data) + direct_hash, preserved.signature)
        device.control('deny')
        rejected(CtapError.ERR.OPERATION_DENIED, ctap.reset)
        device.control('allow'); ctap.reset()
        device.reopen(); ctap = Ctap2(device)
        rejected(CtapError.ERR.NO_CREDENTIALS, lambda: ctap.get_assertion(rp, direct_hash,
            allow_list=[{'type': 'public-key', 'id': old_id}], options={'uv': True}))
        print(f'PIN/UV protocol {protocol.VERSION}: unlock, HID, encrypted persistence, '
              'independent signatures, lock, RP binding, expiry, deletion and reset passed')
    finally:
        device.close()


if __name__ == '__main__':
    for protocol in (PinProtocolV1, PinProtocolV2):
        run(sys.argv[1], protocol)
