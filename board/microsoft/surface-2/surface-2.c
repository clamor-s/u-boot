// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2010-2013, NVIDIA CORPORATION.  All rights reserved.
 */

#include <common.h>
#include <dm.h>
#include <env.h>
#include <i2c.h>
#include <log.h>
#include <asm/arch/pinmux.h>
#include <asm/arch/gp_padctrl.h>
#include <asm/arch-tegra/fuse.h>
#include "pinmux-config-surface-2.h"
#include <linux/delay.h>

#define TPS65090_I2C_ADDR		0x48

#define TPS65913_I2C_ADDR		0x58

/* page 2 */
#define TPS65913_DEV_CTRL		0xA0
#define TPS65913_INT3_MASK		0x1B
#define TPS65913_INT3_MASK_VBUS		BIT(7)

#ifdef CONFIG_CMD_POWEROFF
int do_poweroff(struct cmd_tbl *cmdtp, int flag,
		int argc, char *const argv[])
{
	struct udevice *dev;
	int ret;

	/* Mask INT3 on second page first */
	ret = i2c_get_chip_for_busnum(4, TPS65913_I2C_ADDR + 1, 1, &dev);
	if (ret) {
		log_debug("cannot find PMIC I2C chip\n");
		return 1;
	}

	ret = dm_i2c_reg_read(dev, TPS65913_INT3_MASK);
	if (ret < 0)
		return ret;

	ret = dm_i2c_reg_write(dev, TPS65913_INT3_MASK,
			       ret | TPS65913_INT3_MASK_VBUS);
	if (ret)
		return ret;

	ret = i2c_get_chip_for_busnum(4, TPS65913_I2C_ADDR, 1, &dev);
	if (ret) {
		log_debug("cannot find PMIC I2C chip\n");
		return 1;
	}

	/* TPS65913: DEV_CTRL > OFF */
	ret = dm_i2c_reg_write(dev, TPS65913_DEV_CTRL, 0);
	if (ret)
		log_debug("PMU i2c_write DEV_CTRL < OFF returned %d\n", ret);

	// wait some time and then print error
	mdelay(5000);
	printf("Failed to power off!!!\n");
	return 1;
}
#endif

/*
 * Routine: pinmux_init
 * Description: Do individual peripheral pinmux configs
 */
void pinmux_init(void)
{
	pinmux_config_pingrp_table(tegra114_surface2_pinmux,
		ARRAY_SIZE(tegra114_surface2_pinmux));

	/* Initialize any non-default pad configs (APB_MISC_GP regs) */
	pinmux_config_drvgrp_table(tegra114_surface2_padctrl,
		ARRAY_SIZE(tegra114_surface2_padctrl));
}

#if defined(CONFIG_MMC_SDHCI_TEGRA)
/*
 * This recreates UEFI set up.
 *
 */
static void tps65090_init(void)
{
	struct udevice *dev;
	int ret;

	ret = i2c_get_chip_for_busnum(4, TPS65090_I2C_ADDR, 1, &dev);
	if (ret) {
		log_debug("cannot find charger I2C chip\n");
		return;
	}

	// TPS65090: FET1_CTRL = enable output auto discharge, enable FET6
	// Needed by panel backlight
	ret = dm_i2c_reg_write(dev, 0x0f, 0x0f);
	if (ret)
		log_debug("BAT i2c_write 0x0f < 0x0f returned %d\n", ret);

	// TPS65090: FET2_CTRL = enable output auto discharge, enable FET6
	ret = dm_i2c_reg_write(dev, 0x10, 0x0f);
	if (ret)
		log_debug("BAT i2c_write 0x10 < 0x0f returned %d\n", ret);

	// TPS65090: FET4_CTRL = enable output auto discharge, enable FET4
	// Needed by panel backlight
	ret = dm_i2c_reg_write(dev, 0x12, 0x03);
	if (ret)
		log_debug("BAT i2c_write 0x12 < 0x03 returned %d\n", ret);

	// TPS65090: FET5_CTRL = enable output auto discharge, enable FET6
	ret = dm_i2c_reg_write(dev, 0x13, 0x03);
	if (ret)
		log_debug("BAT i2c_write 0x13 < 0x03 returned %d\n", ret);
}

static void tps65913_init(void)
{
	struct udevice *dev;
	uchar reg, data_buffer[1];
	int ret;

	ret = i2c_get_chip_for_busnum(4, TPS65913_I2C_ADDR, 1, &dev);
	if (ret) {
		log_debug("cannot find PMIC I2C chip\n");
		return;
	}

	// TPS65913: SMPS12_TSTEP
	data_buffer[0] = 0x03;
	ret = dm_i2c_write(dev, 0x21, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: SMPS12_CTRL
	data_buffer[0] = 0xD1;
	ret = dm_i2c_write(dev, 0x20, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: SMPS6_CTRL
	data_buffer[0] = 0xC0;
	ret = dm_i2c_write(dev, 0x2C, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);


	// TPS65913: LDO1_VOLTAGE = 3.3V
	data_buffer[0] = 0x07;
	reg = 0x51;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: LDO1_CTRL = Active
	data_buffer[0] = 0x11;
	reg = 0x50;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: LDO2_VOLTAGE = 3.3V
	data_buffer[0] = 0x27;
	reg = 0x53;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: LDO2_CTRL = Active
	data_buffer[0] = 0x11;
	reg = 0x52;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: LDO3_VOLTAGE = 3.3V
	data_buffer[0] = 0x07;
	reg = 0x55;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: LDO3_CTRL = Active
	data_buffer[0] = 0x11;
	reg = 0x54;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: LDO5_VOLTAGE = 3.3V
	data_buffer[0] = 0x13;
	reg = 0x59;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: LDO5_CTRL = Active
	data_buffer[0] = 0x11;
	reg = 0x58;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: LDO7_VOLTAGE = 3.3V
	data_buffer[0] = 0x13;
	reg = 0x5D;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: LDO7_CTRL = Active
	data_buffer[0] = 0x11;
	reg = 0x5C;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: LDO8_VOLTAGE = 3.3V
	data_buffer[0] = 0x07;
	reg = 0x5F;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: LDO8_CTRL = Active
	data_buffer[0] = 0x11;
	reg = 0x5E;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: LDO9_VOLTAGE = 3.3V
	data_buffer[0] = 0x31;
	reg = 0x61;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: LDO9_CTRL = Active
	data_buffer[0] = 0x01;
	reg = 0x60;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: LDOLN_VOLTAGE = 3.3V
	data_buffer[0] = 0x13;
	reg = 0x63;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);

	// TPS65913: LDOLN_CTRL = Active
	data_buffer[0] = 0x11;
	reg = 0x62;
	ret = dm_i2c_write(dev, reg, data_buffer, 1);
	if (ret)
		printf("%s: PMU i2c_write %02X<-%02X returned %d\n", __func__, reg, data_buffer[0], ret);
}

/*
 * Routine: pin_mux_mmc
 * Description: setup the MMC muxes, power rails, etc.
 */
void pin_mux_mmc(void)
{
	/* Bring up the power rails */
	tps65913_init();
	tps65090_init();
}
#endif /* MMC */

void nvidia_board_late_init(void)
{
	char serialno_str[17];

	/* Set chip id as serialno */
	sprintf(serialno_str, "%016llx", tegra_chip_uid());
	env_set("serial#", serialno_str);
	env_set("platform", "Tegra 4 T114");
}
