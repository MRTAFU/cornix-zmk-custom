# Split BLE stability: the stuck-key investigation

Working notes for the "the right half blips out and the last key machine-guns"
problem. Written down because the next person to touch this — probably me, a
year from now — will otherwise redo the whole investigation from scratch.

Branch: `feature/cornix-layer-gate`
Commits: `31aa034` (BLE parameters), `8b73e66` (BT profile unpair gesture)
Investigated: 2026-09-17 / 2026-09-19

---

## The symptom

After migrating from RMK v1.12 to ZMK, the right half seems to drop its split
BLE link more often than it used to. When it happens mid-keystroke the host
gets a burst of the key that was held:

```
llllllllllllllllllll
```

It happens with both halves fully charged, so it is not simply a low-battery
story (there was a separate low-voltage version of this in the past).

Topology: Right --BLE split--> Left (ZMK central) --USB--> Mac.

## What actually causes the burst

The original guess was "ZMK never releases a key whose release got lost." That
is **wrong**, and it is worth being precise about why, because the real answer
points at a different fix.

ZMK *does* force-release held positions when a peripheral goes away —
`release_peripheral_slot()` in `app/src/split/bluetooth/central.c:172-213`
walks the slot's `position_state` bitmap and raises a `pressed = false` event
for every bit still set. It is reached from `split_central_disconnected()`
(`central.c:951-985`).

The catch is *when* that runs: only once the disconnect callback fires, and for
a link that simply stopped answering, that is one supervision timeout later.
ZMK's default is `ZMK_SPLIT_BLE_PREF_TIMEOUT = 400`, in units of 10 ms, so
**4 seconds**. Timeline:

```
t=0        Right: L down -> bitmap notify -> Left -> Mac sees press
t=+10ms    RF link degrades, packets stop getting through
t=+Xms     Right: L up -> notify never lands
           (the central still believes L is held)
t=+375ms   macOS starts auto-repeating
t=+375ms..+4s   "lllllllllll..."
t=+4000ms  supervision timeout -> split_central_disconnected()
           -> release_peripheral_slot() -> release -> repeat stops
t=+4s..    central rescans, reconnects ("felt like a brief dropout")
```

So the recovery exists; it is just four seconds late, and **the supervision
timeout is what sets the length of the burst**. The ~20 characters observed is
about 2 s at the macOS default repeat rate, which fits.

### A second, rarer path worth knowing about

`app/src/split/bluetooth/service.c:217-226` on the peripheral:

```c
while (k_msgq_get(&position_state_msgq, &state, K_NO_WAIT) == 0) {
    int err = bt_gatt_notify(NULL, &split_svc.attrs[1], &state, sizeof(state));
    if (err) {
        LOG_DBG("Error notifying %d", err);   // dropped; no retry, no requeue
    }
}
```

The message is already dequeued, so a failed notify (`-ENOMEM`, congestion,
`-ENOTCONN`) is silently lost. If the lost snapshot was the release, the
central never hears about it — and because the link never dropped, the
disconnect-time force-release never runs either. The key stays stuck until the
next keypress on the right half re-sends the current bitmap. This is an
upstream bug, not a configuration problem.

**The two are distinguishable in the logs**: path one leaves a disconnect with
`reason 0x08` (`BT_HCI_ERR_CONN_TIMEOUT`) on the central; path two leaves no
disconnect at all.

## Things that turned out not to be the cause

Ruled out from source, so nobody has to re-check them:

- **Sleep / idle.** `CONFIG_ZMK_SLEEP` has no default and is set nowhere in
  this repo, so deep sleep is off. `ZMK_IDLE_TIMEOUT` is 30 s but
  `app/src/activity.c:73-92` shows idle only raises an event — it never
  touches the radio or the connection.
- **Split event queue overflow.** What crosses the link is a full position
  *bitmap snapshot*, not edge events (`service.c:251-259`, and the central
  XORs against the previous state in `central.c:367-370`). Peripheral-side
  overflow drops the oldest and requeues (`service.c:231-244`), so the newest
  snapshot — always the truth — survives. Overflow can lose a transient tap;
  it cannot strand a key in the pressed state.
- **`cornix_layer_gate`.** Its position listener returns `ZMK_EV_EVENT_BUBBLE`
  on every path, it only mutates its own small state table, and
  `CMakeLists.txt:5-9` builds it on the central only — it is not even in the
  right half's firmware. The key that stuck was `&kp L`, a plain keypress that
  never goes near it.

## What was wrong in the config

Three real findings:

1. **TX power asymmetry.** `cornix_left_defconfig` had
   `CONFIG_BT_CTLR_TX_PWR_PLUS_8=y`; the right defconfig had no TX setting at
   all, so it fell back to Zephyr's `default BT_CTLR_TX_PWR_0` = 0 dBm
   (`subsys/bluetooth/controller/Kconfig:330-332`). The entire 8 dB deficit
   sat on the Right→Left direction — the direction every key event travels.

2. **2M PHY was on.** `ZMK_SPLIT_BLE` unconditionally selects
   `BT_USER_PHY_UPDATE` + `BT_AUTO_PHY_UPDATE`
   (`app/src/split/Kconfig:16-20`), `BT_CTLR_PHY_2M` defaults to y, and
   Zephyr's `perform_auto_initiated_procedures()`
   (`subsys/bluetooth/host/conn.c:1786-1796`) then negotiates symmetric 2M on
   connect. We were paying several dB of link budget for throughput a 6-byte
   bitmap does not need.

3. **`CONFIG_BT_PERIPHERAL_PREF_LATENCY=5` in the right defconfig never did
   anything.** Added under refs #7 to cut slave latency, but ZMK sets
   `BT_GAP_AUTO_UPDATE_CONN_PARAMS=n` for split peripherals
   (`app/src/split/bluetooth/Kconfig:99-101`, "Allow central to specify
   connection parameters."), Zephyr only sends a peripheral-initiated param
   update from inside that guard (`conn.c:2270-2285`), and the central never
   reads our PPCP. The parameters that actually apply are the ones the central
   passes to `bt_conn_le_create()` from `CONFIG_ZMK_SPLIT_BLE_PREF_*`
   (`central.c:817-819`). Latency was 30 the whole time.

### Effective split link parameters, before

| | value | meaning |
|---|---|---|
| connection interval | 6 | 7.5 ms, fixed (min == max) |
| slave latency | 30 | up to ~232 ms of skipped events |
| supervision timeout | 400 | **4000 ms** |
| PHY | 2M | both directions |
| TX power | Left +8 dBm / Right **0 dBm** | asymmetric |

## What was changed

### `31aa034` — BLE parameters

- `cornix_right_defconfig`: `CONFIG_BT_CTLR_TX_PWR_PLUS_8=y` (0 → +8 dBm)
- both defconfigs: `CONFIG_BT_CTLR_PHY_2M=n` (2M → 1M)
- `cornix_left_defconfig`: `CONFIG_ZMK_SPLIT_BLE_PREF_TIMEOUT=100` (4 s → 1 s)
  and `CONFIG_ZMK_SPLIT_BLE_PREF_LATENCY=5` (the knob that actually applies)
- `cornix_right_defconfig`: old `BT_PERIPHERAL_PREF_LATENCY=5` commented out
  with the explanation, so #7 does not get retried the same dead-end way
- `config/west.yml`: zmk pinned to `9ebbeff` instead of tracking `main`

On `BT_CTLR_PHY_2M=n` versus `CONFIG_ZMK_BLE_EXPERIMENTAL_CONN=y`: as of
`9ebbeff` the experimental symbol's *only* effect is `BT_CTLR_PHY_2M default n`
(`app/Kconfig:174-201`; three references in the whole app, none in C). We
spell out the PHY setting directly so it cannot quietly acquire extra meaning
when the pin is bumped.

The timeout change is a **mitigation, not a fix** — it does not make dropouts
less frequent, it caps the burst at about a second. The trade-off is less
tolerance for brief RF dropouts, so more reconnects, each much shorter.

### `8b73e66` — BT profile unpair gesture

Tangent from the same session, but related to living with BLE. A bond lives on
the keyboard as well as the host, so forgetting the keyboard on the Mac does
not free the slot: `auth_pairing_accept()` (`app/src/ble.c:625-643`) rejects
the new pairing with `BT_SECURITY_ERR_PAIR_NOT_ALLOWED`, and the keyboard only
advertises to the stored peer. The keymap had `&bt BT_SEL 0/1/2` but no
`BT_CLR` anywhere, so the only escape was reflashing `settings_reset` and
losing the keymap with it.

RMK ships this as a gesture (hold the profile key 5 s to drop that bond), so
the ported keymap now matches:

```
&bt_prof N N    tap      -> switch to profile N
                hold 5s  -> switch to profile N, then BT_CLR
```

Built from stock behaviors (one-param macros + hold-tap) rather than another
out-of-tree driver, because Studio is enabled here and standard behaviors are
what it can round-trip. `BT_CLR` acts on the *active* profile, which is why
the hold side selects the target first instead of being a bare `&bt BT_CLR`.
`flavor = "tap-preferred"` means only the 5 s timer resolves to hold — other
keypresses cannot fire it (`decide_tap_preferred()`,
`behavior_hold_tap.c:301-315`) — which felt like the right bias for something
destructive.

## Status

- Both commits pushed to `origin` (`MRTAFU/cornix-zmk-custom`).
- `31aa034` was flashed and appeared to take (the macOS "could not copy /
  disk ejected" dialog during UF2 drag-and-drop is normal — the board reboots
  the instant the last block lands).
- `8b73e66` had not been verified on hardware at the time of writing. **The
  open question is whether a one-param macro works as a hold-tap binding in
  this ZMK revision.** It is the documented pattern, but it was never built.
  If CI is red, that is the first suspect; fall back to a small out-of-tree
  behavior in the style of `cornix_layer_gate`.
- Effectiveness of the BLE changes is unmeasured — needs a week or so of
  normal use.

Because TX power, PHY and connection parameters all moved in one commit, a
successful outcome will not say *which* of them mattered. That was a
deliberate trade (fewer flash-and-wait cycles) and is fine if it works; if the
problem persists, split them apart before drawing conclusions.

## If it is still happening

Get the disconnect reason before changing anything else. It splits the two
mechanisms above, and it is cheap:

**Instrument the left half, not the right.** Add the `zmk-usb-logging` snippet
to the `cornix_left` entry in `build.yaml`. Left is already on USB with a
snippet line, and `split_central_disconnected()` logs the reason
(`central.c:957`), so this measures the problem **without touching the right
half's RF or power conditions** — which is exactly what instrumenting the
right half would disturb.

- disconnects logged with `reason 8` → supervision timeout, the main path
- burst with **no** disconnect logged → the dropped-notify path, and the fix
  has to be a retry in `service.c` or a central-side watchdog

Turn the level down (`CONFIG_ZMK_LOG_LEVEL=3`) and promote just the lines you
need. At the default level 4, `central.c:371` hexdumps on **every keypress**,
and BT stack debug logging runs on the HCI path — it will perturb the timing
that causes the bug, hiding it or creating new drops.

Other useful sources if it comes to that:

| what | where |
|---|---|
| actual negotiated conn params | `peripheral.c:131-138`, `ble.c:568` |
| peripheral queue overflow | `service.c:235` (already `LOG_WRN`) |
| reset cause (watchdog / brownout / assert) | needs `CONFIG_HWINFO=y` + a `hwinfo_get_reset_cause()` call at boot |
| controller assert | `boards/shields/cornix_indicator/src/bt_ctlr_assert.c` currently `ARG_UNUSED`s file/line and reboots silently — add a `LOG_ERR` to find out if it ever fires |

To log on the right half anyway: `CONFIG_ZMK_USB_LOGGING=y` works despite
`CONFIG_ZMK_USB=n` (it selects `USB`/`USB_CDC_ACM` directly, so you get CDC
without HID). But you then have to plug the right half into USB, which changes
the RF and power environment enough that the dropout may stop reproducing.
RTT (`CONFIG_ZMK_RTT_LOGGING=y`) avoids that but needs a debug probe.

## Remaining ideas, roughly in order

1. Split the `31aa034` changes apart if the combined fix did not work.
2. Central position queue: `ZMK_SPLIT_BLE_CENTRAL_POSITION_QUEUE_SIZE` is 5
   (`split/bluetooth/Kconfig:55`) and the disconnect-time release burst goes
   through it with `K_NO_WAIT` (`central.c:204`). Holding more than ~5 right-hand
   keys when the link drops could lose releases and strand keys for real. Rare,
   but bumping it to 16 is nearly free insurance.
3. Upstream fix for the dropped notify: requeue or retry on `bt_gatt_notify()`
   failure in `service.c:221`.
4. Note that the right half gained SPI3 + WS2812 + a switched ext-power rail
   when `cornix_indicator` arrived (it used to be `CONFIG_SPI=n` /
   `ZMK_EXT_POWER=n`, see `9e6d795`). Not investigated as an RF or current
   transient source.

## What could not be determined

- **Any comparison with RMK's actual settings.** `rmkfw/` holds only `.uf2`
  binaries, and v1.6-era at that, not the v1.12 that was running.
  `reference/20260905_cornix.vil` is a keymap with no radio settings. So
  whether RMK used 1M PHY, what TX power it ran, what its supervision timeout
  was — all unknown from here, and not worth guessing at.
  (RMK's *profile* behaviour was checked, from upstream docs at
  https://rmk.rs/docs/features/wireless : same bond model, but with the 5 s
  hold gesture that `8b73e66` now reproduces.)
- **The real `.config`.** Everything above is derived by reading Kconfig
  resolution rules, not from a generated `.config`. The claim most worth
  double-checking that way is `BT_GAP_AUTO_UPDATE_CONN_PARAMS=n` on the
  peripheral, since the dead `BT_PERIPHERAL_PREF_LATENCY` conclusion rests on
  it. Grab it from a CI build's artifacts or a local `just build`.
- How often the dropouts actually happen, and their reason codes. No logs were
  ever captured.

## Reference: files read during the investigation

This repo: `config/west.yml`, `build.yaml`, `boards/jzf/cornix/cornix{,_left,_right}*`,
`boards/jzf/cornix/Kconfig.defconfig`, `boards/shields/cornix_indicator/*`,
`src/behaviors/behavior_cornix_layer_gate.c`, `config/cornix.keymap`.

ZMK @ `9ebbeff`: `app/Kconfig` (174-201 experimental/PHY, 231-241 peripheral
prefs, 400-420 idle/sleep, 550-617 logging), `app/src/activity.c`,
`app/src/ble.c`, `app/src/split/Kconfig`,
`app/src/split/bluetooth/{Kconfig,central.c,peripheral.c,service.c}`,
`app/src/behaviors/behavior_hold_tap.c`, `app/dts/behaviors/macros.dtsi`.

Zephyr @ `v4.1.0+zmk-fixes`: `subsys/bluetooth/controller/Kconfig`,
`subsys/bluetooth/host/Kconfig`, `subsys/bluetooth/host/conn.c`.
