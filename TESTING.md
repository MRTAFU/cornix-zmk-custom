# cornix_layer_gate test matrix

This sandbox has no `nix`/`west`/Zephyr SDK, so ZMK's `native_posix`
behavior-test harness (`west test`, `just test <path>`) could not be run
here. The state machine in
`src/behaviors/behavior_cornix_layer_gate.c` is small and self-contained
(see its comments for the ordering guarantees relied on), but it has only
been verified by code review against the pinned ZMK `main` sources, not by
an automated or on-hardware run.

Before trusting this on the actual keyboard, run the following on real
hardware (or add a `tests/cornix_layer_gate/` `native_posix` suite once a
toolchain is available — see `just test` in the Justfile for the existing
convention) and check each item below.

| # | Scenario | Expected |
|---|----------|----------|
| 1 | LOWER short tap | LANG2 exactly once |
| 2 | RAISE short tap | LANG1 exactly once |
| 3 | LOWER held ≥350ms alone, release | nothing |
| 4 | RAISE held ≥350ms alone, release | nothing |
| 5 | LOWER + another key, release LOWER | LOWER-layer key fires; no LANG2 |
| 6 | RAISE + another key, release RAISE | RAISE-layer key fires; no LANG1 |
| 7 | LOWER held ≥350ms, then another key, then release | still LOWER-layer resolution; no LANG2 |
| 8 | Same as #7 for RAISE | RAISE-layer resolution; no LANG1 |
| 9 | LOWER down, then RAISE down | ADJUST (Layer 3) active |
| 10 | RAISE down, then LOWER down | ADJUST active |
| 11 | ADJUST + a third key | Layer 3 binding for that key fires |
| 12 | ADJUST, no third key, release both | no LANG1/LANG2 from either |
| 13 | ADJUST, release LOWER first, RAISE still usable as a layer, then release RAISE | no LANG1 |
| 14 | ADJUST, release RAISE first, LOWER still usable, then release LOWER | no LANG2 |
| 15 | Shift held first, then LOWER down, then Shift up, then LOWER up | Shift's release is not "used"; LOWER's own tap/layer logic unaffected by Shift's prior hold |
| 16 | LOWER down → target down → LOWER up → target up, done fast | target always resolves on the LOWER layer, never Base |
| 17 | Same as #16 for RAISE | target always resolves on the RAISE layer |
| 18 | Any of the above with the "other key" on the opposite physical half (left/right split boundary) | identical behavior — the gate's "other key" detection is a global `position_state_changed` listener, not per-half |
| 19 | ADJUST layer's `&bt BT_SEL 0/1/2` keys | select the intended BLE profile slot; confirm via `cornix_indicator` and by typing on the Mac |
| 20 | Encoder rotation on layers 0-3 | left = scroll wheel, right = volume, direction matching `reference/20260905_cornix.vil`'s `encoder_layout` |
