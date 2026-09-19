# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## CRITICAL: This is a downstream fork — never touch upstream

- `origin` is `numachang/cornix-zmk-custom` (a personal fork).
- Upstream is `hitsmaxft/zmk-keyboard-cornix`. **No PRs, pushes, branches, or issues against upstream.** Do not add an `upstream` remote and push to it, do not `gh pr create` targeting `hitsmaxft/*`, do not suggest contributing changes back unless the user explicitly asks.
- All work stays on this fork. When in doubt, confirm the remote before any `git push`, `gh pr create`, or `gh issue` command.
- This fork carries Japan-specific divergence from upstream — see "Japan-specific customizations" below. (The repo was formerly named `zmk-keyboard-cornix-jp`; GitHub redirects the old URL.)

## What this repo is

A ZMK firmware **config + board/shield module** for the Cornix split keyboard (Jezail Funder, 3×6 column-staggered + thumb cluster, nRF52840, Kailh Choc V2). The repo is consumed by ZMK's build system in two roles at once:

1. **ZMK user-config**: `config/` holds the keymap, per-target `.conf`, and `west.yml` manifest. `.github/workflows/build.yml` delegates to `zmkfirmware/zmk/.github/workflows/build-user-config.yml@main`, which reads `build.yaml` as the GH Actions matrix.
2. **ZMK external module**: `zephyr/module.yml` exposes this repo as a Zephyr module with `board_root: .` and `snippet_root: .`, so the boards under `boards/jzf/cornix/` and shields under `boards/shields/` get picked up by the ZMK app build when this repo is on `ZMK_EXTRA_MODULES` or referenced from `west.yml`.

Knowing both roles is necessary to understand why the same repo provides `build.yaml` (consumed when building *this repo*) AND `zephyr/module.yml` (consumed when another repo builds against this one as a module).

## Japan-specific customizations vs upstream

This fork intentionally diverges from `hitsmaxft/zmk-keyboard-cornix` in narrow, deliberate ways. Preserve these when merging from upstream:

- **All dongle build targets are commented out in `build.yaml`** — dongles are not sold in Japan, the keyboard runs as a plain split. Keep `cornix_dongle_*`, `cornix_ph_left`, and `reset_nicenano_nosd` targets commented; only `cornix_left`, `cornix_right`, and `cornix_reset` are active. The shields `cornix_dongle_adapter` / `cornix_dongle_eyelash` still exist on disk but are unreferenced by the active build matrix.
- **ZMK Studio is enabled on `cornix_left`** via the `studio-rpc-usb-uart nrf52840-nosd` snippet line in `build.yaml`. Defconfigs (`CONFIG_ZMK_STUDIO=y`, `CONFIG_ZMK_STUDIO_LOCKING=n`) come from upstream — only the snippet wiring is local.

When `git pull`ing or merging upstream, expect conflicts in `build.yaml`; resolve by keeping dongle targets commented and the Studio snippet present.

## Build & flash workflow

**Primary path: GitHub Actions on this fork.** Push to `main` (only when paths under `boards/*` or `config/*` change) or use `workflow_dispatch` → Actions tab → "Build ZMK firmware" → Run workflow. Note: changes to `build.yaml` alone do **not** trigger Actions automatically — they need a manual `workflow_dispatch`. Artifacts download as `firmware.zip` containing one `.uf2` per `artifact-name`.

**Flashing (irreversible — always wait for user "OK" before drag-and-drop):**
- Use a **data-capable USB-C cable** (not charge-only).
- For full firmware updates, flash **both halves** from the same build. Updating only one half causes split-protocol mismatch — this has bitten the user before. The one documented exception is enabling ZMK Studio on the central only, where peripheral firmware is unaffected by the Studio snippet; treat that as the exception, not the rule.
- Rollback path: backup stock RMK uf2s live in `rmkfw/`. Bootloader recovery (SoftDevice restore) docs in `bootloader/README.md`, but since v2.3 the partition layout no longer needs SD restore — flashing `.uf2` directly works.

**Local build (optional, Nix-based, Linux/macOS only — Justfile uses bash):**
```
nix develop          # provides zephyr-sdk, west, just, yq, keymap-drawer
just init            # west init -l config && west update && west zephyr-export
just list            # show parsed build targets from build.yaml
just build <expr>    # build targets matching <expr> (e.g. `just build cornix_left`, `just build all`)
just clean           # remove .build/ and firmware/
just draw cornix     # render keymap SVG via keymap-drawer into draw/
just test <path>     # run native_posix snapshot test under <path>
```
The Justfile expects `ZMK_LIB_PREFIX` (defaults to `zmk_exts`) pointing to the parent of a checked-out `zmk/` tree. Windows users should rely on GH Actions instead.

## Code layout that matters

- `build.yaml` — GH Actions matrix. Each `include:` entry is one firmware artifact. `board` + optional `shield` + optional `snippet` + `artifact-name`.
- `boards/jzf/cornix/` — the Cornix board definitions (`cornix_left`, `cornix_right`, `cornix_ph_left`). `cornix-layouts.dtsi` defines the `zmk,physical-layout` consumed by ZMK Studio. `*_defconfig` files control per-board Kconfig including the Studio flags.
- `boards/shields/` — optional shields (`cornix_indicator` for RGB; dongle adapter shields exist but are inactive in this fork).
- `config/cornix.keymap` + `config/cornix.json` — the keymap and its editor metadata. `cornix42.*` is the alternate 42-key layout variant.
- `config/west.yml` — manifest pulling `zmk@main`, `urob/zmk-helpers@main`, `englmaxi/zmk-dongle-display@main`. When a build breaks after upstream churn, suspect this file.
- `zephyr/module.yml` — what makes this repo loadable as a Zephyr module.
- `.github/workflows/build.yml` — thin wrapper delegating to ZMK's shared `build-user-config.yml`. Workflow only triggers on paths `boards/*` and `config/*`.
- `docs/ble-split-stability.md` — working notes on the split-BLE stuck-key problem (right half drops mid-keystroke, host auto-repeats the held key). Read this before touching TX power, PHY, connection parameters, or anything in `split/bluetooth/`: it records which hypotheses were already ruled out from source, which config settings turned out to be dead, and what to measure next. Also explains why `config/west.yml` pins zmk to a SHA rather than tracking `main`.

## Things to verify before recommending action

- Version numbers and release filenames the user mentions: cross-check against actual GitHub Releases / the current `build.yaml` artifact-names — don't trust from memory.
- If suggesting a flash, confirm which `.uf2` and which half, and wait for explicit "OK".
- If a code/config change is needed, prefer editing this fork's files; never propose opening a PR upstream.
