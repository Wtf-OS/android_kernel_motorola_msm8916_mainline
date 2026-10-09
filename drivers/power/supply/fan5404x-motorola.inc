/* Copyright (c) 2014 Motorola Mobility LLC. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* Copied Motorola charging helpers; Linux 7 integration is in fan54046-monitor.c. */

/* CONTROL0 Register */
#define REG_CONTROL0         0x00
#define CONTROL0_TMR_RST     BIT(7)
#define CONTROL0_EN_STAT     BIT(6)
#define CONTROL0_STAT        GENMASK(5, 4)
#define CONTROL0_STAT_SHIFT  4
#define STAT_READY           0x00
#define STAT_PWM_ENABLED     0x01
#define STAT_CHARGE_DONE     0x02
#define STAT_FAULT           0x03
#define CONTROL0_BOOST       BIT(3)
#define CONTROL0_FAULT       GENMASK(2, 0)
#define CONTROL0_FAULT_SHIFT 0
#define FAULT_NONE           0x00
#define FAULT_VBUS_OVP       0x01
#define FAULT_SLEEP_MODE     0x02
#define FAULT_POOR_INPUT     0x03
#define FAULT_BATT_OVP       0x04
#define FAULT_THERM_SHUTDOWN 0x05
#define FAULT_TIMER_FAULT    0x06
#define FAULT_NO_BATTERY     0x07

/* CONTROL1 Register */
#define REG_CONTROL1           0x01
#define CONTROL1_IBUSLIM       GENMASK(7, 6)
#define CONTROL1_IBUSLIM_SHIFT 6
#define IBUSLIM_FAN54046_100MA     0x00
#define IBUSLIM_FAN54046_500MA     0x01
#define IBUSLIM_FAN54046_800MA     0x02
#define IBUSLIM_FAN54046_NO_LIMIT  0x03
#define IBUSLIM_FAN54053_500MA     0x00
#define IBUSLIM_FAN54053_800MA     0x01
#define IBUSLIM_FAN54053_1100MA    0x02
#define IBUSLIM_FAN54053_NO_LIMIT  0x03
#define CONTROL1_VLOWV         GENMASK(5, 4)
#define CONTROL1_VLOWV_SHIFT   4
#define VLOWV_3_4V             0
#define VLOWV_3_5V             1
#define VLOWV_3_6V             2
#define VLOWV_3_7V             3
#define CONTROL1_TE            BIT(3)
#define CONTROL1_CE_N          BIT(2)
#define CONTROL1_HZ_MODE       BIT(1)
#define CONTROL1_OPA_MODE      BIT(0)

/* OREG Register */
#define REG_OREG              0x02
#define OREG_OREG             GENMASK(7, 2)
#define OREG_OREG_SHIFT       2
#define OREG_DBAT_B           BIT(1)
#define OREG_EOC              BIT(0)

/* IC INFO Register */
#define REG_IC_INFO           0x03
#define IC_INFO_VENDOR_CODE   GENMASK(7, 6)
#define VENDOR_FAIRCHILD_VAL  0x80
#define IC_INFO_PN            GENMASK(5, 3)
#define IC_INFO_PN_SHIFT      3
#define PN_FAN54040_VAL       0x00
#define PN_FAN54041_VAL       0x08
#define PN_FAN54042_VAL       0x10
#define PN_FAN54045_VAL       0x28
#define PN_FAN54046_VAL       0x30
#define PN_FAN54047_VAL       0x30 /* Spec correct? Same as 54046... */
#define IC_INFO_REV           GENMASK(2, 0)
#define IC_INFO_REV_SHIFT     0

/* IBAT Register */
#define REG_IBAT              0x04
#define IBAT_RESET            BIT(7)
#define IBAT_IOCHARGE         GENMASK(6, 3)
#define IBAT_IOCHARGE_SHIFT   3
#define IBAT_ITERM            GENMASK(2, 0)
#define IBAT_ITERM_SHIFT      0

/* VBUS CONTROL Register */
#define REG_VBUS_CONTROL      0x05
#define VBUS_PROD             BIT(6)
#define VBUS_IO_LEVEL         BIT(5)
#define VBUS_VBUS_CON         BIT(4)
#define VBUS_SP               BIT(3)
#define VBUS_VBUSLIM          GENMASK(2, 0)
#define VBUS_VBUSLIM_SHIFT    0

/* SAFETY Register */
#define REG_SAFETY            0x06
#define SAFETY_ISAFE          GENMASK(7, 4)
#define SAFETY_ISAFE_SHIFT    4
#define SAFETY_VSAFE          GENMASK(3, 0)
#define SAFETY_VSAFE_SHIFT    0

/* POST CHARGING Register */
#define REG_POST_CHARGING     0x07
#define PC_BDET               GENMASK(7, 6)
#define PC_BDET_SHIFT         6
#define PC_VBUS_LOAD          GENMASK(5, 4)
#define PC_VBUS_LOAD_SHIFT    4
#define PC_PC_EN              BIT(3)
#define PC_PC_IT              GENMASK(2, 0)
#define PC_PC_IT_SHIFT        0

/* MONITOR0 Register */
#define REG_MONITOR0          0x10
#define MONITOR0_ITERM_CMP    BIT(7)
#define MONITOR0_VBAT_CMP     BIT(6)
#define MONITOR0_LINCHG       BIT(5)
#define MONITOR0_T_120        BIT(4)
#define MONITOR0_ICHG         BIT(3)
#define MONITOR0_IBUS         BIT(2)
#define MONITOR0_VBUS_VALID   BIT(1)
#define MONITOR0_CV           BIT(0)

/* MONITOR1 Register */
#define REG_MONITOR1          0x11
#define MONITOR1_GATE         BIT(7)
#define MONITOR1_VBAT         BIT(6)
#define MONITOR1_POK_B        BIT(5)
#define MONITOR1_DIS_LEVEL    BIT(4)
#define MONITOR1_NOBAT        BIT(3)
#define MONITOR1_PC_ON        BIT(2)

/* NTC Register */
#define REG_NTC               0x12

/* WD CONTROL Register */
#define REG_WD_CONTROL        0x13
#define WD_CONTROL_EN_VREG    BIT(2)
#define WD_CONTROL_WD_DIS     BIT(1)

/* REG RESTART Register */
#define REG_RESTART           0xFA

/* FAN540XX IC PART NO */
#define FAN54046    0X06
#define FAN54053    0X02


static int __fan5404x_read(struct fan5404x_chg *chip, int reg,
				uint8_t *val)
{
	int ret;

	ret = i2c_smbus_read_byte_data(chip->client, reg);
	if (ret < 0) {
		dev_err(chip->dev,
			"i2c read fail: can't read from %02x: %d\n", reg, ret);
		return ret;
	} else {
		*val = ret;
	}

	return 0;
}

static int __fan5404x_write(struct fan5404x_chg *chip, int reg,
						uint8_t val)
{
	int ret;

	if (chip->factory_mode)
		return 0;

	ret = i2c_smbus_write_byte_data(chip->client, reg, val);
	if (ret < 0) {
		dev_err(chip->dev,
			"i2c write fail: can't write %02x to %02x: %d\n",
			val, reg, ret);
		return ret;
	}

	dev_dbg(chip->dev, "Writing 0x%02x=0x%02x\n", reg, val);
	return 0;
}

static int fan5404x_masked_write(struct fan5404x_chg *chip, int reg,
						uint8_t mask, uint8_t val)
{
	int rc;
	uint8_t temp;

	mutex_lock(&chip->read_write_lock);
	rc = __fan5404x_read(chip, reg, &temp);
	if (rc < 0) {
		dev_err(chip->dev, "read failed: reg=%03X, rc=%d\n", reg, rc);
		goto out;
	}
	temp &= ~mask;
	temp |= val & mask;
	rc = __fan5404x_write(chip, reg, temp);
	if (rc < 0) {
		dev_err(chip->dev,
			"write failed: reg=%03X, rc=%d\n", reg, rc);
	}
out:
	mutex_unlock(&chip->read_write_lock);
	return rc;
}

#define OREG_MIN      3500
#define OREG_STEP_MV  20
#define OREG_STEPS    48
#define OREG_VALUE(s) (OREG_MIN + s*OREG_STEP_MV)
static int fan5404x_set_oreg(struct fan5404x_chg *chip, int value)
{
	int i;
	int rc;

	for (i = OREG_STEPS; i >= 0; i--)
		if (value >= OREG_VALUE(i))
			break;

	if (i < 0)
		return -EINVAL;

	rc = fan5404x_masked_write(chip, REG_OREG, OREG_OREG,
					i << OREG_OREG_SHIFT);
	if (rc) {
		dev_err(chip->dev, "Failed to set OREG_OREG: %d\n", rc);
		return rc;
	}

	return 0;
}

static int ibuslim_fan54046_vals[] = {
	[IBUSLIM_FAN54046_100MA] = 100,
	[IBUSLIM_FAN54046_500MA] = 500,
	[IBUSLIM_FAN54046_800MA] = 800,
	[IBUSLIM_FAN54046_NO_LIMIT] = INT_MAX
};

static int ibuslim_fan54053_vals[] = {
	[IBUSLIM_FAN54053_500MA]   = 500,
	[IBUSLIM_FAN54053_800MA]   = 800,
	[IBUSLIM_FAN54053_1100MA]  = 1100,
	[IBUSLIM_FAN54053_NO_LIMIT] = INT_MAX
};

static int fan5404x_set_ibuslim(struct fan5404x_chg *chip,
				int limit)
{
	int i;
	int rc;

	if (chip->ic_info_pn == FAN54046) {
		for (i = ARRAY_SIZE(ibuslim_fan54046_vals) - 1 -
			     chip->max_rate_cap;
		     i >= 0; i--)
			if (limit >= ibuslim_fan54046_vals[i])
				break;
	} else {
		for (i = ARRAY_SIZE(ibuslim_fan54053_vals) - 1 -
			     chip->max_rate_cap;
		     i >= 0; i--)
			if (limit >= ibuslim_fan54053_vals[i])
				break;
	}

	if (i < 0)
		return -EINVAL;

	rc = fan5404x_masked_write(chip, REG_CONTROL1, CONTROL1_IBUSLIM,
					i << CONTROL1_IBUSLIM_SHIFT);
	if (rc) {
		dev_err(chip->dev, "Failed to set IBUSLIM: %d\n", rc);
		return rc;
	} else if (chip->ic_info_pn == FAN54046) {
		dev_warn(chip->dev, "Set IBUSLIM: %d mA\n",
			 ibuslim_fan54046_vals[i]);
	} else {
		dev_warn(chip->dev, "Set IBUSLIM: %d mA\n",
			 ibuslim_fan54053_vals[i]);
	}

	return 0;
}

#define IBAT_IOCHARGE_MIN     550
#define IBAT_IOCHARGE_STEP_MA 100
#define IBAT_IOCHARGE_STEPS   11
#define IBAT_STEP_CURRENT(s)  (IBAT_IOCHARGE_MIN + s*IBAT_IOCHARGE_STEP_MA)
static int fan5404x_set_iocharge(struct fan5404x_chg *chip,
				int limit)
{
	int i;
	int rc;

	for (i = IBAT_IOCHARGE_STEPS; i >= 0; i--)
		if (limit >= IBAT_STEP_CURRENT(i))
			break;

	if (i < 0)
		return -EINVAL;

	/* Need to keep RESET low... */
	rc = fan5404x_masked_write(chip, REG_IBAT, IBAT_IOCHARGE | IBAT_RESET,
						i << IBAT_IOCHARGE_SHIFT);
	if (rc) {
		dev_err(chip->dev, "Failed to set IOCHARGE: %d\n", rc);
		return rc;
	}

	return 0;
}


static int fan5404x_temp_charging(struct fan5404x_chg *chip, int enable)
{
	int rc = 0;

	pr_debug("%s: charging enable = %d\n", __func__, enable);

	if (enable && (!chip->chg_enabled)) {
		dev_dbg(chip->dev,
			"%s: chg_enabled is %d, not to enable charging\n",
					__func__, chip->chg_enabled);
		return 0;
	}

	rc = fan5404x_masked_write(chip, REG_CONTROL1, CONTROL1_CE_N,
				enable ? 0 : CONTROL1_CE_N);
	if (rc) {
		dev_err(chip->dev, "start-charge: Failed to set CE_N\n");
		return rc;
	}

	chip->charging = enable;

	return 0;
}

static void fan5404x_set_chrg_path_temp(struct fan5404x_chg *chip)
{
	if (chip->demo_mode) {
		fan5404x_set_oreg(chip, 4000);
		fan5404x_temp_charging(chip, 1);
		return;
	} else if ((chip->batt_cool || chip->batt_warm)
		   && !chip->ext_high_temp)
		fan5404x_set_oreg(chip, chip->ext_temp_volt_mv);
	else
		fan5404x_set_oreg(chip, chip->voreg_mv);

	if (chip->ext_high_temp ||
		chip->batt_cold ||
		chip->batt_hot ||
		chip->chg_done_batt_full)
		fan5404x_temp_charging(chip, 0);
	else
		fan5404x_temp_charging(chip, 1);
}

static int fan5404x_hw_init(struct fan5404x_chg *chip)
{
	int rc;

	/* Disable T32 */
	rc = fan5404x_masked_write(chip, REG_WD_CONTROL, WD_CONTROL_WD_DIS,
							WD_CONTROL_WD_DIS);
	if (rc) {
		dev_err(chip->dev, "couldn't disable T32 rc = %d\n", rc);
		return rc;
	}

	return 0;
}
