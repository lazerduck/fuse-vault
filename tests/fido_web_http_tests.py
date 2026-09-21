"""Exercise the actual HTTP handler without opening network sockets."""
import io
import json
from pathlib import Path
import sys
from types import SimpleNamespace
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/fido_web'))
from server import Handler, Harness

class Socket:
    def __init__(self, request):
        self.input = io.BytesIO(request)
        self.output = bytearray()
    def makefile(self, *args):
        return self.input
    def sendall(self, data):
        self.output.extend(data)
    def settimeout(self, timeout):
        pass

app = Harness(':memory:')
server = SimpleNamespace(app=app)
def request(method, path, value=None, host='localhost:8000', origin='http://localhost:8000', cookie='', content_type='application/json'):
    body = json.dumps(value).encode() if value is not None else b''
    raw = f'{method} {path} HTTP/1.1\r\nHost: {host}\r\nOrigin: {origin}\r\nCookie: {cookie}\r\nContent-Type: {content_type}\r\nContent-Length: {len(body)}\r\n\r\n'.encode()+body
    sock = Socket(raw)
    Handler(sock, ('127.0.0.1', 1234), server)
    headers, payload = bytes(sock.output).split(b'\r\n\r\n', 1)
    code = int(headers.split()[1])
    return code, headers.decode(), payload

try:
    code, headers, body = request('GET', '/')
    assert code == 200 and b'passkey' in body and 'frame-ancestors' in headers
    cookie = next(line.split(': ',1)[1].split(';')[0] for line in headers.split('\r\n') if line.startswith('Set-Cookie:'))
    assert 'HttpOnly' in headers and 'SameSite=Strict' in headers
    assert request('GET', '/', host='evil.test:8000')[0] == 403
    assert request('POST', '/api/register/begin', {'name':'Alice'}, origin='http://evil.test')[0] == 403
    assert request('POST', '/api/register/begin', {'name':'Alice'}, origin='null')[0] == 403
    assert request('POST', '/api/register/begin', {'name':'Alice'}, content_type='text/plain')[0] == 400
    assert request('GET', '/server.py')[0] == 404
    assert request('GET', '/api/accounts', cookie=cookie)[0] == 200
    assert request('POST', '/api/register/begin', {'name':'Alice'}, cookie=cookie)[0] == 200
    assert len(app.pending) == 1
    assert request('POST', '/api/register/complete', {}, cookie=cookie)[0] == 400
    assert not app.pending
    code, _, body = request('POST', '/api/register/complete', {}, cookie=cookie)
    assert code == 400 and b'already used' in body
    print('HTTP Host/Origin, cookie, content type, static routing and one-use challenge checks passed')
finally:
    app.db.close()
