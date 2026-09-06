# FIDO stack layout

`sections_stack.incl` and `section_end.incl` are adapted from Pico SDK 2.3.0's
`src/rp2_common/pico_standard_link/script_include`. Its BSD licence is retained
in `LICENSE.pico-sdk`.

The FIDO build reserves the top 64 KiB of main SRAM for the core-0 stack and
places the heap limit immediately below it. Link-time assertions check the
boundaries. Core 1 is not used by this integration. Runtime stack guards are
enabled. A hardware stack watermark is still required before release.

Compiler stack-usage output found approximately 26 KiB in the existing display
present path and 6.7 KiB in GetAssertion. Because display rendering is nested
inside the authenticator's approval callback, a 32 KiB stack is insufficient.
The 64 KiB reservation accommodates this known nesting with additional margin;
it is not a measured worst-case runtime stack peak.

The default build's linker layout is unchanged. FIDO builds require the SDK's
linker include override API and fail configuration if it is unavailable.
