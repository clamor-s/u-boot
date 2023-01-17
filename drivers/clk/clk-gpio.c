// SPDX-License-Identifier: GPL-2.0+
/*
 * GPIO gate clock support
 *
 * Copyright (C) 2023 Svyatoslav Ryhel
 */

#include <common.h>
#include <clk.h>
#include <dm.h>
#include <clk-uclass.h>
#include <linux/clk-provider.h>

#include <asm/gpio.h>

struct clk_gpio_priv {
	struct gpio_desc enable_gpio;
	struct clk *clk;
};

static int clk_gpio_enable(struct clk *clk)
{
	struct clk_gpio_priv *priv = dev_get_priv(clk->dev);
	int ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		printf("%s: error enabling clk (%d)\n", __func__, ret);

	ret = dm_gpio_set_value(&priv->enable_gpio, 1);
	if (ret)
		printf("%s: error changing enable-gpio (%d)\n", __func__, ret);

	return 0;
}

static int clk_gpio_disable(struct clk *clk)
{
	struct clk_gpio_priv *priv = dev_get_priv(clk->dev);
	int ret;

	ret = dm_gpio_set_value(&priv->enable_gpio, 0);
	if (ret)
		printf("%s: error changing enable-gpio (%d)\n", __func__, ret);

	ret = clk_disable_unprepare(priv->clk);
	if (ret)
		printf("%s: error disabling clk (%d)\n", __func__, ret);

	return 0;
}

static ulong clk_gpio_get_rate(struct clk *clk)
{
	struct clk_gpio_priv *priv = dev_get_priv(clk->dev);
	int ret;

	ret = dm_gpio_get_value(&priv->enable_gpio);
	if (ret)
		return clk_get_rate(priv->clk);
	else
		return -EINVAL;
}

static int clk_gpio_of_to_plat(struct udevice *dev)
{
	struct clk_gpio_priv *priv = dev_get_priv(dev);
	int ret;

	priv->clk = devm_clk_get(dev, "gate-clk");
	if (IS_ERR(priv->clk)) {
		printf("%s: Could not get gate clk: %ld\n",
			__func__, PTR_ERR(priv->clk));
		return PTR_ERR(priv->clk);
	}

	ret = gpio_request_by_name(dev, "enable-gpios", 0,
				   &priv->enable_gpio, GPIOD_IS_OUT);
	if (ret) {
		printf("%s: Could not decode enable-gpios (%d)\n", __func__, ret);
		return ret;
	}

	return 0;
}

static struct clk_ops clk_gpio_ops = {
	.enable = clk_gpio_enable,
	.disable = clk_gpio_disable,
	.get_rate = clk_gpio_get_rate,
};

static const struct udevice_id clk_gpio_of_match[] = {
	{ .compatible = "gpio-gate-clock" },
	{ },
};

U_BOOT_DRIVER(clk_gpio) = {
	.name = "clk_gpio",
	.id = UCLASS_CLK,
	.of_match = clk_gpio_of_match,
	.of_to_plat = clk_gpio_of_to_plat,
	.priv_auto = sizeof(struct clk_gpio_priv),
	.ops = &clk_gpio_ops,
};
