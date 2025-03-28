/*
 * Platform device support for Tomahawk series SoC.
 *
 * Copyright 2017, <xianghui.shen@ingenic.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/i2c-gpio.h>
#include <soc/gpio.h>
#include <soc/base.h>
#include <dt-bindings/interrupt-controller/t40-irq.h>
#include <dt-bindings/dma/ingenic-pdma.h>

static void audio_release_codec_device(struct device *dev)
{
	return;
}

static void audio_release_aic_device(struct device *dev)
{
	return;
}

static void audio_release_device(struct device *dev)
{
	return;
}

/* inner codec device */
static struct resource jz_codec_resources[] = {
	[0] = {
		.start  = CODEC_IOBASE,
		.end    = CODEC_IOBASE + 0x130,
		.flags  = IORESOURCE_MEM,
	},
};

struct platform_device jz_codec_device = {
	.name   = "jz-inner-codec",
	.id = -1,
	.dev = {
		.release = audio_release_codec_device,
	},
	.resource   = jz_codec_resources,
	.num_resources  = ARRAY_SIZE(jz_codec_resources),
};


/* aic device */
static u64 jz_aic_dmamask = ~(u32) 0;
static struct resource jz_aic_resources[] = {
	[0] = {
	       .start = AIC0_IOBASE,
	       .end = AIC0_IOBASE + 0x70 - 1,
	       .flags = IORESOURCE_MEM,
	       },
	[1] = {
	       .start = IRQ_AIC0+8,
	       .end = IRQ_AIC0+8,
	       .flags = IORESOURCE_IRQ,
	},
};

struct platform_device jz_aic_device = {
	.name = "jz-aic",
	.id = -1,
	.dev = {
		.dma_mask = &jz_aic_dmamask,
		.coherent_dma_mask = 0xffffffff,
		.platform_data = (void *)&jz_codec_device,
		.release = audio_release_aic_device,
	},
	.resource = jz_aic_resources,
	.num_resources = ARRAY_SIZE(jz_aic_resources),
};

struct platform_device *audio_devices[] = {
	&jz_aic_device,
	NULL,
};

struct platform_device audio_dsp_platform_device = {
	.name             = "jz-dsp",
	.id               = -1,
	.dev = {
		.dma_mask = &jz_aic_dmamask,
		.coherent_dma_mask = 0xffffffff,
		.platform_data = (void *)audio_devices,
		.release = audio_release_device,
	},
	.num_resources = 0,
};
