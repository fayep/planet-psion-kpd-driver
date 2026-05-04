# Hardware Overview

All three Planet Computers clamshell devices — Gemini PDA, Cosmo Communicator, and Astro
Slide — share the same physical keyboard hardware design.  The keypad subsystem is split
across two independent hardware blocks.

## Block diagram

```
Physical QWERTY keys
        |
    AW9523B (I2C)
        |
   i2c bus (SoC)
        |
   aw9523_key driver  ──► input_dev (evdev)
                               ↑
                      MTK KPD HAL (power/home keys via PMIC)
```

---

## AW9523B — main keyboard controller

The [AWINIC AW9523B](https://www.awinic.com/en/productDetail/AW9523BQFR) is a 16-pin
I2C GPIO expander with integrated LED current-sink drivers.  It acts as the keyboard
matrix scanner for all QWERTY keys.

### I2C configuration

The I2C address is **`0x5B`** (7-bit) on confirmed hardware, meaning both AD0 and AD1
are pulled to VCC.  The Android kernel header `aw9523_key.h` contains the incorrect
value `0xB0` (7-bit `0x58`, AD0=AD1=GND); this appears to be a copy-paste from an
earlier reference design and does not reflect production hardware.

| Device | 7-bit address | Confirmed via |
|--------|--------------|---------------|
| Cosmo Communicator | `0x5B` | Live `/sys/bus/i2c/devices/4-005b/driver` + live `/proc/device-tree` (`reg = <0x5b>`) |
| Astro Slide | `0x5B` | Live DTBO (`aw9523_key@5b`, `reg = <0x5b>`) |
| Gemini PDA | `0x5B` (presumed) | Not yet confirmed — no live device available |

On Cosmo, the AW9524 backlight chip is also at address `0x5B` but on a different I2C
bus (bus 3 vs bus 4 for the AW9523).

### Device tree compatible strings

The Android drivers bind via these DT compatible strings (confirmed from live
`/proc/device-tree` on Cosmo running Gemian kernel 4.4.146):

| Chip | Compatible string |
|------|-----------------|
| AW9523B | `mediatek,aw9523_key` |
| AW9524  | `mediatek,aw9524_key` |

**Important**: the AW9523 and AW9524 DT nodes on Cosmo contain only `compatible`,
`reg`, and `status` — no `interrupts`, `reset-gpios`, or any other properties.  The
Android driver hard-codes the SHDN and INT GPIO numbers rather than reading them from
DT.  The out-of-tree driver must define proper DT bindings for these signals.

### GPIO assignment

| Port | Direction | Function |
|------|-----------|----------|
| P0[7:0] | Output (column drive) | 8 keyboard rows (KROW) |
| P1[6:0] | Input (row sense) | 7 keyboard columns (KCOL) |
| P1[7]   | Unused | — |

The driver scans by driving each column low in turn on P0 and reading the row state on P1.

### Register map (AW9523B)

| Offset | Name | Description |
|--------|------|-------------|
| `0x00` | P0_INPUT   | P0 pin input state (read) |
| `0x01` | P1_INPUT   | P1 pin input state (read) |
| `0x02` | P0_OUTPUT  | P0 output value |
| `0x03` | P1_OUTPUT  | P1 output value |
| `0x04` | P0_CONFIG  | P0 direction: 0=output, 1=input |
| `0x05` | P1_CONFIG  | P1 direction: 0=output, 1=input |
| `0x06` | P0_INT     | P0 interrupt enable: 0=enabled, 1=disabled |
| `0x07` | P1_INT     | P1 interrupt enable: 0=enabled, 1=disabled |
| `0x10` | ID_REG     | Chip ID (read-only, returns `0x23`) |
| `0x11` | CTL_REG    | Global control: bit[4] P0 drive mode (0=open-drain, 1=push-pull) |
| `0x12` | P0_LED_MODE | Per-pin LED/GPIO select (0=LED, 1=GPIO) |
| `0x13` | P1_LED_MODE | Per-pin LED/GPIO select |
| `0x20`–`0x2F` | DIM0–DIM15 | LED current control registers |
| `0x7F` | SW_RSTN    | Software reset (write `0x00`) |

### Interrupt / scan strategy

1. On init, configure P0 as all-low output (columns driven), P1 as input, enable P0 interrupts.
2. Wait for any key press with a level interrupt on the AW9523B INT pin.
3. On interrupt: disable the interrupt, start an hrtimer.
4. On each hrtimer tick: scan each column, read P1 rows, decode key state from `key_map[]`.
5. Report press/release events to the input subsystem.
6. When no keys remain pressed, re-enable the interrupt and stop the timer.

Scan interval:
- Gemini / Cosmo: adaptive — 1 ms while keys are active, 10 ms when settling
- Astro: fixed ~10 ms (`HRTIMER_FRAME = 100`, period = `1000/100 ms = 10 ms`)

### Hall sensor integration

All three devices have a hall-effect sensor that detects whether the clamshell lid is
open or closed.  The AW9523 driver registers a notifier (or polls the sensor) and
disables the keyboard IRQ when the lid is closed, to prevent phantom key events from
physical contact between the keyboard and the closed lid.

---

## MTK KPD block — power / home keys only

The MediaTek KPD hardware register block (`KP_*`) is present on all three SoCs but is
used only for the power key and home/reset key, which are routed through the PMIC.  The
full 72-key `kpd-hw-init-map` in the device tree has almost all entries zeroed; only
index 0 (keycode `114`, VOL_DOWN) is mapped.

### MTK KPD register map (identical on all three SoCs)

All offsets are relative to the MMIO base address supplied by the device tree.

| Offset | Name | Description |
|--------|------|-------------|
| `+0x0000` | KP_STA       | Keypad status |
| `+0x0004` | KP_MEM1      | Key state bits [15:0] |
| `+0x0008` | KP_MEM2      | Key state bits [31:16] |
| `+0x000C` | KP_MEM3      | Key state bits [47:32] |
| `+0x0010` | KP_MEM4      | Key state bits [63:48] |
| `+0x0014` | KP_MEM5      | Key state bits [71:64] (lower 8 bits valid) |
| `+0x0018` | KP_DEBOUNCE  | Debounce time (14-bit value) |
| `+0x001C` | KP_SCAN_TIMING | Scan timing |
| `+0x0020` | KP_SEL       | Column select; bit[0] = double-key enable (Astro only) |
| `+0x0024` | KP_EN        | Keypad enable (write 1 to enable, 0 to disable) |

Columns 0–2 are selected via `KP_SEL` bits [12:10].

### Keyboard backlight

Backlight hardware differs by device:

| Device | Mechanism |
|--------|-----------|
| Gemini | Not present / not implemented in Android driver |
| Cosmo  | Separate AW9524 I2C LED driver chip; controlled via MTK PWM |
| Astro  | MTK PWM driven directly from the AW9523 driver |

For the out-of-tree driver the backlight should be exposed as a standard `leds` class
device (`kbd_backlight`).

On Cosmo running Gemian, the existing Android AW9524 driver exposes
`/sys/class/leds/kbd_backlight/` with `max_brightness = 5`.

---

## Per-device GPIO assignments

GPIO numbers are SoC-local and differ between devices.

GPIO numbers are SoC-local and differ between devices.  The I2C SDA/SCK pins are part of
the SoC I2C controller and are not separately configurable as far as the AW9523B driver
is concerned; the driver only needs to know the SHDN (reset) and INT GPIO numbers.

### Gemini (MT6797)

Source: `aeon6797_6m_n.dts` (Android kernel source, not yet confirmed from live DTBO).

Note: the `aw9523_key.h` header contains GPIO136/137/127 — these are I2C *bus* pin
assignments for the legacy MTK I2C GPIO mode, not AW9523B chip-select signals.  The
real SHDN/INT assignments come from the board DTS.

| Signal | GPIO | Pin function | Source |
|--------|------|-------------|--------|
| AW9523B SHDN (reset) | GPIO58  | `GPIO58`  | `aeon6797_6m_n.dts` |
| AW9523B INT          | GPIO87  | `EINT10`  | `aeon6797_6m_n.dts` |

The INT pin on Gemini is muxed to the dedicated EINT10 hardware interrupt input rather
than plain GPIO mode as on the later devices.

### Cosmo (MT6771)

GPIO source: `k71v1_64_bsp.dts` (Android kernel source).  I2C bus assignments and DT
`reg` values confirmed live from Gemian (kernel 4.4.146) via `/sys/bus/i2c/devices/`
and `/proc/device-tree`.  Note: the DT node does **not** carry GPIO or interrupt
properties — these are hard-coded in the Android driver.

| Signal | GPIO / bus | Source |
|--------|-----------|--------|
| AW9523 SHDN (reset) | GPIO175 | `k71v1_64_bsp.dts` |
| AW9523 INT          | GPIO12  | `k71v1_64_bsp.dts` |
| AW9523 I2C bus      | i2c-4   | Live sysfs (`4-005b`, driver "Integrated keyboard") |
| AW9524 SHDN         | GPIO109 | `k71v1_64_bsp.dts` |
| AW9524 I2C bus      | i2c-3   | Live sysfs (`3-005b`, driver "AW9524keyboard") |

### Astro (MT6873)

Source: DTBO dumped live from device (`dtbo_a` partition, kernel 4.14.186).
Pinmux encoding cross-referenced against known labels (`@gpio29` → `0x1d00`, etc.)
and confirmed as `gpio_num << 8 | func`.  Also independently confirmed by the
`aw9523_eint` interrupt cell value (`0x02` = GPIO2).  Matches the Android source
`k6873v1_64.dts` which uses `PINMUX_GPIO5__FUNC_GPIO5` and `PINMUX_GPIO2__FUNC_GPIO2`.

| Signal | GPIO | Pinmux value | Source |
|--------|------|-------------|--------|
| AW9523 SHDN (reset) | GPIO5 | `0x0500` | Live DTBO + `k6873v1_64.dts` |
| AW9523 INT          | GPIO2 | `0x0200` | Live DTBO + `k6873v1_64.dts` + `aw9523_eint` |
| I2C bus | i2c8 | — | Live DTBO (fragment@47) |
| I2C speed | 400 kHz | `clock-frequency = <0x61a80>` | Live DTBO |
