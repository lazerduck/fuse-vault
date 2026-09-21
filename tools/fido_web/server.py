#!/usr/bin/env python3
"""Loopback-only WebAuthn development harness; stores public credentials only."""
import argparse
import json
import secrets
import sqlite3
import time
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

from fido2 import features
from fido2.server import Fido2Server
from fido2.utils import websafe_encode, websafe_decode
from fido2.webauthn import (AttestedCredentialData, RegistrationResponse,
                            AuthenticationResponse, PublicKeyCredentialUserEntity)

if hasattr(features, 'webauthn_json_mapping'):
    features.webauthn_json_mapping.enabled = True


def json_bytes(value):
    return json.dumps(value, default=lambda v: websafe_encode(v) if isinstance(v, bytes) else dict(v)).encode()


class Harness:
    def __init__(self, database, port=8000):
        self.host = f'localhost:{port}'
        self.origin = f'http://{self.host}'
        self.server = Fido2Server({'id': 'localhost', 'name': 'Fuse Vault local test'},
                                 verify_origin=lambda origin: origin == self.origin)
        self.server.allowed_algorithms = [a for a in self.server.allowed_algorithms if a.alg == -7]
        self.db = sqlite3.connect(database)
        self.db.executescript('''
            CREATE TABLE IF NOT EXISTS users(name TEXT PRIMARY KEY, handle BLOB UNIQUE NOT NULL);
            CREATE TABLE IF NOT EXISTS credentials(id BLOB PRIMARY KEY, name TEXT NOT NULL,
                data BLOB NOT NULL, created INTEGER NOT NULL);
        ''')
        self.pending = {}

    def rows(self, name=None):
        if name is None:
            return self.db.execute('SELECT id,name,data FROM credentials ORDER BY created,id').fetchall()
        return self.db.execute('SELECT id,name,data FROM credentials WHERE name=?', (name,)).fetchall()

    def accounts(self):
        return [{'name': n, 'credentials': c} for n, c in self.db.execute(
            'SELECT name,count(*) FROM credentials GROUP BY name ORDER BY name')]

    def begin(self, session, kind, name):
        if kind not in ('register', 'login', 'discover'):
            raise ValueError('Unknown operation')
        name = name.strip()
        if kind != 'discover' and (not name or len(name) > 64 or any(ord(c) < 32 for c in name)):
            raise ValueError('Enter an account name (1–64 characters)')
        now = time.monotonic()
        self.pending = {k: v for k, v in self.pending.items() if v['expires'] > now}
        if len(self.pending) >= 256 and session not in self.pending:
            raise ValueError('Too many pending requests; retry shortly')
        if kind == 'register':
            with self.db:
                self.db.execute('INSERT OR IGNORE INTO users VALUES (?,?)', (name, secrets.token_bytes(32)))
            handle = self.db.execute('SELECT handle FROM users WHERE name=?', (name,)).fetchone()[0]
            options, state = self.server.register_begin(
                PublicKeyCredentialUserEntity(id=handle, name=name, display_name=name),
                credentials=[AttestedCredentialData(r[2]) for r in self.rows(name)],
                resident_key_requirement='required', user_verification='required',
                authenticator_attachment='cross-platform')
        else:
            rows = self.rows(None if kind == 'discover' else name)
            if not rows:
                raise ValueError('Register an account first')
            options, state = self.server.authenticate_begin(
                credentials=None if kind == 'discover' else [AttestedCredentialData(r[2]) for r in rows],
                user_verification='required')
        self.pending[session] = dict(kind=kind, name=name, state=state, expires=now+120)
        return dict(options)

    def complete(self, session, kind, payload):
        # Consume before validation: failures and successes both prevent replay.
        pending = self.pending.pop(session, None)
        if not pending or pending['kind'] != kind or pending['expires'] <= time.monotonic():
            raise ValueError('Challenge expired, already used, or belongs to another operation')
        if kind == 'register':
            response = RegistrationResponse.from_dict(payload)
        else:
            response = AuthenticationResponse.from_dict(payload)
        if response.response.client_data.cross_origin:
            raise ValueError('Cross-origin ceremonies are not supported')
        credential_id = websafe_decode(payload['rawId'])
        if response.type != 'public-key' or payload['id'] != websafe_encode(credential_id):
            raise ValueError('Invalid credential identity')
        name = pending['name']
        if kind == 'register':
            data = self.server.register_complete(pending['state'], response)
            credential = data.credential_data
            if credential_id != credential.credential_id or credential.public_key.get(3) != -7:
                raise ValueError('Unexpected credential or algorithm')
            # Browsers normally strip attestation for the default "none" request.
            # If packed self-attestation is returned, verify its proof too.
            att = response.response.attestation_object
            if att.fmt == 'packed':
                if 'x5c' in att.att_stmt or att.att_stmt.get('alg') != -7:
                    raise ValueError('Expected self-attestation for this test')
                credential.public_key.verify(bytes(data)+response.response.client_data.hash, att.att_stmt['sig'])
            elif att.fmt != 'none':
                raise ValueError('Unsupported attestation format for this test')
            with self.db:
                self.db.execute('INSERT INTO credentials VALUES (?,?,?,?)',
                                (credential.credential_id, name, bytes(credential), int(time.time())))
        else:
            rows = self.rows(None if kind == 'discover' else name)
            credential = self.server.authenticate_complete(pending['state'],
                [AttestedCredentialData(r[2]) for r in rows], response)
            name = next(r[1] for r in rows if r[0] == credential.credential_id)
            handle = self.db.execute('SELECT handle FROM users WHERE name=?', (name,)).fetchone()[0]
            actual_handle = response.response.user_handle
            if (kind == 'discover' and actual_handle is None) or (actual_handle is not None and actual_handle != handle):
                raise ValueError('User handle does not match the registered account')
            data = response.response.authenticator_data
        return {'ok': True, 'account': name, 'operation': kind,
                'uv': data.is_user_verified(), 'up': data.is_user_present(), 'counter': data.counter}


class Handler(BaseHTTPRequestHandler):
    def respond(self, code, data, content_type='application/json'):
        if not isinstance(data, bytes):
            data = json_bytes(data)
        self.send_response(code)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Content-Security-Policy', "default-src 'self'; frame-ancestors 'none'; object-src 'none'; base-uri 'none'")
        if getattr(self, 'new_session', False):
            self.send_header('Set-Cookie', f'fv_test={self.session}; HttpOnly; SameSite=Strict; Path=/')
        self.end_headers()
        self.wfile.write(data)

    def gate(self, post=False):
        app = self.server.app
        if self.headers.get('Host') != app.host or (post and self.headers.get('Origin') != app.origin):
            self.respond(403, {'error': 'Use the exact localhost URL printed by the server'})
            return False
        cookie = SimpleCookie()
        try:
            cookie.load(self.headers.get('Cookie', ''))
            token = cookie['fv_test'].value if 'fv_test' in cookie else ''
        except Exception:
            token = ''
        self.new_session = len(token) != 64 or any(c not in '0123456789abcdef' for c in token)
        self.session = secrets.token_hex(32) if self.new_session else token
        return True

    def do_GET(self):
        if not self.gate():
            return
        if self.path == '/api/accounts':
            self.respond(200, {'accounts': self.server.app.accounts()})
        elif self.path in ('/', '/app.js', '/style.css'):
            filename, mime = {'/': ('index.html', 'text/html; charset=utf-8'),
                              '/app.js': ('app.js', 'text/javascript'),
                              '/style.css': ('style.css', 'text/css')}[self.path]
            self.respond(200, Path(__file__).with_name(filename).read_bytes(), mime)
        else:
            self.respond(404, {'error': 'Not found'})

    def do_POST(self):
        if not self.gate(True):
            return
        try:
            if self.headers.get('Content-Type') != 'application/json' or self.headers.get('Transfer-Encoding'):
                raise ValueError('Expected bounded JSON request')
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 32768:
                raise ValueError('Request size out of range')
            data = json.loads(self.rfile.read(length))
            parts = self.path.split('/')
            if len(parts) != 4 or parts[1] != 'api' or parts[2] not in ('register', 'login', 'discover'):
                raise ValueError('Unknown operation')
            kind, action = parts[2:]
            if action == 'begin':
                result = self.server.app.begin(self.session, kind, data.get('name', ''))
            elif action == 'complete':
                result = self.server.app.complete(self.session, kind, data)
            else:
                raise ValueError('Unknown operation')
            self.respond(200, result)
        except Exception as exc:
            # No secrets are accepted here; keep internal errors out of the browser.
            message = str(exc) if isinstance(exc, ValueError) else 'Verification failed'
            self.respond(400, {'error': message})

    def setup(self):
        super().setup()
        self.connection.settimeout(5)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', type=int, default=8000)
    p.add_argument('--database', type=Path, default=Path('fido-web-test.sqlite3'))
    args = p.parse_args()
    if not 1024 <= args.port <= 65535:
        p.error('Use a port from 1024 to 65535')
    app = Harness(args.database, args.port)
    http = HTTPServer(('127.0.0.1', args.port), Handler)
    http.app = app
    print(f'Open {app.origin} — public credentials stored in {args.database.resolve()}', flush=True)
    try:
        http.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        http.server_close()
        app.db.close()


if __name__ == '__main__':
    main()
