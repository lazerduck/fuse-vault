# Firmware architecture

## Input pipeline

Physical input and screen behaviour are deliberately independent:

```text
RP2354 GPIO or GTK key press/release
                  |
                  v
         six-bit pressed mask
                  |
                  v
      shared input controller
  debounce, edges, hold and repeat
                  |
                  v
       active input binding map
                  |
                  v
     application logical event
                  |
                  v
  state transition and platform command
```

`input.c` owns the hardware-independent controller. It accepts a complete raw
pressed mask and monotonic millisecond timestamp. A stable press emits once;
bindings marked repeatable emit again after the configured delay and interval.
Missed repeat intervals coalesce rather than producing a burst. A binding that
changes while its button is held remains blocked until release, preventing a
press from carrying through to a newly displayed screen.

`input_bindings.c` registers the controls accepted by each application state.
Blocking states such as provisioning and authentication register no physical
events. Selection screens register only their meaningful controls. Secret-entry
screens enable all six controls, but direction-sequence arrows do not repeat
because a held direction must never add hidden duplicate symbols.

`rp2354_input.c` is the production raw-input adapter. It configures the six
active-low navigation GPIOs and returns the pressed mask. The GTK adapter tracks
key press and release events and feeds the same controller, so keyboard tests
exercise the debounce, hold, repeat, and state-binding behaviour used on the
device.

New screens can construct a map with `fv_input_map_clear()` and register a
control using `fv_input_map_bind()`. Input sources must not contain screen or
vault logic.

## Application and secret entry

The application receives only logical events. It owns navigation state and
returns commands for asynchronous platform work; it does not read GPIO or GTK.
The platform acknowledges operations using separate completion or failure
events.

Secret-entry methods are separate stateful objects in `secret_input.c`. Each
method handles the logical controls, renders its own content, and creates a
fixed-capacity canonical encoding. This allows wheels, direction sequences,
the keypad, and word selection to share setup and unlock screens without
changing the input adapters.

The UI-facing `fv_entry_method_t` is deliberately zero-based for selection and
is not a storage format. `fv_secret_method_t` is the canonical and persisted
identifier: the existing v1 values 1 through 4 represent wheels, directions,
keypad, and word list respectively. All boundaries use the validated conversion
in `entry_method.h`; vault headers continue to encode these IDs as little-endian
32-bit integers, so existing headers remain compatible. Unknown IDs, including
zero, are rejected.

The provisioning coordinator consumes the confirmed setup encoding and keeps
envelope inputs and the generated VMK in a caller-owned workspace that is
securely cleared before every return. It reuses active roots or
creates them automatically during first setup when the OTP layout is empty, then stores
and verifies the SD vault header before publishing the first provisioned journal
record. Recoverable media failures never burn the independent OTP revocation
marker. Media inspection and destructive confirmation occur before password
entry. The host and RP2354 builds execute this coordinator through the same
device runtime and persistence-service contract.

At restart, `boot_recovery.c` is the single production-facing boundary for
recovering the entry method. The platform service first establishes record
integrity, then the boundary validates the full vault-header structure and
converts the stable identifier back to `fv_entry_method_t`. The persistent host
simulator supplies its redundant file-backed implementation. On RP2354,
journal recovery binds the expected vault ID before the authenticated SD header
is accepted; missing or inconsistent records fail locked instead of falling
back to wheels. This boundary does not authenticate the entered credential or
unwrap the VMK.

Authentication has a separate portable coordinator using the same service
boundary. It accepts work only in `VAULT_AUTHENTICATING`, a state reachable
only after the attempt reservation is durable. Its caller-owned workspace is
always cleared. A successful unwrap transfers the VMK into a session object,
which owns it until an erase-session-keys command clears it on lock, eject, or
fault. The display simulator and RP2354 entry point execute this complete
command chain. The target uses the SD-backed service and encrypted adapter,
while USB attachment remains fail-closed until the remaining connector-presence
polarity evidence gate is confirmed.

Screen transition and textual rendering logic currently remain together in
`app.c`. They are deterministic and host-tested, but should move into a static
screen-handler table as the number of screens grows. No dynamic callback
allocation is required.
