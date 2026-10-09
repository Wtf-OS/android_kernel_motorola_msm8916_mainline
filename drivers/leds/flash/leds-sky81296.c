// SPDX-License-Identifier: GPL-2.0-only
/* Osprey SKY81296; register sequence from Motorola's camera_v2 driver. */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/led-class-flash.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>

struct sky81296 {
	struct led_classdev_flash flash;
	struct regmap *map;
	struct gpio_desc *enable;
	struct mutex lock;
};

static int sky_torch(struct led_classdev *led, enum led_brightness level)
{
	struct sky81296 *sky = container_of(led, struct sky81296, flash.led_cdev);
	int ret;

	mutex_lock(&sky->lock);
	ret = regmap_write(sky->map, 0x04, 0);
	if (!ret && level) {
		/* 25, 50, or 75 mA per LED; at most 150 mA total. */
		ret = regmap_write(sky->map, 0x03, (level - 1) * 0x11);
		if (!ret)
			ret = regmap_write(sky->map, 0x04, 0x11);
	}
	mutex_unlock(&sky->lock);
	return ret;
}

static int sky_strobe(struct led_classdev_flash *flash, bool state)
{
	struct sky81296 *sky = container_of(flash, struct sky81296, flash);
	int ret;

	mutex_lock(&sky->lock);
	/* Hardware timeout is 760 ms. Never leave high-current mode latched. */
	ret = regmap_write(sky->map, 0x04, state ? 0x22 : 0);
	mutex_unlock(&sky->lock);
	return ret;
}

static int sky_strobe_get(struct led_classdev_flash *flash, bool *state)
{
	struct sky81296 *sky = container_of(flash, struct sky81296, flash);
	unsigned int value;
	int ret = regmap_read(sky->map, 0x04, &value);

	if (!ret)
		*state = (value & 0x22) != 0;
	return ret;
}

static const struct led_flash_ops sky_ops = {
	.strobe_set = sky_strobe,
	.strobe_get = sky_strobe_get,
};

static void sky_disable(void *data)
{
	struct sky81296 *sky = data;
	regmap_write(sky->map, 0x04, 0);
	gpiod_set_value_cansleep(sky->enable, 0);
}

static int sky_probe(struct i2c_client *client)
{
	static const struct regmap_config config = {
		.reg_bits = 8, .val_bits = 8, .max_register = 0x08,
	};
	static const struct reg_sequence init[] = {
		{ 0x00, 0x01 }, { 0x01, 0x01 }, /* 300 mA each, below stock 750 */
		{ 0x02, 0x88 }, /* stock hardware timeout: 760 ms */
		{ 0x03, 0x11 }, { 0x04, 0x00 }, { 0x05, 0x01 },
		{ 0x06, 0x21 }, { 0x07, 0x11 }, { 0x08, 0x11 },
	};
	struct sky81296 *sky;
	int ret;

	sky = devm_kzalloc(&client->dev, sizeof(*sky), GFP_KERNEL);
	if (!sky)
		return -ENOMEM;
	mutex_init(&sky->lock);
	sky->map = devm_regmap_init_i2c(client, &config);
	if (IS_ERR(sky->map))
		return PTR_ERR(sky->map);
	sky->enable = devm_gpiod_get(&client->dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(sky->enable))
		return dev_err_probe(&client->dev, PTR_ERR(sky->enable), "enable GPIO\n");
	ret = devm_add_action_or_reset(&client->dev, sky_disable, sky);
	if (ret)
		return ret;
	usleep_range(1000, 2000);
	ret = regmap_multi_reg_write(sky->map, init, ARRAY_SIZE(init));
	if (ret)
		return dev_err_probe(&client->dev, ret, "initialization\n");
	sky->flash.led_cdev.name = "osprey:flash";
	sky->flash.led_cdev.max_brightness = 3;
	sky->flash.led_cdev.brightness_set_blocking = sky_torch;
	sky->flash.led_cdev.flags = LED_DEV_CAP_FLASH;
	sky->flash.ops = &sky_ops;
	sky->flash.brightness = (struct led_flash_setting) {
		.min = 600000, .max = 600000, .step = 1, .val = 600000,
	};
	sky->flash.timeout = (struct led_flash_setting) {
		.min = 760000, .max = 760000, .step = 1, .val = 760000,
	};
	return devm_led_classdev_flash_register(&client->dev, &sky->flash);
}

static const struct of_device_id sky_match[] = {
	{ .compatible = "skyworks,sky81296" }, { }
};
MODULE_DEVICE_TABLE(of, sky_match);
static struct i2c_driver sky_driver = {
	.driver = { .name = "sky81296", .of_match_table = sky_match },
	.probe = sky_probe,
};
module_i2c_driver(sky_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Motorola Osprey SKY81296 torch and flash");
