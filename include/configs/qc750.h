/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  (C) Copyright 2010,2012
 *  NVIDIA Corporation <www.nvidia.com>
 *
 *  (C) Copyright 2023
 *  Svyatoslav Ryhel <clamor95@gmail.com>
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include <linux/sizes.h>

#include "tegra30-common.h"

/* High-level configuration options */
#define CFG_TEGRA_BOARD_STRING		"Wexler QC750"

#define QC750_FLASH_UBOOT \
	"flash_uboot=echo Preparing RAM;" \
		"mw ${kernel_addr_r} 0 ${boot_block_size_r};" \
		"mw ${ramdisk_addr_r} 0 ${boot_block_size_r};" \
		"echo Reading BCT;" \
		"mmc dev 0 1;" \
		"mmc read ${kernel_addr_r} 0 ${boot_block_size};" \
		"echo Reading bootloader;" \
		"if load mmc 1:1 ${ramdisk_addr_r} ${bootloader_file};" \
		"then echo Calculating bootloader size;" \
			"size mmc 1:1 ${bootloader_file};" \
			"ebtupdate ${kernel_addr_r} ${ramdisk_addr_r} ${filesize};" \
			"echo Writing bootloader to eMMC;" \
			"mmc dev 0 1;" \
			"mmc write ${kernel_addr_r} 0 ${boot_block_size};" \
			"mmc dev 0 2;" \
			"mmc write ${ramdisk_addr_r} 0 ${boot_block_size};" \
			"echo Bootloader written successfully;" \
			"pause 'Press ANY key to reboot device...'; reset;" \
		"else echo Reading bootloader failed;" \
			"pause 'Press ANY key to return to bootmenu...'; bootmenu; fi\0"

#define QC750_BOOTMENU \
	QC750_FLASH_UBOOT \
	"bootmenu_0=mount internal storage=usb start && ums 0 mmc 0; bootmenu\0" \
	"bootmenu_1=mount external storage=usb start && ums 0 mmc 1; bootmenu\0" \
	"bootmenu_2=fastboot=echo Starting Fastboot protocol ...; fastboot usb 0; bootmenu\0" \
	"bootmenu_3=update bootloader=run flash_uboot\0" \
	"bootmenu_4=enter console=usb start; setenv skip_boot 1; exit\0" \
	"bootmenu_5=reboot RCM=enterrcm\0" \
	"bootmenu_6=reboot=reset\0" \
	"bootmenu_7=power off=poweroff\0" \
	"bootmenu_delay=-1\0"

#define BOARD_EXTRA_ENV_SETTINGS \
	"boot_block_size_r=0x200000\0" \
	"boot_block_size=0x1000\0" \
	"bootloader_file=u-boot-dtb-tegra.bin\0" \
	"check_button=gpio input 137; test $? -eq 0\0" \
	"bootloader_file=u-boot-dtb-tegra.bin\0" \
	"partitions=name=emmc,start=0,size=-,uuid=${uuid_gpt_rootfs}\0" \
	QC750_BOOTMENU

/* Board-specific serial config */
#define CFG_SYS_NS16550_COM1		NV_PA_APB_UARTD_BASE

#include "tegra-common-post.h"

#endif /* __CONFIG_H */
