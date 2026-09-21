# F4: browser passkeys and on-device management

F4 supplies a localhost WebAuthn harness and **Settings → Passkeys** on the device.
Software and automated verification are implemented. Actual Chromium/Firefox
ceremonies against the board and physical list/delete acceptance are still pending.
Your existing passkeys and FIDO verification preference survive this firmware
update; do not initialize FIDO again.

## Start the browser harness

Install/use the same `python-fido2` environment used by the smoke client. Run:

```sh
python3 /home/adam/projects/fuse-vault/tools/fido_web/server.py
```

Open **http://localhost:8000** in Chromium or Firefox. Use that exact hostname,
not `127.0.0.1`: the relying-party ID is `localhost` and the expected origin is
`http://localhost:8000`. The server binds only `127.0.0.1`. `--port 8001` changes
both its listener and expected origin; use the URL it prints.

The default database, `fido-web-test.sqlite3`, is created in the current working
directory. Keep running from that directory, or supply an absolute path:

```sh
python3 /home/adam/projects/fuse-vault/tools/fido_web/server.py \
  --database /home/adam/fido-web-test.sqlite3
```

Ctrl-C stops the server. Its database stores account names, random public user
handles, credential IDs and public keys, never the device credential or private
key. It is a development test service, not an account service for production use.
It does not import the CLI smoke tool's separate `.test` RP credentials.

## Board/browser test sequence

1. Flash the latest `build-pico-fido/fuse_vault_security.uf2` using the usual firmware
   update procedure. Open the debug viewer and unlock. Preserve the existing SD
   and enrollment; no erase, OTP provisioning, or FIDO initialization is needed.
2. Enter **Alice** in the browser and select **Register passkey**. If asked, choose
   **security key / USB security key**, rather than a phone or another authenticator.
   Verify `REGISTER localhost` on the device and approve. The server must report
   verified registration, UV=true and UP=true.
3. Select **Sign in to account**. Approve `SIGN IN localhost`; the server verifies
   the assertion signature against Alice's stored public key.
4. Register **Bob**, then select **Choose an account on sign-in**. This sends no
   allow-list. The browser should offer the resident accounts; check that selecting
   each one produces the matching verified account. Account selection is in the
   browser; the device screen approves the RP operation.
5. Safely eject/reconnect the device and restart the server using the same database.
   Both accounts must still log in. Exercise timed and unlocked-session verification
   modes, following the device prompts.
6. With the filesystem unmounted, use **Settings → Passkeys** to delete Alice as
   described below. Alice login must fail afterward, including after reconnect;
   Bob must continue working. Keep Alice's public server record to test this failure.
7. Reject a request with Back and test timeout/cancellation. Current firmware locks
   the shared session on host cancellation or a submitted wrong device credential,
   so finish writes and unmount before those checks. Do not deliberately unplug
   during writes outside the planned fault acceptance procedure.

Registration requests ES256, resident credentials, UV required and a roaming
(cross-platform) authenticator. It requests the default no-attestation preference:
there is no certified vendor allow-list. The server supports stripped `none`
attestation and verifies packed self-attestation if supplied. Login verifies the
signature, origin, challenge, RP hash, UV/UP and account/user-handle binding.
Zero counters are accepted intentionally. This harness cannot establish that an
arbitrary device is a genuine Fuse Vault; watch which device the browser uses.

## On-device passkeys

In the unlocked device's settings choose **Passkeys**. The debug viewer retains
its unmount guard for settings, so unmount before opening this management view.

- Up/Down selects the resident credential.
- Left/Right scrolls long RP/account labels.
- Select opens a deletion confirmation, initially **Cancel**. Down then Select
  confirms deletion; Back exits. A fresh input generation prevents queued viewer
  input or a held prior button press from carrying into confirmation.
- Empty lists show **No stored passkeys**.

The worker checks the vault is unlocked and deletes by the selected credential's
stable resident ID, never by the current display index. A stale/deleted ID fails
rather than deleting a different account. Successful deletion commits the encrypted
snapshot before reporting success. It deletes only this device's resident copy,
not a website account, another authenticator, the USB filesystem or verification
policy. Nonresident credentials cannot be enumerated here. Accepted old-SD-image
replay can still undo deletion.

## Local server boundaries

The HTTP handler checks exact Host and POST Origin, accepts bounded JSON only,
and uses an HttpOnly/SameSite=Strict cookie to bind each ceremony to its challenge.
Challenges expire after 120 seconds and are consumed on every completion attempt,
including failed verification. Starting a new ceremony replaces the previous one
in the same browser session; finish one test at a time. There is no permissive
CORS, arbitrary file serving, remote listener, or device-secret submission endpoint.

## Evidence and remaining acceptance

See `results/fido-f4-build-20260921.md` for automated evidence. Tests send actual
engine-produced credentials/assertions through the WebAuthn verifier and exercise
both resident accounts, encrypted reopen, rejected origin/RP/challenge/replay,
UV/user-handle/signature failures and the HTTP handler. They do not substitute a
virtual browser authenticator for the real board. The in-app browser was used to
inspect layout and the no-account error path, not to claim a USB ceremony passed.

Record Chromium and Firefox registration, login, multiple-account selection,
rejection and deletion/reconnect results in the delivery ledger. Broader platform
compatibility and production hardening remain F5.
