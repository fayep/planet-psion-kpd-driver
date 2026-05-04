// SPDX-License-Identifier: GPL-2.0-only
/*
 * Planet Computers keyboard driver — AW9523B I2C GPIO expander
 *
 * Supports: Gemini PDA, Cosmo Communicator, Astro Slide
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>

#include "planet_kpd_common.h"

#define MODULE_NAME "planet_kpd"

static int planet_kpd_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	int chip_id;

	chip_id = i2c_smbus_read_byte_data(client, AW9523_REG_ID);
	if (chip_id < 0) {
		dev_err(&client->dev, "failed to read chip ID: %d\n", chip_id);
		return chip_id;
	}
	if (chip_id != AW9523_CHIP_ID) {
		dev_err(&client->dev,
			"unexpected chip ID 0x%02x (expected 0x%02x)\n",
			chip_id, AW9523_CHIP_ID);
		return -ENODEV;
	}

	dev_info(&client->dev, "AW9523B detected (chip ID 0x%02x)\n", chip_id);
	return 0;
}

static int planet_kpd_remove(struct i2c_client *client)
{
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
