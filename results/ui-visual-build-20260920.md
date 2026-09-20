# Credential display refresh — 2026-09-20

Four horizontal number reels show adjacent values and a highlighted active wheel.
Word ranges and review words occupy the corresponding D-pad positions; range
labels use three-letter prefixes as in V1. Compact Back/Done icons replace
repeated direction instructions. Credential values, mappings, storage, and
orientation handling are unchanged. No vault reset is required.

Validation: host device_ui_and_capacity passes normally and with sanitizers;
firmware builds; actual framebuffer previews inspected for wheels, word range
selection, and word review. Hardware validation remains for the next flash.

Artifact: build-pico-device-ui/v2_device_ui_visual.uf2

SHA256: d7f24fcbd7142f941368de5c146f2e83f01b18a6fd638150b719f06001c20530
Highest UF2 payload end: 0x10024800; below journal at 0x101fe000.
