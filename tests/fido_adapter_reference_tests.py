"""Real firmware adapter + HID + encrypted store; only platform/UI are simulated."""
import hashlib
import sys
from fido2.ctap import CtapError
from fido2 import cbor
from fido2.hid import CTAPHID
from fido2.hid.base import parse_report_descriptor
from fido2.ctap2 import Ctap2, ClientPin
from fido2.ctap2.pin import PinProtocolV2
from fido_engine_reference_tests import Device, rejected

d = Device(sys.argv[1])
try:
    assert parse_report_descriptor(bytes.fromhex(d.exchange("descriptor"))) == (64, 64)
    c = Ctap2(d)
    assert c.info.options['uv'] and 'clientPin' not in c.info.options
    assert d.call(CTAPHID.CBOR, b'\x01' + cbor.encode({2: 7})) == b'\x12'
    h = hashlib.sha256(b'adapter integration').digest()
    def register(rp='example.test'):
        return c.make_credential(h, {'id': rp}, {'id': b'adapter', 'name': 'Adapter'},
                                [{'type': 'public-key', 'alg': -7}], options={'rk': True, 'uv': True})
    created = register()
    key = created.auth_data.credential_data
    key.public_key.verify(bytes(created.auth_data) + h, created.att_stmt['sig'])
    assert created.auth_data.is_user_present() and created.auth_data.is_user_verified()
    counts = list(map(int, d.exchange('counts').split()))
    assert counts == [1, 1, 0], counts
    result = c.get_assertion('example.test', h, options={'uv': True})
    key.public_key.verify(bytes(result.auth_data)+h, result.signature)
    assert list(map(int, d.exchange('counts').split())) == [2, 1, 0]
    register('another.test')
    assert list(map(int, d.exchange('counts').split())) == [3, 2, 0]
    d.control('reopen')
    c = Ctap2(d)
    result = c.get_assertion('example.test', h, options={'uv': True})
    key.public_key.verify(bytes(result.auth_data)+h, result.signature)
    assert list(map(int, d.exchange('counts').split())) == [4, 3, 0]
    d.control('deny')
    with rejected(CtapError.ERR.OPERATION_DENIED):
        register()
    d.control('allow')
    d.control('time 1000000')
    with rejected(CtapError.ERR.NOT_ALLOWED):
        c.reset()
    # Engine close/reopen must not reopen the USB reset window.
    d.control('engine-close')
    with rejected(CtapError.ERR.NOT_ALLOWED):
        c.reset()
    d.control('time 1700000')
    d.control('wrong')
    with rejected(CtapError.ERR.KEEPALIVE_CANCEL):
        c.get_assertion('example.test', h, options={'uv': True})
    assert list(map(int, d.exchange('counts').split()))[2] == 1
    d.control('correct')
    d.control('cancel')
    with rejected(CtapError.ERR.KEEPALIVE_CANCEL):
        c.get_assertion('example.test', h, options={'uv': True})
    d.control('uncancel')
    d.control('reopen')
    c = Ctap2(d)
    pin = ClientPin(c, PinProtocolV2())
    token = pin.get_uv_token(ClientPin.PERMISSION.GET_ASSERTION, 'example.test')
    result = c.get_assertion('example.test', h, pin_uv_param=pin.protocol.authenticate(token, h), pin_uv_protocol=2)
    key.public_key.verify(bytes(result.auth_data)+h, result.signature)
    assert result.auth_data.is_user_present() and result.auth_data.is_user_verified()
    assert d.exchange('policy-get') == '0'
    d.control('policy-invalid')
    assert d.exchange('policy-get') == '0'
    d.control('policy-fail')
    d.control('policy 1')
    assert d.exchange('policy-get') == '1'
    c.get_assertion('example.test', h, options={'uv': True})  # Fresh verification after changing policy.
    before = list(map(int, d.exchange('counts').split()))
    d.control('time 100000000')
    c.get_assertion('another.test', h, options={'uv': True})
    after = list(map(int, d.exchange('counts').split()))
    assert after[1] == before[1] and after[0] == before[0] + 1
    d.control('reopen')
    d.control('policy-locked')
    c.get_assertion('example.test', h, options={'uv': True})
    assert d.exchange('policy-get') == '1'
    before = list(map(int, d.exchange('counts').split()))
    d.control('time 200000000')
    c.get_assertion('another.test', h, options={'uv': True})
    assert list(map(int, d.exchange('counts').split()))[1] == before[1]
    d.control('policy 0')
    c.get_assertion('example.test', h, options={'uv': True})
    before = list(map(int, d.exchange('counts').split()))
    c.get_assertion('another.test', h, options={'uv': True})
    assert list(map(int, d.exchange('counts').split()))[1] == before[1] + 1
    d.control('cancel-late')
    with rejected(CtapError.ERR.KEEPALIVE_CANCEL):
        c.get_assertion('example.test', h, options={'uv': True})
    d.control('locked')
    print('Firmware HID/store/UI adapter: registration, signed assertion, reopen, RP/age reverify, denial, attempts, cancel, UV tokens passed')
finally:
    d.close()
