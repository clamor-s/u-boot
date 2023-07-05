// SPDX-License-Identifier: GPL-2.0+
/*
 * Samsung LTL106HL02-001 DSI panel driver
 *
 * Copyright (c) 2023 Svyatoslav Ryhel <clamor95@gmail.com>
 */

#include <common.h>
#include <backlight.h>
#include <dm.h>
#include <panel.h>
#include <log.h>
#include <misc.h>
#include <mipi_display.h>
#include <mipi_dsi.h>
#include <asm/gpio.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <power/regulator.h>

struct samsung_ltl106hl02_priv {
	struct udevice *vdd;
	struct udevice *backlight;

	struct gpio_desc reset_gpio;
};

static struct display_timing default_timing = {
	.pixelclock.typ		= 137000000,
	.hactive.typ		= 1920,
	.hfront_porch.typ	= 32,
	.hback_porch.typ	= 32,
	.hsync_len.typ		= 64,
	.vactive.typ		= 1080,
	.vfront_porch.typ	= 6,
	.vback_porch.typ	= 3,
	.vsync_len.typ		= 22,
};

static int samsung_ltl106hl02_enable_backlight(struct udevice *dev)
{
	struct samsung_ltl106hl02_priv *priv = dev_get_priv(dev);

	dm_gpio_set_value(&priv->reset_gpio, 1);
	regulator_set_enable_if_allowed(priv->vdd, 1);

	udelay(8500);

	dm_gpio_set_value(&priv->reset_gpio, 0);

	return 0;
}

static int samsung_ltl106hl02_set_backlight(struct udevice *dev, int percent)
{
	struct samsung_ltl106hl02_priv *priv = dev_get_priv(dev);
	struct mipi_dsi_panel_plat *plat = dev_get_plat(dev);
	struct mipi_dsi_device *dsi = plat->device;
	int ret;

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		log_err("failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	mdelay(70);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		log_err("failed to enable display: %d\n", ret);
		return ret;
	}
	mdelay(15);

	ret = backlight_enable(priv->backlight);
	if (ret)
		return ret;

	ret = backlight_set_brightness(priv->backlight, percent);
	if (ret)
		return ret;

	return 0;
}

static int samsung_ltl106hl02_timings(struct udevice *dev,
				  struct display_timing *timing)
{
	memcpy(timing, &default_timing, sizeof(*timing));
	return 0;
}

static int samsung_ltl106hl02_of_to_plat(struct udevice *dev)
{
	struct samsung_ltl106hl02_priv *priv = dev_get_priv(dev);
	int ret;

	ret = uclass_get_device_by_phandle(UCLASS_PANEL_BACKLIGHT, dev,
					   "backlight", &priv->backlight);
	if (ret) {
		log_err("cannot get backlight: ret = %d\n", ret);
		return ret;
	}

	ret = uclass_get_device_by_phandle(UCLASS_REGULATOR, dev,
					   "vdd-supply", &priv->vdd);
	if (ret)
		debug("%s: cannot get vdd-supply: error %d\n",
		      __func__, ret);

	ret = gpio_request_by_name(dev, "reset-gpios", 0,
				   &priv->reset_gpio, GPIOD_IS_OUT);
	if (ret)
		debug("%s: cannot get reset-gpios: error %d\n",
		      __func__, ret);

	return 0;
}

static int samsung_ltl106hl02_probe(struct udevice *dev)
{
	struct mipi_dsi_panel_plat *plat = dev_get_plat(dev);

	/* fill characteristics of DSI data link */
	plat->lanes = 4;
	plat->format = MIPI_DSI_FMT_RGB888;
	plat->mode_flags = MIPI_DSI_MODE_VIDEO;

	return 0;
}

static const struct panel_ops samsung_ltl106hl02_ops = {
	.enable_backlight	= samsung_ltl106hl02_enable_backlight,
	.set_backlight		= samsung_ltl106hl02_set_backlight,
	.get_display_timing	= samsung_ltl106hl02_timings,
};

static const struct udevice_id samsung_ltl106hl02_ids[] = {
	{ .compatible = "samsung,ltl106hl02-001" },
	{ }
};

U_BOOT_DRIVER(samsung_ltl106hl02) = {
	.name		= "samsung_ltl106hl02",
	.id		= UCLASS_PANEL,
	.of_match	= samsung_ltl106hl02_ids,
	.ops		= &samsung_ltl106hl02_ops,
	.of_to_plat	= samsung_ltl106hl02_of_to_plat,
	.probe		= samsung_ltl106hl02_probe,
	.plat_auto	= sizeof(struct mipi_dsi_panel_plat),
	.priv_auto	= sizeof(struct samsung_ltl106hl02_priv),
};
