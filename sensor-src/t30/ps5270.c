/*
 * ps5270.c
 *
 * Copyright (C) 2012 Ingenic Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
/* #define DEBUG */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/proc_fs.h>
#include <soc/gpio.h>
#include <tx-isp-common.h>
#include <sensor-common.h>

#define SENSOR_CHIP_ID_H 0x52
#define SENSOR_CHIP_ID_L 0x70
#define SENSOR_REG_END 0xff
#define SENSOR_REG_DELAY 0xfe
#define SENSOR_BANK_REG 0xef

#define SENSOR_SUPPORT_PCLK_FPS20 58000000
#define SENSOR_SUPPORT_PCLK_FPS30 86000000
#define SENSOR_SUPPORT_PCLK_WDR	70000000
#define SENSOR_OUTPUT_MAX_FPS 20
#define SENSOR_OUTPUT_MIN_FPS 5
#define AG_HS_MODE	83	// 6.0x
#define AG_LS_MODE	75	// 5.0x
#define NEPLS_LB	20
#define NEPLS_UB	255
#define NEPLS_SCALE	32
#define NE_NEP_CONST	(0x1F4+0x64)

#define SENSOR_VERSION "H20180911a"

typedef enum {
	SENSOR_RAW_MODE_LINEAR_20FPS = 0,
	SENSOR_RAW_MODE_LINEAR_30FPS,
	SENSOR_RAW_MODE_NATIVE_WDR,
} supported_sensor_mode;

static int reset_gpio = GPIO_PA(18);
module_param(reset_gpio, int, S_IRUGO);
MODULE_PARM_DESC(reset_gpio, "Reset GPIO NUM");

static int pwdn_gpio = -1;
module_param(pwdn_gpio, int, S_IRUGO);
MODULE_PARM_DESC(pwdn_gpio, "Power down GPIO NUM");

static int sensor_gpio_func = DVP_PA_12BIT;
module_param(sensor_gpio_func, int, S_IRUGO);
MODULE_PARM_DESC(sensor_gpio_func, "Sensor GPIO function");

static int sensor_raw_mode = SENSOR_RAW_MODE_LINEAR_20FPS;
module_param(sensor_raw_mode, int, S_IRUGO);
MODULE_PARM_DESC(sensor_raw_mode, "Sensor Raw Mode");

struct regval_list {
	unsigned char reg_num;
	unsigned char value;
};

/*
 * the part of driver maybe modify about different sensor and different board.
 */
struct again_lut {
	unsigned int value;
	unsigned int gain;
};

struct again_lut sensor_again_lut[] = {
	{0x00, 0     },
	{0x01, 2053  },
	{0x02, 4104  },
	{0x03, 6151  },
	{0x04, 8193  },
	{0x05, 10229 },
	{0x06, 12283 },
	{0x07, 14329 },
	{0x08, 16393 },
	{0x09, 18418 },
	{0x0A, 20488 },
	{0x0B, 22517 },
	{0x0C, 24589 },
	{0x0D, 26617 },
	{0x0E, 28658 },
	{0x0F, 30711 },
	{0x10, 32778 },
	{0x11, 34824 },
	{0x12, 36847 },
	{0x13, 38915 },
	{0x14, 40957 },
	{0x15, 43009 },
	{0x16, 45068 },
	{0x17, 47097 },
	{0x18, 49171 },
	{0x19, 51212 },
	{0x1A, 53258 },
	{0x1B, 55307 },
	{0x1C, 57359 },
	{0x1D, 59371 },
	{0x1E, 61426 },
	{0x1F, 63481 },
	{0x20, 65536 },
	{0x21, 67589 },
	{0x22, 69640 },
	{0x23, 71687 },
	{0x24, 73729 },
	{0x25, 75765 },
	{0x26, 77845 },
	{0x27, 79865 },
	{0x28, 81929 },
	{0x29, 83982 },
	{0x2A, 86024 },
	{0x2B, 88053 },
	{0x2C, 90125 },
	{0x2D, 92184 },
	{0x2E, 94225 },
	{0x2F, 96247 },
	{0x30, 98314 },
	{0x31, 100360},
	{0x32, 102383},
	{0x33, 104451},
	{0x34, 106493},
	{0x35, 108508},
	{0x36, 110567},
	{0x37, 112671},
	{0x38, 114668},
	{0x39, 116709},
	{0x3A, 118794},
	{0x3B, 120843},
	{0x3C, 122853},
	{0x3D, 124907},
	{0x3E, 127006},
	{0x3F, 129062},
	{0x40, 131072},
	{0x41, 133125},
	{0x42, 135176},
	{0x43, 137223},
	{0x44, 139265},
	{0x45, 141301},
	{0x46, 143355},
	{0x47, 145401},
	{0x48, 147465},
	{0x49, 149490},
	{0x4A, 151560},
	{0x4B, 153589},
	{0x4C, 155661},
	{0x4D, 157689},
	{0x4E, 159730},
	{0x4F, 161783},
	{0x50, 163850},
	{0x51, 165896},
	{0x52, 167919},
	{0x53, 169987},
	{0x54, 172029},
	{0x55, 174081},
	{0x56, 176140},
	{0x57, 178169},
	{0x58, 180243},
	{0x59, 182284},
	{0x5A, 184330},
	{0x5B, 186379},
	{0x5C, 188431},
	{0x5D, 190443},
	{0x5E, 192498},
	{0x5F, 194553},
	{0x60, 196608},
	{0x61, 198661},
	{0x62, 200712},
	{0x63, 202759},
	{0x64, 204801},
	{0x65, 206837},
	{0x66, 208917},
	{0x67, 210937},
	{0x68, 213001},
	{0x69, 215054},
	{0x6A, 217096},
	{0x6B, 219125},
	{0x6C, 221197},
	{0x6D, 223256},
	{0x6E, 225297},
	{0x6F, 227319},
	{0x70, 229386},
	{0x71, 231432},
	{0x72, 233455},
	{0x73, 235523},
	{0x74, 237565},
	{0x75, 239580},
	{0x76, 241639},
	{0x77, 243743},
	{0x78, 245740},
	{0x79, 247781},
	{0x7A, 249866},
	{0x7B, 251915},
	{0x7C, 253925},
	{0x7D, 255979},
	{0x7E, 258078},
	{0x7F, 260134},
	{0x80, 262144},
	{0x81, 264197},
	{0x82, 266200},
	{0x83, 268246},
	{0x84, 270337},
	{0x85, 272373},
	{0x86, 274453},
	{0x87, 276473},
	{0x88, 278537},
	{0x89, 280534},
	{0x8A, 282575},
	{0x8B, 284661},
	{0x8C, 286674},
	{0x8D, 288730},
	{0x8E, 290833},
	{0x8F, 292855},
	{0x90, 294922},
	{0x91, 296902},
	{0x92, 299060},
	{0x93, 300989},
	{0x94, 303101},
	{0x95, 305116},
	{0x96, 307175},
	{0x97, 309279},
	{0x98, 311276},
	{0x99, 313317},
	{0x9A, 315402},
	{0x9B, 317368},
	{0x9C, 319546},
	{0x9D, 321601},
	{0x9E, 323525},
	{0x9F, 325670},
	{0xA0, 327680},
	{0xA1, 329733},
	{0xA2, 331832},
	{0xA3, 333782},
	{0xA4, 335773},
	{0xA5, 338632},
	{0xA6, 339884},
	{0xA7, 342009},
	{0xA8, 343963},
	{0xA9, 346182},
	{0xAA, 348226},
	{0xAB, 350314},
	{0xAC, 352210},
	{0xAD, 354389},
	{0xAE, 356369},
	{0xAF, 358391},
	{0xB0, 360458},
	{0xB1, 362571},
	{0xB2, 364459},
	{0xB3, 366665},
	{0xB4, 368637},
	{0xB5, 370652},
	{0xB6, 372711},
	{0xB7, 374815},
	{0xB8, 376968},
	{0xB9, 378853},
	{0xBA, 380776},
	{0xBB, 383070},
	{0xBC, 385082},
	{0xBD, 387137},
	{0xBE, 389238},
	{0xBF, 391026},
	{0xC0, 393216},
	{0xC1, 395081},
	{0xC2, 397368},
	{0xC3, 399318},
	{0xC4, 401309},
	{0xC5, 403342},
	{0xC6, 405420},
	{0xC7, 407545},
	{0xC8, 409718},
	{0xC9, 411494},
	{0xCA, 413762},
	{0xCB, 415615},
	{0xCC, 417985},
	{0xCD, 419925},
	{0xCE, 421905},
	{0xCF, 423927},
	{0xD0, 425994},
	{0xD1, 428107},
	{0xD2, 430268},
	{0xD3, 431922},
	{0xD4, 434173},
	{0xD5, 436480},
	{0xD6, 438247},
	{0xD7, 440048},
	{0xD8, 442504},
	{0xD9, 444389},
	{0xDA, 446312},
	{0xDB, 448275},
	{0xDC, 450279},
	{0xDD, 452327},
	{0xDE, 454421},
	{0xDF, 456562},
	{0xE0, 458752},
};

struct tx_isp_sensor_attribute sensor_attr;

unsigned int sensor_alloc_again(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
	struct again_lut *lut = sensor_again_lut;
	while (lut->gain <= sensor_attr.max_again) {
		if (isp_gain <= sensor_again_lut[0].gain) {
			*sensor_again = lut[0].value;
			return lut[0].gain;
		} else if (isp_gain < lut->gain) {
			*sensor_again = (lut - 1)->value;
			return (lut - 1)->gain;
		} else {
			if ((lut->gain == sensor_attr.max_again) && (isp_gain >= lut->gain)) {
				*sensor_again = lut->value;
				return lut->gain;
			}
		}

		lut++;
	}
	return isp_gain;
}

unsigned int sensor_alloc_dgain(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_dgain)
{
	return isp_gain;
}

struct tx_isp_sensor_attribute sensor_attr={
	.name = "ps5270",
	.chip_id = 0x5270,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = V4L2_SBUS_MASK_SAMPLE_8BITS | V4L2_SBUS_MASK_ADDR_8BITS,
	.cbus_device = 0x48,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_DVP,
	.dvp = {
		.mode = SENSOR_DVP_HREF_MODE,
		.blanking = {
			.vblanking = 0,
			.hblanking = 0,
		},
		.polar = {
			.hsync_polar = 0,
			.vsync_polar = 0,
		},
	},
	.max_again = 458752,
	.max_dgain = 0,
	.min_integration_time = 2,
	.min_integration_time_native = 2,
	.max_integration_time_native = 0x71b - 2,
	.integration_time_limit = 0x71b - 2,
	.total_width = 0xc70 >> 1,
	.total_height = 0x71b + 1,
	.max_integration_time = 0x71b - 2,
	.one_line_expr_in_us = 27,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 2,
	.sensor_ctrl.alloc_again = sensor_alloc_again,
	.sensor_ctrl.alloc_dgain = sensor_alloc_dgain,
};

struct tx_isp_sensor_attribute sensor_attr_linear_30fps={
	.name = "ps5270",
	.chip_id = 0x5270,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = V4L2_SBUS_MASK_SAMPLE_8BITS | V4L2_SBUS_MASK_ADDR_8BITS,
	.cbus_device = 0x48,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_DVP,
	.dvp = {
		.mode = SENSOR_DVP_HREF_MODE,
		.blanking = {
			.vblanking = 0,
			.hblanking = 0,
		},
		.polar = {
			.hsync_polar = 0,
			.vsync_polar = 0,
		},
	},
	.max_again = 458752,
	.max_dgain = 0,
	.min_integration_time = 2,
	.min_integration_time_native = 2,
	.max_integration_time_native = 0x71b - 2,
	.integration_time_limit = 0x71b - 2,
	.total_width = 0xc3e >> 1,
	.total_height = 0x71b + 1,
	.max_integration_time = 0x71b - 2,
	.one_line_expr_in_us = 18,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 2,
	.sensor_ctrl.alloc_again = sensor_alloc_again,
	.sensor_ctrl.alloc_dgain = sensor_alloc_dgain,
};

struct tx_isp_sensor_attribute sensor_attr_wdr_20fps={
	.name = "ps5270",
	.chip_id = 0x5270,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = V4L2_SBUS_MASK_SAMPLE_8BITS | V4L2_SBUS_MASK_ADDR_8BITS,
	.cbus_device = 0x48,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_DVP,
	.dvp = {
		.mode = SENSOR_DVP_HREF_MODE,
		.blanking = {
			.vblanking = 0,
			.hblanking = 0,
		},
		.polar = {
			.hsync_polar = 0,
			.vsync_polar = 0,
		},
	},
	.max_again = 458752,
	.max_dgain = 0,
	.min_integration_time = 4,
	.min_integration_time_native = 4,
	.max_integration_time_native = 0x71b - 2,
	.integration_time_limit = 0x71b - 2,
	.total_width = 0xf04 >> 1,
	.total_height = 0x71b + 1,
	.max_integration_time = 0x71b - 2,
	.one_line_expr_in_us = 27,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 2,
	.sensor_ctrl.alloc_again = sensor_alloc_again,
	.sensor_ctrl.alloc_dgain = sensor_alloc_dgain,
};

unsigned char g_sns_ver = 0;

static struct regval_list sensor_init_regs_1536_1536_linear_20fps[] = {
	{0xEF, 0x00},
	{0x11, 0x80},	/*clk gated*/
	{0xEF, 0x01},
	{0x05, 0x05},	/*sw pwdn*/
	{0xF5, 0x00},	/*spll disable*/
	{0x09, 0x01},
	{0xEF, 0x00},
	{0x10, 0xA0},
	{0x3A, 0x02},
	{0x3C, 0x07},
	{0x5F, 0x2C},
	{0x60, 0xC2},
	{0x61, 0xFD},
	{0x69, 0x40},
	{0x6A, 0x80},
	{0x6E, 0x80},
	{0x6F, 0x5A},
	{0x71, 0x2D},
	{0x7F, 0x28},
	{0x85, 0x1E},
	{0x87, 0x27},
	{0x90, 0x02},
	{0x9B, 0x0B},
	{0x9E, 0x42},
	{0xA0, 0x05},
	{0xA1, 0x00},
	{0xA2, 0x0A},
	{0xA3, 0x04},
	{0xA4, 0xFE},
	{0xBE, 0x15},
	{0xE1, 0x03},
	{0xE2, 0x02},
	{0xE5, 0x03},
	{0xE6, 0x02},
	{0xED, 0x01},
	{0xEF, 0x01},
	{0x04, 0x10},
	{0x05, 0x01},
	{0x0A, 0x07},
	{0x0B, 0x1B},
	{0x0C, 0x00},
	{0x0D, 0x04},
	{0x0E, 0x01},
	{0x0F, 0xF4},
	{0x10, 0xB0},
	{0x11, 0x4A},
	{0x18, 0x01},
	{0x19, 0x3F},
	{0x1E, 0x04},
	{0x20, 0x06},
	{0x27, 0x0C},
	{0x28, 0x70},
	{0x29, 0x0A},
	{0x2A, 0x0A},
	{0x2F, 0x02},
	{0x31, 0x00},
	{0x32, 0x00},
	{0x33, 0x00},
	{0x37, 0x18},
	{0x38, 0x04},
	{0x39, 0x22},
	{0x3A, 0xFF},
	{0x3B, 0x4C},
	{0x3E, 0x30},
	{0x3F, 0xFF},
	{0x40, 0xFF},
	{0x41, 0x13},
	{0x42, 0xFF},
	{0x43, 0xFF},
	{0x44, 0xFF},
	{0x47, 0x08},
	{0x4B, 0x08},
	{0x4E, 0x00},
	{0x52, 0x00},
	{0x53, 0x00},
	{0x56, 0x02},
	{0x58, 0x00},
	{0x59, 0x00},
	{0x5A, 0x16},
	{0x60, 0x64},
	{0x64, 0x00},
	{0x65, 0x00},
	{0x66, 0x00},
	{0x67, 0x00},
	{0x68, 0x00},
	{0x69, 0x00},
	{0x74, 0x0B},
	{0x75, 0xB8},
	{0x7A, 0x00},
	{0x7B, 0x96},
	{0x7C, 0x0B},
	{0x7D, 0xB8},
	{0x8F, 0x02},
	{0x92, 0x00},
	{0x96, 0x80},
	{0x9B, 0x0A},
	{0x9F, 0x00},
	{0xA1, 0x40},
	{0xA2, 0x40},
	{0xA4, 0x5C},/*Y offset =(1536-1376)/ =80  +0x0C*/
	{0xA5, 0x05},
	{0xA6, 0x60},/*Y output 1376*/
	{0xA8, 0x06},
	{0xA9, 0x06},
	{0xAA, 0x00},
	{0xAB, 0x01},
	{0xB0, 0x00},
	{0xB4, 0x00},
	{0xBC, 0x81},
	{0xBD, 0x09},
	{0xD7, 0x0E},
	{0xE2, 0x4F},
	{0xE3, 0x02},
	{0xE4, 0x00},
	{0xE6, 0x01},
	{0xEA, 0x3B},
	{0xF5, 0x00},/*spll disable*/
	{0xF0, 0x03},
	{0xF1, 0x16},
	{0xF2, 0x1B},
	{0xF8, 0x08},
	{0xFA, 0x75},
	{0xFC, 0x04},
	{0xFD, 0x20},
	{0x09, 0x01},
	{0xEF, 0x02},
	{0x33, 0x85},
	{0x4E, 0x02},
	{0x4F, 0x05},
	{0xED, 0x01},
	{0xEF, 0x05},
	{0x0F, 0x00},
	{0x42, 0x00},
	{0xED, 0x01},
	{0xEF, 0x06},
	{0x00, 0x0C},
	{0x02, 0x13},
	{0x03, 0x8D},
	{0x04, 0x05},
	{0x05, 0x01},
	{0x07, 0x02},
	{0x08, 0x02},
	{0x09, 0x01},
	{0x0A, 0x01},
	{0x0B, 0x82},
	{0x0C, 0xFA},
	{0x0D, 0xDB},
	{0x0F, 0x02},
	{0x10, 0x58},
	{0x11, 0x02},
	{0x12, 0x58},
	{0x17, 0x02},
	{0x18, 0x58},
	{0x19, 0x02},
	{0x1A, 0x58},
	{0x28, 0x02},
	{0x2A, 0xF0},
	{0x2B, 0xB2},
	{0x5E, 0x90},
	{0xBF, 0xC8},
	{0xED, 0x01},
	{0xEF, 0x01},
	{0xF5, 0x10},	/*spll enable*/
	{0x09, 0x01},
	{SENSOR_REG_DELAY, 0x02},
	{0xEF, 0x00},
	{0x11, 0x00},
	{0xEF, 0x01},
	{0x02, 0xFB},
	{0x09, 0x01},

	{SENSOR_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list sensor_init_regs_1536_1376_wdr_20fps[] = {
	{0xEF, 0x00},
	{0x10, 0xA0},
	{0x11, 0x00},
	{0x3A, 0x02},
	{0x3C, 0x07},
	{0x5F, 0x2C},
	{0x60, 0xC2},
	{0x61, 0xFD},
	{0x69, 0x40},
	{0x6A, 0x80},
	{0x6E, 0x80},
	{0x6F, 0x5A},
	{0x71, 0x2D},
	{0x7F, 0x28},
	{0x85, 0x1E},
	{0x87, 0x27},
	{0x90, 0x02},
	{0x9B, 0x0B},
	{0x9E, 0x42},
	{0xA0, 0x05},
	{0xA1, 0x00},
	{0xA2, 0x0A},
	{0xA3, 0x04},
	{0xA4, 0xFE},
	{0xBE, 0x15},
	{0xE1, 0x03},
	{0xE2, 0x02},
	{0xE5, 0x03},
	{0xE6, 0x02},
	{0xED, 0x01},
	{0xEF, 0x01},
	{0x05, 0x01},
	{0x0A, 0x07},
	{0x0B, 0x1B},
	{0x0C, 0x00},
	{0x0D, 0x04},
	{0x0E, 0x01},
	{0x0F, 0xF4},
	{0x10, 0xB0},
	{0x11, 0x4A},
	{0x19, 0x3F},
	{0x1E, 0x04},
	{0x20, 0x06},
	{0x27, 0x0F},/*hts wdr mode*/
	{0x28, 0x04},
	{0x29, 0x0A},
	{0x2A, 0x0A},
	{0x2F, 0x02},
	{0x37, 0x18},
	{0x38, 0x03},
	{0x39, 0x22},
	{0x3A, 0xFF},
	{0x3B, 0x70},
	{0x3E, 0x10},
	{0x3F, 0x7C},
	{0x40, 0xFF},
	{0x41, 0x13},
	{0x42, 0xF4},
	{0x43, 0x70},
	{0x44, 0x00},
	{0x56, 0x02},
	{0x60, 0x64},
	{0x67, 0x11},
	{0x68, 0x4A},
	{0x69, 0x4A},
	{0x74, 0x0E},
	{0x75, 0x4C},
	{0x7A, 0x03},
	{0x7B, 0xB1},
	{0x7C, 0x06},
	{0x7D, 0x77},
	{0x8F, 0x07},
	{0x92, 0x00},
	{0x96, 0x80},
	{0x9B, 0x0A},
	{0x9F, 0x01},
	{0xA1, 0x40},
	{0xA2, 0x40},
	{0xA4, 0x4C},/* Y offset */
	{0xA5, 0x05},
	{0xA6, 0x60},/*output size Y*/
	{0xA8, 0x06},
	{0xA9, 0x06},
	{0xAA, 0x00},
	{0xAB, 0x01},
	{0xB0, 0x00},
	{0xB4, 0x00},
	{0xBC, 0x01},
	{0xBD, 0x09},
	{0xD7, 0x0E},
	{0xE2, 0x4F},
	{0xE3, 0x01},
	{0xE4, 0x00},
	{0xE6, 0x01},
	{0xEA, 0xBB},
	{0xF0, 0x03},
	{0xF1, 0x16},
	{0xF2, 0x21},
	{0xF5, 0x10},
	{0xF8, 0x08},
	{0xFA, 0x75},
	{0xFC, 0x04},
	{0xFD, 0x20},
	{0x09, 0x01},
	{0xEF, 0x02},
	{0x33, 0x85},
	{0x4E, 0x02},
	{0x4F, 0x05},
	{0xED, 0x01},
	{0xEF, 0x05},
	{0x0F, 0x00},
	{0x42, 0x00},
	{0xED, 0x01},
	{0xEF, 0x06},
	{0x00, 0x0C},
	{0x02, 0x13},
	{0x03, 0x8D},
	{0x04, 0x05},
	{0x05, 0x01},
	{0x07, 0x02},
	{0x08, 0x02},
	{0x09, 0x01},
	{0x0A, 0x01},
	{0x0B, 0x82},
	{0x0C, 0xFA},
	{0x0D, 0xDB},
	{0x0F, 0x02},
	{0x10, 0x58},
	{0x11, 0x02},
	{0x12, 0x58},
	{0x17, 0x02},
	{0x18, 0x58},
	{0x19, 0x02},
	{0x1A, 0x58},
	{0x28, 0x02},
	{0x2A, 0xF0},
	{0x2B, 0xB2},
	{0x5E, 0x90},
	{0xBF, 0xC8},
	{0xED, 0x01},

	{SENSOR_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list sensor_init_regs_1536_1536_linear_30fps[] = {
	{0xEF, 0x00},
	{0x11, 0x80},	/*clk gated*/
	{0xEF, 0x01},
	{0x05, 0x05},	/*sw pwdn*/
	{0xF5, 0x00},	/*spll disable*/
	{0x09, 0x01},
	{0xEF, 0x00},
	{0x10, 0xA0},
	{0x3A, 0x02},
	{0x3C, 0x07},
	{0x5F, 0x2C},
	{0x60, 0xC2},
	{0x61, 0xFD},
	{0x69, 0x40},
	{0x6A, 0x80},
	{0x6E, 0x80},
	{0x6F, 0x5A},
	{0x71, 0x2D},
	{0x7F, 0x28},
	{0x85, 0x1E},
	{0x87, 0x27},
	{0x90, 0x02},
	{0x9B, 0x0B},
	{0x9E, 0x42},
	{0xA0, 0x05},
	{0xA1, 0x00},
	{0xA2, 0x0A},
	{0xA3, 0x04},
	{0xA4, 0xFE},
	{0xBE, 0x15},/*For ISP Hsync*/
	{0xE1, 0x03},
	{0xE2, 0x02},
	{0xE5, 0x03},
	{0xE6, 0x02},
	{0xED, 0x01},
	{0xEF, 0x01},
	{0x04, 0x10},
	{0x05, 0x01},
	{0x0A, 0x07},
	{0x0B, 0x1B},
	{0x0C, 0x00},
	{0x0D, 0x04},
	{0x0E, 0x01},
	{0x0F, 0xF4},
	{0x10, 0xB0},
	{0x11, 0x4A},
	{0x18, 0x01},
	{0x19, 0x3F},
	{0x1E, 0x04},
	{0x20, 0x06},
	{0x27, 0x0C},
	{0x28, 0x4E},
	{0x29, 0x0A},
	{0x2A, 0x0A},
	{0x2F, 0x02},
	{0x31, 0x00},
	{0x32, 0x00},
	{0x33, 0x00},
	{0x37, 0x18},
	{0x38, 0x04},
	{0x39, 0x22},
	{0x3A, 0xFF},
	{0x3B, 0x4C},
	{0x3E, 0x30},
	{0x3F, 0xFF},
	{0x40, 0xFF},
	{0x41, 0x13},
	{0x42, 0xFF},
	{0x43, 0xFF},
	{0x44, 0xFF},
	{0x47, 0x08},
	{0x4B, 0x08},
	{0x4E, 0x00},
	{0x52, 0x00},
	{0x53, 0x00},
	{0x56, 0x02},
	{0x58, 0x00},
	{0x59, 0x00},
	{0x5A, 0x16},
	{0x60, 0x64},
	{0x64, 0x00},
	{0x65, 0x00},
	{0x66, 0x00},
	{0x67, 0x00},
	{0x68, 0x00},
	{0x69, 0x00},
	{0x74, 0x0B},
	{0x75, 0xB8},
	{0x7A, 0x00},
	{0x7B, 0x96},
	{0x7C, 0x0B},
	{0x7D, 0xB8},
	{0x8F, 0x02},
	{0x92, 0x00},
	{0x96, 0x80},
	{0x9B, 0x0A},
	{0x9F, 0x00},
	{0xA1, 0x40},
	{0xA2, 0x40},
	{0xA4, 0x0C},
	{0xA5, 0x06},
	{0xA6, 0x00},
	{0xA8, 0x06},
	{0xA9, 0x06},
	{0xAA, 0x00},
	{0xAB, 0x01},
	{0xB0, 0x00},
	{0xB4, 0x00},
	{0xBC, 0x81},
	{0xBD, 0x09},
	{0xD7, 0x0E},
	{0xE2, 0x4F},
	{0xE3, 0x02},
	{0xE4, 0x00},
	{0xE6, 0x01},
	{0xEA, 0x3B},
	{0xF5, 0x00},/*spll disable*/
	{0xF0, 0x03},
	{0xF1, 0x16},
	{0xF2, 0x29},
	{0xF8, 0x08},
	{0xFA, 0x75},
	{0xFC, 0x04},
	{0xFD, 0x20},
	{0x09, 0x01},
	{0xEF, 0x02},
	{0x33, 0x85},
	{0x4E, 0x02},
	{0x4F, 0x05},
	{0xED, 0x01},
	{0xEF, 0x05},
	{0x0F, 0x00},
	{0x42, 0x00},
	{0xED, 0x01},
	{0xEF, 0x06},
	{0x00, 0x0C},
	{0x02, 0x13},
	{0x03, 0x8D},
	{0x04, 0x05},
	{0x05, 0x01},
	{0x07, 0x02},
	{0x08, 0x02},
	{0x09, 0x01},
	{0x0A, 0x01},
	{0x0B, 0x82},
	{0x0C, 0xFA},
	{0x0D, 0xDB},
	{0x0F, 0x02},
	{0x10, 0x58},
	{0x11, 0x02},
	{0x12, 0x58},
	{0x17, 0x02},
	{0x18, 0x58},
	{0x19, 0x02},
	{0x1A, 0x58},
	{0x28, 0x02},
	{0x2A, 0xF0},
	{0x2B, 0xB2},
	{0x5E, 0x90},
	{0xBF, 0xC8},
	{0xED, 0x01},
	{0xEF, 0x01},
	{0xF5, 0x10},	/*spll enable*/
	{0x09, 0x01},
	{SENSOR_REG_DELAY, 0x02},
	{0xEF, 0x00},
	{0x11, 0x00},
	{0xEF, 0x01},
	{0x02, 0xFB},
	{0x09, 0x01},

	{SENSOR_REG_END, 0x00},

};
/*
 * the order of the sensor_win_sizes is [full_resolution, preview_resolution].
 */
static struct tx_isp_sensor_win_setting sensor_win_sizes[] = {
	/* 1536*1536 @20fps linear*/
	{
		.width = 1536,
		.height = 1376,
		.fps = 20 << 16 | 1,
		.mbus_code = V4L2_MBUS_FMT_SBGGR12_1X12,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.regs = sensor_init_regs_1536_1536_linear_20fps,
	},
	/* 1536*1536 @30fps linear*/
	{
		.width = 1536,
		.height = 1536,
		.fps = 30 << 16 | 1,
		.mbus_code = V4L2_MBUS_FMT_SBGGR12_1X12,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.regs = sensor_init_regs_1536_1536_linear_30fps,
	},
	/* 1536*1536 @20fps wdr*/
	{
		.width = 1536,
		.height = 1376,
		.fps = 20 << 16 | 1,
		.mbus_code = V4L2_MBUS_FMT_SBGGR12_1X12,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.regs = sensor_init_regs_1536_1376_wdr_20fps,
	},
};

static enum v4l2_mbus_pixelcode sensor_mbus_code[] = {
	V4L2_MBUS_FMT_SBGGR10_1X10,
	V4L2_MBUS_FMT_SBGGR12_1X12,
};

/*
 * the part of driver was fixed.
 */

static struct regval_list sensor_stream_on[] = {
#if 0
	{0xEF, 0x01},
	{0xF5, 0x10},	/*spll enable*/
	{0x09, 0x01},
	{0x05, 0x01},	/*sw pwdn off*/
	{0x09, 0x01},
	{0x02, 0xfb},	/*sw reset*/
	{0x09, 0x01},
	{0xEF, 0x01},	/* delay > 1ms */
	{SENSOR_REG_DELAY, 0x21},
	{0xEF, 0x00},
	{0x11, 0x00},	/*clk not gated*/
#endif
	{SENSOR_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list sensor_stream_off[] = {
#if 0
	{0xEF, 0x00},
	{0x11, 0x80},	/*clk gated*/
	{0xEF, 0x01},
	{0x05, 0x05},	/*sw pwdn*/
	{0xF5, 0x00},	/*spll disable*/
	{0x09, 0x01},
#endif
	{SENSOR_REG_END, 0x00},	/* END MARKER */
};

int sensor_read(struct tx_isp_subdev *sd, unsigned char reg, unsigned char *value)
{
	int ret;
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	struct i2c_msg msg[2] = {
		[0] = {
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		},
		[1] = {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = value,
		}
	};

	ret = private_i2c_transfer(client->adapter, msg, 2);
	if (ret > 0)
		ret = 0;

	return ret;
}

int sensor_write(struct tx_isp_subdev *sd, unsigned char reg, unsigned char value)
{
	int ret;
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned char buf[2] = {reg, value};
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 2,
		.buf = buf,
	};

	ret = private_i2c_transfer(client->adapter, &msg, 1);
	if (ret > 0)
		ret = 0;

	return ret;
}

static int sensor_read_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != SENSOR_REG_END) {
		if (vals->reg_num == SENSOR_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = sensor_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
			if (vals->reg_num == SENSOR_BANK_REG) {
				val &= 0xe0;
				val = (vals->value & 0x1f);
				ret = sensor_write(sd, vals->reg_num, val);
				ret = sensor_read(sd, vals->reg_num, &val);
			}
			pr_debug("sensor_read_array ->> vals->reg_num:0x%02x, vals->reg_value:0x%02x\n",vals->reg_num, val);
		}
		vals++;
	}

	return 0;
}

static int sensor_write_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != SENSOR_REG_END) {
		if (vals->reg_num == SENSOR_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = sensor_write(sd, vals->reg_num, vals->value);
			if (ret < 0) {
				printk("sensor_write error  %d\n" ,__LINE__);
				return ret;
			}
		}
		vals++;
	}

	return 0;
}

static int sensor_reset(struct tx_isp_subdev *sd, int val)
{
	return 0;
}

static int sensor_detect(struct tx_isp_subdev *sd, unsigned int *ident)
{
	int ret;
	unsigned char v;
	ret = sensor_read(sd, 0x00, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0) {
		printk("err: ps5270 write error, ret= %d \n",ret);
		return ret;
	}
	if (v != SENSOR_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = sensor_read(sd, 0x01, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != SENSOR_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return 0;
}

static int sensor_set_integration_time(struct tx_isp_subdev *sd, int value)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = 0;
	unsigned int Cmd_OffNy = 0, Cmd_OffNep, Cmd_OffNe;

	Cmd_OffNy = sensor->video.attr->total_height - value - 1;
	Cmd_OffNep = NEPLS_LB + ((Cmd_OffNy*NEPLS_SCALE)>>8);
	Cmd_OffNep = (Cmd_OffNep > NEPLS_LB)?((Cmd_OffNep < NEPLS_UB)?Cmd_OffNep:NEPLS_UB):NEPLS_LB;
	Cmd_OffNe = NE_NEP_CONST - Cmd_OffNep;
	ret = sensor_write(sd, 0xef, 0x01);
	ret += sensor_write(sd, 0x0c, (unsigned char)((Cmd_OffNy & 0xff00) >> 8));
	ret += sensor_write(sd, 0x0d, (unsigned char)(Cmd_OffNy & 0xff));
	ret += sensor_write(sd, 0x0e, (unsigned char)((Cmd_OffNe & 0x0f00) >> 8));
	ret += sensor_write(sd, 0x0f, (unsigned char)(Cmd_OffNe & 0xff));
	ret += sensor_write(sd, 0x5F, (unsigned char)((Cmd_OffNep & 0x0100) >> 8));
	ret += sensor_write(sd, 0x60, (unsigned char)(Cmd_OffNep & 0xff));
	ret += sensor_write(sd, 0x09, 0x01);
	if (ret < 0)
		return ret;

	return 0;
}

static int sensor_set_analog_gain(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;
	unsigned int gain = value, sghd_patch = 1;
	static unsigned char tmp = 0;
	if (gain > AG_HS_MODE) {
		if (tmp == 1)
			sghd_patch = 0;
		tmp = 0;
	} else if (gain < AG_LS_MODE) {
		if (tmp == 0)
			sghd_patch = 0;
		tmp = 1;
	}
	if (tmp == 0)
		gain -= 64;		// For 4x ratio
	ret = sensor_write(sd, 0xef, 0x01);
	ret += sensor_write(sd, 0x83, (unsigned char)(gain & 0xff));
	ret += sensor_write(sd, 0x18, (unsigned char)(tmp & 0x01));
	if (g_sns_ver == 0x00)
		ret += sensor_write(sd, 0x97, sghd_patch);	// For HS/LS switching.
	ret += sensor_write(sd, 0x09, 0x01);
	if (ret < 0)
		return ret;

	return 0;
}

static int sensor_set_digital_gain(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int sensor_get_black_pedestal(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int sensor_init(struct tx_isp_subdev *sd, int enable)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	struct tx_isp_sensor_win_setting *wsize = NULL;
	int ret = 0;

	if (!enable)
		return ISP_SUCCESS;

	switch (sensor_raw_mode) {
	case SENSOR_RAW_MODE_LINEAR_20FPS:
		wsize = &sensor_win_sizes[0];
		break;
	case SENSOR_RAW_MODE_LINEAR_30FPS:
		wsize = &sensor_win_sizes[1];
		break;
	case SENSOR_RAW_MODE_NATIVE_WDR:
		wsize = &sensor_win_sizes[2];
		break;
	default:
		printk("Now we do not support this sensor raw mode!!!\n");
	}
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = V4L2_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;

	ret = sensor_write_array(sd, wsize->regs);
	if (ret)
		return ret;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	sensor->priv = wsize;

	ret += sensor_write(sd, 0xef, 0x01);
	ret += sensor_read(sd, 0x01, &g_sns_ver);
	if (ret)
		return ret;
	g_sns_ver &= 0x0f;

	return 0;
}

static int sensor_s_stream(struct tx_isp_subdev *sd, int enable)
{
	int ret = 0;

	if (enable) {
		ret = sensor_write_array(sd, sensor_stream_on);
		pr_debug("ps5270 stream on\n");
	} else {
		ret = sensor_write_array(sd, sensor_stream_off);
		pr_debug("ps5270 stream off\n");
	}

	return ret;
}

static int sensor_set_fps(struct tx_isp_subdev *sd, int fps)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	unsigned int pclk = 0;
	unsigned int hts = 0;
	unsigned int vts = 0;
	unsigned int Cmd_Lpf = 0;
	unsigned int Cur_OffNy = 0;
	unsigned int Cur_ExpLine = 0;
	unsigned char tmp;
	unsigned int newformat = 0; //the format is 24.8
	int ret = 0;

	/* the format of fps is 16/16. for example 25 << 16 | 2, the value is 25/2 fps. */
	newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
	if (newformat > (SENSOR_OUTPUT_MAX_FPS << 8) || newformat < (SENSOR_OUTPUT_MIN_FPS << 8)) {
		printk("warn: fps(%d) no in range\n", fps);
		return -1;
	}
	ret = sensor_write(sd, 0xef, 0x01);
	if (ret < 0)
		return -1;
	ret = sensor_read(sd, 0x27, &tmp);
	hts = tmp;
	ret += sensor_read(sd, 0x28, &tmp);
	if (ret < 0)
		return -1;

	hts = (((hts & 0x1f) << 8) | tmp) >> 1;
	if (sensor_raw_mode==SENSOR_RAW_MODE_LINEAR_20FPS) {
		pclk = SENSOR_SUPPORT_PCLK_FPS20;
	} else if (sensor_raw_mode==SENSOR_RAW_MODE_LINEAR_30FPS) {
		pclk = SENSOR_SUPPORT_PCLK_FPS30;
	} else if (sensor_raw_mode==SENSOR_RAW_MODE_NATIVE_WDR) {
		pclk = SENSOR_SUPPORT_PCLK_WDR;
	} else
		printk("Now ps5270 Do not support this sensor raw mode.\n");

	vts = (pclk * (fps & 0xffff) / hts / ((fps & 0xffff0000) >> 16));
	Cmd_Lpf = vts -1;
	ret = sensor_write(sd, 0xef, 0x01);
	ret += sensor_write(sd, 0x0b, (unsigned char)(Cmd_Lpf & 0xff));
	ret += sensor_write(sd, 0x0a, (unsigned char)(Cmd_Lpf >> 8));
	ret += sensor_write(sd, 0x09, 0x01);
	if (ret < 0) {
		printk("err: sensor_write err\n");
		return ret;
	}
	ret = sensor_read(sd, 0x0c, &tmp);
	Cur_OffNy = tmp;
	ret += sensor_read(sd, 0x0d, &tmp);
	if (ret < 0)
		return -1;
	Cur_OffNy = (((Cur_OffNy & 0xff) << 8) | tmp);
	Cur_ExpLine = sensor->video.attr->total_height - Cur_OffNy;

	sensor->video.fps = fps;
	sensor->video.attr->max_integration_time_native = vts - 2;
	sensor->video.attr->integration_time_limit = vts - 2;
	sensor->video.attr->total_height = vts;
	sensor->video.attr->max_integration_time = vts - 2;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	ret = sensor_set_integration_time(sd, Cur_ExpLine);
	if (ret < 0)
		return -1;

	return ret;
}

static int sensor_set_mode(struct tx_isp_subdev *sd, int value)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	struct tx_isp_sensor_win_setting *wsize = NULL;
	int ret = ISP_SUCCESS;

	if (value == TX_ISP_SENSOR_FULL_RES_MAX_FPS) {
		if (sensor_raw_mode == SENSOR_RAW_MODE_LINEAR_20FPS)
			wsize = &sensor_win_sizes[0];
		else if (sensor_raw_mode == SENSOR_RAW_MODE_LINEAR_30FPS)
			wsize = &sensor_win_sizes[1];
		else if (sensor_raw_mode == SENSOR_RAW_MODE_NATIVE_WDR)
			wsize = &sensor_win_sizes[2];
		else
			printk("Now ps5270 Do not support this sensor raw mode.\n");
	} else if (value == TX_ISP_SENSOR_PREVIEW_RES_MAX_FPS) {
		if (sensor_raw_mode == SENSOR_RAW_MODE_LINEAR_20FPS)
			wsize = &sensor_win_sizes[0];
		else if (sensor_raw_mode == SENSOR_RAW_MODE_LINEAR_30FPS)
			wsize = &sensor_win_sizes[1];
		else if (sensor_raw_mode == SENSOR_RAW_MODE_NATIVE_WDR)
			wsize = &sensor_win_sizes[2];
		else
			printk("Now ps5270 Do not support this sensor raw mode.\n");
	}

	if (wsize) {
		sensor->video.mbus.width = wsize->width;
		sensor->video.mbus.height = wsize->height;
		sensor->video.mbus.code = wsize->mbus_code;
		sensor->video.mbus.field = V4L2_FIELD_NONE;
		sensor->video.mbus.colorspace = wsize->colorspace;
		sensor->video.fps = wsize->fps;
		ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	}

	return ret;
}

static int sensor_set_vflip(struct tx_isp_subdev *sd, int enable)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = 0;
	unsigned char val = 0;

	ret = sensor_write(sd, 0xef, 0x01);
	ret += sensor_read(sd, 0x1d, &val);
	if (enable)
		val = val | 0x80;
	else
		val = val & 0x7f;

	ret += sensor_write(sd, 0xef, 0x01);
	ret += sensor_write(sd, 0x1d, val);
	ret += sensor_write(sd, 0x09, 0x01);
	sensor->video.mbus_change = 0;
	if (!ret)
		ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	return ret;
}

static int sensor_g_chip_ident(struct tx_isp_subdev *sd,
			       struct tx_isp_chip_ident *chip)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned int ident = 0;
	int ret = ISP_SUCCESS;

	if (pwdn_gpio != -1) {
		ret = private_gpio_request(pwdn_gpio,"sensor_pwdn");
		if (!ret) {
			private_gpio_direction_output(pwdn_gpio, 1);
			private_msleep(50);
			private_gpio_direction_output(pwdn_gpio, 0);
			private_msleep(10);
		} else {
			printk("gpio requrest fail %d\n",pwdn_gpio);
		}
	}
	if (reset_gpio != -1) {
		ret = private_gpio_request(reset_gpio,"sensor_reset");
		if (!ret) {
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(5);
			private_gpio_direction_output(reset_gpio, 0);
			private_msleep(10);
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(20);
		} else {
			printk("gpio requrest fail %d\n",reset_gpio);
		}
	}

	ret = sensor_detect(sd, &ident);
	if (ret) {
		printk("chip found @ 0x%x (%s) is not an ps5270 chip.\n",
				client->addr, client->adapter->name);
		return ret;
	}
	printk("ov2735 chip found @ 0x%02x (%s)\n", client->addr, client->adapter->name);
	if (chip) {
		memcpy(chip->name, "ps5270", sizeof("ps5270"));
		chip->ident = ident;
		chip->revision = SENSOR_VERSION;
	}

	return 0;
}

static int sensor_sensor_ops_ioctl(struct tx_isp_subdev *sd, unsigned int cmd, void *arg)
{
	long ret = 0;
	if (IS_ERR_OR_NULL(sd)) {
		printk("[%d]The pointer is invalid!\n", __LINE__);
		return -EINVAL;
	}
	switch(cmd) {
	case TX_ISP_EVENT_SENSOR_INT_TIME:
		if (arg)
			ret = sensor_set_integration_time(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_AGAIN:
		if (arg)
			ret = sensor_set_analog_gain(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_DGAIN:
		if (arg)
			ret = sensor_set_digital_gain(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_BLACK_LEVEL:
		if (arg)
			ret = sensor_get_black_pedestal(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_RESIZE:
		if (arg)
			ret = sensor_set_mode(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_PREPARE_CHANGE:
		ret = sensor_write_array(sd, sensor_stream_off);
		break;
	case TX_ISP_EVENT_SENSOR_FINISH_CHANGE:
		ret = sensor_write_array(sd, sensor_stream_on);
		break;
	case TX_ISP_EVENT_SENSOR_FPS:
		if (arg)
			ret = sensor_set_fps(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_VFLIP:
		if (arg)
			ret = sensor_set_vflip(sd, *(int*)arg);
		break;
	default:
		break;;
	}
	return 0;
}

static int sensor_g_register(struct tx_isp_subdev *sd, struct tx_isp_dbg_register *reg)
{
	unsigned char val = 0;
	int len = 0;
	int ret = 0;

	len = strlen(sd->chip.name);
	if (len && strncmp(sd->chip.name, reg->name, len))
		return -EINVAL;
	if (!private_capable(CAP_SYS_ADMIN))
		return -EPERM;
	ret = sensor_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 2;

	return ret;
}

static int sensor_s_register(struct tx_isp_subdev *sd, const struct tx_isp_dbg_register *reg)
{
	int len = 0;

	len = strlen(sd->chip.name);
	if (len && strncmp(sd->chip.name, reg->name, len))
		return -EINVAL;
	if (!private_capable(CAP_SYS_ADMIN))
		return -EPERM;
	sensor_write(sd, reg->reg & 0xffff, reg->val & 0xff);

	return 0;
}

static struct tx_isp_subdev_core_ops sensor_core_ops = {
	.g_chip_ident = sensor_g_chip_ident,
	.reset = sensor_reset,
	.init = sensor_init,
	.g_register = sensor_g_register,
	.s_register = sensor_s_register,
};

static struct tx_isp_subdev_video_ops sensor_video_ops = {
	.s_stream = sensor_s_stream,
};

static struct tx_isp_subdev_sensor_ops	sensor_sensor_ops = {
	.ioctl = sensor_sensor_ops_ioctl,
};
static struct tx_isp_subdev_ops sensor_ops = {
	.core = &sensor_core_ops,
	.video = &sensor_video_ops,
	.sensor = &sensor_sensor_ops,
};

/* It's the sensor device */
static u64 tx_isp_module_dma_mask = ~(u64)0;
struct platform_device sensor_platform_device = {
	.name = "ps5270",
	.id = -1,
	.dev = {
		.dma_mask = &tx_isp_module_dma_mask,
		.coherent_dma_mask = 0xffffffff,
		.platform_data = NULL,
	},
	.num_resources = 0,
};


static int sensor_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct tx_isp_subdev *sd;
	struct tx_isp_video_in *video;
	struct tx_isp_sensor *sensor;
	struct tx_isp_sensor_win_setting *wsize = &sensor_win_sizes[0];
	int ret;
	int i=0;
	enum v4l2_mbus_pixelcode mbus;

	sensor = (struct tx_isp_sensor *)kzalloc(sizeof(*sensor), GFP_KERNEL);
	if (!sensor) {
		printk("Failed to allocate sensor subdev.\n");
		return -ENOMEM;
	}
	memset(sensor, 0 ,sizeof(*sensor));
	/* request mclk of sensor */
	sensor->mclk = clk_get(NULL, "cgu_cim");
	if (IS_ERR(sensor->mclk)) {
		printk("Cannot get sensor input clock cgu_cim\n");
		goto err_get_mclk;
	}
	clk_set_rate(sensor->mclk, 24000000);
	clk_enable(sensor->mclk);

	ret = set_sensor_gpio_function(sensor_gpio_func);
	if (ret < 0)
		goto err_set_sensor_gpio;
	sensor_attr.dvp.gpio = sensor_gpio_func;

	switch(sensor_gpio_func) {
	case DVP_PA_LOW_10BIT:
	case DVP_PA_HIGH_10BIT:
		mbus = sensor_mbus_code[0];
		break;
	case DVP_PA_12BIT:
		mbus = sensor_mbus_code[1];
		break;
	default:
		goto err_set_sensor_gpio;
	}

	for(i = 0; i < ARRAY_SIZE(sensor_win_sizes); i++)
		sensor_win_sizes[i].mbus_code = mbus;

	/*
	  convert sensor-gain into isp-gain,
	*/
	sd = &sensor->sd;
	video = &sensor->video;
	switch(sensor_raw_mode) {
	case SENSOR_RAW_MODE_LINEAR_20FPS:
		wsize = &sensor_win_sizes[0];
		sensor->video.attr = &sensor_attr;
		break;
	case SENSOR_RAW_MODE_LINEAR_30FPS:
		wsize = &sensor_win_sizes[1];
		sensor->video.attr = &sensor_attr_linear_30fps;
		break;
	case SENSOR_RAW_MODE_NATIVE_WDR:
		wsize = &sensor_win_sizes[2];
		sensor->video.attr = &sensor_attr_wdr_20fps;
		break;
	default:
		printk("Now ps5270 Do not support this sensor raw mode.\n");
		break;
	}
	sensor->video.mbus_change = 0;
	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = V4L2_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	tx_isp_subdev_init(&sensor_platform_device, sd, &sensor_ops);
	tx_isp_set_subdevdata(sd, client);
	tx_isp_set_subdev_hostdata(sd, sensor);
	private_i2c_set_clientdata(client, sd);

	pr_debug("probe ok ------->ps5270\n");

	return 0;
err_set_sensor_gpio:
	private_clk_disable(sensor->mclk);
	private_clk_put(sensor->mclk);
err_get_mclk:
	kfree(sensor);

	return -1;
}

static int sensor_remove(struct i2c_client *client)
{
	struct tx_isp_subdev *sd = private_i2c_get_clientdata(client);
	struct tx_isp_sensor *sensor = tx_isp_get_subdev_hostdata(sd);

	if (reset_gpio != -1)
		private_gpio_free(reset_gpio);
	if (pwdn_gpio != -1)
		private_gpio_free(pwdn_gpio);

	private_clk_disable(sensor->mclk);
	private_clk_put(sensor->mclk);
	tx_isp_subdev_deinit(sd);
	kfree(sensor);

	return 0;
}

static const struct i2c_device_id sensor_id[] = {
	{ "ps5270", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "ps5270",
	},
	.probe = sensor_probe,
	.remove = sensor_remove,
	.id_table = sensor_id,
};

static __init int init_sensor(void)
{
	int ret = 0;
	ret = private_driver_get_interface();
	if (ret) {
		printk("Failed to init ps5270 driver.\n");
		return -1;
	}

	return private_i2c_add_driver(&sensor_driver);
}

static __exit void exit_sensor(void)
{
	private_i2c_del_driver(&sensor_driver);
}

module_init(init_sensor);
module_exit(exit_sensor);

MODULE_DESCRIPTION("A low-level driver for Primesensor ps5270 sensors");
MODULE_LICENSE("GPL");
