# Device Comparison

## Source repositories

| Device | SoC | Kernel | Android source |
|--------|-----|--------|----------------|
| Gemini PDA       | MT6797 (Helio X25) | 3.18     | `android_kernel_planetcom_mt6797` |
| Cosmo Communicator | MT6771 (Helio P60) | 4.4.146  | `android_kernel_planetcom_mt6771` |
| Astro Slide      | MT6873 (Dimensity 800) | 4.14.186 | `android_kernel_planetcom_mt6873` |

All repositories are under `https://github.com/planet-community/`.

## Driver locations in Android kernels

| Component | Path |
|-----------|------|
| Common KPD driver | `drivers/input/keyboard/mediatek/kpd.c` |
| Common KPD header | `drivers/input/keyboard/mediatek/kpd.h` |
| SoC-specific HAL  | `drivers/input/keyboard/mediatek/<soc>/hal_kpd.{c,h}` |
| AW9523 key driver | `drivers/misc/mediatek/aw9523/aw9523_key.{c,h}` |
| AW9524 LED driver | `drivers/misc/mediatek/aw9524/aw9524_key.{c,h}` (Cosmo only) |

## Feature comparison

| Feature | Gemini (MT6797) | Cosmo (MT6771) | Astro (MT6873) |
|---------|-----------------|----------------|----------------|
| Kernel version | 3.18 | 4.4.146 | 4.14.186 |
| AW9523B chip | ✓ | ✓ | ✓ |
| AW9524 (dedicated LED chip) | ✗ | ✓ | ✗ |
| Backlight | none | AW9524 + MTK PWM | MTK PWM via AW9523 driver |
| P0_4/P1_3 key | `KEY_LEFTMETA` | `KEY_LEFTMETA` | `KEY_FN` |
| Scan timing | hrtimer 1 ms (active) / 10 ms (idle) | same | hrtimer fixed ~10 ms |
| MMIO access style | `*(volatile u16 *)` | `readw()` / `writew()` | `readw()` / `writew()` |
| Wakelock API | `<linux/wakelock.h>` (deprecated) | `<linux/wakelock.h>` (deprecated) | `<linux/pm_wakeup.h>` |
| Meta+Power → `KEY_POWER` | ✗ | ✓ (in KPD HAL) | ✗ (userspace) |
| Hall-sensor key suppression | ✗ | ✓ via notifier | ✓ via `is_hall_state()` |
| Home key remapping (lid closed) | ✗ | ✓ `BTN_LEFT` → rstkey | ✗ |
| KPD double-key enable | ✗ | ✗ | ✓ |
| Long-press power log dump | ✗ | ✗ | ✓ |
| FM radio wakeup guard | ✓ | ✗ | ✗ |

## Key differences in detail

### Meta key vs Fn key (P0_4 / P1_3)

The modifier key in the lower-left region of the keyboard:

- **Gemini and Cosmo**: reported as `KEY_LEFTMETA` (Super / Windows key).
- **Astro**: reported as `KEY_FN`.

The physical key is labelled differently on each device but electrically occupies the
same matrix position.  The out-of-tree driver should expose this via a per-board device
tree property or board ID so that the correct keycode is emitted.

### Power key routing

On all three devices the physical power button connects to the PMIC, not to the AW9523B
matrix.  The PMIC fires a keypad interrupt handled by the KPD HAL.

The `Esc` key physically printed on the keyboard is `Meta/Fn + Esc`, i.e. the AW9523
modifier key held while the PMIC power button is pressed.

Android driver behaviour:

- **Bare power button** (no modifier) → `KEY_ESC`.  This is the phone-style "back"
  action used in the Android navigation model.
- **Meta + power button** (Cosmo only, in HAL) → `KEY_POWER`.  The Cosmo KPD HAL
  checks the `aw9523MetaKeyPressed` global and reroutes the PMIC event accordingly.
- **Astro**: the HAL does not implement the interlock; the `Fn + power` combination is
  left to userspace.

For a Linux desktop environment both mappings make sense:
- Bare power button → `KEY_ESC` (or `KEY_POWER` if preferred; configurable).
- `Meta/Fn + power` → `KEY_POWER` to trigger suspend/shutdown.

The out-of-tree driver should implement the interlock in kernel space (as the Cosmo
driver does) so the behaviour is consistent regardless of which userspace is running.

### MMIO access style

The Gemini driver uses raw volatile casts (`*(volatile u16 *)KP_MEM1`) inherited from
very old MediaTek BSP code.  Cosmo and Astro both use the correct `readw()` / `writew()`
accessor functions.  The out-of-tree driver must use `readw()` / `writew()`.

### Wakelock API

The Gemini and Cosmo drivers use the deprecated Android `wakelock.h` API.  The Astro
driver was updated to `pm_wakeup.h` (`wakeup_source_*`).  The out-of-tree driver must
use `pm_wakeup.h`.

### Hall sensor

The clamshell state (lid open / lid closed) is detected by a hall-effect sensor.  When
the lid is closed:

- **Cosmo**: the AW9523 driver registers a `notifier_block` against the MediaTek hall
  driver (`soc/mediatek/hall.h`) and sets `is_device_closed`.  The KPD HAL reads
  `kpd_is_device_closed` to decide whether to map the home/rstkey to `BTN_LEFT` (lid
  open) or `kpd_sw_rstkey` (lid closed).
- **Astro**: `is_hall_state()` is polled directly; no notifier.

The out-of-tree driver should register with the hall sensor subsystem (when available)
and suppress key events while the lid is closed, to prevent the keyboard from being
pressed against the screen from producing spurious input.

### Backlight (Cosmo-specific AW9524)

The Cosmo adds a second AWINIC chip — the AW9524 — which is wired exclusively for LED
current control of the keyboard backlight.  It shares the same I2C register layout as
the AW9523B but does not implement the interrupt / matrix-scan logic.  The Gemini has
no backlight; the Astro drives backlight via MTK PWM directly from the AW9523 driver.

The out-of-tree driver should register a `leds` class device named `kbd_backlight` on
devices that have backlight hardware, and expose brightness control through the standard
`/sys/class/leds/kbd_backlight/brightness` interface.

## Live device observations

### Cosmo running Gemian (kernel 4.4.146, firmware V25)

Observed on Cosmo Communicator booted into Gemian Linux (April 2021 build,
Android firmware V25) via SSH.

**Input devices registered at boot (`dmesg`):**

| evdev | Name | Driver |
|-------|------|--------|
| `input0` | `ACCDET` | Accessory detection |
| `input1` | `mtk-kpd` | MediaTek KPD block (power/home keys) |
| `input2` | `cf-keys` | Cover-flip keys |
| `input3` | `Integrated keyboard` | AW9523B keyboard matrix |
| `input4` | `mtk-tpd` | Touchpad |

**I2C devices:**

| sysfs path | Chip | Driver |
|-----------|------|--------|
| `4-005b` | AW9523B keyboard | `Integrated keyboard` |
| `3-005b` | AW9524 backlight | `AW9524keyboard` |

**LED class devices:** `kbd_backlight` (max_brightness=5), `lcd-backlight`,
`mt6370_pmu_bled`, `mt6370_pmu_led3`, `red`, `green`, `blue`.

**DT node structure** (`/proc/device-tree/i2c@11008000/aw9523_key@5b/`):
properties present: `compatible`, `reg`, `status`, `name`, `phandle` only.
No `interrupts` or GPIO properties — these are hard-coded in the Android driver.

---

## Android DTS keypad configuration

The MTK KPD hardware block is configured in the device tree.  The `kpd-hw-init-map` on
all three devices is essentially empty (all zeros) except for index 0 = keycode `114`
(VOL_DOWN).  This confirms the KPD block is not used for the QWERTY matrix.

```dts
/* Representative — from Gemini cust_kpd.dtsi */
&keypad {
    mediatek,kpd-key-debounce  = <1024>;
    mediatek,kpd-sw-pwrkey     = <116>;   /* KEY_POWER */
    mediatek,kpd-hw-pwrkey     = <8>;
    mediatek,kpd-sw-rstkey     = <115>;   /* KEY_VOLUMEDOWN */
    mediatek,kpd-hw-rstkey     = <17>;
    mediatek,kpd-use-extend-type = <0>;
    mediatek,kpd-hw-map-num    = <72>;
    mediatek,kpd-hw-init-map   = <114 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
                                    0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
                                    0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
                                    0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
                                    0 0 0 0 0 0 0 0>;
};
```
