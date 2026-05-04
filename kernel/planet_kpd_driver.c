// SPDX-License-Identifier: GPL-2.0-only
/*
 * Planet Computers keyboard driver — AW9523B I2C GPIO expander
 *
 * Supports: Gemini PDA, Cosmo Communicator, Astro Slide
 *
 * The AW9523B is wired as an 8-row × 7-column matrix:
 *   P0[7:0] = row sense inputs  (pulled HIGH; go LOW when key pressed)
 *   P1[6:0] = column drive outputs (driven LOW one at a time during scan)
 *   P1[7]   = unused
 *
 * Two scan modes, selected at module load via the int_gpio parameter:
 *
 *   Interrupt mode (int_gpio >= 0):
 *     The AW9523B INT pin asserts LOW on any key state change.  The driver
 *     disables the IRQ, debounces for DEBOUNCE_MS, scans the matrix, and
 *     continues scanning every SCAN_PERIOD_MS while any key is held.  When
 *     all keys are released it re-enables the IRQ and waits.
 *
 *   Polling mode (int_gpio < 0, default):
 *     A delayed_work fires every SCAN_PERIOD_MS unconditionally.  Does not
 *     require the INT GPIO to be described in the device tree.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "planet_kpd_common.h"

#define MODULE_NAME    "planet_kpd"
#define SCAN_PERIOD_MS 10
#define DEBOUNCE_MS    5

static int int_gpio = -1;
module_param(int_gpio, int, 0444);
MODULE_PARM_DESC(int_gpio,
	"AW9523B INT GPIO number.  -1 (default) = use int_irq or polling.");

static int int_irq = -1;
module_param(int_irq, int, 0444);
MODULE_PARM_DESC(int_irq,
	"AW9523B IRQ number (overrides int_gpio).  -1 (default) = use int_gpio or polling.\n"
	"On Cosmo: the MTK EINT controller owns GPIO 12 so devm_gpio_request fails;\n"
	"pass int_irq=31 to use the aw9523-eint IRQ directly.");

/*
 * Key map indexed [row][col] → Linux keycode.
 * 0 = unconnected matrix position.
 *
 * P0_4/P1_3 is KEY_LEFTMETA on Gemini and Cosmo, KEY_FN on Astro.
 * Per-device key maps will replace this table once board data is added.
 */
static const u16 planet_kpd_keymap[AW9523_NUM_ROWS][AW9523_NUM_COLS] = {
	/* P0_0 */ { KEY_1,          KEY_2,       KEY_3,         KEY_4,          KEY_5,    KEY_6,     KEY_7         },
	/* P0_1 */ { KEY_U,          KEY_W,       KEY_Y,         KEY_T,          KEY_E,    KEY_Q,     KEY_R         },
	/* P0_2 */ { KEY_S,          KEY_D,       KEY_TAB,       KEY_F,          KEY_G,    KEY_A,     KEY_H         },
	/* P0_3 */ { KEY_Z,          KEY_C,       KEY_N,         KEY_X,          KEY_V,    KEY_B,     KEY_LEFTSHIFT  },
	/* P0_4 */ { KEY_COMMA,      KEY_LEFTALT, KEY_M,         KEY_LEFTMETA,   KEY_SPACE, KEY_DOT,  KEY_LEFTCTRL  },
	/* P0_5 */ { KEY_APOSTROPHE, KEY_LEFT,    KEY_DOWN,      KEY_RIGHTSHIFT, KEY_UP,   KEY_RIGHT, KEY_L         },
	/* P0_6 */ { KEY_8,          KEY_9,       KEY_BACKSPACE, KEY_P,          KEY_O,    KEY_ENTER, KEY_0         },
	/* P0_7 */ { KEY_J,          KEY_K,       KEY_I,         0,              0,        0,         0             },
};

struct planet_kpd_dev {
	struct i2c_client  *client;
	struct input_dev   *input;
	struct delayed_work work;
	u8                  prev_state[AW9523_NUM_COLS];
	int                 irq;	/* >=0: interrupt mode; -1: polling */
};

static int planet_kpd_chip_init(struct i2c_client *client)
{
	int ret;

	/* Software reset */
	ret = i2c_smbus_write_byte_data(client, AW9523_REG_SW_RSTN, 0x00);
	if (ret < 0)
		return ret;
	msleep(20);

	/* All pins to GPIO mode (not LED current sink) */
	ret = i2c_smbus_write_byte_data(client, AW9523_REG_P0_LED_MODE, 0xFF);
	if (ret < 0)
		return ret;
	ret = i2c_smbus_write_byte_data(client, AW9523_REG_P1_LED_MODE, 0xFF);
	if (ret < 0)
		return ret;

	/* P0 all inputs (rows, sense via internal pull-ups), P1 all outputs (columns) */
	ret = i2c_smbus_write_byte_data(client, AW9523_REG_P0_CONFIG, 0xFF);
	if (ret < 0)
		return ret;
	ret = i2c_smbus_write_byte_data(client, AW9523_REG_P1_CONFIG, 0x00);
	if (ret < 0)
		return ret;

	/* All P1 lines Hi-Z — no column selected, matrix at rest */
	ret = i2c_smbus_write_byte_data(client, AW9523_REG_P1_OUTPUT, 0xFF);
	if (ret < 0)
		return ret;

	/* Disable all interrupts; interrupt mode enables P0 after GPIO setup */
	ret = i2c_smbus_write_byte_data(client, AW9523_REG_P0_INT, 0xFF);
	if (ret < 0)
		return ret;
	ret = i2c_smbus_write_byte_data(client, AW9523_REG_P1_INT, 0xFF);
	if (ret < 0)
		return ret;

	return 0;
}

static void planet_kpd_scan(struct planet_kpd_dev *kpd)
{
	bool changed = false;
	int row, col;

	for (col = 0; col < AW9523_NUM_COLS; col++) {
		u8 row_state;
		u8 delta;
		int ret;

		/* Drive only this column LOW, all others Hi-Z */
		ret = i2c_smbus_write_byte_data(kpd->client, AW9523_REG_P1_OUTPUT,
						(u8)(~BIT(col)));
		if (ret < 0)
			continue;

		udelay(2);

		/* P0 is active-low: 0 = pressed. Invert to get pressed-high mask. */
		ret = i2c_smbus_read_byte_data(kpd->client, AW9523_REG_P0_INPUT);
		if (ret < 0)
			continue;

		row_state = ~(u8)ret;
		delta = row_state ^ kpd->prev_state[col];

		if (delta) {
			for (row = 0; row < AW9523_NUM_ROWS; row++) {
				if (!(delta & BIT(row)))
					continue;
				if (!planet_kpd_keymap[row][col])
					continue;
				input_report_key(kpd->input,
						 planet_kpd_keymap[row][col],
						 !!(row_state & BIT(row)));
			}
			kpd->prev_state[col] = row_state;
			changed = true;
		}
	}

	/* Return all columns Hi-Z — matrix at rest between scans */
	i2c_smbus_write_byte_data(kpd->client, AW9523_REG_P1_OUTPUT, 0xFF);

	if (changed)
		input_sync(kpd->input);
}

static void planet_kpd_work_fn(struct work_struct *work)
{
	struct planet_kpd_dev *kpd =
		container_of(work, struct planet_kpd_dev, work.work);
	int col;

	planet_kpd_scan(kpd);

	if (kpd->irq >= 0) {
		/*
		 * Interrupt mode: keep scanning while any key is held so we
		 * catch releases.  When the matrix is clear, re-arm the IRQ.
		 * Reading P0_INPUT during the scan clears the AW9523B interrupt
		 * latch, so re-enabling the IRQ here is safe.
		 */
		for (col = 0; col < AW9523_NUM_COLS; col++) {
			if (kpd->prev_state[col]) {
				schedule_delayed_work(&kpd->work,
						      msecs_to_jiffies(SCAN_PERIOD_MS));
				return;
			}
		}
		enable_irq(kpd->irq);
	} else {
		/* Polling mode: reschedule unconditionally */
		schedule_delayed_work(&kpd->work, msecs_to_jiffies(SCAN_PERIOD_MS));
	}
}

static irqreturn_t planet_kpd_irq_handler(int irq, void *dev_id)
{
	struct planet_kpd_dev *kpd = dev_id;

	disable_irq_nosync(irq);
	schedule_delayed_work(&kpd->work, msecs_to_jiffies(DEBOUNCE_MS));
	return IRQ_HANDLED;
}

static int planet_kpd_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct planet_kpd_dev *kpd;
	struct input_dev *input;
	int chip_id, ret, row, col;

	chip_id = i2c_smbus_read_byte_data(client, AW9523_REG_ID);
	if (chip_id < 0) {
		dev_err(&client->dev, "failed to read chip ID: %d\n", chip_id);
		return chip_id;
	}
	if (chip_id != AW9523_CHIP_ID) {
		dev_err(&client->dev, "unexpected chip ID 0x%02x\n", chip_id);
		return -ENODEV;
	}

	kpd = devm_kzalloc(&client->dev, sizeof(*kpd), GFP_KERNEL);
	if (!kpd)
		return -ENOMEM;

	kpd->client = client;
	kpd->irq    = -1;
	i2c_set_clientdata(client, kpd);

	ret = planet_kpd_chip_init(client);
	if (ret) {
		dev_err(&client->dev, "chip init failed: %d\n", ret);
		return ret;
	}

	input = devm_input_allocate_device(&client->dev);
	if (!input)
		return -ENOMEM;

	input->name       = "Planet keyboard";
	input->phys       = "planet_kpd/input0";
	input->id.bustype = BUS_I2C;

	for (row = 0; row < AW9523_NUM_ROWS; row++)
		for (col = 0; col < AW9523_NUM_COLS; col++)
			if (planet_kpd_keymap[row][col])
				input_set_capability(input, EV_KEY,
						     planet_kpd_keymap[row][col]);

	kpd->input = input;

	ret = input_register_device(input);
	if (ret) {
		dev_err(&client->dev, "input_register_device failed: %d\n", ret);
		return ret;
	}

	INIT_DELAYED_WORK(&kpd->work, planet_kpd_work_fn);

	if (int_irq >= 0) {
		/*
		 * Direct IRQ mode: the MTK EINT controller owns the GPIO pin so
		 * devm_gpio_request fails with -EPROBE_DEFER on Cosmo.  The EINT
		 * is already configured by the DT; we just attach our handler.
		 */
		/*
		 * On Cosmo (4.4 kernel) the built-in aw9523_key driver claims
		 * this IRQ at init time (flags=0, no IRQF_SHARED) even when its
		 * I2C probe is inactive, so request_irq returns -EBUSY here.
		 * Interrupt mode works on kernels with proper DT interrupt
		 * bindings (e.g. Astro 5.4+).
		 */
		ret = devm_request_irq(&client->dev, int_irq,
				       planet_kpd_irq_handler,
				       IRQF_TRIGGER_FALLING,
				       MODULE_NAME, kpd);
		if (ret) {
			dev_warn(&client->dev,
				 "cannot request IRQ %d (%d) — IRQ already claimed; falling back to polling\n",
				 int_irq, ret);
			goto polling;
		}

		/* Enable P0 change interrupts on the chip */
		i2c_smbus_write_byte_data(client, AW9523_REG_P0_INT, 0x00);

		kpd->irq = int_irq;
		dev_info(&client->dev,
			 "AW9523B keyboard ready (interrupt mode, IRQ %d)\n",
			 int_irq);
		return 0;
	}

	if (int_gpio >= 0) {
		int gpio_irq;

		ret = devm_gpio_request_one(&client->dev, int_gpio,
					    GPIOF_IN, MODULE_NAME);
		if (ret) {
			dev_warn(&client->dev,
				 "cannot claim INT GPIO %d (%d), falling back to polling\n",
				 int_gpio, ret);
			goto polling;
		}

		gpio_irq = gpio_to_irq(int_gpio);
		if (gpio_irq < 0) {
			dev_warn(&client->dev,
				 "cannot map INT GPIO %d to IRQ, falling back to polling\n",
				 int_gpio);
			goto polling;
		}

		ret = devm_request_irq(&client->dev, gpio_irq,
				       planet_kpd_irq_handler,
				       IRQF_TRIGGER_FALLING,
				       MODULE_NAME, kpd);
		if (ret) {
			dev_warn(&client->dev,
				 "cannot request IRQ %d (%d), falling back to polling\n",
				 gpio_irq, ret);
			goto polling;
		}

		/* Enable P0 change interrupts on the chip now the GPIO is wired */
		i2c_smbus_write_byte_data(client, AW9523_REG_P0_INT, 0x00);

		kpd->irq = gpio_irq;
		dev_info(&client->dev,
			 "AW9523B keyboard ready (interrupt mode, GPIO %d / IRQ %d)\n",
			 int_gpio, gpio_irq);
		return 0;
	}

polling:
	schedule_delayed_work(&kpd->work, msecs_to_jiffies(SCAN_PERIOD_MS));
	dev_info(&client->dev, "AW9523B keyboard ready (polling mode)\n");
	return 0;
}

static int planet_kpd_remove(struct i2c_client *client)
{
	struct planet_kpd_dev *kpd = i2c_get_clientdata(client);

	/*
	 * Disable the IRQ before cancelling work so the handler cannot
	 * reschedule after cancel_delayed_work_sync returns.
	 */
	if (kpd->irq >= 0)
		disable_irq(kpd->irq);
	cancel_delayed_work_sync(&kpd->work);
	return 0;
}

static const struct of_device_id planet_kpd_of_match[] = {
	{ .compatible = "mediatek,aw9523_key" },
	{ }
};
MODULE_DEVICE_TABLE(of, planet_kpd_of_match);

static const struct i2c_device_id planet_kpd_id[] = {
	{ "planet_kpd", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, planet_kpd_id);

static struct i2c_driver planet_kpd_driver = {
	.driver = {
		.name           = MODULE_NAME,
		.of_match_table = planet_kpd_of_match,
	},
	.probe    = planet_kpd_probe,
	.remove   = planet_kpd_remove,
	.id_table = planet_kpd_id,
};

module_i2c_driver(planet_kpd_driver);

MODULE_AUTHOR("Planet Computers Ltd.");
MODULE_DESCRIPTION("Planet Computers keyboard driver (AW9523B)");
MODULE_LICENSE("GPL");
