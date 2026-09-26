# Piantor Pro BT ZMK Config — Miryoku-style

This is a firmware-only configuration for Keebart's Piantor Pro BT **5×3+3 (36-key)** layout, pinned to ZMK v0.3. It preserves the custom home-row mods, combos, gaming layer, RGB controls, timing configuration, Sharp MIP display, ZMK Studio support, and settings-reset builds from the Corne configuration.

## Build targets

The workflow builds the current Keebart Piantor 5-column targets:

- `piantor_pro_bt_5col_left`
- `piantor_pro_bt_5col_right`

Normal firmware artifacts:

- `piantor_pro_bt_5col_left`
- `piantor_pro_bt_5col_right`

Both normal builds include the `sharp_mip` shield and the ZMK Studio USB/UART snippet. Matching settings-reset artifacts are also built for recovery:

- `piantor_pro_bt_5col_left_settings_reset`
- `piantor_pro_bt_5col_right_settings_reset`

## Files required for this build

- `.github/workflows/build.yml` — GitHub Actions entry point
- `build.yaml` — Piantor 5-column build matrix
- `config/piantor_pro_bt_5col.keymap` — preserved custom 5×3+3 keymap
- `config/piantor_pro_bt_5col.conf` — firmware configuration
- `config/piantor_pro_bt_5col.json` — Piantor-specific ZMK Keymap Editor geometry
- `config/west.yml` — ZMK manifest
- `zephyr/module.yml` — module declaration

The Piantor board definitions in `boards/arm/piantor_pro_bt*` are copied from the current `Keebart/zmk-config` repository. The JSON file is used by the ZMK Keymap Editor and is not consumed by the firmware compiler.
