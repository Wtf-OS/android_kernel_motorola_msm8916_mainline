// SPDX-License-Identifier: GPL-2.0-only
/* ROHM BD65B60; register programming follows Motorola's osprey driver. */
#include <linux/backlight.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

static int bd65b60_update(struct backlight_device *bl)
{
    struct regmap *map = bl_get_data(bl);
    int brightness = backlight_get_brightness(bl);
    int ret;

    /* ILED=0 still supplies 1/256 current: PON must also be cleared. */
    if (!brightness)
        return regmap_write(map, 0x0e, 0);
    /* The panel rail can reset the chip between display modesets. */
    ret = regmap_update_bits(map, 0x07, 0x20, 0x20);
    if (ret)
        return ret;
    ret = regmap_write(map, 0x05, brightness);
    if (ret)
        return ret;
    return regmap_write(map, 0x0e, 1);
}

static const struct backlight_ops bd65b60_ops = {
    .update_status = bd65b60_update,
};

static int bd65b60_probe(struct i2c_client *client)
{
    static const struct regmap_config config = {
        .reg_bits = 8, .val_bits = 8, .max_register = 0x0e,
    };
    struct backlight_properties props = {
        .type = BACKLIGHT_RAW, .max_brightness = 255,
        .brightness = 255, .power = BACKLIGHT_POWER_OFF,
    };
    struct backlight_device *bl;
    struct regmap *map;

    map = devm_regmap_init_i2c(client, &config);
    if (IS_ERR(map))
        return PTR_ERR(map);
    /* GPIO36 also powers the panel: defer I2C until its consumer enables it. */
    bl = devm_backlight_device_register(&client->dev, "osprey-led-backlight",
                                      &client->dev, map, &bd65b60_ops, &props);
    if (IS_ERR(bl))
        return PTR_ERR(bl);
    i2c_set_clientdata(client, bl);
    return 0;
}

static void bd65b60_shutdown(struct i2c_client *client)
{
    backlight_disable(i2c_get_clientdata(client));
}

static const struct of_device_id bd65b60_match[] = {
    { .compatible = "rohm,bd65b60" }, { }
};
MODULE_DEVICE_TABLE(of, bd65b60_match);
static const struct i2c_device_id bd65b60_ids[] = {
    { "bd65b60" }, { }
};
MODULE_DEVICE_TABLE(i2c, bd65b60_ids);
static struct i2c_driver bd65b60_driver = {
    .probe = bd65b60_probe,
    .shutdown = bd65b60_shutdown,
    .id_table = bd65b60_ids,
    .driver = { .name = "bd65b60", .of_match_table = bd65b60_match },
};
module_i2c_driver(bd65b60_driver);
MODULE_DESCRIPTION("ROHM BD65B60 backlight");
MODULE_LICENSE("GPL");
