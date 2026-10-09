// SPDX-License-Identifier: GPL-2.0-only
/* Osprey sensor identification only: no mode writes, no streaming. */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

static bool front_scan = false;
module_param(front_scan, bool, 0444);
MODULE_PARM_DESC(front_scan, "power the front sensor and scan CCI bus addresses");

static int osprey_camera_id_probe(struct i2c_client *client)
{
 struct device *dev = &client->dev;
 bool rear = client->addr == 0x2d, io_on = false, af_on = false, clk_on = false;
 struct gpio_desc *reset, *digital, *analog = NULL;
 struct regulator *io, *af = NULL;
 struct clk *mclk;
 u8 address[2] = {0, 0}, value[2];
 struct i2c_msg messages[] = {
  { .addr = client->addr, .len = sizeof(address), .buf = address },
  { .addr = client->addr, .flags = I2C_M_RD, .len = sizeof(value), .buf = value },
 };
 unsigned int id, expected = rear ? 0x30d2 : 0x5e20;
 int ret;

  reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
 if (IS_ERR(reset)) return dev_err_probe(dev, PTR_ERR(reset), "reset GPIO\n");
 digital = devm_gpiod_get(dev, "digital-enable", GPIOD_OUT_LOW);
 if (IS_ERR(digital)) return dev_err_probe(dev, PTR_ERR(digital), "digital GPIO\n");
 analog = devm_gpiod_get_optional(dev, "analog-enable", GPIOD_OUT_LOW);
 if (IS_ERR(analog)) return dev_err_probe(dev, PTR_ERR(analog), "analog GPIO\n");
 /* NOTE: on osprey the front sensor shares rear VANA (gpio33) and stays
  * silent without it. Only one node may own gpio33, so a stock-faithful
  * tree (front without analog) cannot ID the front sensor. Verified by a
  * scan build with rear disabled: front at 0x10 answered 0x5E20 once
  * gpio33 was driven. Real camera support needs one combined power driver
  * for both sensors instead of per-sensor GPIO claims. */
 if (rear) {
  af = devm_regulator_get(dev, "vaf");
  if (IS_ERR(af)) return dev_err_probe(dev, PTR_ERR(af), "autofocus supply\n");
 }
 io = devm_regulator_get(dev, "vio");
 if (IS_ERR(io)) return dev_err_probe(dev, PTR_ERR(io), "IO supply\n");
 mclk = devm_clk_get(dev, NULL);
 if (IS_ERR(mclk)) return dev_err_probe(dev, PTR_ERR(mclk), "sensor clock\n");
 ret = clk_set_rate(mclk, 23880000);
 if (ret) return dev_err_probe(dev, ret, "clock rate\n");

 /* Sequence recovered from this device's ARM32 sensor libraries;
  * stock DTS runs both sensors at 23.88 MHz, clock before reset. */
 usleep_range(1000, 1500);
 if (!rear)
  gpiod_set_value_cansleep(digital, 1);
 else if (analog)
  gpiod_set_value_cansleep(analog, 1);
 usleep_range(1000, 1500);
 ret = regulator_enable(io);
 if (ret) goto power_down;
 io_on = true;
 usleep_range(1000, 1500);
 if (!rear && analog)
  gpiod_set_value_cansleep(analog, 1);
 else
  gpiod_set_value_cansleep(digital, 1);
 usleep_range(1000, 1500);
 if (rear) {
  ret = regulator_enable(af);
  if (ret) goto power_down;
  af_on = true;
  usleep_range(1000, 1500);
 }
 ret = clk_prepare_enable(mclk);
 if (ret) goto power_down;
 clk_on = true;
 usleep_range(2000, 2500);
 gpiod_set_value_cansleep(reset, 0);
 if (rear)
  usleep_range(1000, 1500);
 else
  msleep(30);
 usleep_range(1000, 1500);
 if (!rear && front_scan) {
  int addr, found = 0;
  for (addr = 0x08; addr <= 0x77; addr++) {
   u8 w[2] = {0, 0}, r[2];
   struct i2c_msg scan[] = {
    { .addr = addr, .len = sizeof(w), .buf = w },
    { .addr = addr, .flags = I2C_M_RD, .len = sizeof(r), .buf = r },
   };
   if (addr == 0x2d)
    continue;
   if (i2c_transfer(client->adapter, scan, ARRAY_SIZE(scan)) == ARRAY_SIZE(scan)) {
    dev_info(dev, "scan: ACK at 0x%02x, bytes 0x%02x%02x\n", addr, r[0], r[1]);
    found++;
   }
  }
  dev_info(dev, "scan done, %d responding addresses\n", found);
  ret = -ENODEV;
  goto power_down;
 }
 ret = i2c_transfer(client->adapter, messages, ARRAY_SIZE(messages));
 if (ret != ARRAY_SIZE(messages)) {
  ret = ret < 0 ? ret : -EIO;
  dev_err(dev, "sensor ID read failed: %d\n", ret);
 } else {
  id = (value[0] << 8) | value[1];
  dev_info(dev, "%s ID 0x%04x (expected 0x%04x), clock %lu Hz\n",
           rear ? "rear S5K3M2" : "front S5K5E2", id, expected, clk_get_rate(mclk));
  ret = id == expected ? 0 : -ENODEV;
 }

power_down:
 if (clk_on) clk_disable_unprepare(mclk);
 if (rear) usleep_range(1000, 1500); else msleep(10);
 gpiod_set_value_cansleep(reset, 1);
 if (rear) usleep_range(1000, 1500);
 gpiod_set_value_cansleep(digital, 0);
 usleep_range(1000, 1500);
 if (analog)
  gpiod_set_value_cansleep(analog, 0);
 if (rear)
  usleep_range(1000, 1500);
 if (io_on) regulator_disable(io);
 usleep_range(1000, 1500);
 if (af_on) regulator_disable(af);
 return ret;
}

static const struct of_device_id osprey_camera_id_of_match[] = {
 { .compatible = "motorola,osprey-camera-id" },
 { }
};
MODULE_DEVICE_TABLE(of, osprey_camera_id_of_match);
static struct i2c_driver osprey_camera_id_driver = {
 .driver = { .name = "osprey-camera-id", .of_match_table = osprey_camera_id_of_match },
 .probe = osprey_camera_id_probe,
};
module_i2c_driver(osprey_camera_id_driver);
MODULE_DESCRIPTION("Motorola Osprey camera sensor read-only ID probe");
MODULE_LICENSE("GPL");
