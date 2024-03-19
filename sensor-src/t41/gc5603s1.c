// SPDX-License-Identifier: GPL-2.0+
/*
 * gc5603s1.c
 *
 * Settings:
 * sboot        resolution       fps     interface              mode
 *   0          2880*1620        25     mipi_2lane             linear
 *   1          2880*1620        15     mipi_2lane             wdr
 *   2          2960*1666        25     mipi_2lane             linear
 *   3          2880*1620        40     mipi_2lane             linear
 */

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
#include <txx-funcs.h>

#define SENSOR_CHIP_ID_H (0x56)
#define SENSOR_CHIP_ID_L (0x03)
#define SENSOR_REG_END 0xffff
#define SENSOR_REG_DELAY 0x0000
#define SENSOR_OUTPUT_MAX_WDR_FPS 15
#define SENSOR_OUTPUT_MIN_FPS 5
#define SENSOR_VERSION "H20230817a"

static int reset_gpio = -1;
static int pwdn_gpio = -1;
static int wdr_bufsize = 2 * 4800 * 400;//cache lines corrponding on VPB1
static int shvflip = 1;

static int fsync_mode = 3;
module_param(fsync_mode, int, S_IRUGO);
MODULE_PARM_DESC(fsync_mode, "Sensor Indicates the frame synchronization mode");

struct regval_list {
	uint16_t reg_num;
	unsigned char value;
};

struct again_lut {
	unsigned int index;
	unsigned char reg614;
	unsigned char reg615;
	unsigned char reg225;
	unsigned char reg1467;
	unsigned char reg1468;
	unsigned char regb8;
	unsigned char regb9;
	unsigned int gain;
};

struct again_lut sensor_again_lut[] = {
	//index,0614,0615, 0225, 1467, 1468, 00b8, 00b9, gain
	{0x00, 0x00, 0x00, 0x04, 0x15, 0x15, 0x01, 0x00, 0},	  // 1.000000
	{0x01, 0x90, 0x02, 0x04, 0x15, 0x15, 0x01, 0x0A, 13726},  // 1.156250
	{0x02, 0x00, 0x00, 0x00, 0x15, 0x15, 0x01, 0x12, 23431},  // 1.281250
	{0x03, 0x90, 0x02, 0x00, 0x15, 0x15, 0x01, 0x20, 38335},  // 1.500000
	{0x04, 0x01, 0x00, 0x00, 0x15, 0x15, 0x01, 0x30, 52910},  // 1.750000
	{0x05, 0x91, 0x02, 0x00, 0x15, 0x15, 0x02, 0x05, 69158},  // 2.078125
	{0x06, 0x02, 0x00, 0x00, 0x15, 0x15, 0x02, 0x19, 82403},  // 2.390625
	{0x07, 0x92, 0x02, 0x00, 0x16, 0x16, 0x02, 0x3F, 103377}, // 2.984375
	{0x08, 0x03, 0x00, 0x00, 0x16, 0x16, 0x03, 0x20, 118446}, // 3.500000
	{0x09, 0x93, 0x02, 0x00, 0x17, 0x17, 0x04, 0x0A, 134694}, // 4.156250
	{0x0a, 0x00, 0x00, 0x01, 0x18, 0x18, 0x05, 0x02, 152758}, // 5.031250
	{0x0b, 0x90, 0x02, 0x01, 0x19, 0x19, 0x05, 0x39, 167667}, // 5.890625
	{0x0c, 0x01, 0x00, 0x01, 0x19, 0x19, 0x06, 0x3C, 183134}, // 6.937500
	{0x0d, 0x91, 0x02, 0x01, 0x19, 0x19, 0x08, 0x0D, 198977}, // 8.203125
	{0x0e, 0x02, 0x00, 0x01, 0x1a, 0x1a, 0x09, 0x21, 213010}, // 9.515625
	{0x0f, 0x92, 0x02, 0x01, 0x1a, 0x1a, 0x0B, 0x0F, 228710}, // 11.234375
	{0x10, 0x03, 0x00, 0x01, 0x1c, 0x1c, 0x0D, 0x17, 245089}, // 13.359375
	{0x11, 0x93, 0x02, 0x01, 0x1c, 0x1c, 0x0F, 0x33, 260934}, // 15.796875
	{0x12, 0x04, 0x00, 0x01, 0x1d, 0x1d, 0x12, 0x30, 277139}, // 18.750000
	{0x13, 0x94, 0x02, 0x01, 0x1d, 0x1d, 0x16, 0x10, 293321}, // 22.250000
	{0x14, 0x05, 0x00, 0x01, 0x1e, 0x1e, 0x1A, 0x19, 309456}, // 26.390625
	{0x15, 0x95, 0x02, 0x01, 0x1e, 0x1e, 0x1F, 0x13, 325578}, // 31.296875
	{0x16, 0x06, 0x00, 0x01, 0x20, 0x20, 0x25, 0x08, 341724}, // 37.125000
	{0x17, 0x96, 0x02, 0x01, 0x20, 0x20, 0x2C, 0x03, 357889}, // 44.046875
	{0x18, 0xb6, 0x04, 0x01, 0x20, 0x20, 0x34, 0x0F, 374007}, // 52.234375
	{0x19, 0x86, 0x06, 0x01, 0x20, 0x20, 0x3D, 0x3D, 390142}, // 61.953125
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

unsigned int sensor_alloc_dgain(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_dgain)
{
	return 0;
}

struct tx_isp_mipi_bus sensor_mipi_linear = {
	.mode = SENSOR_MIPI_OTHER_MODE,
	.clk = 846,
	.lans = 2,
	.settle_time_apative_en = 0,
	.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,//RAW10
	.mipi_sc.hcrop_diff_en = 0,
	.mipi_sc.mipi_vcomp_en = 0,
	.mipi_sc.mipi_hcomp_en = 0,
	.image_twidth = 2880,
	.image_theight = 1620,
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
	.clk = 1152,
	.lans = 2,
	.settle_time_apative_en = 0,
	.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,//RAW10
	.mipi_sc.hcrop_diff_en = 0,
	.mipi_sc.mipi_vcomp_en = 0,
	.mipi_sc.mipi_hcomp_en = 0,
	.image_twidth = 2880,
	.image_theight = 1620,
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

struct tx_isp_mipi_bus sensor_mipi_2960_1666_linear = {
	.mode = SENSOR_MIPI_OTHER_MODE,
	.clk = 846,
	.lans = 2,
	.settle_time_apative_en = 0,
	.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,//RAW10
	.mipi_sc.hcrop_diff_en = 0,
	.mipi_sc.mipi_vcomp_en = 0,
	.mipi_sc.mipi_hcomp_en = 0,
	.image_twidth = 2960,
	.image_theight = 1666,
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

struct tx_isp_sensor_attribute sensor_attr={
	.name = "gc5603s1",
	.chip_id = 0x5603,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = TISP_SBUS_MASK_SAMPLE_8BITS | TISP_SBUS_MASK_ADDR_16BITS,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI,
	.data_type = TX_SENSOR_DATA_TYPE_LINEAR,
	.cbus_device = 0x31,
	.max_again = 390142,
	.max_dgain = 0,
	.expo_fs = 1,
	.min_integration_time = 1,
	.min_integration_time_short = 1,
	.min_integration_time_native = 1,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 2,
	.sensor_ctrl.alloc_again = sensor_alloc_again,
	.sensor_ctrl.alloc_again_short = sensor_alloc_again_short,
	.sensor_ctrl.alloc_dgain = sensor_alloc_dgain,
	.sensor_ctrl.alloc_integration_time_short = sensor_alloc_integration_time_short,
        .fsync_attr = {
                .mode = TX_ISP_SENSOR_FSYNC_MODE_MS_REALTIME_MISPLACE,
                .call_times = 1,
                .sdelay = 1000,
        }
};

static struct regval_list sensor_init_regs_2880_1620_25fps_mipi[] = {
	{0x03fe,0xf0},
	{0x03fe,0x00},
	{0x03fe,0x10},
	{0x03fe,0x00},
	{0x0a38,0x02},
	{0x0a38,0x03},
	{0x0a20,0x07},
	{0x061b,0x03},
	{0x061c,0x50},
	{0x061d,0x05},
	{0x061e,0x70},
	{0x061f,0x03},
	{0x0a21,0x08},
	{0x0a34,0x40},
	{0x0a35,0x11},
	{0x0a36,0x5e},
	{0x0a37,0x03},
	{0x0314,0x50},
	{0x0315,0x32},
	{0x031c,0xce},
	{0x0219,0x47},
	{0x0342,0x04},//hts 0x4b0 = 1200
	{0x0343,0xb0},//
	{0x0340,0x08},//vts 0x834 = 2100 25fps  0x6d6 -> 30fps
	{0x0341,0x34},//
	{0x0345, 0x02},
	{0x0347, 0x02},
	{0x0348,0x0b},
	{0x0349,0x98},
	{0x034a,0x06},
	{0x034b,0x7a},
	{0x0094,0x0b},
	{0x0095,0x40},
	{0x0096,0x06},
	{0x0097,0x54},
	{0x0099, 0x15},
	{0x009b, 0x28},
	{0x060c, 0x01},
	{0x060e,0xd2},
	{0x060f,0x05},
	{0x070c,0x01},
	{0x070e,0xd2},
	{0x070f,0x05},
	{0x0709,0x40},
	{0x0719,0x40},
	{0x0909,0x07},
	{0x0902,0x04},
	{0x0904,0x0b},
	{0x0907,0x54},
	{0x0908,0x06},
	{0x0903,0x9d},
	{0x072a,0x1c},//18
	{0x072b,0x1c},//18
	{0x0724,0x2b},
	{0x0727,0x2b},
	{0x1466,0x18},
	{0x1467,0x15},
	{0x1468,0x15},
	{0x1469,0x70},
	{0x146a,0xe8},//b8
	//{0x1412,0x20},
	{0x0707,0x07},
	{0x0737,0x0f},
	{0x0704,0x01},
	{0x0706,0x02},
	{0x0716,0x02},
	{0x0708,0xc8},
	{0x0718,0xc8},
	{0x061a,0x02},//03
	{0x1430,0x80},
	{0x1407,0x10},
	{0x1408,0x16},
	{0x1409,0x03},
	{0x1438,0x01},
	{0x02ce,0x03},
	{0x0245,0xc9},
	{0x023a,0x08},//3B
	{0x02cd,0x88},
	{0x0612,0x02},
	{0x0613,0xc7},
	{0x0243,0x03},//06
	{0x0089,0x03},
	{0x0002,0xab},
	{0x0040,0xa3},
	{0x0075,0x64},
	{0x0004,0x0f},
	{0x0053,0x0a},
	{0x0205,0x0c},
	{0x0052,0x02},
	{0x0076,0x01},
	{0x021a,0x10},
	{0x0049,0x0f}, //darkrow select
	{0x004a,0x3c},
	{0x004b,0x00},
	{0x0430,0x25},
	{0x0431,0x25},
	{0x0432,0x25},
	{0x0433,0x25},
	{0x0434,0x59},
	{0x0435,0x59},
	{0x0436,0x59},
	{0x0437,0x59},
	{0x0a67,0x80},
	{0x0a54,0x0e},
	{0x0a65,0x10},
	{0x0a98,0x04},
	{0x05be,0x00},
	{0x05a9,0x01},
	{0x0023,0x00},
	{0x0022,0x00},
	{0x0025,0x00},
	{0x0024,0x00},
	{0x0028,0x0b},
	{0x0029,0x98},
	{0x002a,0x06},
	{0x002b,0x86},
	{0x0a83,0xe0},
	{0x0a72,0x02},
	{0x0a73,0x60},
	{0x0a75,0x41},
	{0x0a70,0x03},
	{0x0a5a,0x80},
	{0x0181,0x30},
	{0x0182,0x05},
	{0x0185,0x01},
	{0x0180,0x46},
	{0x0100,0x08},
	{0x010d,0x10},
	{0x010e,0x0e},
	{0x0113,0x02},
	{0x0114,0x01},
	{0x0115,0x10},
	{0x0100,0x09},
	{0x0a70,0x00},
	{0x0080,0x02},
	{0x0a67,0x00},
       //{0x022c,0x03},
       //{0x0063,0x03},
{SENSOR_REG_END, 0x00},
};


static struct regval_list sensor_init_regs_2880_1620_15fps_mipi_dol[] = {
	{0x03fe,0xf0},
	{0x03fe,0x00},
	{0x03fe,0x10},
	{0x03fe,0x00},
	{0x0a38,0x02},
	{0x0a38,0x03},
	{0x0a20,0x07},
	{0x061b,0x03},
	{0x061c,0x50},
	{0x061d,0x05},
	{0x061e,0x70},
	{0x061f,0x03},
	{0x0a21,0x08},
	{0x0a34,0x40},
	{0x0a35,0x11},
	{0x0a36,0x80},
	{0x0a37,0x03},
	{0x0314,0x50},
	{0x0315,0x32},
	{0x031c,0xce},
	{0x0219,0x47},
	{0x0342,0x03},//hts 0x384 = 900
	{0x0343,0x84},//
	{0x0340,0x09},//vts 20fps -> 0x6d6 = 1720  15fps -> 0x91E = 2334
	{0x0341,0x1e},//
	{0x0345,0x02},
	{0x0347,0x02},
	{0x0348,0x0b},
	{0x0349,0x98},
	{0x034a,0x06},
	{0x034b,0x8a},
	{0x0094,0x0b},
	{0x0095,0x40},
	{0x0096,0x06},
	{0x0097,0x54},
	{0x0099, 0x04},
	{0x009b, 0x04},
//bayer
#if 1
	{0x0099, 0x03},
	{0x009b, 0x02},
#endif
	{0x060c,0x01},
	{0x060e,0xd2},
	{0x060f,0x05},
	{0x070c,0x01},
	{0x070e,0xd2},
	{0x070f,0x05},
	{0x0709,0x40},
	{0x0719,0x40},
	{0x0909,0x07},
	{0x0902,0x04},
	{0x0904,0x0b},
	{0x0907,0x54},
	{0x0908,0x06},
	{0x0903,0x9d},
	{0x072a,0x1c},
	{0x072b,0x1c},
	{0x0724,0x2b},
	{0x0727,0x2b},
	{0x1412,0x20},
	{0x1466,0x18},
	{0x1467,0x08},
	{0x1468,0x10},
	{0x1469,0x80},
	{0x146a,0xe8},
	{0x0707,0x07},
	{0x0737,0x0f},
	{0x0704,0x01},
	{0x0706,0x02},
	{0x0716,0x02},
	{0x0708,0xc8},
	{0x0718,0xc8},
	{0x061a,0x02},
	{0x1407,0x10},
	{0x1408,0x16},
	{0x1409,0x03},
	{0x1438,0x01},
	{0x0222,0x41},
	{0x0107,0x89},
	{0x02ce,0x03},
	{0x0245,0xc9},
	{0x023a,0x08},
	{0x02cd,0x88},
	{0x0612,0x02},
	{0x0613,0xc7},
	{0x0243,0x03},
	{0x0089,0x03},
	{0x0002,0xab},
	{0x0040,0xa3},
	{0x0075,0x64},
	{0x0004,0x0f},
	{0x0053,0x0a},
	{0x0205,0x0c},
	{0x0a67,0x80},
	{0x0a54,0x0e},
	{0x0a65,0x10},
	{0x0a98,0x04},
	{0x05be,0x00},
	{0x05a9,0x01},
	{0x0028,0x0b},
	{0x0029,0x98},
	{0x002a,0x06},
	{0x002b,0x86},
	{0x0a83,0xe0},
	{0x0a72,0x02},
	{0x0a73,0x60},
	{0x0a75,0x41},
	{0x0a70,0x03},
	{0x0a5a,0x80},
	{0x0123,0x30},
	{0x0124,0x04},
	{0x0125,0x30},
	{0x0129,0x0c},
	{0x012a,0x18},
	{0x012b,0x18},
	{0x0181,0x30},
	{0x0182,0x05},
	{0x0185,0x01},
	{0x0180,0x46},
	{0x0100,0x08},
	{0x010d,0x10},
	{0x010e,0x0e},
	{0x0113,0x02},
	{0x0114,0x01},
	{0x0115,0x10},
	{0x0a70,0x00},
	{0x0080,0x02},
	{0x0a67,0x00},
	{0x0052,0x02},
	{0x0076,0x01},
	{0x021a,0x10},
	{0x021b, 0x69},
	{0x0049,0x0f},
	{0x004a,0x3c},
	{0x004b,0x00},
	{0x0430,0x25},
	{0x0431,0x25},
	{0x0432,0x25},
	{0x0433,0x25},
	{0x0434,0x59},
	{0x0435,0x59},
	{0x0436,0x59},
	{0x0437,0x59},
	{0x0100,0x09},
      //{0x022c,0x03},
      //{0x0063,0x03},
	{SENSOR_REG_END, 0x00},
};

static struct regval_list sensor_init_regs_2960_1666_25fps_mipi[] = {
	{0x03fe,0xf0},
	{0x03fe,0x00},
	{0x03fe,0x10},
	{0x03fe,0x00},
	{0x0a38,0x02},
	{0x0a38,0x03},
	{0x0a20,0x07},
	{0x061b,0x03},
	{0x061c,0x50},
	{0x061d,0x05},
	{0x061e,0x70},
	{0x061f,0x03},
	{0x0a21,0x08},
	{0x0a34,0x40},
	{0x0a35,0x11},
	{0x0a36,0x5e},
	{0x0a37,0x03},
	{0x0314,0x50},
	{0x0315,0x32},
	{0x031c,0xce},
	{0x0219,0x47},
	{0x0342,0x04},//hts -> 0x4b0 = 1200
	{0x0343,0xb0},//
	{0x0340,0x08},//vts -> 0x834 = 2100
	{0x0341,0x34},//
	{0x0345,0x02},
	{0x0347,0x02},
	{0x0348,0x0b},
	{0x0349,0x98},
	{0x034a,0x06},
	{0x034b,0x8a},
	{0x0094,0x0b},
	{0x0095,0x90},
	{0x0096,0x06},
	{0x0097,0x82},
	{0x0099,0x04},
	{0x009b,0x04},
	{0x060c,0x01},
	{0x060e,0xd2},
	{0x060f,0x05},
	{0x070c,0x01},
	{0x070e,0xd2},
	{0x070f,0x05},
	{0x0709,0x40},
	{0x0719,0x40},
	{0x0909,0x07},
	{0x0902,0x04},
	{0x0904,0x0b},
	{0x0907,0x54},
	{0x0908,0x06},
	{0x0903,0x9d},
	{0x072a,0x1c},
	{0x072b,0x1c},
	{0x0724,0x2b},
	{0x0727,0x2b},
	{0x1466,0x18},
	{0x1467,0x15},
	{0x1468,0x15},
	{0x1469,0x70},
	{0x146a,0xe8},
	{0x0707,0x07},
	{0x0737,0x0f},
	{0x0704,0x01},
	{0x0706,0x02},
	{0x0716,0x02},
	{0x0708,0xc8},
	{0x0718,0xc8},
	{0x061a,0x02},
	{0x1430,0x80},
	{0x1407,0x10},
	{0x1408,0x16},
	{0x1409,0x03},
	{0x1438,0x01},
	{0x02ce,0x03},
	{0x0245,0xc9},
	{0x023a,0x08},
	{0x02cd,0x88},
	{0x0612,0x02},
	{0x0613,0xc7},
	{0x0243,0x03},
	{0x0089,0x03},
	{0x0002,0xab},
	{0x0040,0xa3},
	{0x0075,0x64},
	{0x0004,0x0f},
	{0x0053,0x0a},
	{0x0205,0x0c},
	{0x0a67,0x80},
	{0x0a54,0x0e},
	{0x0a65,0x10},
	{0x0a98,0x04},
	{0x05be,0x00},
	{0x05a9,0x01},
	{0x0023,0x00},
	{0x0022,0x00},
	{0x0025,0x00},
	{0x0024,0x00},
	{0x0028,0x0b},
	{0x0029,0x98},
	{0x002a,0x06},
	{0x002b,0x86},
	{0x0a83,0xe0},
	{0x0a72,0x02},
	{0x0a73,0x60},
	{0x0a75,0x41},
	{0x0a70,0x03},
	{0x0a5a,0x80},
	{0x0181,0x30},
	{0x0182,0x05},
	{0x0185,0x01},
	{0x0180,0x46},
	{0x0100,0x08},
	{0x010d,0x74},
	{0x010e,0x0e},
	{0x0113,0x02},
	{0x0114,0x01},
	{0x0115,0x10},
	{0x0a70,0x00},
	{0x0080,0x02},
	{0x0a67,0x00},
	{0x0052,0x02},
	{0x0076,0x01},
	{0x021a,0x10},
	{0x0049,0x0f},
	{0x004a,0x3c},
	{0x004b,0x00},
	{0x0430,0x25},
	{0x0431,0x25},
	{0x0432,0x25},
	{0x0433,0x25},
	{0x0434,0x59},
	{0x0435,0x59},
	{0x0436,0x59},
	{0x0437,0x59},
	{0x0100,0x09},
	{SENSOR_REG_END, 0x00},
};

static struct regval_list sensor_init_regs_2880_1620_40fps_mipi[] = {
        {0x03fe, 0xf0},
        {0x03fe, 0x00},
        {0x03fe, 0x10},
        {0x03fe, 0x00},
        {0x0a38, 0x02},
        {0x0a38, 0x03},
        {0x0a20, 0x07},
        {0x061b, 0x03},
        {0x061c, 0x50},
        {0x061d, 0x05},
        {0x061e, 0x7e},
        {0x061f, 0x03},
        {0x0a21, 0x08},
        {0x0a34, 0x40},
        {0x0a35, 0x11},
        {0x0a36, 0x90},
        {0x0a37, 0x03},
        {0x0314, 0x50},
        {0x0315, 0x32},
        {0x031c, 0xce},
        {0x0219, 0x47},
        {0x0342, 0x03},
        {0x0343, 0x84},
        {0x0340, 0x06},
        {0x0341, 0xf9},
        {0x0345, 0x02},
        {0x0347, 0x02},
        {0x0348, 0x0b},
        {0x0349, 0x98},
        {0x034a, 0x06},
        {0x034b, 0x8a},
        {0x0094, 0x0b},
        {0x0095, 0x40},
        {0x0096, 0x06},
        {0x0097, 0x54},
        {0x0099, 0x16},
        {0x009b, 0x28},
        {0x060c, 0x01},
        {0x060e, 0xd2},
        {0x060f, 0x05},
        {0x070c, 0x01},
        {0x070e, 0xd2},
        {0x070f, 0x05},
        {0x0709, 0x40},
        {0x0719, 0x40},
        {0x0909, 0x07},
        {0x0902, 0x04},
        {0x0904, 0x0b},
        {0x0907, 0x54},
        {0x0908, 0x06},
        {0x0903, 0x9d},
        {0x072a, 0x1c},
        {0x072b, 0x1c},
        {0x0724, 0x2b},
        {0x0727, 0x2b},
        {0x1412, 0x20},
        {0x1466, 0x18},
        {0x1467, 0x08},
        {0x1468, 0x10},
        {0x1469, 0x80},
        {0x146a, 0xe8},
        {0x0707, 0x07},
        {0x0737, 0x0f},
        {0x0704, 0x01},
        {0x0706, 0x02},
        {0x0716, 0x02},
        {0x0708, 0xc8},
        {0x0718, 0xc8},
        {0x061a, 0x02},
        {0x1407, 0x10},
        {0x1408, 0x16},
        {0x1409, 0x03},
        {0x1438, 0x01},
        {0x02ce, 0x03},
        {0x0245, 0xc9},
        {0x023a, 0x08},
        {0x02cd, 0x88},
        {0x0612, 0x02},
        {0x0613, 0xc7},
        {0x0243, 0x03},
        {0x0089, 0x03},
        {0x0002, 0xab},
        {0x0040, 0xa3},
        {0x0075, 0x64},
        {0x0004, 0x0f},
        {0x0053, 0x0a},
        {0x0205, 0x0c},
        {0x0a67, 0x80},
        {0x0a54, 0x0e},
        {0x0a65, 0x10},
        {0x0a98, 0x04},
        {0x05be, 0x00},
        {0x05a9, 0x01},
        {0x0028, 0x0b},
        {0x0029, 0x98},
        {0x002a, 0x06},
        {0x002b, 0x86},
        {0x0a83, 0xe0},
        {0x0a72, 0x02},
        {0x0a73, 0x60},
        {0x0a75, 0x41},
        {0x0a70, 0x03},
        {0x0a5a, 0x80},
        {0x0123, 0x30},
        {0x0124, 0x04},
        {0x0125, 0x30},
        {0x0129, 0x0c},
        {0x012a, 0x18},
        {0x012b, 0x18},
        {0x0181, 0x30},
        {0x0182, 0x05},
        {0x0185, 0x01},
        {0x0180, 0x46},
        {0x0107, 0x09},
        {0x0100, 0x08},
        {0x010d, 0x10},
        {0x010e, 0x0e},
        {0x0113, 0x02},
        {0x0114, 0x01},
        {0x0115, 0x10},
        {0x0a70, 0x00},
        {0x0080, 0x02},
        {0x0a67, 0x00},
        {0x0052, 0x02},
        {0x0076, 0x01},
        {0x021a, 0x10},
        {0x0049, 0x0f},
        {0x004a, 0x3c},
        {0x004b, 0x00},
        {0x0430, 0x25},
        {0x0431, 0x25},
        {0x0432, 0x25},
        {0x0433, 0x25},
        {0x0434, 0x59},
        {0x0435, 0x59},
        {0x0436, 0x59},
        {0x0437, 0x59},
        {0x0100, 0x09},

        {0x027f, 0x03},
        {0x02f7, 0x02},
        {0x02e1, 0x07},
	{SENSOR_REG_END, 0x00},
};

/*
 * the order of the jxf23_win_sizes is [full_resolution, preview_resolution]. */
static struct tx_isp_sensor_win_setting sensor_win_sizes[] = {
	/* [0] 2880*1620 @ max 30fps*/
	{
		.width = 2880,
		.height = 1620,
		.fps = 25 << 16 | 1,
		.mbus_code = TISP_VI_FMT_SGRBG10_1X10,
		.colorspace = TISP_COLORSPACE_SRGB,
		.regs = sensor_init_regs_2880_1620_25fps_mipi,
	},
	{
		.width = 2880,
		.height = 1620,
		.fps = 15 << 16 | 1,
		.mbus_code = TISP_VI_FMT_SGRBG10_1X10,
		.colorspace = TISP_COLORSPACE_SRGB,
		.regs = sensor_init_regs_2880_1620_15fps_mipi_dol,
	},
	{
		.width = 2960,
		.height = 1666,
		.fps = 25 << 16 | 1,
		.mbus_code = TISP_VI_FMT_SBGGR10_1X10,
		.colorspace = TISP_COLORSPACE_SRGB,
		.regs = sensor_init_regs_2960_1666_25fps_mipi,
	},
	{
		.width = 2880,
		.height = 1620,
		.fps = 40 << 16 | 1,
		.mbus_code = TISP_VI_FMT_SBGGR10_1X10,
		.colorspace = TISP_COLORSPACE_SRGB,
		.regs = sensor_init_regs_2880_1620_40fps_mipi,
	},
};

struct tx_isp_sensor_win_setting *wsize = &sensor_win_sizes[0];

static struct regval_list sensor_stream_on[] = {
	{SENSOR_REG_END, 0x00},
};

static struct regval_list sensor_stream_off[] = {
	{SENSOR_REG_END, 0x00},
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
	unsigned char v = 0;
	int ret;

	ret = sensor_read(sd, 0x03f0, &v);
	ISP_WARNING("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != SENSOR_CHIP_ID_H)
		return -ENODEV;
	ret = sensor_read(sd, 0x03f1, &v);
	ISP_WARNING("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
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
	int expo = (value & 0xffff);
	int again = (value & 0xffff0000) >> 16;
	struct again_lut *val_lut = sensor_again_lut;

	/*set integration time*/
	ret = sensor_write(sd, 0x0203, expo & 0xff);
	ret += sensor_write(sd, 0x0202, expo >> 8);
	/*set sensor analog gain*/
	//return 0;
	ret += sensor_write(sd, 0x031d ,0x2d);
	ret += sensor_write(sd, 0x0614, val_lut[again].reg614);
	ret += sensor_write(sd, 0x0615, val_lut[again].reg615);
	ret += sensor_write(sd, 0x0225, val_lut[again].reg225);
	ret += sensor_write(sd, 0x031d, 0x28);

	ret += sensor_write(sd, 0x1467, val_lut[again].reg1467);
	ret += sensor_write(sd, 0x1468, val_lut[again].reg1468);
	ret += sensor_write(sd, 0x00b8, val_lut[again].regb8);
	ret += sensor_write(sd, 0x00b9, val_lut[again].regb9);
	if (ret < 0)
		ISP_ERROR("sensor_write error  %d\n" ,__LINE__ );

	return ret;
}

# if  0
static int sensor_set_integration_time(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;

	ret = sensor_write(sd, 0x0203, value & 0xff);
	ret += sensor_write(sd, 0x0202, value >> 8);
	if (ret < 0)
		ISP_ERROR("sensor_write error  %d\n" ,__LINE__ );

	return ret;
}

static int sensor_set_analog_gain(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;
	struct again_lut *val_lut = sensor_again_lut;

	ret += sensor_write(sd, 0x031d ,0x2d);
	ret += sensor_write(sd, 0x0614, val_lut[value].reg614);
	ret += sensor_write(sd, 0x0615, val_lut[value].reg615);
	ret += sensor_write(sd, 0x0225, val_lut[value].reg225);
	ret += sensor_write(sd, 0x031d, 0x28);

	ret += sensor_write(sd, 0x1467, val_lut[value].reg1467);
	ret += sensor_write(sd, 0x1468, val_lut[value].reg1468);
	ret += sensor_write(sd, 0x00b8, val_lut[value].regb8);
	ret += sensor_write(sd, 0x00b9, val_lut[value].regb9);
	if (ret < 0)
		ISP_ERROR("sensor_write error  %d\n" ,__LINE__ );

	return ret;
}
#endif

static int sensor_set_integration_time_short(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;

	if (value <= 1) value = 1;
	ret = sensor_write(sd, 0x0201, value & 0xff);
	ret += sensor_write(sd, 0x0200, (value>>8) & 0xff);
	if (ret < 0) {
		ISP_ERROR("sensor_write error  %d\n" ,__LINE__ );
		return ret;
	}

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

static int sensor_set_attr(struct tx_isp_subdev *sd, struct tx_isp_sensor_win_setting *wise)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);

	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = TISP_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	sensor->video.max_fps = wsize->fps;
	sensor->video.min_fps = SENSOR_OUTPUT_MIN_FPS << 16 | 1;

	return 0;
}

static int sensor_init(struct tx_isp_subdev *sd, struct tx_isp_initarg *init)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = 0;

	if (!init->enable)
		return ISP_SUCCESS;

	sensor_set_attr(sd, wsize);
	sensor->video.state = TX_ISP_MODULE_DEINIT;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	sensor->priv = wsize;

	return 0;
}

static int sensor_s_stream(struct tx_isp_subdev *sd, struct tx_isp_initarg *init)
{
	int ret = 0;
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);

        printk("==========>> %s %d\n", __func__, __LINE__);
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
			ISP_WARNING("%s stream on\n", SENSOR_NAME));
			sensor->video.state = TX_ISP_MODULE_RUNNING;
		}
	} else {
		ret = sensor_write_array(sd, sensor_stream_off);
		pr_debug("%s stream off\n", SENSOR_NAME);
	}

	return ret;
}

static int sensor_set_fps(struct tx_isp_subdev *sd, int fps)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	unsigned int sclk = 0;
	unsigned int vts = 0;
	unsigned int hts = 0;
	unsigned int max_fps;
	unsigned char tmp;
	unsigned int newformat = 0; //the format is 24.8
	unsigned int short_time = 0;
	unsigned char val = 0;
	int ret = 0;

        ISP_WARNING("[%s %d] Frame rate setting is not supported !!!\n", __func__, __LINE__);
        return 0;
	switch(sensor->info.default_boot) {
	case 0:
		sclk = 1200 * 1750 * 30 * 2;
		max_fps = TX_SENSOR_MAX_FPS_30;
		break;
	case 1:
		sclk = 900 * 2334 * 15 * 2;
		max_fps = TX_SENSOR_MAX_FPS_15;
		break;
	case 2:
		sclk = 1200 *2100 * 25 * 2;
		max_fps = TX_SENSOR_MAX_FPS_25;
		break;
	case 3:
		sclk = 900 * 1750 * 40 * 2;
		max_fps = TX_SENSOR_MAX_FPS_40;
		break;
	default:
		ISP_ERROR("Now we do not support this framerate!!!\n");
	}

	printk("===========> fps = 0x%x\n", fps);
	/* the format of fps is 16/16. for example 30 << 16 | 2, the value is 30/2 fps. */
	newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
	if (newformat > (max_fps << 8) || newformat < (SENSOR_OUTPUT_MIN_FPS << 8)) {
		ISP_ERROR("warn: fps(%x) not in range\n", fps);
		return -1;
	}
	ret += sensor_read(sd, 0x0342, &tmp);
	hts = tmp & 0x0f;
	ret += sensor_read(sd, 0x0343, &tmp);
	if (ret < 0)
		return -1;
	hts = ((hts << 8) | tmp) << 1;
	vts = sclk * (fps & 0xffff) / hts / ((fps & 0xffff0000) >> 16);
	ret = sensor_write(sd, 0x0340, (unsigned char)((vts & 0x3f00) >> 8));
	ret += sensor_write(sd, 0x0341, (unsigned char)(vts & 0xff));
	if (ret < 0)
		return -1;

	if (sensor->info.default_boot == 1) {
		ret = sensor_read(sd, 0x0200, &val);
		short_time = val << 8;
		ret += sensor_read(sd, 0x0201, &val);
		short_time = val;
	}

	sensor->video.fps = fps;
	sensor->video.attr->max_integration_time_native = (sensor->info.default_boot == 0) ? (vts -8) : (vts -short_time -16);
	sensor->video.attr->integration_time_limit = (sensor->info.default_boot == 0) ? (vts -8) : (vts -short_time -16);
	sensor->video.attr->total_height = vts;
	sensor->video.attr->max_integration_time = (sensor->info.default_boot == 0) ? (vts -8) : (vts -short_time -16);

	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	return 0;
}
static int sensor_set_hvflip(struct tx_isp_subdev *sd, int enable)
{
	int ret = 0;
	uint8_t val, val1;
	uint8_t otp_val = 0x0;

	/* 2'b01: mirror; 2'b10:flip*/
	val = sensor_read(sd, 0x022c, &val);
	val1 = sensor_read(sd, 0x0063, &val1);

	/* 2'b01 mirror; 2'b10 flip; 2'b11 mirror &flip */
	switch(enable) {
		case 0:
			sensor_write(sd, 0x022c, val & 0x00); /*normal*/
			sensor_write(sd, 0x0063, val1 & 0x00);
			break;
		case 1:
			sensor_write(sd, 0x022c, val | 0x01); /*mirror*/
			sensor_write(sd, 0x0063, val1 | 0x01);
			break;
		case 2:
			sensor_write(sd, 0x022c, val | 0x02); /*filp*/
			sensor_write(sd, 0x0063, val1 | 0x02);
			break;
		case 3:
			sensor_write(sd, 0x022c, val | 0x03); /*mirror & filp*/
			sensor_write(sd, 0x0063, val1 | 0x03);
			break;
	}
		otp_val=0x60|val;

		//auto_load
		ret = sensor_write(sd,0x0a67,0x80);
		ret = sensor_write(sd,0x0a54,0x0e);
		ret = sensor_write(sd,0x0a65,0x10);
		ret = sensor_write(sd,0x0a98,0x04);
		ret = sensor_write(sd,0x05be,0x00);
		ret = sensor_write(sd,0x05a9,0x01);
		ret = sensor_write(sd,0x0023,0x00);
		ret = sensor_write(sd,0x0022,0x00);
		ret = sensor_write(sd,0x0025,0x00);
		ret = sensor_write(sd,0x0024,0x00);
		ret = sensor_write(sd,0x0028,0x0b);
		ret = sensor_write(sd,0x0029,0x98);
		ret = sensor_write(sd,0x002a,0x06);
		ret = sensor_write(sd,0x002b,0x86);
		ret = sensor_write(sd,0x0a83,0xe0);
		ret = sensor_write(sd,0x0a72,0x02);
		ret = sensor_write(sd,0x0a73,otp_val);
		ret = sensor_write(sd,0x0a75,0x41);
		ret = sensor_write(sd,0x0a70,0x03);
		ret = sensor_write(sd,0x0a5a,0x80);
		private_msleep(20);
		ret = sensor_write(sd,0x0a70,0x00);
		ret = sensor_write(sd,0x0080,0x02);
		ret = sensor_write(sd,0x0a67,0x00);
	return ret;
}

static int sensor_set_mode(struct tx_isp_subdev *sd, int value)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = ISP_SUCCESS;

	if (wsize) {
		sensor_set_attr(sd, wsize);
		ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	}
	return ret;
}

struct clk *sclka;
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
		sensor_attr.one_line_expr_in_us = 19;
		sensor_attr.total_width = 2400;
		sensor_attr.total_height = 2100;
		sensor_attr.max_integration_time_native = 2100 - 8;
		sensor_attr.integration_time_limit = 2100 - 8;
		sensor_attr.max_integration_time = 2100 - 8;
		sensor_attr.again = 0;
		sensor_attr.integration_time = 0x6c6;
		break;
	case 1:
		sensor_attr.wdr_cache = wdr_bufsize;
		wsize = &sensor_win_sizes[1];
		memcpy(&sensor_attr.mipi, &sensor_mipi_dol, sizeof(sensor_mipi_dol));
		sensor_attr.wdr_cache = wdr_bufsize;
		sensor_attr.one_line_expr_in_us = 10;
		sensor_attr.min_integration_time = 2;
		sensor_attr.min_integration_time_short = 2;
		//sensor_attr.max_again_short = 390142;
		sensor_attr.total_width = 1800;
		sensor_attr.total_height = 2334;
		sensor_attr.max_integration_time_native = 2334 - 16 - 102;
		sensor_attr.integration_time_limit = 2334 - 16 - 102;
		sensor_attr.max_integration_time = 2334 - 16 - 102;
		sensor_attr.max_integration_time_short = 102;
		sensor_attr.data_type = TX_SENSOR_DATA_TYPE_WDR_DOL;
		printk("================>hdr@15fps is ok !!!\n");
		break;
	case 2:
		wsize = &sensor_win_sizes[2];
		sensor_attr.data_type = TX_SENSOR_DATA_TYPE_LINEAR;
		memcpy(&sensor_attr.mipi, &sensor_mipi_2960_1666_linear, sizeof(sensor_mipi_2960_1666_linear));
		sensor_attr.one_line_expr_in_us = 19;
		sensor_attr.total_width = 2400;
		sensor_attr.total_height = 2100;
		sensor_attr.max_integration_time_native = 2100 - 8;
		sensor_attr.integration_time_limit = 2100 - 8;
		sensor_attr.max_integration_time = 2100 - 8;
		sensor_attr.again = 0;
		sensor_attr.integration_time = 0x6c6;
		break;
	case 3:
		wsize = &sensor_win_sizes[3];
		sensor_attr.data_type = TX_SENSOR_DATA_TYPE_LINEAR;
		memcpy(&sensor_attr.mipi, &sensor_mipi_linear, sizeof(sensor_mipi_linear));
                sensor_attr.mipi.clk = 1152;
		sensor_attr.one_line_expr_in_us = 14;
		sensor_attr.total_width = 3600;
		sensor_attr.total_height = 1750;
		sensor_attr.max_integration_time_native = 1750 - 8;
		sensor_attr.integration_time_limit = 1750 - 8;
		sensor_attr.max_integration_time = 1750 - 8;
		sensor_attr.again = 0;
		sensor_attr.integration_time = 0x6c6;
		break;
	default:
		ISP_ERROR("Have no this setting!!!\n");
	}
        sensor_attr.fsync_attr.mode = fsync_mode;
        if (fsync_mode == TX_ISP_SENSOR_FSYNC_MODE_MS_REALTIME_MISPLACE) {
                sensor_attr.total_height = sensor_attr.total_height * 8 / 3;
                wsize->fps = ( (wsize->fps & 0xffff0000) * 3 | (wsize->fps & 0xffff) * 8 );
        }
        sensor_attr.max_integration_time_native = sensor_attr.total_height - 8;
        sensor_attr.integration_time_limit = sensor_attr.total_height - 8;
        sensor_attr.max_integration_time = sensor_attr.total_height - 8;

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

	rate = private_clk_get_rate(sensor->mclk);
	switch(info->default_boot) {
	case 0:
    case 1:
	case 2:
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
                private_clk_set_rate(sensor->mclk, 27000000);
                private_clk_prepare_enable(sensor->mclk);
                break;
        case 3:
                private_clk_set_rate(sensor->mclk, 24000000);
                private_clk_prepare_enable(sensor->mclk);
                break;
	}

	ISP_WARNING("\n====>[default_boot=%d] [resolution=%dx%d] [video_interface=%d] [MCLK=%d] \n", info->default_boot, wsize->width, wsize->height, info->video_interface, info->mclk);
	reset_gpio = info->rst_gpio;
	pwdn_gpio = info->pwdn_gpio;

	sensor_set_attr(sd, wsize);
	sensor->priv = wsize;
	return 0;
}

static int sensor_g_chip_ident(struct tx_isp_subdev *sd,
		struct tx_isp_chip_ident *chip)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned int ident = 0;
	int ret = ISP_SUCCESS;

	sensor_attr_check(sd);
	ret = sensor_detect(sd, &ident);
	if (ret) {
		ISP_ERROR("chip found @ 0x%x (%s) is not an gc5603s1 chip.\n",
			client->addr, client->adapter->name);
		return ret;
	}
	ISP_WARNING("gc5603s1 chip found @ 0x%02x (%s)\n sensor drv version %s", client->addr, client->adapter->name, SENSOR_VERSION);
	if (chip) {
		memcpy(chip->name, "gc5603s1", sizeof("gc5603s1"));
		chip->ident = ident;
		chip->revision = SENSOR_VERSION;
	}
	return 0;
}

static int sensor_set_wdr(struct tx_isp_subdev *sd, int wdr_en)
{
	int ret = 0;

	private_gpio_direction_output(reset_gpio, 1);
	private_msleep(1);
	private_gpio_direction_output(reset_gpio, 0);
	private_msleep(1);
	private_gpio_direction_output(reset_gpio, 1);
	private_msleep(1);

	ret += sensor_write_array(sd, wsize->regs);
	sensor_write(sd, 0x0200, 0x00);
	sensor_write(sd, 0x0201, 0x32);
	sensor_write(sd, 0x0202, 0x03);
	sensor_write(sd, 0x0203, 0xe8);
	ret += sensor_write_array(sd, sensor_stream_on);

	return 0;
}

static int sensor_set_wdr_stop(struct tx_isp_subdev *sd, int wdr_en)
{
	int ret = 0;
	struct tx_isp_sensor *sensor = tx_isp_get_subdev_hostdata(sd);
	struct tx_isp_sensor_register_info *info = &sensor->info;

	ret = sensor_write(sd, 0x03fe,0xf0);

	if ( wdr_en == 1) {
		info->default_boot = 1;
		memcpy(&sensor_attr.mipi, &sensor_mipi_dol, sizeof(sensor_mipi_dol));
		sensor_attr.data_type = TX_SENSOR_DATA_TYPE_WDR_DOL;
		sensor_attr.wdr_cache = wdr_bufsize;
		wsize = &sensor_win_sizes[1];
		sensor_attr.one_line_expr_in_us = 10;
		sensor_attr.min_integration_time = 2;
		sensor_attr.min_integration_time_short = 2;
		sensor_attr.total_width = 1800;
		sensor_attr.total_height = 2334;
		sensor_attr.max_integration_time_native = 2334 - 16 - 102;
		sensor_attr.integration_time_limit = 2334 - 16 - 102;
		sensor_attr.max_integration_time = 2334 - 16 - 102;
		sensor_attr.max_integration_time_short = 102;
		ISP_WARNING("-----------------------------> switch wdr is ok <-----------------------\n");
	} else if (wdr_en == 0) {
		info->default_boot = 0;
		wsize = &sensor_win_sizes[0];
		sensor_attr.data_type = TX_SENSOR_DATA_TYPE_LINEAR;
		memcpy(&sensor_attr.mipi, &sensor_mipi_linear, sizeof(sensor_mipi_linear));
		sensor_attr.one_line_expr_in_us = 19;
		sensor_attr.total_width = 2400;
		sensor_attr.total_height = 2100;
		sensor_attr.max_integration_time_native = 2100 - 8;
		sensor_attr.integration_time_limit = 2100 - 8;
		sensor_attr.max_integration_time = 2100 - 8;
		sensor_attr.again = 0;
		sensor_attr.integration_time = 0x6ce;
		ISP_WARNING("-----------------------------> switch linear is ok <-----------------------\n");
	} else {
		ISP_ERROR("Can not support this data type!!!");
		return -1;
	}

	sensor_set_attr(sd, wsize);
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	return ret;
}

static int sensor_fsync(struct tx_isp_subdev *sd, struct tx_isp_sensor_fsync *fsync)
{
        uint8_t val;
        uint16_t ret_val;

        printk("=========>> [%s %d]\n", __func__, __LINE__);
        if (fsync->place != TX_ISP_SENSOR_FSYNC_PLACE_STREAMON_AFTER)
                return 0;
        switch (fsync->call_index) {
        case 0:
                switch (fsync_mode) {
                case 2:
                        printk("=========>> [%s %d]\n", __func__, __LINE__);
                        sensor_write(sd, 0x027f, 0x03);
                        sensor_write(sd, 0x02f7, 0x02);
                        sensor_write(sd, 0x02e1, 0x07);
                        break;
                case 3:
                        printk("=========>> [%s %d]\n", __func__, __LINE__);
                        sensor_read(sd, 0x0340, &val);
                        ret_val = val << 8;
                        sensor_read(sd, 0x0341, &val);
                        ret_val = val;
                        ret_val = ret_val * 8 / 3;
                        sensor_write(sd, 0x0340, ret_val >> 8);
                        sensor_write(sd, 0x0341, ret_val & 0xff);
                        sensor_write(sd, 0x027f, 0x03);
                        sensor_write(sd, 0x02f7, 0x02);
                        sensor_write(sd, 0x02e1, 0x07);
                        break;
                }
                break;
        }

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
			ret = sensor_set_expo(sd,sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_INT_TIME_SHORT:
		if (arg)
			ret = sensor_set_integration_time_short(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_INT_TIME:
		//if (arg)
		//	ret = sensor_set_integration_time(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_AGAIN:
		//if (arg)
		//	ret = sensor_set_analog_gain(sd, sensor_val->value);
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
			ret = sensor_set_hvflip(sd, sensor_val->value);
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

static struct tx_isp_subdev_sensor_ops sensor_sensor_ops = {
	.ioctl = sensor_sensor_ops_ioctl,
        .fsync = sensor_fsync,
};

static struct tx_isp_subdev_ops sensor_ops = {
	.core = &sensor_core_ops,
	.video = &sensor_video_ops,
	.sensor = &sensor_sensor_ops,
};

/* It's the sensor device */
static u64 tx_isp_module_dma_mask = ~(u64)0;
struct platform_device sensor_platform_device = {
	.name = "gc5603s1",
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
	struct tx_isp_video_in *video;
	struct tx_isp_sensor *sensor;

	sensor = (struct tx_isp_sensor *)kzalloc(sizeof(*sensor), GFP_KERNEL);
	if (!sensor) {
		ISP_ERROR("Failed to allocate sensor subdev.\n");
		return -ENOMEM;
	}
	memset(sensor, 0 ,sizeof(*sensor));
	sensor->dev = &client->dev;
	sd = &sensor->sd;
	video = &sensor->video;
	sensor->video.shvflip = shvflip;
	sensor_attr.expo_fs = 1;
	sensor->video.attr = &sensor_attr;
	tx_isp_subdev_init(&sensor_platform_device, sd, &sensor_ops);
	tx_isp_set_subdevdata(sd, client);
	tx_isp_set_subdev_hostdata(sd, sensor);
	private_i2c_set_clientdata(client, sd);

	pr_debug("probe ok ------->%s\n", SENSOR_NAME);

	return 0;
}

static int sensor_remove(struct i2c_client *client)
{
	struct tx_isp_subdev *sd = private_i2c_get_clientdata(client);
	struct tx_isp_sensor *sensor = tx_isp_get_subdev_hostdata(sd);

	private_clk_disable_unprepare(sensor->mclk);
	tx_isp_subdev_deinit(sd);
	kfree(sensor);

	return 0;
}

static const struct i2c_device_id sensor_id[] = {
	{ "gc5603s1", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "gc5603s1",
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

MODULE_DESCRIPTION("A low-level driver for "SENSOR_NAME" sensor");
MODULE_LICENSE("GPL");
