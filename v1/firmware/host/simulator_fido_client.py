"""Simulated relying party using python-fido2 against the GUI's real CTAP engine.

Only public registration records live in simulated-fido-host.json. Signing keys
remain in the authenticator's encrypted private reservation. No network is used.
The line protocol is private parent/child IPC, not an exported authenticator API.
"""
import base64
import hashlib
import json
import os
from pathlib import Path
import sys
from fido2.ctap import CtapDevice, CtapError
from fido2.ctap2 import Ctap2
from fido2.hid import CTAPHID, CAPABILITY
from fido2.webauthn import AttestedCredentialData


class Device(CtapDevice):
    @property
    def capabilities(self):
        return CAPABILITY.CBOR

    def call(self, cmd, data=b"", event=None, on_keepalive=None):
        if cmd != CTAPHID.CBOR:
            raise ValueError("Only CTAP CBOR is supported by the simulator")
        print("CTAP " + bytes(data).hex(), flush=True)
        response = sys.stdin.readline()
        if not response:
            raise RuntimeError("Simulator disconnected")
        return bytes.fromhex(response.strip())

    def close(self):
        pass

    @classmethod
    def list_devices(cls):
        return []


def save(path, records):
    temporary = path.with_suffix(".tmp")
    with temporary.open("w", encoding="utf-8") as stream:
        json.dump(records, stream)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def run(directory, operation, rp, account):
    if not rp or len(rp.encode()) > 253 or not account or len(account.encode()) > 64:
        raise ValueError("Enter a site (up to 253 bytes) and account (up to 64 bytes)")
    path = Path(directory) / "simulated-fido-host.json"
    records = json.loads(path.read_text()) if path.exists() else {}
    record_id = json.dumps([rp, account])
    ctap = Ctap2(Device())
    challenge = os.urandom(32)
    if operation == "register":
        result = ctap.make_credential(
            challenge, {"id": rp, "name": rp},
            {"id": hashlib.sha256(account.encode()).digest(), "name": account},
            [{"type": "public-key", "alg": -7}], options={"rk": True, "uv": True})
        auth = result.auth_data
        if auth.rp_id_hash != hashlib.sha256(rp.encode()).digest():
            raise ValueError("Registration site mismatch")
        if not auth.is_user_present() or not auth.is_user_verified():
            raise ValueError("Registration lacks presence or verification")
        credential = auth.credential_data
        credential.public_key.verify(bytes(auth) + challenge, result.att_stmt["sig"])
        records[record_id] = {
            "credential": base64.b64encode(bytes(credential)).decode(),
            "counter": auth.counter,
        }
        save(path, records)
        return "Passkey registered; signature verified. Lock/unlock, then Authenticate."
    if operation != "authenticate":
        raise ValueError("Unknown operation")
    if record_id not in records:
        raise ValueError("No simulated-site registration for this account. Register first.")
    record = records[record_id]
    credential = AttestedCredentialData(base64.b64decode(record["credential"]))
    result = ctap.get_assertion(rp, challenge,
        allow_list=[{"type": "public-key", "id": credential.credential_id}],
        options={"uv": True})
    if result.credential["id"] != credential.credential_id:
        raise ValueError("Credential mismatch")
    if result.auth_data.rp_id_hash != hashlib.sha256(rp.encode()).digest():
        raise ValueError("Authentication site mismatch")
    if not result.auth_data.is_user_present() or not result.auth_data.is_user_verified():
        raise ValueError("Authentication lacks presence or verification")
    credential.public_key.verify(bytes(result.auth_data) + challenge, result.signature)
    if (record["counter"] or result.auth_data.counter) and result.auth_data.counter <= record["counter"]:
        raise ValueError("Authenticator counter did not advance")
    record["counter"] = result.auth_data.counter
    save(path, records)
    return "Authenticated; signature verified against the simulated site's public key."


if __name__ == "__main__":
    try:
        message = run(*sys.argv[1:])
    except CtapError as error:
        if error.code in (CtapError.ERR.UV_BLOCKED, CtapError.ERR.PIN_AUTH_INVALID):
            message = "Unlock again before using this site (verification expired or site changed)."
        else:
            message = str(error)
        print("ERROR " + message.replace("\n", " "), flush=True)
        sys.exit(1)
    except Exception as error:
        print("ERROR " + str(error).replace("\n", " "), flush=True)
        sys.exit(1)
    print("DONE " + message, flush=True)
