"""WebAuthn server verification with credentials produced by the actual C engine."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/fido_web'))
from server import Harness, json_bytes
from fido2.ctap2 import Ctap2
from fido2.ctap import CtapError
from fido2.utils import websafe_decode, websafe_encode
from fido2.webauthn import CollectedClientData, AttestationObject
from fido_engine_reference_tests import Device


def reject(call):
    try:
        call()
    except (ValueError, KeyError):
        return
    raise AssertionError('Invalid ceremony accepted')


with tempfile.TemporaryDirectory() as temp:
    database = Path(temp) / 'public.sqlite'
    app = Harness(database)
    device = Device(sys.argv[1])
    ctap = Ctap2(device)
    session = 'a'*64
    def begin(kind, name='Alice'):
        return json.loads(json_bytes(app.begin(session, kind, name)))['publicKey']
    def registration(name='Alice', origin=None, rp='localhost', challenge=None):
        p = begin('register', name)
        client = CollectedClientData.create('webauthn.create', challenge or p['challenge'], origin or app.origin)
        result = ctap.make_credential(client.hash, {'id': rp},
            {'id': websafe_decode(p['user']['id']), 'name': name}, [{'type': 'public-key', 'alg': -7}],
            options={'rk': True, 'uv': True})
        identity = websafe_encode(result.auth_data.credential_data.credential_id)
        return {'id': identity, 'rawId': identity, 'type': 'public-key', 'clientExtensionResults': {},
                'response': {'clientDataJSON': websafe_encode(client),
                             'attestationObject': websafe_encode(AttestationObject.create(result.fmt, result.auth_data, result.att_stmt))}}
    def login(kind='login', name='Alice', origin=None, next_account=False):
        p = begin(kind, name)
        client = CollectedClientData.create('webauthn.get', p['challenge'], origin or app.origin)
        allow = p.get('allowCredentials')
        if allow:
            allow = [{'type': c['type'], 'id': websafe_decode(c['id'])} for c in allow]
        assertion = ctap.get_assertion('localhost', client.hash, allow_list=allow, options={'uv': True})
        if next_account:
            assert assertion.number_of_credentials == 2
            assertion = ctap.get_next_assertion()
        identity = websafe_encode(assertion.credential['id'])
        return {'id': identity, 'rawId': identity, 'type': 'public-key', 'clientExtensionResults': {},
                'response': {'clientDataJSON': websafe_encode(client),
                             'authenticatorData': websafe_encode(assertion.auth_data),
                             'signature': websafe_encode(assertion.signature),
                             'userHandle': websafe_encode(assertion.user['id']) if assertion.user else None}}
    try:
        for name in ('Alice', 'Bob'):
            response = registration(name)
            assert app.complete(session, 'register', response)['account'] == name
            reject(lambda: app.complete(session, 'register', response))
        response = login()
        assert app.complete(session, 'login', response) == dict(ok=True, account='Alice', operation='login', uv=True, up=True, counter=0)
        reject(lambda: app.complete(session, 'login', response))
        response = login('discover', '')
        first = app.complete(session, 'discover', response)['account']
        response = login('discover', '', next_account=True)
        second = app.complete(session, 'discover', response)['account']
        assert {first, second} == {'Alice', 'Bob'}
        response = login(origin='http://evil.localhost:8000')
        reject(lambda: app.complete(session, 'login', response))
        response = login();response['response']['signature'] = websafe_encode(b'bad signature')
        reject(lambda: app.complete(session, 'login', response))
        response = login();response['response']['userHandle'] = websafe_encode(b'other user')
        reject(lambda: app.complete(session, 'login', response))
        response = login();app.pending[session]['expires'] = 0
        reject(lambda: app.complete(session, 'login', response))
        response = login()
        reject(lambda: app.complete('b'*64, 'login', response))
        assert app.complete(session, 'login', response)['ok']
        response = registration('Other', rp='wrong.test')
        reject(lambda: app.complete(session, 'register', response))
        response = registration('Other', challenge=b'wrong challenge')
        reject(lambda: app.complete(session, 'register', response))
        response = registration('Other', origin='https://localhost:8000')
        reject(lambda: app.complete(session, 'register', response))
        response = login();auth = bytearray(websafe_decode(response['response']['authenticatorData']));auth[32] &= ~4
        response['response']['authenticatorData'] = websafe_encode(auth)
        reject(lambda: app.complete(session, 'login', response))
        app.db.close();app = Harness(database)
        device.control('reopen')
        response = login('login', 'Bob')
        assert app.complete(session, 'login', response)['account'] == 'Bob'
        assert len(app.accounts()) == 2
        print('WebAuthn real-engine signatures, resident accounts, persistence, origin/RP/challenge/session/UV/handle/replay rejection passed')
    finally:
        device.close();app.db.close()
