/*
 * gc4663.c
 *
 * Copyright (C) 2022 Ingenic Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Settings:
 * sboot        resolution      fps     interface              mode
 *   0          2560*1440       25        mipi_2lane           linear
 *   1          2560*1440       30        mipi_2lane           hdr
 */
/* #define DEBUG */
#define __WDR__

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

#define MCLK 27000000
#define SENSOR_CHIP_ID_H (0x46)
#define SENSOR_CHIP_ID_L (0x53)
#define SENSOR_REG_END 0xffff
#define SENSOR_REG_DELAY 0x0000
#define SENSOR_SUPPORT_30FPS_SCLK (144 * 1000 * 1000)
#define SENSOR_SUPPORT_30FPS_SCLK_HDR (132000000)
#define SENSOR_OUTPUT_MIN_FPS 5
#define SENSOR_VERSION "H20230928a"

static int wdr_bufsize = 2 * 3000 * 188;

struct regval_list {
	uint16_t reg_num;
	unsigned char value;
};

/*
 * the part of driver maybe modify about different sensor and different board.
 */
struct again_lut {
	unsigned int index;
	unsigned char reg2b3;
	unsigned char reg2b4;
	unsigned char reg2b8;
	unsigned char reg2b9;
	unsigned char reg515;
	unsigned char reg519;
	unsigned char reg2d9;
	unsigned int gain;
};

struct again_lut sensor_again_lut[] = {
	{0x00, 0x00, 0x0, 0x01, 0x00, 0x30, 0x1e, 0x5c, 0},   // 1.000000
	{0x01, 0x20, 0x0, 0x01, 0x0b, 0x30, 0x1e, 0x5c, 14995},   // 1.171875
	{0x02, 0x01, 0x0, 0x01, 0x19, 0x30, 0x1d, 0x5b, 31177},   // 1.390625
	{0x03, 0x21, 0x0, 0x01, 0x2a, 0x30, 0x1e, 0x5c, 47704},   // 1.656250
	{0x04, 0x02, 0x0, 0x02, 0x00, 0x30, 0x1e, 0x5c, 65536},   // 2.000000
	{0x05, 0x22, 0x0, 0x02, 0x17, 0x30, 0x1d, 0x5b, 81159},   // 2.359375
	{0x06, 0x03, 0x0, 0x02, 0x33, 0x20, 0x16, 0x54, 97243},   // 2.796875
	{0x07, 0x23, 0x0, 0x03, 0x14, 0x20, 0x17, 0x55, 113240},   // 3.312500
	{0x08, 0x04, 0x0, 0x04, 0x00, 0x20, 0x17, 0x55, 131072},   // 4.000000
	{0x09, 0x24, 0x0, 0x04, 0x2f, 0x20, 0x19, 0x57, 147007},   // 4.734375
	{0x0a, 0x05, 0x0, 0x05, 0x26, 0x20, 0x19, 0x57, 162779},   // 5.593750
	{0x0b, 0x25, 0x0, 0x06, 0x28, 0x20, 0x1b, 0x59, 178776},   // 6.625000
	{0x0c, 0x0c, 0x0, 0x08, 0x00, 0x20, 0x1d, 0x5b, 196608},   // 8.000000
	{0x0d, 0x2c, 0x0, 0x09, 0x1e, 0x20, 0x1f, 0x5d, 212543},   // 9.468750
	{0x0e, 0x0d, 0x0, 0x0b, 0x0c, 0x20, 0x21, 0x5f, 228315},   // 11.187500
	{0x0f, 0x2d, 0x0, 0x0d, 0x11, 0x20, 0x24, 0x62, 244423},   // 13.265625
	{0x10, 0x1c, 0x0, 0x10, 0x00, 0x20, 0x26, 0x64, 262144},   // 16.000000
	{0x11, 0x3c, 0x0, 0x12, 0x3d, 0x18, 0x2a, 0x68, 278158},   // 18.953125
	{0x12, 0x5c, 0x0, 0x16, 0x19, 0x18, 0x2c, 0x6a, 293916},   // 22.390625
	{0x13, 0x7c, 0x0, 0x1a, 0x22, 0x18, 0x2e, 0x6c, 309959},   // 26.531250
	{0x14, 0x9c, 0x0, 0x20, 0x00, 0x18, 0x32, 0x70, 327680},   // 32.000000
	{0x15, 0xbc, 0x0, 0x25, 0x3a, 0x18, 0x35, 0x73, 343694},   // 37.906250
	{0x16, 0xdc, 0x0, 0x2c, 0x33, 0x10, 0x36, 0x74, 359485},   // 44.796875
	{0x17, 0xfc, 0x0, 0x35, 0x05, 0x10, 0x38, 0x76, 375523},   // 53.078125
	{0x18, 0x1c, 0x1, 0x40, 0x00, 0x10, 0x3c, 0x7a, 393216},   // 64.000000
	{0x19, 0x3c, 0x1, 0x4b, 0x35, 0x10, 0x42, 0x80, 409249},   // 75.828125
};
struct again_lut sensor_again_lut_60fps[] = {
	{0x00, 0x00, 0x0, 0x01, 0x00, 0x30, 0x28, 0x66, 0},   // 1.000000
	{0x01, 0x20, 0x0, 0x01, 0x0b, 0x30, 0x2a, 0x68, 14995},   // 1.171875
	{0x02, 0x01, 0x0, 0x01, 0x19, 0x30, 0x27, 0x65, 31177},   // 1.390625
	{0x03, 0x21, 0x0, 0x01, 0x2a, 0x30, 0x29, 0x67, 47704},   // 1.656250
	{0x04, 0x02, 0x0, 0x02, 0x00, 0x30, 0x27, 0x65, 65536},   // 2.000000
	{0x05, 0x22, 0x0, 0x02, 0x17, 0x30, 0x29, 0x67, 81159},   // 2.359375
	{0x06, 0x03, 0x0, 0x02, 0x33, 0x30, 0x28, 0x66, 97243},   // 2.796875
	{0x07, 0x23, 0x0, 0x03, 0x14, 0x30, 0x2a, 0x68, 113240},   // 3.312500
	{0x08, 0x04, 0x0, 0x04, 0x00, 0x30, 0x2a, 0x68, 131072},   // 4.000000
	{0x09, 0x24, 0x0, 0x04, 0x2f, 0x30, 0x2b, 0x69, 147007},   // 4.734375
	{0x0a, 0x05, 0x0, 0x05, 0x26, 0x30, 0x2c, 0x6a, 162779},   // 5.593750
	{0x0b, 0x25, 0x0, 0x06, 0x28, 0x30, 0x2e, 0x6c, 178776},   // 6.625000
	{0x0c, 0x06, 0x0, 0x08, 0x00, 0x30, 0x2f, 0x6d, 196608},   // 8.000000
	{0x0d, 0x26, 0x0, 0x09, 0x1e, 0x30, 0x31, 0x6f, 212543},   // 9.468750
	{0x0e, 0x46, 0x0, 0x0b, 0x0c, 0x30, 0x34, 0x72, 228315},   // 11.187500
	{0x0f, 0x66, 0x0, 0x0d, 0x11, 0x30, 0x37, 0x75, 244423},   // 13.265625
	{0x10, 0x0e, 0x0, 0x10, 0x00, 0x30, 0x3a, 0x78, 262144},   // 16.000000
	{0x11, 0x2e, 0x0, 0x12, 0x3d, 0x30, 0x3e, 0x7c, 278158},   // 18.953125
	{0x12, 0x4e, 0x0, 0x16, 0x19, 0x30, 0x41, 0x7f, 293916},   // 22.390625
	{0x13, 0x6e, 0x0, 0x1a, 0x22, 0x30, 0x45, 0x83, 309959},   // 26.531250
	{0x14, 0x1e, 0x0, 0x20, 0x00, 0x30, 0x49, 0x87, 327680},   // 32.000000
	{0x15, 0x3e, 0x0, 0x25, 0x3a, 0x30, 0x4d, 0x8b, 343694},   // 37.906250
	{0x16, 0x5e, 0x0, 0x2c, 0x33, 0x30, 0x53, 0x91, 359485},   // 44.796875
	{0x17, 0x7e, 0x0, 0x35, 0x05, 0x30, 0x5a, 0x98, 375523},   // 53.078125
	{0x18, 0x9e, 0x0, 0x40, 0x00, 0x30, 0x60, 0x9e, 393216},   // 64.000000
	{0x19, 0xbe, 0x0, 0x4b, 0x35, 0x30, 0x67, 0xa5, 409249},   // 75.828125
};

struct tx_isp_sensor_attribute sensor_attr;

unsigned int sensor_alloc_integration_time(unsigned int it, unsigned char shift, unsigned int *sensor_it)
{
	unsigned int expo = it >> shift;
	unsigned int isp_it = it;

	*sensor_it = expo;

	return isp_it;
}
unsigned int sensor_alloc_integration_time_short(unsigned int it, unsigned char shift, unsigned int *sensor_it)
{
	unsigned int expo = it >> shift;
	unsigned int isp_it = it;

	*sensor_it = expo;

	return isp_it;
}

unsigned int sensor_alloc_again(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
	struct again_lut *lut = sensor_again_lut;
	while (lut->gain <= sensor_attr.max_again) {
		if (isp_gain == 0) {
			*sensor_again = 0;
			return lut[0].gain;
		} else if (isp_gain < lut->gain) {
			*sensor_again = (lut - 1)->index;
			return (lut - 1)->gain;
		} else {
			if ((lut->gain == sensor_attr.max_again) && (isp_gain >= lut->gain)) {
				*sensor_again = lut->index;
				return lut->gain;
			}
		}

		lut++;
	}

	return 0;
}
unsigned int sensor_alloc_again_short(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
	struct again_lut *lut = sensor_again_lut;
	while (lut->gain <= sensor_attr.max_again_short) {
		if (isp_gain == 0) {
			*sensor_again = 0;
			return 0;
		}
		else if (isp_gain < lut->gain) {
			*sensor_again = (lut - 1)->gain;
			return (lut - 1)->gain;
		}
		else {
			if ((lut->gain == sensor_attr.max_again_short) && (isp_gain >= lut->gain)) {
				*sensor_again = lut->index;
				return lut->gain;
			}
		}

		lut++;
	}

	return isp_gain;
}

unsigned int sensor_alloc_dgain(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_dgain)
{
	return 0;
}

struct tx_isp_mipi_bus sensor_mipi_linear = {
	.mode = SENSOR_MIPI_OTHER_MODE,
	.clk = 848,
	.lans = 2,
	.settle_time_apative_en = 0,
	.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,//RAW10
	.mipi_sc.hcrop_diff_en = 0,
	.mipi_sc.mipi_vcomp_en = 0,
	.mipi_sc.mipi_hcomp_en = 0,
	.image_twidth = 2560,
	.image_theight = 1440,
	.mipi_sc.mipi_crop_start0x = 0,
	.mipi_sc.mipi_crop_start0y = 0,
	.mipi_sc.mipi_crop_start1x = 0,
	.mipi_sc.mipi_crop_start1y = 0,
	.mipi_sc.mipi_crop_start2x = 0,
	.mipi_sc.mipi_crop_start2y = 0,
	.mipi_sc.mipi_crop_start3x = 0,
	.mipi_sc.mipi_crop_start3y = 0,
	.mipi_sc.line_sync_mode = 0,
	.mipi_sc.work_start_flag = 0,
	.mipi_sc.data_type_en = 0,
	.mipi_sc.data_type_value = 0,
	.mipi_sc.del_start = 0,
	.mipi_sc.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE,
	.mipi_sc.sensor_fid_mode = 0,
	.mipi_sc.sensor_mode = TX_SENSOR_DEFAULT_MODE,
};


struct tx_isp_mipi_bus sensor_mipi_linear_120fps = {
	.mode = SENSOR_MIPI_OTHER_MODE,
	.clk = 168,
	.lans = 2,
	.settle_time_apative_en = 0,
	.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,//RAW10
	.mipi_sc.hcrop_diff_en = 0,
	.mipi_sc.mipi_vcomp_en = 0,
	.mipi_sc.mipi_hcomp_en = 0,
	.image_twidth = 1280,
	.image_theight = 720,
	.mipi_sc.mipi_crop_start0x = 0,
	.mipi_sc.mipi_crop_start0y = 0,
	.mipi_sc.mipi_crop_start1x = 0,
	.mipi_sc.mipi_crop_start1y = 0,
	.mipi_sc.mipi_crop_start2x = 0,
	.mipi_sc.mipi_crop_start2y = 0,
	.mipi_sc.mipi_crop_start3x = 0,
	.mipi_sc.mipi_crop_start3y = 0,
	.mipi_sc.line_sync_mode = 0,
	.mipi_sc.work_start_flag = 0,
	.mipi_sc.data_type_en = 0,
	.mipi_sc.data_type_value = 0,
	.mipi_sc.del_start = 0,
	.mipi_sc.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE,
	.mipi_sc.sensor_fid_mode = 0,
	.mipi_sc.sensor_mode = TX_SENSOR_DEFAULT_MODE,
};


struct tx_isp_mipi_bus sensor_mipi_dol = {
	.mode = SENSOR_MIPI_OTHER_MODE,
	.clk = 1344,
	.lans = 2,
	.settle_time_apative_en = 0,
	.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,//RAW10
	.mipi_sc.hcrop_diff_en = 0,
	.mipi_sc.mipi_vcomp_en = 0,
	.mipi_sc.mipi_hcomp_en = 0,
	.image_twidth = 2560,
	.image_theight = 1440,
	.mipi_sc.mipi_crop_start0x = 0,
	.mipi_sc.mipi_crop_start0y = 0,
	.mipi_sc.mipi_crop_start1x = 0,
	.mipi_sc.mipi_crop_start1y = 0,
	.mipi_sc.mipi_crop_start2x = 0,
	.mipi_sc.mipi_crop_start2y = 0,
	.mipi_sc.mipi_crop_start3x = 0,
	.mipi_sc.mipi_crop_start3y = 0,
	.mipi_sc.line_sync_mode = 0,
	.mipi_sc.work_start_flag = 0,
	.mipi_sc.data_type_en = 0,
	.mipi_sc.data_type_value = 0,
	.mipi_sc.del_start = 0,
	.mipi_sc.sensor_frame_mode = TX_SENSOR_WDR_2_FRAME_MODE,
	.mipi_sc.sensor_fid_mode = 0,
	.mipi_sc.sensor_mode = TX_SENSOR_VC_MODE,
};

struct tx_isp_sensor_attribute sensor_attr={
	.name = "gc4663",
	.chip_id = 0x4653,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = TISP_SBUS_MASK_SAMPLE_8BITS | TISP_SBUS_MASK_ADDR_16BITS,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI,
	.data_type = TX_SENSOR_DATA_TYPE_WDR_DOL,
	.cbus_device = 0x29,
	.max_again = 409249,
	.max_dgain = 0,
	.expo_fs = 1,
	.min_integration_time = 2,
	.min_integration_time_short = 2,
	.min_integration_time_native = 2,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 2,
	.sensor_ctrl.alloc_again = sensor_alloc_again,
	.sensor_ctrl.alloc_again_short = sensor_alloc_again_short,
	.sensor_ctrl.alloc_dgain = sensor_alloc_dgain,
	.sensor_ctrl.alloc_integration_time_short = sensor_alloc_integration_time_short,
};

static struct regval_list sensor_init_regs_2560_1440_25fps_mipi[] = {
	/*SYSTEM*/
	{0x03fe, 0xf0},
	{0x03fe, 0x00},
	{0x0317, 0x00},
	{0x0320, 0x77},
	{0x0324, 0xc8},
	{0x0325, 0x06},
	{0x0326, 0x60},
	{0x0327, 0x03},
	{0x0334, 0x40},
	{0x0336, 0x60},
	{0x0337, 0x82},
	{0x0315, 0x25},
	{0x031c, 0xc6},
	{0x0287, 0x18},
	{0x0084, 0x00},
	{0x0087, 0x50},
	{0x029d, 0x08},
	{0x0290, 0x00},
	{0x0340, 0x07},//vts
	{0x0341, 0x80},
	{0x0345, 0x06},
	{0x034b, 0xb0},
	{0x0352, 0x08},
	{0x0354, 0x08},
	{0x02d1, 0xe0},
	{0x0223, 0xf2},
	{0x0238, 0xa4},
	{0x02ce, 0x7f},
	{0x0232, 0xc4},
	{0x02d3, 0x01},/*fwc*/
	{0x0243, 0x06},
	{0x02ee, 0x30},
	{0x026f, 0x70},
	{0x0257, 0x09},
	{0x0211, 0x02},
	{0x0219, 0x09},
	{0x023f, 0x2d},
	{0x0518, 0x00},
	{0x0519, 0x01},
	{0x0515, 0x08},
	{0x02d9, 0x3f},
	{0x02da, 0x02},
	{0x02db, 0xe8},
	{0x02e6, 0x20},
	{0x021b, 0x10},
	{0x0252, 0x22},
	{0x024e, 0x22},
	{0x02c4, 0x01},
	{0x021d, 0x17},
	{0x024a, 0x01},
	{0x02ca, 0x02},
	{0x0262, 0x10},
	{0x029a, 0x20},
	{0x021c, 0x0e},
	{0x0298, 0x03},
	{0x029c, 0x00},
	{0x027e, 0x14},
	{0x02c2, 0x10},
	{0x0540, 0x20},
	{0x0546, 0x01},
	{0x0548, 0x01},
	{0x0544, 0x01},
	{0x0242, 0x1b},
	{0x02c0, 0x1b},
	{0x02c3, 0x40},/*0x20 -> 0x40*/
	{0x02e4, 0x10},
	{0x022e, 0x00},
	{0x027b, 0x3f},
	{0x0269, 0x0f},
	{0x02d2, 0x40},
	{0x027c, 0x08},
	{0x023a, 0x2e},
	{0x0245, 0xce},
	{0x0530, 0x20},
	{0x0531, 0x02},
	{0x0228, 0x50},
	{0x02ab, 0x00},
	{0x0250, 0x00},
	{0x0221, 0x50},
	{0x02ac, 0x00},
	{0x02a5, 0x02},
	{0x0260, 0x0b},
	{0x0216, 0x04},
	{0x0299, 0x1C},
	{0x02bb, 0x0d},
	{0x02a3, 0x02},
	{0x02a4, 0x02},
	{0x021e, 0x02},
	{0x024f, 0x08},
	{0x028c, 0x08},
	{0x0532, 0x3f},
	{0x0533, 0x02},
	{0x0277, 0xc0},
	{0x0276, 0xc0},
	{0x0239, 0xc0},
	{0x0202, 0x05},
	{0x0203, 0xd0},
	{0x0205, 0xc0},
	{0x02b0, 0x68},
	{0x0002, 0xa9},
	{0x0004, 0x00},/*bit0: dpc on/off*/
	{0x021a, 0x98},
	{0x0266, 0xa0},
	{0x0020, 0x01},
	{0x0021, 0x03},
	{0x0022, 0x00},
	{0x0023, 0x04},
	{0x0342, 0x05},//hts
	{0x0343, 0xdc},
	{0x03fe, 0x10},
	{0x03fe, 0x00},
	{0x0106, 0x78},
	{0x0108, 0x0c},
	{0x0114, 0x01},
	{0x0115, 0x12},
	{0x0180, 0x46},
	{0x0181, 0x30},
	{0x0182, 0x05},
	{0x0185, 0x01},
	{0x03fe, 0x10},
	{0x03fe, 0x00},
	{0x0100, 0x09},
	{0x000f, 0x10},/*for flip dpc*/
	/*** otp ***/
	{0x0080,0x02},
	{0x0097,0x0a},
	{0x0098,0x10},
	{0x0099,0x05},
	{0x009a,0xb0},
	{0x0317,0x08},
	{0x0a67,0x80},
	{0x0a70,0x03},
	{0x0a82,0x00},
	{0x0a83,0x10},
	{0x0a80,0x2b},
	{0x05be,0x00},
	{0x05a9,0x01},
	{0x0313,0x80},
	{0x05be,0x01},
	{0x0317,0x00},
	{0x0a67,0x00},

	{SENSOR_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list sensor_init_regs_2560_1440_30fps_mipi_dol[] = {
	{0x03fe,0xf0},
	{0x03fe,0x00},
	{0x0317,0x00},
	{0x0320,0x77},
	{0x0324,0xc4},
	{0x0326,0x3a},
	{0x0327,0x03},
	{0x0321,0x10},
	{0x0314,0x50},
	{0x0334,0x40},
	{0x0335,0xd1},
	{0x0336,0x63},
	{0x0337,0x82},
	{0x0315,0x33},
	{0x031c,0xce},
	{0x0287,0x18},
	{0x0084,0x00},
	{0x0087,0x50},
	{0x029d,0x08},
	{0x0290,0x00},
	{0x0340,0x06},//
	{0x0341,0x40},/*vts 0x640 === > 20fps 0x960*/
	{0x0345,0x06},
	{0x034b,0xb0},
	{0x0352,0x08},
	{0x0354,0x08},
	{0x02d1,0xc0},
	{0x023c,0x04},
	{0x0238,0xb4},
	{0x0223,0xfb},
	{0x0232,0xc4},
	{0x0279,0x53},
	{0x02d3,0x01},
	{0x0243,0x06},
	{0x02ce,0xbf},
	{0x02ee,0x30},
	{0x026f,0x70},
	{0x0257,0x09},
	{0x0211,0x02},
	{0x0219,0x09},
	{0x023f,0x2d},
	{0x0518,0x00},
	{0x0519,0x14},
	{0x0515,0x18},
	{0x02d9,0x50},
	{0x02da,0x02},
	{0x02db,0xe8},
	{0x02e6,0x20},
	{0x021b,0x10},
	{0x0252,0x22},
	{0x024e,0x22},
	{0x02c4,0x01},
	{0x021d,0x17},
	{0x024a,0x01},
	{0x02ca,0x02},
	{0x0262,0x10},
	{0x029a,0x20},
	{0x021c,0x0e},
	{0x0298,0x03},
	{0x029c,0x00},
	{0x027e,0x14},
	{0x02c2,0x10},
	{0x0540,0x20},
	{0x0546,0x01},
	{0x0548,0x01},
	{0x0544,0x01},
	{0x0242,0x36},
	{0x02c0,0x36},
	{0x02c3,0x4d},
	{0x02e4,0x10},
	{0x022e,0x00},
	{0x027b,0x3f},
	{0x0269,0x0f},
	{0x02d2,0x40},
	{0x027c,0x08},
	{0x023a,0x2e},
	{0x0245,0xce},
	{0x0530,0x3f},
	{0x0531,0x02},
	{0x0228,0x50},
	{0x02ab,0x00},
	{0x0250,0x00},
	{0x0221,0x50},
	{0x02ac,0x00},
	{0x02a5,0x02},
	{0x0260,0x0b},
	{0x0216,0x04},
	{0x0299,0x1C},
	{0x021a,0x98},
	{0x0266,0xd0},
	{0x0020,0x01},
	{0x0021,0x05},
	{0x0022,0xc0},
	{0x0023,0x08},

	{0x0098,0x10},
	{0x009a,0xb0},
	{0x02bb,0x0d},
	{0x02a3,0x02},
	{0x02a4,0x02},
	{0x021e,0x02},
	{0x024f,0x08},
	{0x028c,0x08},
	{0x0532,0x3f},
	{0x0533,0x02},
	{0x0277,0x70},//tx_width
	{0x0276,0xc0},
	{0x0239,0xc0},
	{0x0200,0x00},
	{0x0201,0x50},
	{0x0202,0x05},
	{0x0203,0x00},
	{0x0205,0xc0},
	{0x02b0,0x68},
	{0x000f,0x10},/*for flip*/
	{0x0006,0xe0},
	{0x0002,0xa9},
	{0x0004,0x00},/*bit0: dpc on/off*/
	{0x0060,0x40},
	{0x0218,0x12},
	{0x0342,0x05},
	{0x0343,0x5f},
	{0x03fe,0x10},
	{0x03fe,0x00},
	{0x0106,0x78},
	{0x0107,0x89},
	{0x0108,0x0c},
	{0x0114,0x01},
	{0x0115,0x12},
	{0x0180,0x4f},
	{0x0181,0x30},
	{0x0182,0x05},
	{0x0185,0x01},
	{0x03fe,0x10},
	{0x03fe,0x00},
	{0x0100,0x09},
	//otp
        {0x0129,0x0a},
	{0x0080,0x02},
	{0x0097,0x0a},
	{0x0098,0x10},
	{0x0099,0x05},
	{0x009a,0xb0},
	{0x0317,0x08},
	{0x0a67,0x80},
	{0x0a70,0x03},
	{0x0a82,0x00},
	{0x0a83,0x10},
	{0x0a80,0x2b},
	{0x05be,0x00},
	{0x05a9,0x01},
	{0x0313,0x80},
	{0x05be,0x01},
	{0x0317,0x00},
	{0x0a67,0x00},
	{SENSOR_REG_END, 0x00},	/* END MARKER */
};


static struct regval_list sensor_init_regs_1280_720_120fps_mipi[] = {
    {0x03fe,0xf0},
    {0x03fe,0x00},
    {0x0317,0x00},
    {0x0320,0x77},
    {0x0324,0xc4},
    {0x0326,0x42},
    {0x0327,0x03},
    {0x0321,0x10},
    {0x0314,0x50},
    {0x0334,0x40},
    {0x0335,0xd5},
    {0x0336,0x70},
    {0x0337,0x82},
    {0x0315,0x33},
    {0x031c,0xce},
    {0x0287,0x18},
    {0x0084,0x00},
    {0x0087,0x50},
    {0x029d,0x08},
    {0x0290,0x00},
    {0x0234,0x20},
    {0x0217,0xc0},
    {0x0340,0x06},
    {0x0341,0x40},
    {0x0345,0x06},
    {0x034b,0xb0},
    {0x034c,0x05},
    {0x034e,0x02},
    {0x034f,0xd0},
    {0x0352,0x04},
    {0x0354,0x04},
    {0x02d1,0xc0},
    {0x023c,0x04},
    {0x0238,0xb4},
    {0x0223,0xfb},
    {0x0232,0xc4},
    {0x0279,0x53},
    {0x02d3,0x01},
    {0x0243,0x06},
    {0x02ce,0xbf},
    {0x02ee,0x30},
    {0x026f,0x70},
    {0x0257,0x09},
    {0x0211,0x02},
    {0x0219,0x09},
    {0x023f,0x2d},
    {0x0518,0x00},
    {0x0519,0x14},
    {0x0515,0x18},
    {0x02d9,0x50},
    {0x02da,0x02},
    {0x02db,0xe8},
    {0x02e6,0x20},
    {0x021b,0x10},
    {0x0252,0x22},
    {0x024e,0x22},
    {0x02c4,0x01},
    {0x021d,0x17},
    {0x024a,0x01},
    {0x02ca,0x02},
    {0x0262,0x10},
    {0x029a,0x20},
    {0x021c,0x0e},
    {0x0298,0x03},
    {0x029c,0x00},
    {0x027e,0x14},
    {0x02c2,0x10},
    {0x0540,0x20},
    {0x0546,0x01},
    {0x0548,0x01},
    {0x0544,0x01},
    {0x0242,0x36},
    {0x02c0,0x36},
    {0x02c3,0x4d},
    {0x02e4,0x10},
    {0x022e,0x00},
    {0x027b,0x3f},
    {0x0269,0x0f},
    {0x02d2,0x40},
    {0x027c,0x08},
    {0x023a,0x2e},
    {0x0245,0xce},
    {0x0530,0x3f},
    {0x0531,0x02},
    {0x0228,0x50},
    {0x02ab,0x00},
    {0x0250,0x00},
    {0x0221,0x50},
    {0x02ac,0x00},
    {0x02a5,0x02},
    {0x0260,0x0b},
    {0x0216,0x04},
    {0x0299,0x1C},
    {0x021a,0x98},
    {0x0266,0xd0},
    {0x0020,0x01},
    {0x0021,0x05},
    {0x0022,0xc0},
    {0x0023,0x08},
    {0x0098,0x10},
    {0x009a,0xb0},
    {0x02bb,0x0d},
    {0x02a3,0x02},
    {0x02a4,0x02},
    {0x021e,0x02},
    {0x024f,0x08},
    {0x028c,0x08},
    {0x0532,0x3f},
    {0x0533,0x02},
    {0x0277,0x70},
    {0x0276,0xc0},
    {0x0239,0xc0},
    {0x0200,0x00},
    {0x0201,0x5f},
    {0x0202,0x05},
    {0x0203,0xf0},
    {0x0205,0xc0},
    {0x02b0,0x68},
    {0x000f,0x00},
    {0x0006,0xe0},
    {0x0002,0xa9},
    {0x0004,0x01},
    {0x000f,0x00},
    {0x0060,0x40},
    {0x0218,0x02},
    {0x0342,0x05},
    {0x0343,0x5f},
    {0x03fe,0x10},
    {0x03fe,0x00},
    {0x0106,0x78},
    {0x0107,0x09},
    {0x0108,0x0c},
    {0x0114,0x01},
    {0x0115,0x12},
    {0x0180,0x4f},
    {0x0181,0x30},
    {0x0182,0x05},
    {0x0185,0x01},
    {0x03fe,0x10},
    {0x03fe,0x00},
    {0x0100,0x09},
	{SENSOR_REG_END, 0x00},	/* END MARKER */
};


static struct tx_isp_sensor_win_setting sensor_win_sizes[] = {
	{
		.width = 2560,
		.height = 1440,
		.fps = 25 << 16 | 1,
		.mbus_code = TISP_VI_FMT_SGRBG10_1X10,
		.colorspace = TISP_COLORSPACE_SRGB,
		.regs = sensor_init_regs_2560_1440_25fps_mipi,
	},
	{
		.width = 2560,
		.height = 1440,
		.fps = 30 << 16 | 1,
		.mbus_code = TISP_VI_FMT_SGRBG10_1X10,
		.colorspace = TISP_COLORSPACE_SRGB,
		.regs = sensor_init_regs_2560_1440_30fps_mipi_dol,
	},
    {
		.width = 1280,
		.height = 720,
		.fps = 120 << 16 | 1,
		.mbus_code = TISP_VI_FMT_SGRBG10_1X10,
		.colorspace = TISP_COLORSPACE_SRGB,
		.regs = sensor_init_regs_1280_720_120fps_mipi,
	},
};
struct tx_isp_sensor_win_setting *wsize = &sensor_win_sizes[0];

static struct regval_list sensor_stream_on[] = {
	{SENSOR_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list sensor_stream_off[] = {
	{SENSOR_REG_END, 0x00},	/* END MARKER */
};

int sensor_read(struct tx_isp_subdev *sd,  uint16_t reg,
		unsigned char *value)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	uint8_t buf[2] = {(reg>>8)&0xff, reg&0xff};
	struct i2c_msg msg[2] = {
		[0] = {
			.addr = client->addr,
			.flags = 0,
			.len = 2,
			.buf = buf,
		},
		[1] = {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = value,
		}
	};
	int ret;
	ret = private_i2c_transfer(client->adapter, msg, 2);
	if (ret > 0)
		ret = 0;

	return ret;
}

int sensor_write(struct tx_isp_subdev *sd, uint16_t reg,
		 unsigned char value)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	uint8_t buf[3] = {(reg >> 8) & 0xff, reg & 0xff, value};
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 3,
		.buf = buf,
	};
	int ret;
	ret = private_i2c_transfer(client->adapter, &msg, 1);
	if (ret > 0)
		ret = 0;

	return ret;
}

#if 0
static int sensor_read_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != SENSOR_REG_END) {
		if (vals->reg_num == SENSOR_REG_DELAY) {
			private_msleep(vals->value);
		} else {
			ret = sensor_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
		}
		pr_debug("vals->reg_num:0x%x, vals->value:0x%02x\n",vals->reg_num, val);
		vals++;
	}
	return 0;
}
#endif

static int sensor_write_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != SENSOR_REG_END) {
		if (vals->reg_num == SENSOR_REG_DELAY) {
			private_msleep(vals->value);
		} else {
			ret = sensor_write(sd, vals->reg_num, vals->value);
			if (ret < 0)
				return ret;
		}
		vals++;
	}

	return 0;
}

static int sensor_reset(struct tx_isp_subdev *sd, struct tx_isp_initarg *init)
{
	return 0;
}

static int sensor_detect(struct tx_isp_subdev *sd, unsigned int *ident)
{
	unsigned char v;
	int ret;
	ret = sensor_read(sd, 0x03f0, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != SENSOR_CHIP_ID_H)
		return -ENODEV;
	ret = sensor_read(sd, 0x03f1, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != SENSOR_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return 0;
}

static int sensor_set_expo(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;
	int it = value & 0xffff;
	int again = (value & 0xffff0000) >> 16;
	struct again_lut *val_lut = sensor_again_lut;
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);

	/*set integration time*/
	ret = sensor_write(sd, 0x0203, it & 0xff);
	ret += sensor_write(sd, 0x0202, it >> 8);

	switch(sensor->info.default_boot) {
	case 0:
		memcpy(val_lut, &sensor_again_lut, sizeof(sensor_again_lut));
		break;
	case 1: /*use new lut when switch to hdr or linear 60fps mode*/
		memcpy(val_lut, &sensor_again_lut_60fps, sizeof(sensor_again_lut_60fps));
		break;
	default:
		break;
	}

	/*set analog gain*/
	ret += sensor_write(sd, 0x02b3, val_lut[again].reg2b3);
	ret += sensor_write(sd, 0x02b4, val_lut[again].reg2b4);
	ret += sensor_write(sd, 0x02b8, val_lut[again].reg2b8);
	ret += sensor_write(sd, 0x02b9, val_lut[again].reg2b9);
	ret += sensor_write(sd, 0x0515, val_lut[again].reg515);
	ret += sensor_write(sd, 0x0519, val_lut[again].reg519);
	ret += sensor_write(sd, 0x02d9, val_lut[again].reg2d9);

	/*set dpc*/
	if (again > 0x14)
		ret += sensor_write(sd, 0x0004, 0x01);
	if (again < 0x11)
		ret += sensor_write(sd, 0x0004, 0x00);

	if (ret < 0)
		ISP_ERROR("sensor_write error  %d\n" ,__LINE__ );

	return ret;
}

#if 0
static int sensor_set_integration_time(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;

	ret = sensor_write(sd, 0x0203, value & 0xff);
	ret += sensor_write(sd, 0x0202, value >> 8);
	if (ret < 0) {
		ISP_ERROR("sensor_write error  %d\n" ,__LINE__ );
		return ret;
	}

	return 0;
}
#endif

static int sensor_set_integration_time_short(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;

	ret = sensor_write(sd, 0x0201, value & 0xff);
	ret += sensor_write(sd, 0x0200, (value >> 8)&0x3f);
	if (ret < 0) {
		ISP_ERROR("sensor_write error  %d\n" ,__LINE__ );
		return ret;
	}

	return 0;
}

#if 0
static int sensor_set_analog_gain(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;
	struct again_lut *val_lut = sensor_again_lut;


	ret = sensor_write(sd, 0x02b3, val_lut[value].reg2b3);
	ret = sensor_write(sd, 0x02b4, val_lut[value].reg2b4);
	ret = sensor_write(sd, 0x02b8, val_lut[value].reg2b8);
	ret = sensor_write(sd, 0x02b9, val_lut[value].reg2b9);
	ret = sensor_write(sd, 0x0515, val_lut[value].reg515);
	ret = sensor_write(sd, 0x0519, val_lut[value].reg519);
	ret = sensor_write(sd, 0x02d9, val_lut[value].reg2d9);

	if (ret < 0) {
		ISP_ERROR("sensor_write error  %d" ,__LINE__ );
		return ret;
	}

	return 0;
}
#endif

static int sensor_set_digital_gain(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int sensor_get_black_pedestal(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int sensor_init(struct tx_isp_subdev *sd, struct tx_isp_initarg *init)
{
	int ret = ISP_SUCCESS;

	ISP_INFO("[ %s:%d ] enable is %d\n", __func__, __LINE__, init->enable);

	return ret;
}

static int sensor_s_stream(struct tx_isp_subdev *sd, struct tx_isp_initarg *init)
{
	int ret = 0;
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);

	if (init->enable) {
		if (sensor->video.state == TX_ISP_MODULE_DEINIT) {
			ret = sensor_write_array(sd, wsize->regs);
			if (ret)
				return ret;
			sensor->video.state = TX_ISP_MODULE_INIT;
		}
		if (sensor->video.state == TX_ISP_MODULE_INIT) {
			ret = sensor_write_array(sd, sensor_stream_on);
			sensor->video.state = TX_ISP_MODULE_RUNNING;
			pr_debug("gc4663 stream on\n");
			sensor->video.state = TX_ISP_MODULE_RUNNING;
		}
	} else {
		ret = sensor_write_array(sd, sensor_stream_off);
		pr_debug("gc4663 stream off\n");
	}

	return ret;
}

static int sensor_set_fps(struct tx_isp_subdev *sd, int fps)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	unsigned int wpclk = 0;
	unsigned short vts = 0;
	unsigned short hts=0;
	unsigned char tmp;
	unsigned int sensor_max_fps;
	unsigned int newformat = 0; //the format is 24.8
	int ret = 0;

	switch(sensor->info.default_boot) {
	case 0:
		wpclk = SENSOR_SUPPORT_30FPS_SCLK;
		sensor_max_fps = TX_SENSOR_MAX_FPS_30;
		break;
	case 1:
		wpclk = SENSOR_SUPPORT_30FPS_SCLK_HDR;
		sensor_max_fps = TX_SENSOR_MAX_FPS_30;
		break;
    case 2:
		wpclk = 0x640*0x55f*120*2;
		sensor_max_fps = 120;
		break;
	default:
		ISP_ERROR("Now we do not support this framerate!!!\n");
	}

	/* the format of fps is 16/16. for example 30 << 16 | 2, the value is 30/2 fps. */
	newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
	if (newformat > (sensor_max_fps << 8) || newformat < (SENSOR_OUTPUT_MIN_FPS << 8)) {
		ISP_ERROR("warn: fps(%x) no in range\n", fps);
		return -1;
	}

	ret += sensor_read(sd, 0x0342, &tmp);
	hts = tmp;
	ret += sensor_read(sd, 0x0343, &tmp);
	if (ret < 0)
		return -1;
	hts = ((hts << 8) + tmp) << 1;

	vts = wpclk * (fps & 0xffff) / hts / ((fps & 0xffff0000) >> 16);
	ret = sensor_write(sd, 0x0340, (unsigned char)((vts & 0x3f00) >> 8));
	ret += sensor_write(sd, 0x0341, (unsigned char)(vts & 0xff));
	if (ret < 0)
		return -1;

	sensor->video.fps = fps;
	sensor->video.attr->max_integration_time_native = vts - 4;
	sensor->video.attr->integration_time_limit = vts - 4;
	sensor->video.attr->total_height = vts;
	sensor->video.attr->max_integration_time = vts;

	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	return 0;
}

static int sensor_set_vflip(struct tx_isp_subdev *sd, int enable)
{
	int ret = 0;
	uint8_t val;
	unsigned char start_pos = 0x00;

	/* 2'b01: mirror; 2'b10:flip*/
	enable &= 0x03;
	switch(enable) {
	case 0: /*normal*/
		val = 0x00;
		start_pos = 0x00;
		break;
	case 1: /*mirror*/
		val = 0x01;
		start_pos = 0x00;
		break;
	case 2: /*filp*/
		val = 0x02;
		start_pos = 0x10;
		break;
	case 3: /*mirror & filp*/
		val = 0x03;
		start_pos = 0x10;
		break;
	}
	ret = sensor_write(sd, 0x031d, 0x2d);
	ret += sensor_write(sd, 0x0101, val);
	ret += sensor_write(sd, 0x000f, start_pos);
	ret = sensor_write(sd, 0x031d, 0x28);
	if (ret != 0)
		ISP_ERROR("%d: gc4653 write err!!\n",__LINE__);

	return ret;
}

static int sensor_set_mode(struct tx_isp_subdev *sd, int value)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = ISP_SUCCESS;

	if (wsize) {
		sensor->video.mbus.width = wsize->width;
		sensor->video.mbus.height = wsize->height;
		sensor->video.mbus.code = wsize->mbus_code;
		sensor->video.mbus.field = TISP_FIELD_NONE;
		sensor->video.mbus.colorspace = wsize->colorspace;
		sensor->video.fps = wsize->fps;
		ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	}
	return ret;
}

char* __attribute__((weak)) sclk_name[3];
static int sensor_attr_check(struct tx_isp_subdev *sd)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	struct tx_isp_sensor_register_info *info = &sensor->info;
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	struct clk *sclka;
	unsigned long rate;
	int ret = 0;

	switch(info->default_boot) {
	case 0:
		wsize = &sensor_win_sizes[0];
		sensor_attr.data_type = TX_SENSOR_DATA_TYPE_LINEAR;
		memcpy(&sensor_attr.mipi, &sensor_mipi_linear, sizeof(sensor_mipi_linear));
		sensor_attr.one_line_expr_in_us = 30;
		sensor_attr.total_width = 3000;
		sensor_attr.total_height = 1920;
		sensor_attr.max_integration_time_native = 1920 - 4;
		sensor_attr.integration_time_limit = 1920 - 4;
		sensor_attr.max_integration_time = 1920 - 4;
		break;
	case 1:
		sensor_attr.wdr_cache = wdr_bufsize;
		wsize = &sensor_win_sizes[1];
		memcpy(&sensor_attr.mipi, &sensor_mipi_dol, sizeof(sensor_mipi_dol));
		sensor_attr.wdr_cache = wdr_bufsize;
		sensor_attr.one_line_expr_in_us = 27;
		sensor_attr.min_integration_time = 2;
		sensor_attr.min_integration_time_short = 2;
		sensor_attr.max_again_short = 409249;
		sensor_attr.total_width = 2750;
		sensor_attr.total_height = 1600;
		sensor_attr.max_integration_time_native = 1504;
		sensor_attr.integration_time_limit = 1504;
		sensor_attr.max_integration_time = 1504;
		sensor_attr.max_integration_time_short = 94;
		sensor_attr.data_type = TX_SENSOR_DATA_TYPE_WDR_DOL;
		break;
    case 2:
		wsize = &sensor_win_sizes[2];
		sensor_attr.data_type = TX_SENSOR_DATA_TYPE_LINEAR;
		memcpy(&sensor_attr.mipi, &sensor_mipi_linear_120fps, sizeof(sensor_mipi_linear_120fps));
		sensor_attr.one_line_expr_in_us = 10;
		sensor_attr.total_width = 0x640*2;
		sensor_attr.total_height = 0x55f;
		sensor_attr.max_integration_time_native = - 4;
		sensor_attr.integration_time_limit = 0x55f - 4;
		sensor_attr.max_integration_time = 0x55f - 4;
		break;
	default:
		ISP_ERROR("Have no this setting!!!\n");
	}

	switch(info->video_interface) {
	case TISP_SENSOR_VI_MIPI_CSI0:
		sensor_attr.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI;
		sensor_attr.mipi.index = 0;
		break;
	case TISP_SENSOR_VI_MIPI_CSI1:
		sensor_attr.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI;
		sensor_attr.mipi.index = 1;
		break;
	case TISP_SENSOR_VI_DVP:
		sensor_attr.dbus_type = TX_SENSOR_DATA_INTERFACE_DVP;
		break;
	default:
		ISP_ERROR("Have no this interface!!!\n");
	}

	switch(info->mclk) {
	case TISP_SENSOR_MCLK0:
	case TISP_SENSOR_MCLK1:
	case TISP_SENSOR_MCLK2:
                sclka = private_devm_clk_get(&client->dev, SEN_MCLK);
                sensor->mclk = private_devm_clk_get(sensor->dev, SEN_BCLK);
		set_sensor_mclk_function(0);
		break;
	default:
		ISP_ERROR("Have no this MCLK Source!!!\n");
	}

	if (IS_ERR(sensor->mclk)) {
		ISP_ERROR("Cannot get sensor input clock cgu_cim\n");
		goto err_get_mclk;
	}

	rate = private_clk_get_rate(sensor->mclk);
	if (((rate / 1000) % 27000) != 0) {
                ret = clk_set_parent(sclka, clk_get(NULL, SEN_TCLK));
                sclka = private_devm_clk_get(&client->dev, SEN_TCLK);
		if (IS_ERR(sclka)) {
			pr_err("get sclka failed\n");
		} else {
			rate = private_clk_get_rate(sclka);
			if (((rate / 1000) % 27000) != 0) {
				private_clk_set_rate(sclka, 1188000000);
			}
		}
	}
    switch(info->default_boot) {
        case 0:
        case 1:
	private_clk_set_rate(sensor->mclk, MCLK);
	private_clk_prepare_enable(sensor->mclk);
            break;
        case 2:
            private_clk_set_rate(sensor->mclk, 24000000);
	        private_clk_prepare_enable(sensor->mclk);
            break;
        default:
		    ISP_ERROR("Have no this setting!!!\n");
        }
	sensor->video.shvflip = 1;
	sensor->video.fps = wsize->fps;
        sensor->video.max_fps = wsize->fps;
	sensor->video.min_fps = SENSOR_OUTPUT_MIN_FPS << 16 | 1;
	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = TISP_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	if (ret) {
		ISP_WARNING("Description Failed to synchronize the attributes of sensor!!!");
	}

	return 0;

err_get_mclk:
	return -1;
}

static int sensor_g_chip_ident(struct tx_isp_subdev *sd,
			       struct tx_isp_chip_ident *chip)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	struct tx_isp_sensor_register_info *info = &sensor->info;
	unsigned int ident = 0;
	int ret = ISP_SUCCESS;
	int reset_gpio = info->rst_gpio;
	int pwdn_gpio = info->pwdn_gpio;

	sensor_attr_check(sd);
	if (reset_gpio != -1) {
		ret = private_gpio_request(reset_gpio,"sensor_reset");
		if (!ret) {
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(10);
			private_gpio_direction_output(reset_gpio, 0);
			private_msleep(20);
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(10);
		} else {
			ISP_ERROR("gpio requrest fail %d\n",reset_gpio);
		}
	}
	if (pwdn_gpio != -1) {
		ret = private_gpio_request(pwdn_gpio,"sensor_pwdn");
		if (!ret) {
			private_gpio_direction_output(pwdn_gpio, 1);
			private_msleep(10);
			private_gpio_direction_output(pwdn_gpio, 0);
			private_msleep(10);
			private_gpio_direction_output(pwdn_gpio, 1);
			private_msleep(10);
		} else {
			ISP_ERROR("gpio requrest fail %d\n",pwdn_gpio);
		}
	}
	ret = sensor_detect(sd, &ident);
	if (ret) {
		ISP_ERROR("chip found @ 0x%x (%s) is not an gc4663 chip.\n",
			  client->addr, client->adapter->name);
		return ret;
	}
	ISP_WARNING("gc4663 chip found @ 0x%02x (%s)\n sensor drv version %s", client->addr, client->adapter->name, SENSOR_VERSION);
	if (chip) {
		memcpy(chip->name, "gc4663", sizeof("gc4663"));
		chip->ident = ident;
		chip->revision = SENSOR_VERSION;
	}
	return 0;
}

static int sensor_set_wdr_stop(struct tx_isp_subdev *sd, int wdr_en)
{
	struct tx_isp_sensor *sensor = tx_isp_get_subdev_hostdata(sd);
	struct tx_isp_sensor_register_info *info = &sensor->info;
	int ret = 0;

	ret = sensor_write(sd, 0x03fe, 0xf0);

	if (wdr_en == 1) {
		info->default_boot = 1;
		memcpy(&sensor_attr.mipi, &sensor_mipi_dol, sizeof(sensor_mipi_dol));
		sensor_attr.data_type = TX_SENSOR_DATA_TYPE_WDR_DOL;
		sensor_attr.wdr_cache = wdr_bufsize;
		wsize = &sensor_win_sizes[1];
		sensor_attr.wdr_cache = wdr_bufsize;
		sensor_attr.one_line_expr_in_us = 27;
		sensor_attr.min_integration_time = 2;
		sensor_attr.min_integration_time_short = 2;
		sensor_attr.total_width = 2750;
		sensor_attr.total_height = 1600;
		sensor_attr.max_integration_time_native = 1920;//1504;
		sensor_attr.integration_time_limit = 1920;//1504;
		sensor_attr.max_integration_time = 1920;//1504;
		sensor_attr.max_integration_time_short = 94;
	} else if (wdr_en == 0) {
		info->default_boot = 0;
		memcpy(&sensor_attr.mipi, &sensor_mipi_linear, sizeof(sensor_mipi_linear));
		sensor_attr.data_type = TX_SENSOR_DATA_TYPE_LINEAR;
		wsize = &sensor_win_sizes[0];
		sensor_attr.one_line_expr_in_us = 30;
		sensor_attr.total_width = 3000;
		sensor_attr.total_height = 1920;
		sensor_attr.max_integration_time_native = 1920 - 4;
		sensor_attr.integration_time_limit = 1920 - 4;
		sensor_attr.max_integration_time = 1920 - 4;
	} else {
		ISP_ERROR("Can not support this data type!!!");
		return -1;
	}

	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	return ret;
}

static int sensor_set_wdr(struct tx_isp_subdev *sd, int wdr_en)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	struct tx_isp_sensor_register_info *info = &sensor->info;
	int ret = 0;

	private_gpio_direction_output(info->rst_gpio, 1);
	private_msleep(1);
	private_gpio_direction_output(info->rst_gpio, 0);
	private_msleep(1);
	private_gpio_direction_output(info->rst_gpio, 1);
	private_msleep(1);

	ret = sensor_write_array(sd, wsize->regs);
	ret = sensor_write_array(sd, sensor_stream_on);

	return 0;
}

static int sensor_sensor_ops_ioctl(struct tx_isp_subdev *sd, unsigned int cmd, void *arg)
{
	long ret = 0;
	struct tx_isp_sensor_value *sensor_val = arg;
	struct tx_isp_initarg *init = arg;

	if (IS_ERR_OR_NULL(sd)) {
		ISP_ERROR("[%d]The pointer is invalid!\n", __LINE__);
		return -EINVAL;
	}
	switch(cmd) {
	case TX_ISP_EVENT_SENSOR_EXPO:
		if (arg)
			ret = sensor_set_expo(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_INT_TIME:
	//	if (arg)
	//		ret = sensor_set_integration_time(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_AGAIN:
	//	if (arg)
	//		ret = sensor_set_analog_gain(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_DGAIN:
		if (arg)
			ret = sensor_set_digital_gain(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_BLACK_LEVEL:
		if (arg)
			ret = sensor_get_black_pedestal(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_RESIZE:
		if (arg)
			ret = sensor_set_mode(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_PREPARE_CHANGE:
		ret = sensor_write_array(sd, sensor_stream_off);
		break;
	case TX_ISP_EVENT_SENSOR_FINISH_CHANGE:
		ret = sensor_write_array(sd, sensor_stream_on);
		break;
	case TX_ISP_EVENT_SENSOR_FPS:
		if (arg)
			ret = sensor_set_fps(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_INT_TIME_SHORT:
		if (arg)
			ret = sensor_set_integration_time_short(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_WDR:
		if (arg)
			ret = sensor_set_wdr(sd, init->enable);
		break;
	case TX_ISP_EVENT_SENSOR_WDR_STOP:
		if (arg)
			ret = sensor_set_wdr_stop(sd, init->enable);
		break;
	case TX_ISP_EVENT_SENSOR_VFLIP:
		if (arg)
			ret = sensor_set_vflip(sd, sensor_val->value);
		break;
	default:
		break;
	}

	return ret;
}

static int sensor_g_register(struct tx_isp_subdev *sd, struct tx_isp_dbg_register *reg)
{
	unsigned char val = 0;
	int len = 0;
	int ret = 0;

	len = strlen(sd->chip.name);
	if (len && strncmp(sd->chip.name, reg->name, len)) {
		return -EINVAL;
	}
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
	if (len && strncmp(sd->chip.name, reg->name, len)) {
		return -EINVAL;
	}
	if (!private_capable(CAP_SYS_ADMIN))
		return -EPERM;
	sensor_write(sd, reg->reg & 0xffff, reg->val & 0xff);

	return 0;
}

static struct tx_isp_subdev_core_ops sensor_core_ops = {
	.g_chip_ident = sensor_g_chip_ident,
	.reset = sensor_reset,
	.init = sensor_init,
	/*.ioctl = sensor_ops_ioctl,*/
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
	.name = "gc4663",
	.id = -1,
	.dev = {
		.dma_mask = &tx_isp_module_dma_mask,
		.coherent_dma_mask = 0xffffffff,
		.platform_data = NULL,
	},
	.num_resources = 0,
};

static int sensor_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct tx_isp_subdev *sd;
	struct tx_isp_sensor *sensor;

	sensor = (struct tx_isp_sensor *)kzalloc(sizeof(*sensor), GFP_KERNEL);
	if (!sensor) {
		ISP_ERROR("Failed to allocate sensor subdev.\n");
		return -ENOMEM;
	}
	memset(sensor, 0 ,sizeof(*sensor));

	sd = &sensor->sd;
	sensor->dev = &client->dev;
	sensor->video.attr = &sensor_attr;
        sensor->video.state = TX_ISP_MODULE_DEINIT;
	tx_isp_subdev_init(&sensor_platform_device, sd, &sensor_ops);
	tx_isp_set_subdevdata(sd, client);
	tx_isp_set_subdev_hostdata(sd, sensor);
	private_i2c_set_clientdata(client, sd);

	pr_debug("probe ok ------->gc4663\n");

	return 0;
}

static int sensor_remove(struct i2c_client *client)
{
	struct tx_isp_subdev *sd = private_i2c_get_clientdata(client);
	struct tx_isp_sensor *sensor = tx_isp_get_subdev_hostdata(sd);
	struct tx_isp_sensor_register_info *info = &sensor->info;

	if (info->rst_gpio != -1)
		private_gpio_free(info->rst_gpio);
	if (info->pwdn_gpio != -1)
		private_gpio_free(info->pwdn_gpio);

	private_clk_disable_unprepare(sensor->mclk);
	tx_isp_subdev_deinit(sd);
	kfree(sensor);

	return 0;
}

static const struct i2c_device_id sensor_id[] = {
	{ "gc4663", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "gc4663",
	},
	.probe = sensor_probe,
	.remove = sensor_remove,
	.id_table = sensor_id,
};

static __init int init_sensor(void)
{
	return private_i2c_add_driver(&sensor_driver);
}

static __exit void exit_sensor(void)
{
	private_i2c_del_driver(&sensor_driver);
}

module_init(init_sensor);
module_exit(exit_sensor);

MODULE_DESCRIPTION("A low-level driver for gc4663 sensors");
MODULE_LICENSE("GPL");
