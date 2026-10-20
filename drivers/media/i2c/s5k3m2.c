// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Linaro Ltd

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/units.h>
#include <media/v4l2-cci.h>
#include "s5k3m2-stock.h"
/* Stock 1280x720 tables (libmmcamera offset 0x7fe8). */
static const struct cci_reg_sequence s5k3m2_stock_1280x720[] = {
	{ CCI_REG16(0x6028), 0x2000 },
	{ CCI_REG16(0x602a), 0x14f0 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6028), 0x4000 },
	{ CCI_REG16(0x0344), 0x00bc },
	{ CCI_REG16(0x0346), 0x01dc },
	{ CCI_REG16(0x0348), 0x0fc3 },
	{ CCI_REG16(0x034a), 0x0a63 },
	{ CCI_REG16(0x034c), 0x0500 },
	{ CCI_REG16(0x034e), 0x02d0 },
	{ CCI_REG16(0x0900), 0x0113 },
	{ CCI_REG16(0x0380), 0x0001 },
	{ CCI_REG16(0x0382), 0x0001 },
	{ CCI_REG16(0x0384), 0x0001 },
	{ CCI_REG16(0x0386), 0x0005 },
	{ CCI_REG16(0x0400), 0x0001 },
	{ CCI_REG16(0x0404), 0x0030 },
	{ CCI_REG16(0x0114), 0x0300 },
	{ CCI_REG16(0x0110), 0x0002 },
	{ CCI_REG16(0x112c), 0x0000 },
	{ CCI_REG16(0x112e), 0x0000 },
	{ CCI_REG16(0x0136), 0x17e1 },
	{ CCI_REG16(0x0304), 0x0006 },
	{ CCI_REG16(0x0306), 0x006f },
	{ CCI_REG16(0x0302), 0x0001 },
	{ CCI_REG16(0x0300), 0x0004 },
	{ CCI_REG16(0x030c), 0x0003 },
	{ CCI_REG16(0x030e), 0x0044 },
	{ CCI_REG16(0x030a), 0x0001 },
	{ CCI_REG16(0x0308), 0x0008 },
	{ CCI_REG16(0x0342), 0x11f0 },
	{ CCI_REG16(0x0340), 0x0321 },
	{ CCI_REG16(0x0202), 0x0200 },
	{ CCI_REG16(0x0200), 0x0400 },
	{ CCI_REG16(0x0b04), 0x0101 },
	{ CCI_REG16(0x0b08), 0x0000 },
	{ CCI_REG16(0x602a), 0x3058 },
	{ CCI_REG16(0x6f12), 0x0800 },
	{ CCI_REG16(0x0b00), 0x0007 },
	{ CCI_REG16(0x3b3c), 0x0107 },
	{ CCI_REG16(0x3b34), 0x3030 },
	{ CCI_REG16(0x3b36), 0x3030 },
	{ CCI_REG16(0x3b38), 0x3030 },
	{ CCI_REG16(0x3b3a), 0x3030 },
	{ CCI_REG16(0x306a), 0x0068 },
	{ CCI_REG16(0x602a), 0x0216 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x70dc), 0x0000 },
};

/* Stock SRAM patch (libmmcamera offset 0x8318), applied after init. */

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

/* Stock PLL rate is estimated; the real value is found by sweeping
 * the link frequency from userspace (camcap link index). */
#define S5K3M2_LINK_FREQ_400MHZ	(400000ULL * HZ_PER_KHZ)
#define S5K3M2_LINK_FREQ_450MHZ	(450000ULL * HZ_PER_KHZ)
#define S5K3M2_LINK_FREQ_487MHZ	(487000ULL * HZ_PER_KHZ)
#define S5K3M2_LINK_FREQ_550MHZ	(550000ULL * HZ_PER_KHZ)
#define S5K3M2_LINK_FREQ_600MHZ	(600000ULL * HZ_PER_KHZ)
#define S5K3M2_MCLK_FREQ_24MHZ		(24 * HZ_PER_MHZ)
#define S5K3M2_DATA_LANES		4

/* Register map is similar to MIPI CCS compliant camera sensors */
#define S5K3M2_REG_CHIP_ID		CCI_REG16(0x0000)
#define S5K3M2_CHIP_ID			0x30d2

/* Both streaming and flipping settings are controlled at the same time */
#define S5K3M2_REG_CTRL_MODE		CCI_REG16(0x0100)
#define S5K3M2_MODE_STREAMING		BIT(8)
#define S5K3M2_VFLIP			BIT(1)
#define S5K3M2_HFLIP			BIT(0)

#define S5K3M2_REG_EXPOSURE		CCI_REG16(0x0202)
#define S5K3M2_EXPOSURE_MIN		8
#define S5K3M2_EXPOSURE_STEP		1
#define S5K3M2_EXPOSURE_MARGIN		4

#define S5K3M2_REG_AGAIN		CCI_REG16(0x0204)
#define S5K3M2_AGAIN_MIN		1
#define S5K3M2_AGAIN_MAX		16
#define S5K3M2_AGAIN_STEP		1
#define S5K3M2_AGAIN_DEFAULT		1
#define S5K3M2_AGAIN_SHIFT		5

#define S5K3M2_REG_VTS			CCI_REG16(0x0340)
#define S5K3M2_VTS_MAX			0xffff

#define S5K3M2_REG_HTS			CCI_REG16(0x0342)
#define S5K3M2_REG_X_ADDR_START		CCI_REG16(0x0344)
#define S5K3M2_REG_Y_ADDR_START		CCI_REG16(0x0346)
#define S5K3M2_REG_X_ADDR_END		CCI_REG16(0x0348)
#define S5K3M2_REG_Y_ADDR_END		CCI_REG16(0x034a)
#define S5K3M2_REG_X_OUTPUT_SIZE	CCI_REG16(0x034c)
#define S5K3M2_REG_Y_OUTPUT_SIZE	CCI_REG16(0x034e)

#define S5K3M2_REG_TEST_PATTERN		CCI_REG16(0x0600)

#define to_s5k3m2(_sd)			container_of(_sd, struct s5k3m2, sd)

static const s64 s5k3m2_link_freq_menu[] = {
	S5K3M2_LINK_FREQ_550MHZ,
	S5K3M2_LINK_FREQ_400MHZ,
	S5K3M2_LINK_FREQ_450MHZ,
	S5K3M2_LINK_FREQ_487MHZ,
	S5K3M2_LINK_FREQ_600MHZ,
};

/* List of supported formats to cover horizontal and vertical flip controls */
static const u32 s5k3m2_mbus_formats[] = {
	MEDIA_BUS_FMT_SGRBG10_1X10,	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10,	MEDIA_BUS_FMT_SGBRG10_1X10,
};

struct s5k3m2_reg_list {
	const struct cci_reg_sequence *regs;
	unsigned int num_regs;
};

struct s5k3m2_mode {
	u32 width;			/* Frame width in pixels */
	u32 height;			/* Frame height in pixels */
	u32 hts;			/* Horizontal timing size */
	u32 vts;			/* Default vertical timing size */
	u32 exposure;			/* Default exposure value */

	const struct s5k3m2_reg_list reg_list;	/* Sensor register setting */
};

static const char * const s5k3m2_test_pattern_menu[] = {
	"Disabled",
	"Solid colour",
	"Colour bars",
	"Fade to grey colour bars",
	"PN9",
};

/* Osprey powers AVDD/DVDD through GPIO-controlled rails, so only the
 * PM8916 regulators are modelled here. Shared VANA (gpio33) also feeds
 * the front sensor; see the camera-id probe notes. */
static const char * const s5k3m2_supply_names[] = {
	"vaf",	/* Autofocus power (pm8916_l17) */
	"vio",	/* Digital I/O power (pm8916_l10) */
};

#define S5K3M2_NUM_SUPPLIES	ARRAY_SIZE(s5k3m2_supply_names)

struct s5k3m2 {
	struct device *dev;
	struct regmap *regmap;
	struct clk *mclk;
	struct gpio_desc *reset_gpio;
	struct regulator *analog_supply;
	struct gpio_desc *digital_gpio;
	struct regulator_bulk_data supplies[S5K3M2_NUM_SUPPLIES];

	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;

	const struct s5k3m2_mode *mode;
	bool initialized;
};



static const struct cci_reg_sequence s5k3m2_4208x3120_30fps_mode[] = {
	/* Stock 4160x3120 tables; only this mode is validated. */
#include "s5k3m2-stock-mode.inc"
};


/* Stock 1920x1080 mode with a 16-pixel Bayer guard, same clocks. */
static const struct cci_reg_sequence s5k3m2_stock_1936x1080[] = {
	{ CCI_REG16(0x6028), 0x2000 },
	{ CCI_REG16(0x602a), 0x14f0 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6028), 0x4000 },
	{ CCI_REG16(0x0344), 0x00ac },
	{ CCI_REG16(0x0346), 0x01e0 },
	{ CCI_REG16(0x0348), 0x0fd3 },
	{ CCI_REG16(0x034a), 0x0a5f },
	{ CCI_REG16(0x034c), 0x0790 },
	{ CCI_REG16(0x034e), 0x0438 },
	{ CCI_REG16(0x0900), 0x0112 },
	{ CCI_REG16(0x0380), 0x0001 },
	{ CCI_REG16(0x0382), 0x0001 },
	{ CCI_REG16(0x0384), 0x0001 },
	{ CCI_REG16(0x0386), 0x0003 },
	{ CCI_REG16(0x0400), 0x0001 },
	{ CCI_REG16(0x0404), 0x0020 },
	{ CCI_REG16(0x0114), 0x0300 },
	{ CCI_REG16(0x0110), 0x0002 },
	{ CCI_REG16(0x112c), 0x0000 },
	{ CCI_REG16(0x112e), 0x0000 },
	{ CCI_REG16(0x0136), 0x17e1 },
	{ CCI_REG16(0x0304), 0x0006 },
	{ CCI_REG16(0x0306), 0x006f },
	{ CCI_REG16(0x0302), 0x0001 },
	{ CCI_REG16(0x0300), 0x0004 },
	{ CCI_REG16(0x030c), 0x0003 },
	{ CCI_REG16(0x030e), 0x0044 },
	{ CCI_REG16(0x030a), 0x0001 },
	{ CCI_REG16(0x0308), 0x0008 },
	{ CCI_REG16(0x0342), 0x11f0 },
	{ CCI_REG16(0x0340), 0x0642 },
	{ CCI_REG16(0x0202), 0x0200 },
	{ CCI_REG16(0x0200), 0x0400 },
	{ CCI_REG16(0x0b04), 0x0101 },
	{ CCI_REG16(0x0b08), 0x0000 },
	{ CCI_REG16(0x602a), 0x3058 },
	{ CCI_REG16(0x6f12), 0x0800 },
	{ CCI_REG16(0x0b00), 0x0007 },
	{ CCI_REG16(0x3b3c), 0x0107 },
	{ CCI_REG16(0x3b34), 0x3030 },
	{ CCI_REG16(0x3b36), 0x3030 },
	{ CCI_REG16(0x3b38), 0x3030 },
	{ CCI_REG16(0x3b3a), 0x3030 },
	{ CCI_REG16(0x306a), 0x0068 },
	{ CCI_REG16(0x602a), 0x0216 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x70dc), 0x0000 },
};

/* Stock 1280x720 mode with a 16-pixel Bayer guard, same clocks. */
static const struct cci_reg_sequence s5k3m2_stock_1296x720[] = {
	{ CCI_REG16(0x6028), 0x2000 },
	{ CCI_REG16(0x602a), 0x14f0 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6028), 0x4000 },
	{ CCI_REG16(0x0344), 0x00a4 },
	{ CCI_REG16(0x0346), 0x01dc },
	{ CCI_REG16(0x0348), 0x0fdb },
	{ CCI_REG16(0x034a), 0x0a63 },
	{ CCI_REG16(0x034c), 0x0510 },
	{ CCI_REG16(0x034e), 0x02d0 },
	{ CCI_REG16(0x0900), 0x0113 },
	{ CCI_REG16(0x0380), 0x0001 },
	{ CCI_REG16(0x0382), 0x0001 },
	{ CCI_REG16(0x0384), 0x0001 },
	{ CCI_REG16(0x0386), 0x0005 },
	{ CCI_REG16(0x0400), 0x0001 },
	{ CCI_REG16(0x0404), 0x0030 },
	{ CCI_REG16(0x0114), 0x0300 },
	{ CCI_REG16(0x0110), 0x0002 },
	{ CCI_REG16(0x112c), 0x0000 },
	{ CCI_REG16(0x112e), 0x0000 },
	{ CCI_REG16(0x0136), 0x17e1 },
	{ CCI_REG16(0x0304), 0x0006 },
	{ CCI_REG16(0x0306), 0x006f },
	{ CCI_REG16(0x0302), 0x0001 },
	{ CCI_REG16(0x0300), 0x0004 },
	{ CCI_REG16(0x030c), 0x0003 },
	{ CCI_REG16(0x030e), 0x0044 },
	{ CCI_REG16(0x030a), 0x0001 },
	{ CCI_REG16(0x0308), 0x0008 },
	{ CCI_REG16(0x0342), 0x11f0 },
	{ CCI_REG16(0x0340), 0x0321 },
	{ CCI_REG16(0x0202), 0x0200 },
	{ CCI_REG16(0x0200), 0x0400 },
	{ CCI_REG16(0x0b04), 0x0101 },
	{ CCI_REG16(0x0b08), 0x0000 },
	{ CCI_REG16(0x602a), 0x3058 },
	{ CCI_REG16(0x6f12), 0x0800 },
	{ CCI_REG16(0x0b00), 0x0007 },
	{ CCI_REG16(0x3b3c), 0x0107 },
	{ CCI_REG16(0x3b34), 0x3030 },
	{ CCI_REG16(0x3b36), 0x3030 },
	{ CCI_REG16(0x3b38), 0x3030 },
	{ CCI_REG16(0x3b3a), 0x3030 },
	{ CCI_REG16(0x306a), 0x0068 },
	{ CCI_REG16(0x602a), 0x0216 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x70dc), 0x0000 },
};

static const struct s5k3m2_mode s5k3m2_supported_modes[] = {
	{
		.width = 4160,
		.height = 3120,
		.hts = 0x11f0,
		// Keep the minimum period below 1/30 s for legacy video.
		.vts = 0x0c76,
		.exposure = 256,
		.reg_list = {
			.regs = s5k3m2_4208x3120_30fps_mode,
			.num_regs = ARRAY_SIZE(s5k3m2_4208x3120_30fps_mode),
		},
	},
	{
		.width = 1280,
		.height = 720,
		.hts = 0x11f0,
		// Allow normal 30fps exposure instead of the stock 120fps ceiling.
		// Keep the minimum period below 1/30 s for legacy video.
		.vts = 0x0c76,
		.exposure = 256,
		.reg_list = {
			.regs = s5k3m2_stock_1280x720,
			.num_regs = ARRAY_SIZE(s5k3m2_stock_1280x720),
		},
	},
	{
		.width = 1936,
		.height = 1080,
		.hts = 0x11f0,
		.vts = 0x0c76,
		.exposure = 256,
		.reg_list = {
			.regs = s5k3m2_stock_1936x1080,
			.num_regs = ARRAY_SIZE(s5k3m2_stock_1936x1080),
		},
	},
	{
		.width = 1296,
		.height = 720,
		.hts = 0x11f0,
		.vts = 0x0c76,
		.exposure = 256,
		.reg_list = {
			.regs = s5k3m2_stock_1296x720,
			.num_regs = ARRAY_SIZE(s5k3m2_stock_1296x720),
		},
	},
};

static int s5k3m2_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5k3m2 *s5k3m2 = container_of(ctrl->handler, struct s5k3m2,
					     ctrl_handler);
	const struct s5k3m2_mode *mode = s5k3m2->mode;
	s64 exposure_max;
	int ret;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		/* The orientation settings are applied along with streaming */
		return 0;
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		exposure_max = mode->height + ctrl->val - S5K3M2_EXPOSURE_MARGIN;
		__v4l2_ctrl_modify_range(s5k3m2->exposure,
					 s5k3m2->exposure->minimum,
					 exposure_max,
					 s5k3m2->exposure->step,
					 s5k3m2->exposure->default_value);
		break;
	}

	/* V4L2 controls are applied, when sensor is powered up for streaming */
	if (!pm_runtime_get_if_active(s5k3m2->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(s5k3m2->regmap, S5K3M2_REG_AGAIN,
				ctrl->val << S5K3M2_AGAIN_SHIFT, NULL);
		break;
	case V4L2_CID_EXPOSURE:
		ret = cci_write(s5k3m2->regmap, S5K3M2_REG_EXPOSURE,
				ctrl->val, NULL);
		break;
	case V4L2_CID_VBLANK:
		ret = cci_write(s5k3m2->regmap, S5K3M2_REG_VTS,
				ctrl->val + mode->height, NULL);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = cci_write(s5k3m2->regmap, S5K3M2_REG_TEST_PATTERN,
				ctrl->val, NULL);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(s5k3m2->dev);

	return ret;
}

static const struct v4l2_ctrl_ops s5k3m2_ctrl_ops = {
	.s_ctrl = s5k3m2_set_ctrl,
};

static inline u64 s5k3m2_freq_to_pixel_rate(const u64 freq)
{
	return div_u64(freq * 2 * S5K3M2_DATA_LANES, 10);
}

static int s5k3m2_init_controls(struct s5k3m2 *s5k3m2)
{
	struct v4l2_ctrl_handler *ctrl_hdlr = &s5k3m2->ctrl_handler;
	const struct s5k3m2_mode *mode = s5k3m2->mode;
	s64 pixel_rate, hblank, vblank, exposure_max;
	struct v4l2_fwnode_device_properties props;
	int ret;

	v4l2_ctrl_handler_init(ctrl_hdlr, 9);

	s5k3m2->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &s5k3m2_ctrl_ops,
					V4L2_CID_LINK_FREQ,
					ARRAY_SIZE(s5k3m2_link_freq_menu) - 1,
					0, s5k3m2_link_freq_menu);
	if (s5k3m2->link_freq)
		s5k3m2->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate = s5k3m2_freq_to_pixel_rate(s5k3m2_link_freq_menu[0]);
	s5k3m2->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &s5k3m2_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       0, pixel_rate, 1, pixel_rate);

	hblank = mode->hts - mode->width;
	s5k3m2->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &s5k3m2_ctrl_ops,
					   V4L2_CID_HBLANK, hblank,
					   hblank, 1, hblank);
	if (s5k3m2->hblank)
		s5k3m2->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank = mode->vts - mode->height;
	s5k3m2->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &s5k3m2_ctrl_ops,
					   V4L2_CID_VBLANK, vblank,
					   S5K3M2_VTS_MAX - mode->height, 1,
					   vblank);

	v4l2_ctrl_new_std(ctrl_hdlr, &s5k3m2_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  S5K3M2_AGAIN_MIN, S5K3M2_AGAIN_MAX,
			  S5K3M2_AGAIN_STEP, S5K3M2_AGAIN_DEFAULT);

	exposure_max = mode->vts - S5K3M2_EXPOSURE_MARGIN;
	s5k3m2->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &s5k3m2_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     S5K3M2_EXPOSURE_MIN,
					     exposure_max,
					     S5K3M2_EXPOSURE_STEP,
					     mode->exposure);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &s5k3m2_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(s5k3m2_test_pattern_menu) - 1,
				     0, 0, s5k3m2_test_pattern_menu);

	s5k3m2->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &s5k3m2_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	if (s5k3m2->hflip)
		s5k3m2->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	s5k3m2->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &s5k3m2_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (s5k3m2->vflip)
		s5k3m2->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	ret = v4l2_fwnode_device_parse(s5k3m2->dev, &props);
	if (ret)
		goto error_free_hdlr;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &s5k3m2_ctrl_ops,
					      &props);
	if (ret)
		goto error_free_hdlr;

	s5k3m2->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error_free_hdlr:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static int s5k3m2_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct s5k3m2 *s5k3m2 = to_s5k3m2(sd);
	const struct s5k3m2_reg_list *reg_list = &s5k3m2->mode->reg_list;
	int ret;

	ret = pm_runtime_resume_and_get(s5k3m2->dev);
	if (ret)
		return ret;

	/* Firmware survives stream-off but not the reset in power_off(). */
	if (!s5k3m2->initialized) {
		cci_multi_reg_write(s5k3m2->regmap, s5k3m2_stock_init,
				    ARRAY_SIZE(s5k3m2_stock_init), &ret);
		if (ret)
			goto error;
		/* Stock init descriptor requests 10 ms settling. */
		msleep(10);
		s5k3m2->initialized = true;
	}
	cci_multi_reg_write(s5k3m2->regmap, reg_list->regs,
			    reg_list->num_regs, &ret);
	if (ret)
		goto error;

	/* Stock mode descriptor requests 10 ms settling. */
	msleep(10);
	ret = __v4l2_ctrl_handler_setup(s5k3m2->sd.ctrl_handler);
	if (ret)
		goto error;

	cci_write(s5k3m2->regmap, S5K3M2_REG_CTRL_MODE,
		  S5K3M2_MODE_STREAMING |
		  (s5k3m2->vflip->val ? S5K3M2_VFLIP : 0) |
		  (s5k3m2->hflip->val ? S5K3M2_HFLIP : 0), &ret);
	if (ret)
		goto error;

	return 0;

error:
	s5k3m2->initialized = false;
	dev_err(s5k3m2->dev, "failed to start streaming: %d\n", ret);
	pm_runtime_put_autosuspend(s5k3m2->dev);

	return ret;
}

static int s5k3m2_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct s5k3m2 *s5k3m2 = to_s5k3m2(sd);
	int ret;

	ret = cci_write(s5k3m2->regmap, S5K3M2_REG_CTRL_MODE, 0, NULL);
	if (ret) {
		s5k3m2->initialized = false;
		dev_err(s5k3m2->dev, "failed to stop streaming: %d\n", ret);
	}

	pm_runtime_put_autosuspend(s5k3m2->dev);

	return ret;
}

static u32 s5k3m2_get_format_code(struct s5k3m2 *s5k3m2)
{
	unsigned int i;

	i = (s5k3m2->vflip->val ? 2 : 0) | (s5k3m2->hflip->val ? 1 : 0);

	return s5k3m2_mbus_formats[i];
}

static void s5k3m2_update_pad_format(struct s5k3m2 *s5k3m2,
				     const struct s5k3m2_mode *mode,
				     struct v4l2_mbus_framefmt *fmt)
{
	fmt->code = s5k3m2_get_format_code(s5k3m2);
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int s5k3m2_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_format *fmt)
{
	struct s5k3m2 *s5k3m2 = to_s5k3m2(sd);
	s64 hblank, vblank, exposure_max;
	const struct s5k3m2_mode *mode;

	mode = v4l2_find_nearest_size(s5k3m2_supported_modes,
				      ARRAY_SIZE(s5k3m2_supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	s5k3m2_update_pad_format(s5k3m2, mode, &fmt->format);

	/* Format code could be updated with respect to flip controls */
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY || s5k3m2->mode == mode)
		goto set_format;

	s5k3m2->mode = mode;

	/* Update limits and reset the frame duration. */
	hblank = mode->hts - mode->width;
	__v4l2_ctrl_modify_range(s5k3m2->hblank, hblank, hblank, 1, hblank);

	vblank = mode->vts - mode->height;
	__v4l2_ctrl_modify_range(s5k3m2->vblank, vblank,
				 S5K3M2_VTS_MAX - mode->height, 1, vblank);
	__v4l2_ctrl_s_ctrl(s5k3m2->vblank, vblank);

	exposure_max = mode->vts - S5K3M2_EXPOSURE_MARGIN;
	__v4l2_ctrl_modify_range(s5k3m2->exposure, S5K3M2_EXPOSURE_MIN,
				 exposure_max, S5K3M2_EXPOSURE_STEP,
				 mode->exposure);
	/* Keep metered exposure across preview/still modes; range update clamps it. */

	if (s5k3m2->sd.ctrl_handler->error)
		return s5k3m2->sd.ctrl_handler->error;

	s5k3m2->mode = mode;

set_format:
	*v4l2_subdev_state_get_format(state, 0) = fmt->format;

	return 0;
}

static int s5k3m2_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct s5k3m2 *s5k3m2 = to_s5k3m2(sd);

	/* Media bus code index is constant, but code formats are not */
	if (code->index > 0)
		return -EINVAL;

	code->code = s5k3m2_get_format_code(s5k3m2);

	return 0;
}

static int s5k3m2_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct s5k3m2 *s5k3m2 = to_s5k3m2(sd);

	if (fse->index >= ARRAY_SIZE(s5k3m2_supported_modes))
		return -EINVAL;

	if (fse->code != s5k3m2_get_format_code(s5k3m2))
		return -EINVAL;

	fse->min_width = s5k3m2_supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = s5k3m2_supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int s5k3m2_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct s5k3m2 *s5k3m2 = to_s5k3m2(sd);

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r = (struct v4l2_rect) { 0, 0, 4160, 3120 };
		return 0;
	case V4L2_SEL_TGT_CROP:
		/* Effective stock-mode ROI, relative to the usable full-resolution array. */
		switch (s5k3m2->mode->width) {
		case 1280:
			sel->r = (struct v4l2_rect) { 160, 480, 3840, 2160 };
			break;
		case 1296:
			sel->r = (struct v4l2_rect) { 136, 480, 3888, 2160 };
			break;
		case 1936:
			sel->r = (struct v4l2_rect) { 144, 480, 3872, 2160 };
			break;
		default:
			sel->r = (struct v4l2_rect) { 0, 0, 4160, 3120 };
			break;
		}
		return 0;
	default:
		return -EINVAL;
	}

	return 0;
}

static int s5k3m2_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	struct s5k3m2 *s5k3m2 = to_s5k3m2(sd);
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.pad = 0,
		.format = {
			/* Media bus code depends on current flip controls */
			.width = s5k3m2->mode->width,
			.height = s5k3m2->mode->height,
		},
	};

	s5k3m2_set_pad_format(sd, state, &fmt);

	return 0;
}

static const struct v4l2_subdev_video_ops s5k3m2_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops s5k3m2_pad_ops = {
	.set_fmt = s5k3m2_set_pad_format,
	.get_fmt = v4l2_subdev_get_fmt,
	.get_selection = s5k3m2_get_selection,
	.enum_mbus_code = s5k3m2_enum_mbus_code,
	.enum_frame_size = s5k3m2_enum_frame_size,
	.enable_streams = s5k3m2_enable_streams,
	.disable_streams = s5k3m2_disable_streams,
};

static const struct v4l2_subdev_ops s5k3m2_subdev_ops = {
	.video = &s5k3m2_video_ops,
	.pad = &s5k3m2_pad_ops,
};

static const struct v4l2_subdev_internal_ops s5k3m2_internal_ops = {
	.init_state = s5k3m2_init_state,
};

static const struct media_entity_operations s5k3m2_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int s5k3m2_identify_sensor(struct s5k3m2 *s5k3m2)
{
	u64 val;
	int ret;

	ret = cci_read(s5k3m2->regmap, S5K3M2_REG_CHIP_ID, &val, NULL);
	if (ret) {
		dev_err(s5k3m2->dev, "failed to read chip id: %d\n", ret);
		return ret;
	}

	if (val != S5K3M2_CHIP_ID) {
		dev_err(s5k3m2->dev, "chip id mismatch: %x!=%llx\n",
			S5K3M2_CHIP_ID, val);
		return -ENODEV;
	}

	return 0;
}

static int s5k3m2_check_hwcfg(struct s5k3m2 *s5k3m2)
{
	struct fwnode_handle *fwnode = dev_fwnode(s5k3m2->dev), *ep;
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus = {
			.mipi_csi2 = {
				.num_data_lanes = S5K3M2_DATA_LANES,
			},
		},
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	unsigned long freq_bitmap;
	int ret;

	if (!fwnode)
		return -ENODEV;

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -EINVAL;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != S5K3M2_DATA_LANES) {
		dev_err(s5k3m2->dev, "Invalid number of data lanes: %u\n",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto endpoint_free;
	}

	ret = v4l2_link_freq_to_bitmap(s5k3m2->dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       s5k3m2_link_freq_menu,
				       ARRAY_SIZE(s5k3m2_link_freq_menu),
				       &freq_bitmap);

endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int s5k3m2_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5k3m2 *s5k3m2 = to_s5k3m2(sd);
	int ret;

	/* Power sequence recovered from this device's stock sensor setup. */
	ret = regulator_enable(s5k3m2->analog_supply);
	if (ret)
		return ret;
	usleep_range(1000, 1500);

	ret = regulator_bulk_enable(S5K3M2_NUM_SUPPLIES, s5k3m2->supplies);
	if (ret)
		goto disable_analog;
	usleep_range(1000, 1500);

	gpiod_set_value_cansleep(s5k3m2->digital_gpio, 1);
	usleep_range(1000, 1500);

	ret = clk_prepare_enable(s5k3m2->mclk);
	if (ret)
		goto disable_regulators;
	usleep_range(2000, 2500);

	gpiod_set_value_cansleep(s5k3m2->reset_gpio, 0);
	usleep_range(1000, 1500);

	return 0;

disable_regulators:
	gpiod_set_value_cansleep(s5k3m2->digital_gpio, 0);
	regulator_bulk_disable(S5K3M2_NUM_SUPPLIES, s5k3m2->supplies);
disable_analog:
	regulator_disable(s5k3m2->analog_supply);

	return ret;
}

static int s5k3m2_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5k3m2 *s5k3m2 = to_s5k3m2(sd);

	s5k3m2->initialized = false;

	gpiod_set_value_cansleep(s5k3m2->reset_gpio, 1);
	usleep_range(1000, 1500);

	clk_disable_unprepare(s5k3m2->mclk);

	gpiod_set_value_cansleep(s5k3m2->digital_gpio, 0);
	usleep_range(1000, 1500);
	regulator_disable(s5k3m2->analog_supply);
	usleep_range(1000, 1500);

	regulator_bulk_disable(S5K3M2_NUM_SUPPLIES, s5k3m2->supplies);

	return 0;
}

static int s5k3m2_probe(struct i2c_client *client)
{
	struct s5k3m2 *s5k3m2;
	unsigned long freq;
	unsigned int i;
	int ret;

	s5k3m2 = devm_kzalloc(&client->dev, sizeof(*s5k3m2), GFP_KERNEL);
	if (!s5k3m2)
		return -ENOMEM;

	s5k3m2->dev = &client->dev;
	v4l2_i2c_subdev_init(&s5k3m2->sd, client, &s5k3m2_subdev_ops);

	s5k3m2->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(s5k3m2->regmap))
		return dev_err_probe(s5k3m2->dev, PTR_ERR(s5k3m2->regmap),
				     "failed to init CCI\n");

	s5k3m2->mclk = devm_v4l2_sensor_clk_get(s5k3m2->dev, NULL);
	if (IS_ERR(s5k3m2->mclk))
		return dev_err_probe(s5k3m2->dev, PTR_ERR(s5k3m2->mclk),
				     "failed to get MCLK clock\n");

	freq = clk_get_rate(s5k3m2->mclk);
	/* Osprey feeds 23.88 MHz (stock rate, GCC rounds it to 23880597 Hz);
	 * 24 MHz tables are kept and validated on hardware, starting with
	 * the test pattern. */
	if (freq != S5K3M2_MCLK_FREQ_24MHZ && (freq < 23800000 || freq > 23900000))
		return dev_err_probe(s5k3m2->dev, -EINVAL,
				     "MCLK clock frequency %lu is not supported\n",
				     freq);

	ret = s5k3m2_check_hwcfg(s5k3m2);
	if (ret)
		return dev_err_probe(s5k3m2->dev, ret,
				     "failed to check HW configuration\n");

    s5k3m2->analog_supply = devm_regulator_get(s5k3m2->dev, "vana");
    if (IS_ERR(s5k3m2->analog_supply))
        return dev_err_probe(s5k3m2->dev, PTR_ERR(s5k3m2->analog_supply),
                             "failed to get shared analog supply\n");

	s5k3m2->digital_gpio = devm_gpiod_get(s5k3m2->dev, "digital-enable",
					     GPIOD_OUT_LOW);
	if (IS_ERR(s5k3m2->digital_gpio))
		return dev_err_probe(s5k3m2->dev, PTR_ERR(s5k3m2->digital_gpio),
				     "failed to get digital enable gpio\n");

	s5k3m2->reset_gpio = devm_gpiod_get_optional(s5k3m2->dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(s5k3m2->reset_gpio))
		return dev_err_probe(s5k3m2->dev, PTR_ERR(s5k3m2->reset_gpio),
				     "cannot get reset GPIO\n");

	for (i = 0; i < S5K3M2_NUM_SUPPLIES; i++)
		s5k3m2->supplies[i].supply = s5k3m2_supply_names[i];

	ret = devm_regulator_bulk_get(s5k3m2->dev, S5K3M2_NUM_SUPPLIES,
				      s5k3m2->supplies);
	if (ret)
		return dev_err_probe(s5k3m2->dev, ret,
				     "failed to get supply regulators\n");

	/* The sensor must be powered on to read the CHIP_ID register */
	ret = s5k3m2_power_on(s5k3m2->dev);
	if (ret)
		return ret;

	ret = s5k3m2_identify_sensor(s5k3m2);
	if (ret) {
		dev_err_probe(s5k3m2->dev, ret, "failed to find sensor\n");
		goto power_off;
	}

	s5k3m2->mode = &s5k3m2_supported_modes[0];
	ret = s5k3m2_init_controls(s5k3m2);
	if (ret) {
		dev_err_probe(s5k3m2->dev, ret, "failed to init controls\n");
		goto power_off;
	}

	s5k3m2->sd.state_lock = s5k3m2->ctrl_handler.lock;
	s5k3m2->sd.internal_ops = &s5k3m2_internal_ops;
	s5k3m2->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	s5k3m2->sd.entity.ops = &s5k3m2_subdev_entity_ops;
	s5k3m2->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	s5k3m2->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&s5k3m2->sd.entity, 1, &s5k3m2->pad);
	if (ret) {
		dev_err_probe(s5k3m2->dev, ret,
			      "failed to init media entity pads\n");
		goto v4l2_ctrl_handler_free;
	}

	ret = v4l2_subdev_init_finalize(&s5k3m2->sd);
	if (ret < 0) {
		dev_err_probe(s5k3m2->dev, ret,
			      "failed to init media entity pads\n");
		goto media_entity_cleanup;
	}

	pm_runtime_set_active(s5k3m2->dev);
	pm_runtime_enable(s5k3m2->dev);

	ret = v4l2_async_register_subdev_sensor(&s5k3m2->sd);
	if (ret < 0) {
		dev_err_probe(s5k3m2->dev, ret,
			      "failed to register V4L2 subdev\n");
		goto subdev_cleanup;
	}

	pm_runtime_set_autosuspend_delay(s5k3m2->dev, 1000);
	pm_runtime_use_autosuspend(s5k3m2->dev);
	pm_runtime_idle(s5k3m2->dev);

	return 0;

subdev_cleanup:
	v4l2_subdev_cleanup(&s5k3m2->sd);
	pm_runtime_disable(s5k3m2->dev);
	pm_runtime_set_suspended(s5k3m2->dev);

media_entity_cleanup:
	media_entity_cleanup(&s5k3m2->sd.entity);

v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(s5k3m2->sd.ctrl_handler);

power_off:
	s5k3m2_power_off(s5k3m2->dev);

	return ret;
}

static void s5k3m2_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k3m2 *s5k3m2 = to_s5k3m2(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(s5k3m2->dev);

	if (!pm_runtime_status_suspended(s5k3m2->dev)) {
		s5k3m2_power_off(s5k3m2->dev);
		pm_runtime_set_suspended(s5k3m2->dev);
	}
}

static const struct dev_pm_ops s5k3m2_pm_ops = {
	SET_RUNTIME_PM_OPS(s5k3m2_power_off, s5k3m2_power_on, NULL)
};

static const struct of_device_id s5k3m2_of_match[] = {
	{ .compatible = "samsung,s5k3m2" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s5k3m2_of_match);

static struct i2c_driver s5k3m2_i2c_driver = {
	.driver = {
		.name = "s5k3m2",
		.pm = &s5k3m2_pm_ops,
		.of_match_table = s5k3m2_of_match,
	},
	.probe = s5k3m2_probe,
	.remove = s5k3m2_remove,
};

module_i2c_driver(s5k3m2_i2c_driver);

MODULE_AUTHOR("Vladimir Zapolskiy <vladimir.zapolskiy@linaro.org>");
MODULE_DESCRIPTION("Samsung S5K3M2 image sensor driver");
MODULE_LICENSE("GPL");
