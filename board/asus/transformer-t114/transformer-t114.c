// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2010-2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * Copyright (c) 2023, Svyatoslav Ryhel <clamor95@gmail.com>
 */

/* T114 Transformers derive from Macallan board */

#include <common.h>
#include <asm/arch/pinmux.h>
#include <asm/arch/gp_padctrl.h>

#include "pinmux-config-transformer.h"

/*
 * Routine: pinmux_init
 * Description: Do individual peripheral pinmux configs
 */
void pinmux_init(void)
{
	pinmux_config_pingrp_table(tegra114_pinmux_common,
		ARRAY_SIZE(tegra114_pinmux_common));

	/* Initialize any non-default pad configs (APB_MISC_GP regs) */
	pinmux_config_drvgrp_table(transformer_t114_padctrl,
		ARRAY_SIZE(transformer_t114_padctrl));
}
