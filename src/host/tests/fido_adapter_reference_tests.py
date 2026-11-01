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
    assert d.exchange('scope') == '2 1 1'  # Host unlock grants passkeys only.
    key = created.auth_data.credential_data
    key.public_key.verify(bytes(created.auth_data) + h, created.att_stmt['sig'])
    assert created.auth_data.is_user_present() and created.auth_data.is_user_verified()
    counts = list(map(int, d.exchange('counts').split()))
    assert counts == [1, 1, 0], counts
    result = c.get_assertion('example.test', h, options={'uv': True})
    key.public_key.verify(bytes(result.auth_data)+h, result.signature)
    assert list(map(int, d.exchange('counts').split())) == [2, 1, 0]
    another_key = register('another.test').auth_data.credential_data
    assert d.exchange('scope') == '2 1 1'  # RP reverification cannot broaden access.
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
    entries = [d.exchange(f'local-list {i}').split() for i in range(2)]
    assert all(int(e[0]) == 2 for e in entries)
    assert entries[0][2] != entries[1][2]
    assert d.exchange('local-delete ' + '00'*42) == 'denied'
    assert int(d.exchange('local-list 0').split()[0]) == 2
    d.control('local-delete ' + entries[1][2])
    assert int(d.exchange('local-list 0').split()[0]) == 1
    assert d.exchange('local-delete ' + entries[1][2]) == 'denied'
    d.control('reopen')
    assert d.exchange('local-list 0') == 'denied'
    # Whichever RP survived, its credential still signs after local deletion.
    surviving = None
    for rp, credential in (('example.test', key), ('another.test', another_key)):
        try:
            c.get_assertion(rp,h,allow_list=[{'type':'public-key','id':credential.credential_id}],options={'uv':True})
            surviving = rp
        except CtapError as exc:
            assert exc.code == CtapError.ERR.NO_CREDENTIALS
    assert surviving
    assert int(d.exchange('local-list 0').split()[0]) == 1
    d.control('cancel-late')
    with rejected(CtapError.ERR.KEEPALIVE_CANCEL):
        c.get_assertion(surviving, h, options={'uv': True})
    d.control('locked')

    # Full-session reverification preserves the existing authorization.
    d.control('mode 1')
    c.get_assertion(surviving, h, options={'uv': True})
    assert d.exchange('scope') == '1 1 1'
    d.control('time 300000000')
    c.get_assertion(surviving, h, options={'uv': True})
    assert d.exchange('scope') == '1 1 1'

    # A file-only session denies every private command before parsing, prompting,
    # secret checking or opening the FIDO filesystem. Public GetInfo remains usable.
    d.control('mode 3')
    before = d.exchange('effects')
    c.get_info()
    for command in (1, 2, 6, 7, 8, 10, 11, 12, 13, 0xff):
        assert d.call(CTAPHID.CBOR, bytes([command])) == b'\x27'
        assert d.exchange('scope') == '3 1 0'
    d.control('local-readonly-check')
    assert d.exchange('effects') == before
    for timing in ('cancel-early', 'cancel-late'):
        d.control(timing)
        assert d.call(CTAPHID.CBOR, b'\x01') == b'\x2d'
        assert d.exchange('scope') == '3 1 0'
        assert d.exchange('effects') == before

    # A genuine USB invalidation is distinct from HID CANCEL and revokes RO too.
    d.control('invalidate')
    assert d.call(CTAPHID.CBOR, b'\x01') == b'\x2d'
    assert d.exchange('scope') == '0 0 0'
    assert d.exchange('effects') == before
    d.control('clear-invalidation')

    # An invalid authorization value fails closed even with vault keys in RAM.
    d.control('mode 99')
    before = d.exchange('effects')
    assert d.call(CTAPHID.CBOR, b'\x02') == b'\x27'
    assert d.exchange('scope') == '99 1 0'
    assert d.exchange('effects') == before

    # Browser requests cannot overwrite a local read-only unlock in progress.
    d.control('mode 0')
    d.control('local-begin')
    before = d.exchange('effects')
    assert d.call(CTAPHID.CBOR, b'\x01') == b'\x27'
    d.control('local-preserved')
    assert d.exchange('effects') == before
    assert d.exchange('scope') == '0 0 0'
    d.control('local-end')
    # The same guard also handles a local flow beginning after host work was queued.
    d.control('local-race')
    before = d.exchange('effects')
    with rejected(CtapError.ERR.KEEPALIVE_CANCEL):
        c.get_assertion(surviving, h, options={'uv': True})
    d.control('local-preserved')
    assert d.exchange('effects') == before
    d.control('local-end')
    c.get_assertion(surviving, h, options={'uv': True})
    assert d.exchange('scope') == '2 1 1'
    d.control('time 400000000')
    c.get_assertion(surviving, h, options={'uv': True})
    assert d.exchange('scope') == '2 1 1'
    # Completed results remain bound to the old session until USB publishes them.
    d.control('lock-after-worker')
    with rejected(CtapError.ERR.KEEPALIVE_CANCEL):
        c.get_assertion(surviving, h, options={'uv': True})
    assert d.exchange('scope') == '0 0 0'
    c.get_assertion(surviving, h, options={'uv': True})
    d.control('lock-between-fragments')
    request = b'\x02' + cbor.encode({1: surviving, 2: h, 5: {'uv': True}})
    assert d.exchange(request.hex()) == 'fragment-blocked'
    assert d.exchange('scope') == '0 0 0'
    # The disabled attempt policy applies to actual CTAP verification as well:
    # no false "blocked" retry count, no decrement or persistent flash churn.
    d.control('attempt-limit-off')
    sequence = d.exchange('authority-sequence')
    d.control('wrong')
    for _ in range(12):
        with rejected(CtapError.ERR.KEEPALIVE_CANCEL):
            c.get_assertion(surviving, h, options={'uv': True})
    assert list(map(int, d.exchange('counts').split()))[2] == 0
    assert d.exchange('authority-sequence') == sequence
    d.control('correct')
    c.get_assertion(surviving, h, options={'uv': True})
    assert ClientPin(c, PinProtocolV2()).get_uv_retries() == 255
    assert d.exchange('authority-sequence') == sequence
    assert d.exchange('scope') == '2 1 1'

    # Run the actual device timeout runtime beside the firmware FIDO adapter.
    # Public discovery and rejected requests leave the deadline unchanged;
    # a completed, authorized private operation renews it.
    d.control('mode 1')
    d.control('timer 60')
    d.control('advance 30000')
    c.get_info()
    assert d.exchange('remaining') == '30'
    assert ClientPin(c, PinProtocolV2()).get_uv_retries() == 255
    assert d.exchange('remaining') == '30'
    agreement = c.client_pin(PinProtocolV2.VERSION, ClientPin.CMD.GET_KEY_AGREEMENT)
    assert ClientPin.RESULT.KEY_AGREEMENT in agreement
    assert d.exchange('remaining') == '30'
    assert d.call(CTAPHID.CBOR, b'\x01') == b'\x12'
    assert d.exchange('remaining') == '30'
    c.get_assertion(surviving, h, options={'uv': True})
    assert d.exchange('remaining') == '60'
    d.control('advance 60000')
    with rejected(CtapError.ERR.KEEPALIVE_CANCEL):
        c.get_info()
    assert d.exchange('scope') == '0 0 0'
    assert d.exchange('remaining') == '0'
    c.get_info()  # Discovery works after the expired session has been cleared.
    assert d.exchange('scope') == '0 0 0'

    # Expiry while a credential prompt waits, and after the answer is accepted
    # but before verification starts, must not consume a guess or reopen keys.
    for timing in ('timeout-prompt', 'timeout-before-verify'):
        d.control('mode 1')
        d.control('timer 60')
        before = d.exchange('counts')
        locks = int(d.exchange('timeout-locks'))
        d.control(timing)
        with rejected(CtapError.ERR.KEEPALIVE_CANCEL):
            c.get_assertion(surviving, h, options={'uv': True})
        assert d.exchange('counts') == before
        assert int(d.exchange('timeout-locks')) == locks + 1
        assert d.exchange('scope') == '0 0 0'
        assert d.exchange('remaining') == '0'

    # The same deadline also cancels a presence-only modal after cached UV.
    d.control('mode 1')
    c.get_assertion(surviving, h, options={'uv': True})
    d.control('timer 60')
    before = d.exchange('counts')
    d.control('timeout-prompt')
    with rejected(CtapError.ERR.KEEPALIVE_CANCEL):
        c.get_assertion(surviving, h, options={'uv': True})
    assert d.exchange('counts') == before
    assert d.exchange('scope') == '0 0 0'

    # A valid completed result waiting for USB, or unsent response fragments,
    # loses permission when the session deadline passes.
    d.control('mode 1')
    d.control('timer 60')
    d.control('timeout-after-worker')
    with rejected(CtapError.ERR.KEEPALIVE_CANCEL):
        c.get_assertion(surviving, h, options={'uv': True})
    assert d.exchange('scope') == '0 0 0'
    d.control('mode 1')
    d.control('timer 60')
    d.control('timeout-between-fragments')
    assert d.exchange(request.hex()) == 'fragment-blocked'
    assert d.exchange('scope') == '0 0 0'
    assert d.exchange('remaining') == '0'
    print('Firmware HID/store/UI adapter: signatures, persistence, verification, access scopes, file-only denial, local UI preservation, attempts, cancellation and session timeout races passed')
finally:
    d.close()
