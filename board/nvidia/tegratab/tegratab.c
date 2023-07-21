// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2010-2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * Copyright (c) 2023, Svyatoslav Ryhel <clamor95@gmail.com>
 */

#include <common.h>
#include <env.h>
#include <fdt_support.h>
#include <asm/gpio.h>
#include <asm/arch/pinmux.h>
#include <asm/arch/gp_padctrl.h>

#include "pinmux-config-tegratab.h"

/*
 * Routine: pinmux_init
 * Description: Do individual peripheral pinmux configs
 */
void pinmux_init(void)
{
	pinmux_config_pingrp_table(tegra114_tegratab_pinmux,
		ARRAY_SIZE(tegra114_tegratab_pinmux));

	/* Initialize any non-default pad configs (APB_MISC_GP regs) */
	pinmux_config_drvgrp_table(tegratab_padctrl,
		ARRAY_SIZE(tegratab_padctrl));
}

#if defined(CONFIG_MMC_SDHCI_TEGRA)
/*
 * Routine: pin_mux_mmc
 * Description: setup the MMC muxes, power rails, etc.
 */
void pin_mux_mmc(void)
{
	gpio_request(260, "PALMAS GPIO4");
	gpio_direction_output(260, 1);
}
#endif /* MMC */

#if defined(CONFIG_OF_LIBFDT) && defined(CONFIG_OF_BOARD_SETUP)
int ft_board_setup(void *blob, struct bd_info *bd)
{
	/* Remove TN7 bootloader restrictions */
	fdt_del_node_and_alias(blob, "/chosen");

	return 0;
}
#endif
