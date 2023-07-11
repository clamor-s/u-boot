// SPDX-License-Identifier: GPL-2.0+
/*
 *  (C) Copyright 2010-2013
 *  NVIDIA Corporation <www.nvidia.com>
 *
 *  (C) Copyright 2023
 *  Svyatoslav Ryhel <clamor95@gmail.com>
 */

#include <common.h>
#include <asm/arch/pinmux.h>
#include <asm/arch/gp_padctrl.h>

#include "pinmux-config-qc750.h"

/*
 * Routine: pinmux_init
 * Description: Do individual peripheral pinmux configs
 */
void pinmux_init(void)
{
	pinmux_config_pingrp_table(qc750_pinmux_common,
		ARRAY_SIZE(qc750_pinmux_common));

	pinmux_config_drvgrp_table(qc750_padctrl,
		ARRAY_SIZE(qc750_padctrl));
}
