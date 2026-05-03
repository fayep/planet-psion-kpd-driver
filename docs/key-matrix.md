# Key Matrix

## Physical layout

The QWERTY keyboard matrix is an 8-row × 7-column design, giving 56 addressable
positions.  The matrix wiring is identical on Gemini, Cosmo, and Astro — the same
physical keyboard PCB is used across all three devices, with only minor per-device
firmware differences (see [device-comparison.md](device-comparison.md)).

Rows are driven on **P0[7:0]** (AW9523B port 0, outputs).
Columns are sensed on **P1[6:0]** (AW9523B port 1, inputs).
P1[7] is unused.

## Matrix map

Columns left→right are P1_0 through P1_6.
Rows top→bottom are P0_0 through P0_7.
Linux keycodes are shown; `—` denotes an unconnected (NULL) position.

```
         P1_0        P1_1        P1_2        P1_3        P1_4        P1_5        P1_6
P0_0:    KEY_1       KEY_2       KEY_3       KEY_4       KEY_5       KEY_6       KEY_7
P0_1:    KEY_U       KEY_W       KEY_Y       KEY_T       KEY_E       KEY_Q       KEY_R
P0_2:    KEY_S       KEY_D       KEY_TAB     KEY_F       KEY_G       KEY_A       KEY_H
P0_3:    KEY_Z       KEY_C       KEY_N       KEY_X       KEY_V       KEY_B       KEY_LEFTSHIFT
P0_4:    KEY_COMMA   KEY_LEFTALT KEY_M       KEY_LEFTMETA†  KEY_SPACE  KEY_DOT  KEY_LEFTCTRL
P0_5:    KEY_APOSTROPHE  KEY_LEFT  KEY_DOWN  KEY_RIGHTSHIFT  KEY_UP  KEY_RIGHT   KEY_L
P0_6:    KEY_8       KEY_9       KEY_BACKSPACE  KEY_P    KEY_O       KEY_ENTER   KEY_0
P0_7:    KEY_J       KEY_K       KEY_I       —           —           —           —
```

† On Astro this position reports `KEY_FN` instead of `KEY_LEFTMETA`.
See [device-comparison.md](device-comparison.md) for the full difference list.

## The Power key

The physical keyboard has no dedicated Power key.  Power is obtained as a **shifted**
combination using the modifier key at P0_4/P1_3:

- **Gemini / Cosmo**: that key is labelled and reported as `KEY_LEFTMETA`.
  `Meta + Esc` produces the power action.  On Cosmo, the Android KPD HAL
  implements this directly: when `aw9523MetaKeyPressed` is set and the PMIC power
  button fires, the HAL routes it to `KEY_POWER` rather than `KEY_ESC`.

- **Astro**: the key is labelled and reported as `KEY_FN`.
  `Fn + Esc` produces the power action.  The Astro KPD HAL does not implement
  the interlock — the combination is expected to be handled in userspace.

The bare PMIC power button (with no modifier held) is mapped to `KEY_ESC` on all
devices in the Android drivers, reflecting the phone-style navigation model.  For a
Linux desktop session the out-of-tree driver should preserve this behaviour: the
physical power button alone sends `KEY_ESC`, and `Meta/Fn + Esc` sends `KEY_POWER`.

## Key count

Of the 56 matrix positions, 52 are assigned and 4 are unconnected (P0_7 × P1_3/4/5/6).
Modifier keys present:

| Key | Linux keycode | Matrix position |
|-----|--------------|-----------------|
| Left Shift  | `KEY_LEFTSHIFT`  | P0_3 / P1_6 |
| Right Shift | `KEY_RIGHTSHIFT` | P0_5 / P1_3 |
| Left Ctrl   | `KEY_LEFTCTRL`   | P0_4 / P1_6 |
| Left Alt    | `KEY_LEFTALT`    | P0_4 / P1_1 |
| Meta / Fn   | `KEY_LEFTMETA` or `KEY_FN` | P0_4 / P1_3 |

## Encoding in source

The matrix is encoded in `key_map[]` in each per-device `.c` file as:

```c
KEY_STATE key_map[] = {
    // { name, linux_keycode, val, row_P0, col_P1 }
    {"1", KEY_1, 0, KROW_P0_0, KROW_P1_0},
    ...
};
```

where `KROW_P0_n` and `KROW_P1_n` are simply the integer indices 0–7.
