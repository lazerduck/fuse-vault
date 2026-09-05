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

Screen transition and textual rendering logic currently remain together in
`app.c`. They are deterministic and host-tested, but should move into a static
screen-handler table as the number of screens grows. No dynamic callback
allocation is required.
