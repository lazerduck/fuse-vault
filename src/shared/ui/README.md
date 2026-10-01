# Device UI

The same `fv_ui` library runs on the Pico and in desktop tests/previews. It uses
static scene definitions and state owned by each `fv_ui` instance. No allocation,
inheritance framework, or global event subscriptions are needed.

## Start here

- `device_ui.h`: public UI state and the job/result contract with the application.
- `device_ui.c`: public entry points, FIDO modal gating, job submission, credential
  cleanup, input orientation and rendering of the active scene.
- `scene.h` / `scene.c`: scene interface, registry and transitions.
- `scenes/`: each feature's input behaviour and drawing, kept together.
- `widgets/menu.*`: menu labels/actions, selection, scrolling and confirmation input.
- `drawing.*`: font, pixels, text, headers, footers and choice rows.
- `credential_entry.*`: wheel/word entry logic, independent of scene navigation.
- `button_input.*`: clock-injected debounce and stable-release gating, without GPIO.

## Event flow

```text
pico/platform/buttons.c       GPIO polarity and board pin mapping
    -> button_input.c         debounce; one event per press; prompt release gate
    -> fv_ui_keypress()       modal rules and physical-to-logical orientation
    -> active scene.key()     component interaction, state updates, transitions
    -> fv_ui_render()         active scene.render(), then optional frame rotation
    -> pico/platform/tft_display.c
```

The device adapter polls these layers on core 0. A scene action calls
`ui_submit()` for a worker operation. The adapter copies the job into its queue,
wipes the UI's submitted credentials, and later calls `fv_ui_complete()` with
the response. Progress and storage activity updates can redraw without a key
press. The UI does not call the vault, USB stack or worker directly.

## Scene lifecycle

`ui_scene_show(ui, screen)` selects the scene, resets selection to the first row,
and calls its optional `enter` function. Credential scenes initialise their
entry controls there. Confirmation scenes therefore always start at Cancel.
Passkey scene entry advances the input generation to require a fresh press.

The manager dispatches keys only to the active scene, so transitions do not need
to subscribe/unsubscribe button delegates. Rendering never calls `enter`, changes
navigation, or submits operations. A handler requests a transition and returns;
the public entry point renders the resulting state once.

Credential state spans multiple scenes. It is deliberately owned by the UI
workflow, not cleared on every scene transition: setup needs the first credential
while the confirmation scene is active. `ui_clear_input()` wipes all credential
buffers on workflow cancellation/completion. `flow` names the mutually exclusive
setup, unlock, policy and credential-change workflows. FIDO modal approval has a
separate handshake with the adapter; Back always rejects that request.

## Menus and actions

A menu item contains its label and a typed callback accepting the current
`fv_ui *`. The pointer is the callback's context, equivalent to the instance
captured by a C# delegate. Static item tables contain no mutable state.

`scenes/settings.c` is the simplest example. Both rendering and key handling use
`settings_menu()`, so the visible item count and available actions stay together.
The menu widget owns wraparound selection and scrolling. The scene handles Back
and its actions decide whether to navigate or submit a job.

To add an item, add its label/action pair and update the corresponding item ID/count
in `scenes/settings.h`. Feature visibility is decided in `settings_menu()`.

To add a scene:

1. Add a screen ID before `UI_SCREEN_COUNT` in `device_ui.h`. Existing IDs are used
   by debug tooling; preserve their values.
2. Implement `key` and `render`, with `enter` when state needs initialisation.
   Expose one `const ui_scene` descriptor in `scene.h`.
3. Register the descriptor in `scene.c` and its source in `CMakeLists.txt`.
4. Navigate with `ui_scene_show()`; do not assign `screen` directly in application
   or scene code. Synthetic preview fixtures may assign it to inspect a frame.
5. Add navigation tests and a preview fixture when the screen changes visually.

Renderers use the logical 160 x 80 RGB332 colour framebuffer. Orientation is handled
centrally, so scenes do not need separate flipped layouts. Menus and scenes share
one selection cursor because only one scene receives input at a time.

## Checks

From the repository root after building desktop tests:

```sh
ctest --test-dir build-host --output-on-failure -R 'device_ui|ui_components|fido_firmware_adapter'
python3 src/host/tools/ui_preview.py --output /tmp/fuse-vault-ui-preview
```

The component tests exercise bounce, held-button release gating, chord rejection,
per-instance menu callbacks and scene entry. The existing UI integration tests
cover setup/confirmation, cancellation and wiping, credential changes, orientation,
FIDO approval, passkey navigation/deletion and progress rendering.

## Colour and memory

Pixels and text accept an 8-bit RGB332 colour; use `UI_COLOUR_*` semantic
colours for common controls. The 160 x 80 framebuffer occupies 12,800 bytes.
The TFT keeps another 12,800-byte sent-frame cache and converts one row at a
time to RGB565, retaining the same SPI transfer size and polling behaviour.
Together these use 22,400 bytes (21.875 KiB) more than the old packed buffers.
Debug-screen builds reserve another 2 KiB for the hex-encoded screen response.
Rotation reverses pixel order without changing colour values. Offline previews
and viewer snapshots use PPM RGB images.

## Startup animation

`ui_draw_splash(ui, frame)` defines ten 50 ms frames: blue circuit connections
carry amber pulses into a fusion core, which settles into a vault dial. The
wordmark remains stationary. Run `python3 src/host/tools/generate_splash.py`
after changing the artwork to regenerate `startup_frames.inc`; add
`--preview /tmp/fuse-startup.gif` for a looping, enlarged preview.

The device uses the generated 4-bit palette indices directly from flash
(64,000 bytes plus the palette), decoding only one 160-byte row on the stack.
No additional RAM framebuffer is reserved. The initial frame is flushed before
the backlight lights, then the animation runs for 500 ms while USB and worker
polling continue. Frame selection changes only at scan boundaries. The next
scan after the deadline displays the latest live UI; physical and debug key
presses are discarded during startup. The offline gallery includes the settled
emblem as `00-startup.png`. This is a startup-only animation, not a lock-state
indicator.

## Transfer dashboards

Successful unlocked status/operation responses land on `UI_DASHBOARD`. Left or
Right flips screen orientation. Settings > Default Dashboard selects scrolling
history or live bars, with explicit Save/Cancel; the choice survives lock/unlock
until reboot. Select opens Settings directly; Back there returns
to the dashboard. Back on the dashboard submits the usual lock operation.

Core 0 supplies actual host read/write byte counters every 500 ms through
`fv_ui_activity()`. Upload is host-to-vault writes; download is vault-to-host reads.
A 60-sample ring retains approximately 30 seconds and continues sampling in menus.
Both views share an automatic scale based on the largest retained rate, with a
minimum of 1 KiB/s; the numbers are current rates, not percentages. Idle samples
are zero. Locking clears transfer history and rates. The ring consumes 480 bytes
per UI instance and requires no allocation.
