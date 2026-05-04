/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PLANET_KPD_COMMON_H
#define PLANET_KPD_COMMON_H

/* AW9523B register map */
#define AW9523_REG_P0_INPUT     0x00
#define AW9523_REG_P1_INPUT     0x01
#define AW9523_REG_P0_OUTPUT    0x02
#define AW9523_REG_P1_OUTPUT    0x03
#define AW9523_REG_P0_CONFIG    0x04  /* 0=output, 1=input */
#define AW9523_REG_P1_CONFIG    0x05
#define AW9523_REG_P0_INT       0x06  /* 0=enabled, 1=disabled */
#define AW9523_REG_P1_INT       0x07
#define AW9523_REG_ID           0x10  /* read-only, returns AW9523_CHIP_ID */
#define AW9523_REG_CTL          0x11  /* bit[4]: P0 drive mode (0=open-drain, 1=push-pull) */
#define AW9523_REG_P0_LED_MODE  0x12  /* 0=LED, 1=GPIO */
#define AW9523_REG_P1_LED_MODE  0x13
#define AW9523_REG_SW_RSTN      0x7F  /* software reset: write 0x00 */

#define AW9523_CHIP_ID          0x23

/* Matrix: P0[7:0] = rows (inputs, sense), P1[6:0] = columns (outputs, drive), P1[7] unused */
#define AW9523_NUM_ROWS         8
#define AW9523_NUM_COLS         7

#endif /* PLANET_KPD_COMMON_H */
