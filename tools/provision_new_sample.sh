#!/usr/bin/env bash
# Run only after the provisioning build passes an unprovisioned cold-start check.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
fv_sample=66ED2A91873CF67F
fv_port="/dev/serial/by-id/usb-Fuse_Vault_Fuse_Vault_SECURITY_DEBUG_${fv_sample}-if00"
python3 tools/worker_check.py --device "$fv_sample" --expect-unprovisioned
fv_logdir=$(mktemp -d "$PWD/results/provision-new-sample-XXXXXXXX")
printf 'Saving results to %s\n' "$fv_logdir"
python3 tools/security_probe.py --port "$fv_port" snapshot --out "$fv_logdir/otp-before.json" > "$fv_logdir/before-output.json"
python3 tools/security_probe.py --port "$fv_port" provision --confirm-device "$fv_sample" | tee "$fv_logdir/provision.json"
python3 tools/security_probe.py --port "$fv_port" state | tee "$fv_logdir/state-after.json"
python3 tools/security_probe.py --port "$fv_port" snapshot --out "$fv_logdir/otp-after.json" > "$fv_logdir/after-output.json"
python3 tools/security_probe.py diff "$fv_logdir/otp-before.json" "$fv_logdir/otp-after.json" | tee "$fv_logdir/otp-diff.json"
printf '\nProvision command succeeded. Unplug for five seconds, reconnect without BOOTSEL, then run:\n'
printf 'python3 %s/tools/worker_check.py --device %s --expect-empty\n' "$PWD" "$fv_sample"
