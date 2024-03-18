/*
 * jxh61.c
 *
 * Copyright (C) 2012 Ingenic Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <sensor-common.h>
#include <sensor-info.h>
#include <apical-isp/apical_math.h>

#include <soc/gpio.h>

#define SENSOR_NAME "jxh61"
#define SENSOR_BUS_TYPE TX_SENSOR_CONTROL_INTERFACE_I2C
#define SENSOR_I2C_ADDRESS 0x30
#define SENSOR_MAX_WIDTH 1600
#define SENSOR_MAX_HEIGHT 900
#define SENSOR_CHIP_ID_H (0xa0)
#define SENSOR_CHIP_ID_L (0x42)

#define SENSOR_REG_END 0xff
#define SENSOR_REG_DELAY 0xfe

#define SENSOR_SUPPORT_PCLK (36*1000*1000)
#define SENSOR_OUTPUT_MAX_FPS 30
#define SENSOR_OUTPUT_MIN_FPS 5
#define DRIVE_CAPABILITY_1

static struct sensor_info sensor_info = {
	.name = SENSOR_NAME,
	.chip_id = SENSOR_CHIP_ID,
	.version = SENSOR_VERSION,
	.min_fps = SENSOR_OUTPUT_MIN_FPS,
	.max_fps = SENSOR_OUTPUT_MAX_FPS,
	.chip_i2c_addr = SENSOR_I2C_ADDRESS,
	.width = SENSOR_MAX_WIDTH,
	.height = SENSOR_MAX_HEIGHT,
};

struct regval_list {
	unsigned char reg_num;
	unsigned char value;
};

static int reset_gpio = GPIO_PA(18);
module_param(reset_gpio, int, S_IRUGO);
MODULE_PARM_DESC(reset_gpio, "Reset GPIO NUM");

static int pwdn_gpio = -1;
module_param(pwdn_gpio, int, S_IRUGO);
MODULE_PARM_DESC(pwdn_gpio, "Power down GPIO NUM");

static int sensor_gpio_func = DVP_PA_LOW_10BIT;
module_param(sensor_gpio_func, int, S_IRUGO);
MODULE_PARM_DESC(sensor_gpio_func, "Sensor GPIO function");

struct tx_isp_sensor_attribute jxh61_attr;
#if 0
static inline unsigned char cale_again_register(unsigned int gain)
{
	int index = 0;
	int i = 0, p = 0;
	for(index = 3; index >= 0; index--)
		if (gain & (1 << (index + TX_ISP_GAIN_FIXED_POINT)))
			break;
	i = index;
	p = (gain >> (TX_ISP_GAIN_FIXED_POINT - 4)) & ((1 << (4 + i)) - 1);
	return (i << 4) | p;
}

unsigned int jxh61_alloc_again(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
	unsigned int again = 0;
	unsigned int gain_one = math_exp2(isp_gain, shift, TX_ISP_GAIN_FIXED_POINT);

	if (gain_one < jxh61_attr.max_again) {
		again = (gain_one  >> (TX_ISP_GAIN_FIXED_POINT - 4) << (TX_ISP_GAIN_FIXED_POINT - 4));
	} else {
		again = jxh61_attr.max_again;
	}
	isp_gain = log2_fixed_to_fixed(again, TX_ISP_GAIN_FIXED_POINT, shift);
	*sensor_again = cale_again_register(again);
	return isp_gain;
}
#else
static inline unsigned char cale_again_register(unsigned int gain)
{
	int index = 0;
	int i = 0, p = 0;
	for(index = 3; index >= 0; index--)
		if (gain & (1 << (index + TX_ISP_GAIN_FIXED_POINT)))
			break;
	i = index;
	p = (gain >> (TX_ISP_GAIN_FIXED_POINT + index - 4)) & 0xf;
	return (i << 4) | p;
}
static inline unsigned int cale_sensor_again_to_isp(unsigned char reg)
{
	unsigned int h,l;
	h = reg >> 4;
	l = reg & 0xf;
	return (1 << (h + TX_ISP_GAIN_FIXED_POINT)) | (l << ((TX_ISP_GAIN_FIXED_POINT + h - 4)));
}
unsigned int jxh61_alloc_again(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
	unsigned int again = 0;
	unsigned int gain_one = 0;
	unsigned int gain_one1 = 0;

	if (isp_gain > jxh61_attr.max_again) {
		isp_gain = jxh61_attr.max_again;
	}
	again = isp_gain;
	gain_one = math_exp2(isp_gain, shift, TX_ISP_GAIN_FIXED_POINT);
	*sensor_again = cale_again_register(gain_one);
	gain_one1 = cale_sensor_again_to_isp(*sensor_again);
	isp_gain = log2_fixed_to_fixed(gain_one1, TX_ISP_GAIN_FIXED_POINT, shift);
//	printk("again = %d gain_one = 0x%0x sensor_gain = 0x%0x gain_one1 = 0x%0x isp_gain = %d\n", again, gain_one, *sensor_again, gain_one1, isp_gain);
	return isp_gain;
}
#endif
unsigned int jxh61_alloc_dgain(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_dgain)
{
	return isp_gain;
}
struct tx_isp_sensor_attribute jxh61_attr={
	.name = "jxh61",
	.chip_id = 0xa061,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = V4L2_SBUS_MASK_SAMPLE_8BITS | V4L2_SBUS_MASK_ADDR_8BITS,
	.cbus_device = SENSOR_I2C_ADDRESS,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_DVP,
	.dvp = {
		.mode = SENSOR_DVP_HREF_MODE,
		.blanking = {
			.vblanking = 0,
			.hblanking = 0,
		},
	},
	.max_again = 0xff << (TX_ISP_GAIN_FIXED_POINT - 4),
	.max_dgain = 0,
	.min_integration_time = 1,
	.min_integration_time_native = 1,
	.max_integration_time_native = 896,
	.integration_time_limit = 896,
	.total_width = 1600,
	.total_height = 900,
	.max_integration_time = 896,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 1,
	.dgain_apply_delay = 1,
	.sensor_ctrl.alloc_again = jxh61_alloc_again,
	.sensor_ctrl.alloc_dgain = jxh61_alloc_dgain,
	//void priv; /* point to struct tx_isp_sensor_board_info */
};

static struct regval_list jxh61_init_regs_1280_720_25fps[] = {
/* ;========================================= */
/* ;INI Create Date : 2016/3/30 */
/* ;Terra Ver : Gaia20151110-05 */
/* ;Create By steve */

/* ;;Output Detail: */
/* ;;MCLK:24 MHz */
/* ;;PCLK:36 MHz */
/* ;;FrameW:1600 */
/* ;;FrameH:900 */

#ifdef DRIVE_CAPABILITY_1
	{0x0D,0x40},
#elif defined(DRIVE_CAPABILITY_2)
	{0x0D,0x44},
#endif
	/* {0x0D,0x44}, */

	/* {0x0c,0x41}, */

	{0x1F,0x04},
	{0x0E,0x1D},
	{0x0F,0x09},
	{0x10,0x1E},
	{0x11,0x80},
	{0x20,0x40},
	{0x21,0x06},
	{0x22,0x84},
	{0x23,0x03},
	{0x24,0x00},
	{0x25,0xD0},
	{0x26,0x25},
	{0x12,0x40},
	{0x27,0x3B},
	{0x28,0x0B},
	{0x29,0x01},
	{0x2A,0x24},
	{0x2B,0x29},
	{0x2C,0x03},
	{0x2D,0x01},
	{0x2E,0xB8},
	{0x2F,0x00},
	{0x30,0x92},
	{0x31,0x0A},
	{0x32,0xAA},
	{0x33,0x14},
	{0x34,0x38},
	{0x35,0x54},
	{0x42,0x41},
	{0x43,0x08},
	{0x1D,0xFF},
	{0x1E,0x9F},
	{0x6C,0x90},
	{0x73,0xB3},
	{0x70,0x68},
	{0x76,0x40},
	{0x77,0x06},
	{0x72,0x48},
	{0x6D,0xA2},
	{0x48,0x40},
	{0x60,0xA4},
	{0x61,0xFF},
	{0x62,0x40},
	{0x65,0x00},
	{0x66,0x20},
	{0x67,0x30},
	{0x68,0x04},
	{0x69,0x74},
	{0x6F,0x24},
	{0x63,0x59},
	{0x6A,0x09},
	{0x13,0x87},
	{0x14,0x80},
	{0x16,0xC0},
	{0x17,0x40},
	{0x18,0xE1},
	{0x38,0x35},
	{0x39,0x98},
	{0x4a,0x03},
	{0x49,0x10},
	/* {0x12,0x00}, */
	{SENSOR_REG_END, 0x00},	/* END MARKER */
};

/* static struct regval_list jxh61_init_version_80[] = { */

/* 	{0x27,0x46}, */
/* 	{0x2C,0x00}, */
/* 	{0x63,0x19}, */
/* 	{SENSOR_REG_END, 0x00},	/\* END MARKER *\/ */
/* }; */

/* static struct regval_list jxh61_init_version_81[] = { */

/* 	{0x27,0x3c}, */
/* 	{0x2C,0x04}, */
/* 	{0x63,0x51}, */
/* 	{SENSOR_REG_END, 0x00},	/\* END MARKER *\/ */
/* }; */

/*
 * the order of the jxh61_win_sizes is [full_resolution, preview_resolution].
 */
static struct tx_isp_sensor_win_setting jxh61_win_sizes[] = {
	/* 1280*800 */
	{
		.width = 1280,
		.height = 720,
		.fps = 25 << 16 | 1,
		.mbus_code = V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.regs = jxh61_init_regs_1280_720_25fps,
	}
};

/*
 * the part of driver was fixed.
 */

static struct regval_list jxh61_stream_on[] = {
	{0x12, 0x00},

	{SENSOR_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list jxh61_stream_off[] = {
	/* Sensor enter LP11*/
	{0x12, 0x40},

	{SENSOR_REG_END, 0x00},	/* END MARKER */
};

int jxh61_read(struct v4l2_subdev *sd, unsigned char reg,
		unsigned char *value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
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
	int ret;
	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret > 0)
		ret = 0;

	return ret;
}

int jxh61_write(struct v4l2_subdev *sd, unsigned char reg,
		unsigned char value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned char buf[2] = {reg, value};
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 2,
		.buf = buf,
	};
	int ret;
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret > 0)
		ret = 0;

	return ret;
}

static int jxh61_read_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != SENSOR_REG_END) {
		if (vals->reg_num == SENSOR_REG_DELAY) {
			if (vals->value >= (1000 / HZ))
				msleep(vals->value);
			else
				mdelay(vals->value);
		} else {
			ret = jxh61_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
		}
		/*printk("vals->reg_num:0x%02x, vals->value:0x%02x\n",vals->reg_num, val);*/
		vals++;
	}
	return 0;
}
static int jxh61_write_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != SENSOR_REG_END) {
		if (vals->reg_num == SENSOR_REG_DELAY) {
			if (vals->value >= (1000 / HZ))
				msleep(vals->value);
			else
				mdelay(vals->value);
		} else {
			ret = jxh61_write(sd, vals->reg_num, vals->value);
			if (ret < 0)
				return ret;
		}
		/*printk("vals->reg_num:%x, vals->value:%x\n",vals->reg_num, vals->value);*/
		vals++;
	}
	return 0;
}

static int jxh61_reset(struct v4l2_subdev *sd, u32 val)
{
	return 0;
}

static int jxh61_detect(struct v4l2_subdev *sd, unsigned int *ident)
{
	unsigned char v;
	int ret;
	ret = jxh61_read(sd, 0x0a, &v);
	/*printk("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);*/
	if (ret < 0)
		return ret;
	if (v != SENSOR_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = jxh61_read(sd, 0x0b, &v);
	/*printk("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);*/
	if (ret < 0)
		return ret;
	if (v != SENSOR_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;
	return 0;
}

static int jxh61_set_integration_time(struct v4l2_subdev *sd, int value)
{
	unsigned int expo = value;
	int ret = 0;
	jxh61_write(sd, 0x01, (unsigned char)(expo & 0xff));
	jxh61_write(sd, 0x02, (unsigned char)((expo >> 8) & 0xff));
	if (ret < 0)
		return ret;

	return 0;
}
static int jxh61_set_analog_gain(struct v4l2_subdev *sd, int value)
{
	/* 0x00 bit[6:0] */
	jxh61_write(sd, 0x00, (unsigned char)(value & 0x7f));
	return 0;
}
static int jxh61_set_digital_gain(struct v4l2_subdev *sd, int value)
{
	/* 0x00 bit[7] if gain > 2X set 0; if gain > 4X set 1 */
	return 0;
}

static int jxh61_get_black_pedestal(struct v4l2_subdev *sd, int value)
{
#if 0
	int ret = 0;
	int black = 0;
	unsigned char h,l;
	unsigned char reg = 0xff;
	unsigned int * v = (unsigned int *)(value);
	ret = jxh61_read(sd, 0x48, &h);
	if (ret < 0)
		return ret;
	switch(*v) {
		case SENSOR_R_BLACK_LEVEL:
			black = (h & 0x3) << 8;
			reg = 0x44;
			break;
		case SENSOR_GR_BLACK_LEVEL:
			black = (h & (0x3 << 2)) << 8;
			reg = 0x45;
			break;
		case SENSOR_GB_BLACK_LEVEL:
			black = (h & (0x3 << 4)) << 8;
			reg = 0x46;
			break;
		case SENSOR_B_BLACK_LEVEL:
			black = (h & (0x3 << 6)) << 8;
			reg = 0x47;
			break;
		default:
			return -1;
	}
	ret = jxh61_read(sd, reg, &l);
	if (ret < 0)
		return ret;
	*v = (black | l);
#endif
	return 0;
}

static int jxh61_init(struct v4l2_subdev *sd, u32 enable)
{
	struct tx_isp_sensor *sensor = (container_of(sd, struct tx_isp_sensor, sd));
	struct tx_isp_notify_argument arg;
	struct tx_isp_sensor_win_setting *wsize = &jxh61_win_sizes[0];
	int ret = 0;
	unsigned char val;

	if (!enable)
		return ISP_SUCCESS;

	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = V4L2_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	ret = jxh61_write_array(sd, wsize->regs);
	ret = jxh61_read(sd, 0x09, &val);
	/* if (val == 0x00 || val == 0x80) */
	/* 	jxh61_write_array(sd, jxh61_init_version_80); */
	/* else if (val == 0x81) */
	/* 	jxh61_write_array(sd, jxh61_init_version_81); */
	/* if (ret) */
	/* 	return ret; */
	arg.value = (int)&sensor->video;
	sd->v4l2_dev->notify(sd, TX_ISP_NOTIFY_SYNC_VIDEO_IN, &arg);
	sensor->priv = wsize;
	return 0;
}

static int jxh61_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret = 0;

	if (enable) {
		ret = jxh61_write_array(sd, jxh61_stream_on);
		printk("jxh61 stream on\n");
	}
	else {
		ret = jxh61_write_array(sd, jxh61_stream_off);
		printk("jxh61 stream off\n");
	}
	return ret;
}

static int jxh61_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	return 0;
}

static int jxh61_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	return 0;
}


static int jxh61_set_fps(struct tx_isp_sensor *sensor, int fps)
{
	struct tx_isp_notify_argument arg;
	struct v4l2_subdev *sd = &sensor->sd;
	unsigned int pclk = SENSOR_SUPPORT_PCLK;
	unsigned short hts;
	unsigned short vts = 0;
	unsigned char tmp;
	unsigned int newformat = 0; //the format is 24.8
	int ret = 0;
	/* the format of fps is 16/16. for example 25 << 16 | 2, the value is 25/2 fps. */
	newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
	if (newformat > (SENSOR_OUTPUT_MAX_FPS << 8) || newformat < (SENSOR_OUTPUT_MIN_FPS << 8))
		return -1;
	ret += jxh61_read(sd, 0x21, &tmp);
	hts = tmp;
	ret += jxh61_read(sd, 0x20, &tmp);
	if (ret < 0)
		return -1;
	hts = (hts << 8) + tmp;
	/*vts = (pclk << 4) / (hts * (newformat >> 4));*/
	vts = pclk * (fps & 0xffff) / hts / ((fps & 0xffff0000) >> 16);
	ret = jxh61_write(sd, 0x22, (unsigned char)(vts & 0xff));
	ret += jxh61_write(sd, 0x23, (unsigned char)(vts >> 8));
	if (ret < 0)
		return -1;
	sensor->video.fps = fps;
	sensor->video.attr->max_integration_time_native = vts - 4;
	sensor->video.attr->integration_time_limit = vts - 4;
	sensor->video.attr->total_height = vts;
	sensor->video.attr->max_integration_time = vts - 4;
	arg.value = (int)&sensor->video;
	sd->v4l2_dev->notify(sd, TX_ISP_NOTIFY_SYNC_VIDEO_IN, &arg);
	return 0;
}

static int jxh61_set_mode(struct tx_isp_sensor *sensor, int value)
{
	struct tx_isp_notify_argument arg;
	struct v4l2_subdev *sd = &sensor->sd;
	struct tx_isp_sensor_win_setting *wsize = NULL;
	int ret = ISP_SUCCESS;
	if (value == TX_ISP_SENSOR_FULL_RES_MAX_FPS) {
		wsize = &jxh61_win_sizes[0];
	} else if (value == TX_ISP_SENSOR_PREVIEW_RES_MAX_FPS) {
		wsize = &jxh61_win_sizes[0];
	}
	if (wsize) {
		sensor->video.mbus.width = wsize->width;
		sensor->video.mbus.height = wsize->height;
		sensor->video.mbus.code = wsize->mbus_code;
		sensor->video.mbus.field = V4L2_FIELD_NONE;
		sensor->video.mbus.colorspace = wsize->colorspace;
		if (sensor->priv != wsize) {
			ret = jxh61_write_array(sd, wsize->regs);
			if (!ret)
				sensor->priv = wsize;
		}
		sensor->video.fps = wsize->fps;
		arg.value = (int)&sensor->video;
		sd->v4l2_dev->notify(sd, TX_ISP_NOTIFY_SYNC_VIDEO_IN, &arg);
	}
	return ret;
}
static int jxh61_g_chip_ident(struct v4l2_subdev *sd,
		struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned int ident = 0;
	int ret = ISP_SUCCESS;

	if (reset_gpio != -1) {
		ret = gpio_request(reset_gpio,"jxh61_reset");
		if (!ret) {
			gpio_direction_output(reset_gpio, 1);
			msleep(10);
			gpio_direction_output(reset_gpio, 0);
			msleep(10);
			gpio_direction_output(reset_gpio, 1);
			msleep(1);
		} else {
			printk("gpio requrest fail %d\n",reset_gpio);
		}
	}
	if (pwdn_gpio != -1) {
		ret = gpio_request(pwdn_gpio, "jxh61_pwdn");
		if (!ret) {
			gpio_direction_output(pwdn_gpio, 1);
			msleep(50);
			gpio_direction_output(pwdn_gpio, 0);
			msleep(10);
		} else {
			printk("gpio requrest fail %d\n", pwdn_gpio);
		}
	}
	ret = jxh61_detect(sd, &ident);
	if (ret) {
		v4l_err(client,
				"chip found @ 0x%x (%s) is not an jxh61 chip.\n",
				client->addr, client->adapter->name);
		return ret;
	}
	v4l_info(client, "jxh61 chip found @ 0x%02x (%s)\n",
			client->addr, client->adapter->name);
	return v4l2_chip_ident_i2c_client(client, chip, ident, 0);
}

static int jxh61_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}
static long jxh61_ops_private_ioctl(struct tx_isp_sensor *sensor, struct isp_private_ioctl *ctrl)
{
	struct v4l2_subdev *sd = &sensor->sd;
	long ret = 0;
	switch(ctrl->cmd) {
		case TX_ISP_PRIVATE_IOCTL_SENSOR_INT_TIME:
			ret = jxh61_set_integration_time(sd, ctrl->value);
			break;
		case TX_ISP_PRIVATE_IOCTL_SENSOR_AGAIN:
			ret = jxh61_set_analog_gain(sd, ctrl->value);
			break;
		case TX_ISP_PRIVATE_IOCTL_SENSOR_DGAIN:
			ret = jxh61_set_digital_gain(sd, ctrl->value);
			break;
		case TX_ISP_PRIVATE_IOCTL_SENSOR_BLACK_LEVEL:
			ret = jxh61_get_black_pedestal(sd, ctrl->value);
			break;
		case TX_ISP_PRIVATE_IOCTL_SENSOR_RESIZE:
			ret = jxh61_set_mode(sensor,ctrl->value);
			break;
		case TX_ISP_PRIVATE_IOCTL_SUBDEV_PREPARE_CHANGE:
		//	ret = jxh61_write_array(sd, jxh61_stream_off);
			break;
		case TX_ISP_PRIVATE_IOCTL_SUBDEV_FINISH_CHANGE:
		//	ret = jxh61_write_array(sd, jxh61_stream_on);
			break;
		case TX_ISP_PRIVATE_IOCTL_SENSOR_FPS:
			ret = jxh61_set_fps(sensor, ctrl->value);
			break;
		default:
			break;;
	}
	return 0;
}
static long jxh61_ops_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct tx_isp_sensor *sensor =container_of(sd, struct tx_isp_sensor, sd);
	int ret;
	switch(cmd) {
		case VIDIOC_ISP_PRIVATE_IOCTL:
			ret = jxh61_ops_private_ioctl(sensor, arg);
			break;
		default:
			return -1;
			break;
	}
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int jxh61_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned char val = 0;
	int ret;

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	ret = jxh61_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 2;
	return ret;
}

static int jxh61_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	jxh61_write(sd, reg->reg & 0xffff, reg->val & 0xff);
	return 0;
}
#endif

static const struct v4l2_subdev_core_ops jxh61_core_ops = {
	.g_chip_ident = jxh61_g_chip_ident,
	.reset = jxh61_reset,
	.init = jxh61_init,
	.s_power = jxh61_s_power,
	.ioctl = jxh61_ops_ioctl,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = jxh61_g_register,
	.s_register = jxh61_s_register,
#endif
};

static const struct v4l2_subdev_video_ops jxh61_video_ops = {
	.s_stream = jxh61_s_stream,
	.s_parm = jxh61_s_parm,
	.g_parm = jxh61_g_parm,
};

static const struct v4l2_subdev_ops jxh61_ops = {
	.core = &jxh61_core_ops,
	.video = &jxh61_video_ops,
};

static int jxh61_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct tx_isp_video_in *video;
	struct tx_isp_sensor *sensor;
	struct tx_isp_sensor_win_setting *wsize = &jxh61_win_sizes[0];
	int ret = -1;

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

	jxh61_attr.dvp.gpio = sensor_gpio_func;
	 /*
		convert sensor-gain into isp-gain,
	 */
	jxh61_attr.max_again = log2_fixed_to_fixed(jxh61_attr.max_again, TX_ISP_GAIN_FIXED_POINT, LOG2_GAIN_SHIFT);
	jxh61_attr.max_dgain = jxh61_attr.max_dgain;
	sd = &sensor->sd;
	video = &sensor->video;
	sensor->video.attr = &jxh61_attr;
	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	v4l2_i2c_subdev_init(sd, client, &jxh61_ops);
	v4l2_set_subdev_hostdata(sd, sensor);

	return 0;
err_set_sensor_gpio:
	clk_disable(sensor->mclk);
	clk_put(sensor->mclk);
err_get_mclk:
	kfree(sensor);

	return -1;
}

static int jxh61_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tx_isp_sensor *sensor = v4l2_get_subdev_hostdata(sd);

	if (reset_gpio != -1)
		gpio_free(reset_gpio);
	if (pwdn_gpio != -1)
		gpio_free(pwdn_gpio);

	clk_disable(sensor->mclk);
	clk_put(sensor->mclk);

	v4l2_device_unregister_subdev(sd);
	kfree(sensor);
	return 0;
}

static const struct i2c_device_id jxh61_id[] = {
	{ "jxh61", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, jxh61_id);

static struct i2c_driver jxh61_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "jxh61",
	},
	.probe = jxh61_probe,
	.remove = jxh61_remove,
	.id_table = jxh61_id,
};

static __init int init_sensor(void)
{
	return i2c_add_driver(&jxh61_driver);
}

static __exit void exit_sensor(void)
{
	i2c_del_driver(&jxh61_driver);
}

module_init(init_sensor);
module_exit(exit_sensor);

MODULE_DESCRIPTION("A low-level driver for OmniVision jxh61 sensors");
MODULE_LICENSE("GPL");
