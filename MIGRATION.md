# RMK v1.12 → ZMK Migration (Cornix, this fork)

Physical hardware is unchanged: Right Cornix (peripheral, BLE) → Left Cornix
(central, ZMK split) → USB → macOS. No dongle.

Source of truth for the migrated keymap: `reference/20260905_cornix.vil`
(the RMK v1.12 / Vial export actually running on the keyboard before this
migration). See `config/cornix.keymap` header comment for what changed and
why (LOWER/RAISE → `cornix_layer_gate`, ADJUST → `conditional_layers`,
USER00-02 → `&bt BT_SEL 0/1/2`).

## SoftDevice: not required for this migration

Both `cornix_left` and `cornix_right` build with the `nrf52840-nosd`
snippet (see `build.yaml`) — this firmware runs **without** a SoftDevice.
If the keyboard is already running RMK v1.12 successfully, its SoftDevice
state is irrelevant to this migration; do **not** run the SoftDevice
restore procedure in `bootloader/README.md` unless a flash goes wrong in a
way that specifically points at a missing/corrupt SoftDevice (e.g. the
board won't boot into a UF2 bootloader drive at all). That doc predates
the current no-SD partition layout and is legacy-only.

## Pre-flight backups (confirm before doing anything else)

1. `reference/20260905_cornix.vil` is present and committed (this repo).
2. RMK v1.12 Left and Right `.uf2` backups exist (this repo keeps stock
   RMK backups under `rmkfw/`, but confirm you also have the specific
   v1.12 Left/Right build you're running today — `rmkfw/` here only has
   V1.6.x archives, so locate/export the actual v1.12 UF2s you're
   currently running before flashing anything).

## Forward migration: RMK v1.12 → ZMK

1. Confirm the backups above.
2. Flash `cornix_reset` (settings reset) to **both** halves, one at a
   time, using a data-capable USB-C cable. This clears any stale ZMK
   bond/settings state so the new split pairs cleanly. Wait for each
   half to finish and reboot before moving to the next step.
3. Flash the Left ZMK firmware (`cornix_left_default_nosd.uf2`) to the
   **Left** half.
4. Flash the Right ZMK firmware (`cornix_right_nosd.uf2`) to the
   **Right** half.
5. Power-cycle (or let both halves reboot naturally after flashing).
6. Check `cornix_indicator` on both halves for split-connection status
   before doing anything else.
7. Connect Left to the Mac over USB (same cable as flashing, data-capable).
8. Verify LOWER / RAISE / ADJUST:
   - Tap LOWER alone (short) → 英数 (LANG2) fires once.
   - Tap RAISE alone (short) → かな (LANG1) fires once.
   - Hold LOWER ≥350ms alone and release → nothing fires.
   - Hold LOWER + press another key → that key resolves on the LOWER
     layer, no LANG2 on LOWER's release.
   - Hold both LOWER and RAISE together → ADJUST (Layer 3); releasing
     either one first does not fire LANG1/LANG2.
9. Verify Bluetooth profile selection: on ADJUST, the three left-column
   keys (`&bt BT_SEL 0/1/2`) switch the active BLE profile; confirm via
   `cornix_indicator`'s connection/profile display and by checking the
   Mac actually receives input after each switch.
10. Verify encoders: left encoder = scroll wheel (down/up by rotation
    direction), right encoder = volume down/up, on every layer 0-3.

## ZMK Studio note

`CONFIG_ZMK_STUDIO=y` is only enabled on `cornix_left` (central), via the
`studio-rpc-usb-uart` snippet in `build.yaml` — unchanged by this
migration. If ZMK Studio has previously saved a keymap to the central's
NVS, that persisted keymap takes priority over the firmware-default
`config/cornix.keymap` at runtime (Studio bindings override; it does not
touch `sensor-bindings`, `conditional_layers`, or behaviors — see the
keymap file's header comment). If LOWER/RAISE/ADJUST don't match what's
described above after flashing, a prior Studio-saved keymap is the most
likely cause: use Studio's "Restore Stock Settings" / a `cornix_reset`
flash to clear it back to this firmware default before re-testing.

## Rollback: ZMK → RMK v1.12

You need: your actual RMK v1.12 Left/Right `.uf2` backups (not the
`rmkfw/` v1.6.x archives) and `reference/20260905_cornix.vil` as the spec
to restore against if you re-flash and re-pair via Vial.

1. Flash `cornix_reset` (settings reset) to both halves first, to clear
   ZMK's BLE bonds/NVS state before switching firmware families.
2. Flash the RMK v1.12 Left `.uf2` to Left, RMK v1.12 Right `.uf2` to
   Right.
3. Re-pair Bluetooth from the OS side if needed — ZMK and RMK do not
   share bond state, so old ZMK-side pairings on the Mac won't work with
   RMK's BLE stack and should be removed/re-added in macOS Bluetooth
   settings.
4. Re-open Vial and confirm the layout matches
   `reference/20260905_cornix.vil` (it should, since RMK's on-flash
   layout for v1.12 is what produced that export).
5. No SoftDevice restore step is required going backward either, for the
   same reason as the forward direction — only touch
   `bootloader/README.md` if a half fails to enter its UF2 bootloader at
   all.
