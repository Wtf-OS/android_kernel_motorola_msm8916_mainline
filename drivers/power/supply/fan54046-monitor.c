// SPDX-License-Identifier: GPL-2.0-only
/* FAN5404x monitoring and optional Motorola 3.10 charging-control port. */
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/pm_wakeup.h>

static bool control_charging;
module_param(control_charging, bool, 0444);
MODULE_PARM_DESC(control_charging, "Enable Motorola charging control for osprey FAN54053");

struct fan5404x_chg {
 struct i2c_client *client;
 struct device *dev;
 struct mutex read_write_lock;
 int ic_info_pn, max_rate_cap, ext_temp_volt_mv, voreg_mv;
 bool factory_mode, chg_enabled, charging, demo_mode;
 bool batt_cool, batt_warm, batt_cold, batt_hot;
 bool ext_high_temp, chg_done_batt_full;
};
#include "fan5404x-motorola.inc"

struct fan_monitor {
 struct i2c_client *client;
 struct power_supply *psy;
 struct delayed_work work;
 int last_online, last_status;
 struct fan5404x_chg charge;
 bool configured, awake;
};

/* The legacy ADC/health notifier is replaced by MAX17050 properties.
 * Keep the Motorola osprey thresholds: cold -20 C, cool 0 C,
 * warm 45 C, hot 60 C; warm/cool voltage 4.20 V, normal 4.35 V
 * (the original helper rounds normal voltage down to 4.34 V).
 */
static int fan_control(struct fan_monitor *fan, int online)
{
 struct fan5404x_chg *chip = &fan->charge;
 struct power_supply *battery;
 union power_supply_propval present, temp, voltage;
 int ret, control;
 if (!control_charging) return 0;
 if (!online) {
  fan->configured = false;
  fan5404x_temp_charging(chip, 0);
  if (fan->awake) { pm_relax(chip->dev); fan->awake = false; }
  return 0;
 }
 /* Keep temperature checks running until a proper ADC wakeup IRQ port exists. */
 if (!fan->awake) { pm_stay_awake(chip->dev); fan->awake = true; }
 battery = power_supply_get_by_name("max170xx_battery");
 if (!battery) return fan5404x_temp_charging(chip, 0);
 ret = power_supply_get_property(battery, POWER_SUPPLY_PROP_PRESENT, &present);
 if (!ret) ret = power_supply_get_property(battery, POWER_SUPPLY_PROP_TEMP, &temp);
 if (!ret) ret = power_supply_get_property(battery, POWER_SUPPLY_PROP_VOLTAGE_NOW, &voltage);
 power_supply_put(battery);
 if (ret || !present.intval) {
  fan5404x_temp_charging(chip, 0);
  return ret ? ret : -ENODEV;
 }
 chip->batt_cold = temp.intval <= -200;
 chip->batt_hot = temp.intval >= 600;
 chip->batt_cool = !chip->batt_cold && temp.intval <= 0;
 chip->batt_warm = !chip->batt_hot && temp.intval >= 450;
 chip->ext_high_temp = (chip->batt_cool || chip->batt_warm) &&
                       voltage.intval > chip->ext_temp_volt_mv * 1000;
 if (chip->batt_cold || chip->batt_hot || chip->ext_high_temp)
  return fan5404x_temp_charging(chip, 0);
 control = i2c_smbus_read_byte_data(fan->client, REG_CONTROL1);
 if (control < 0) return control;
 if (control & CONTROL1_OPA_MODE) return -EBUSY;
 if (!fan->configured) {
  /* Same sequence as Motorola start_charging(), with USB capped at 500 mA. */
  ret = fan5404x_temp_charging(chip, 0);
  if (!ret) ret = fan5404x_masked_write(chip, REG_CONTROL0, CONTROL0_TMR_RST, CONTROL0_TMR_RST);
  if (!ret) ret = fan5404x_set_ibuslim(chip, 500);
  if (!ret) ret = fan5404x_set_iocharge(chip, 1450);
  if (!ret) ret = fan5404x_masked_write(chip, REG_VBUS_CONTROL, VBUS_IO_LEVEL, 0);
  if (!ret) ret = fan5404x_set_oreg(chip, (chip->batt_cool || chip->batt_warm) ? chip->ext_temp_volt_mv : chip->voreg_mv);
  if (!ret) ret = fan5404x_hw_init(chip);
  /* Mainline lacks Motorola's TAPER_REACHED property; use hardware termination. */
  if (!ret) ret = fan5404x_masked_write(chip, REG_CONTROL1, CONTROL1_TE, CONTROL1_TE);
  if (ret) { fan5404x_temp_charging(chip, 0); return ret; }
  fan->configured = true;
  dev_info(chip->dev, "Motorola charging initialized: USB 500 mA, target %d mV\n", chip->voreg_mv);
 }
 fan5404x_set_chrg_path_temp(chip);
 return 0;
}
static int fan_read_status(struct fan_monitor *fan, int *online, int *status)
{
 int monitor = i2c_smbus_read_byte_data(fan->client, 0x10);
 int control;
 if (monitor < 0) return monitor;
 control = i2c_smbus_read_byte_data(fan->client, 0x00);
 if (control < 0) return control;
 *online = !!(monitor & BIT(1));
 if (!*online) *status = POWER_SUPPLY_STATUS_DISCHARGING;
 else switch ((control >> 4) & 3) {
 case 1: *status = POWER_SUPPLY_STATUS_CHARGING; break;
 case 2: *status = POWER_SUPPLY_STATUS_FULL; break;
 default: *status = POWER_SUPPLY_STATUS_NOT_CHARGING; break;
 }
 return 0;
}
static int fan_get_property(struct power_supply *psy, enum power_supply_property prop,
 union power_supply_propval *val)
{
 struct fan_monitor *fan = power_supply_get_drvdata(psy);
 int online, status, ret = fan_read_status(fan, &online, &status);
 if (ret) return ret;
 switch (prop) {
 case POWER_SUPPLY_PROP_ONLINE: val->intval = online; return 0;
 case POWER_SUPPLY_PROP_STATUS: val->intval = status; return 0;
 default: return -EINVAL;
 }
}
static enum power_supply_property fan_props[] = {
 POWER_SUPPLY_PROP_ONLINE, POWER_SUPPLY_PROP_STATUS,
};
static const struct power_supply_desc fan_desc = {
 .name = "fan54046_usb", .type = POWER_SUPPLY_TYPE_USB,
 .properties = fan_props, .num_properties = ARRAY_SIZE(fan_props),
 .get_property = fan_get_property,
};
static void fan_poll(struct work_struct *work)
{
 struct fan_monitor *fan = container_of(to_delayed_work(work), struct fan_monitor, work);
 int online, status, ret;
 ret = fan_read_status(fan, &online, &status);
 if (!ret) ret = fan_control(fan, online);
 if (ret) {
  if (control_charging) fan5404x_temp_charging(&fan->charge, 0);
  dev_err_ratelimited(&fan->client->dev, "charging update failed: %d\n", ret);
 }
 if (!fan_read_status(fan, &online, &status) &&
     (online != fan->last_online || status != fan->last_status)) {
  fan->last_online = online; fan->last_status = status;
  power_supply_changed(fan->psy);
 }
 schedule_delayed_work(&fan->work, msecs_to_jiffies(5000));
}
static void fan_cancel(void *data)
{
 struct fan_monitor *fan = data;
 cancel_delayed_work_sync(&fan->work);
 if (control_charging) {
  fan5404x_temp_charging(&fan->charge, 0);
  if (fan->awake) pm_relax(fan->charge.dev);
  device_init_wakeup(fan->charge.dev, false);
 }
}
static char *fan_supplied_to[] = { "max170xx_battery" };
static int fan_probe(struct i2c_client *client)
{
 struct fan_monitor *fan;
 struct power_supply_config cfg = {};
 int id, ret;
 if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) return -EOPNOTSUPP;
 id = i2c_smbus_read_byte_data(client, 0x03);
 if (id < 0) return id;
 /* Osprey variants use FAN54046 (PN 6) or FAN54053 (PN 2).
  * Both share the status and VBUS monitor registers used here. */
 if ((id & 0xf8) != 0xb0 && (id & 0xf8) != 0x90)
  return dev_err_probe(&client->dev, -ENODEV, "Unsupported FAN5404x ID: %#x\n", id);
 if (control_charging && (id & 0xf8) != 0x90)
  return dev_err_probe(&client->dev, -ENODEV, "Charging control is only validated for osprey FAN54053\n");
 fan = devm_kzalloc(&client->dev, sizeof(*fan), GFP_KERNEL);
 if (!fan) return -ENOMEM;
 fan->client = client; fan->last_online = fan->last_status = -1;
 fan->charge.client = client; fan->charge.dev = &client->dev;
 fan->charge.ic_info_pn = FAN54053; fan->charge.chg_enabled = true;
 fan->charge.voreg_mv = 4350; fan->charge.ext_temp_volt_mv = 4200;
 mutex_init(&fan->charge.read_write_lock);
 if (control_charging) {
  ret = device_init_wakeup(&client->dev, true);
  if (ret) return ret;
 }
 cfg.drv_data = fan; cfg.fwnode = dev_fwnode(&client->dev);
 cfg.supplied_to = fan_supplied_to; cfg.num_supplicants = ARRAY_SIZE(fan_supplied_to);
 fan->psy = devm_power_supply_register(&client->dev, &fan_desc, &cfg);
 if (IS_ERR(fan->psy)) {
  if (control_charging) device_init_wakeup(&client->dev, false);
  return PTR_ERR(fan->psy);
 }
 INIT_DELAYED_WORK(&fan->work, fan_poll);
 ret = devm_add_action_or_reset(&client->dev, fan_cancel, fan);
 if (ret) return ret;
 schedule_delayed_work(&fan->work, 0);
 dev_info(&client->dev, "FAN5404x ID %#x; Motorola charging control=%d\n", id, control_charging);
 return 0;
}
static const struct of_device_id fan_of[] = {
 { .compatible = "fairchild,fan54046-monitor" }, {},
};
MODULE_DEVICE_TABLE(of, fan_of);
static const struct i2c_device_id fan_ids[] = { { "fan54046-monitor", 0 }, {} };
MODULE_DEVICE_TABLE(i2c, fan_ids);
static struct i2c_driver fan_driver = {
 .driver = { .name = "fan54046-monitor", .of_match_table = fan_of },
 .probe = fan_probe, .id_table = fan_ids,
};
module_i2c_driver(fan_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FAN5404x monitor with optional Motorola osprey charging control");
