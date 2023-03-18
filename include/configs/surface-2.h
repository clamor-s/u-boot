/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2010-2013, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include <linux/sizes.h>

#include "tegra114-common.h"

/* High-level configuration options */
#define CFG_TEGRA_BOARD_STRING	"Microsoft Surface 2"

/* Stolen from 'test/dm/video.c' */
#define ANSI_ESC "\x1b"

/* Stolen from 'drivers/video/vidconsole-uclass.c */
/*
 * Foreground Colors:
 *
 *   30	Black
 *   31	Red
 *   32	Green
 *   33	Yellow
 *   34	Blue
 *   35	Magenta
 *   36	Cyan
 *   37	White
 *
 * Background Colors:
 *
 *   40	Black
 *   41	Red
 *   42	Green
 *   43	Yellow
 *   44	Blue
 *   45	Magenta
 *   46	Cyan
 *   47	White
 */

#define COLOR_WHITE_BLACK ANSI_ESC"[37';'40m"
#define COLOR_BLACK_WHITE ANSI_ESC"[30';'47m"
#define COLOR_BLACK_RED ANSI_ESC"[30';'41m"
#define COLOR_BLACK_YELLOW ANSI_ESC"[30';'43m"
#define COLOR_BLACK_GREEN ANSI_ESC"[30';'42m"

#define SURFACE_RT_BOOT_SCRIPT_SD \
       "boot_scr_sd=echo 'Run bootscript from SD ...';" \
               "load mmc 1 ${loadaddr} /boot.scr;" \
               "source\0"

#define SURFACE_RT_BOOT_SCRIPT_MMC \
       "boot_scr_mmc=echo 'Run bootscript from MMC ...';" \
               "load mmc 0 ${loadaddr} /boot.scr;" \
               "source\0"

#define SURFACE_RT_BOOT_DOWNSTREAM_SD \
       "boot_downstream_sd=echo 'Boot downstream from SD ...';" \
               "env set bootargs 'console=ttyS0,115200n8 debug_uartport=lsport,0';" \
               "load mmc 1:1 ${kernel_addr_r} /uImage;" \
               "bootm ${kernel_addr_r}\0"

#define SURFACE_RT_ENV_FAIL \
	"echo Press any key to continue;" \
	"continue;" \

#define SURFACE_RT_ENV_COMMON \
	"loaded_dtb=0;" \
	"loaded_initramfs=0;" \
	"if test -n ${dtb_file}; then " \
		"if test -e ${dev_type} ${load_dev}:${load_part} ${dtb_file}; then " \
			"if load ${dev_type} ${load_dev}:${load_part} ${fdt_addr_r} ${dtb_file}; then " \
				"echo "COLOR_BLACK_GREEN"Loaded DTB"COLOR_WHITE_BLACK";" \
				"loaded_dtb=1;" \
			"fi;" \
		"else " \
			"echo "COLOR_BLACK_RED"<error>"COLOR_WHITE_BLACK" DTB not found;" \
			"echo "COLOR_BLACK_WHITE"Please check your dtb_file= var"COLOR_WHITE_BLACK";" \
			SURFACE_RT_ENV_FAIL \
		"fi;" \
	"else " \
		"echo "COLOR_BLACK_YELLOW"<warn>"COLOR_WHITE_BLACK" DTB not specified;" \
	"fi;" \
	"if test -n ${ramdisk_file}; then " \
		"if test -e ${dev_type} ${load_dev}:${load_part} ${ramdisk_file}; then " \
			"if load ${dev_type} ${load_dev}:${load_part} ${ramdisk_addr_r} ${ramdisk_file}; then " \
				"echo Loaded Initramfs"COLOR_WHITE_BLACK";" \
				"loaded_initramfs=1;" \
			"fi;" \
		"else " \
			"echo "COLOR_BLACK_RED"<error>"COLOR_WHITE_BLACK" Initramfs not found;" \
			"echo "COLOR_BLACK_WHITE"Please check your ramdisk_file= var"COLOR_WHITE_BLACK";" \
			SURFACE_RT_ENV_FAIL \
		"fi;" \
	"else " \
		"echo "COLOR_BLACK_YELLOW"<warn>"COLOR_WHITE_BLACK" Initramfs not specified;" \
	"fi;" \
	"if test -n ${kernel_file}; then " \
		"if test -e ${dev_type} ${load_dev}:${load_part} ${kernel_file}; then " \
			"if load ${dev_type} ${load_dev}:${load_part} ${kernel_addr_r} ${kernel_file}; then " \
				"echo "COLOR_BLACK_GREEN"Loaded Kernel"COLOR_WHITE_BLACK";" \
				"if test $loaded_dtb -eq 1 && test $loaded_initramfs -eq 1; then " \
					"echo "COLOR_BLACK_GREEN"kernel / dt / init"COLOR_WHITE_BLACK";" \
					"bootz ${kernel_addr_r} ${ramdisk_addr_r} ${fdt_addr_r};" \
				"elif test $loaded_dtb -eq 1; then " \
					"echo "COLOR_BLACK_GREEN"kernel / dt"COLOR_WHITE_BLACK";" \
					"bootz ${kernel_addr_r} - ${fdt_addr_r};" \
				"elif test $loaded_initramfs -eq 1; then " \
					"echo "COLOR_BLACK_GREEN"kernel / init"COLOR_WHITE_BLACK";" \
					"bootz ${kernel_addr_r} ${ramdisk_addr_r};" \
				"else " \
					"echo "COLOR_BLACK_GREEN"kernel"COLOR_WHITE_BLACK";" \
					"bootz ${kernel_addr_r};" \
				"fi;" \
			"fi;" \
		"else " \
			"echo "COLOR_BLACK_RED"<error>"COLOR_WHITE_BLACK" Kernel not found;" \
			"echo "COLOR_BLACK_WHITE"Please check your kernel_file= var"COLOR_WHITE_BLACK";" \
			SURFACE_RT_ENV_FAIL \
		"fi;" \
	"else " \
		"echo "COLOR_BLACK_RED"<error>"COLOR_WHITE_BLACK" Kernel not specified;" \
		"echo "COLOR_BLACK_WHITE"Please check your kernel_file= var"COLOR_WHITE_BLACK";" \
		SURFACE_RT_ENV_FAIL \
	"fi;" \

#define SURFACE_RT_ENV_MMC \
	"boot_env_mmc=echo Loading boot env from MMC;" \
		"if load mmc 0:1 ${loadaddr} uboot.env; then " \
			"env import -t -r ${loadaddr} ${filesize};" \
			SURFACE_RT_ENV_COMMON \
		"else " \
			"echo "COLOR_BLACK_WHITE"Boot env NOT FOUND on eMMC!"COLOR_WHITE_BLACK";" \
			SURFACE_RT_ENV_FAIL \
		"fi;" \
		"\0"

#define SURFACE_RT_ENV_SD \
	"boot_env_sd=echo Loading boot env from SD;" \
		"if load mmc 1:1 ${loadaddr} uboot.env; then " \
			"env import -t -r ${loadaddr} ${filesize};" \
			SURFACE_RT_ENV_COMMON \
		"else " \
			"echo "COLOR_BLACK_WHITE"Boot env NOT FOUND on SD!"COLOR_WHITE_BLACK";" \
			SURFACE_RT_ENV_FAIL \
		"fi;" \
		"\0"

#define SURFACE_RT_ENV_USB \
	"boot_env_usb=echo Loading boot env from USB;" \
		"usb start;" \
		"if load usb 0:1 ${loadaddr} uboot.env; then " \
			"env import -t -r ${loadaddr} ${filesize};" \
			SURFACE_RT_ENV_COMMON \
		"else " \
			"echo "COLOR_BLACK_WHITE"Boot env NOT FOUND on USB!"COLOR_WHITE_BLACK";" \
			SURFACE_RT_ENV_FAIL \
		"fi;" \
		"\0"

#define BOARD_EXTRA_ENV_SETTINGS \
	"bootkernel=bootz ${kernel_addr_r}\0" \
	"bootrdkernel=bootz ${kernel_addr_r} ${ramdisk_addr_r}\0" \
	"bootdtkernel=bootz ${kernel_addr_r} - ${fdt_addr_r}\0" \
	"bootdtrdkernel=bootz ${kernel_addr_r} ${ramdisk_addr_r} ${fdt_addr_r}\0" \
	SURFACE_RT_BOOT_SCRIPT_SD \
	SURFACE_RT_BOOT_SCRIPT_MMC \
	SURFACE_RT_BOOT_DOWNSTREAM_SD \
	SURFACE_RT_ENV_MMC \
	SURFACE_RT_ENV_SD \
	SURFACE_RT_ENV_USB \
	"bootmenu_0=Env USB=run boot_env_usb; sleep 10; bootmenu;\0" \
	"bootmenu_1=Env MMC=run boot_env_mmc; sleep 10; bootmenu;\0" \
	"bootmenu_2=Env SD=run boot_env_sd; sleep 10; bootmenu;\0" \
	"bootmenu_3=USB Mass Storage - eMMC=ums 0 mmc 0; bootmenu;\0" \
	"bootmenu_4=USB Mass Storage - SD Card=ums 0 mmc 1; bootmenu;\0" \
	"bootmenu_5=Fastboot=fastboot usb 0\0" \
	"bootmenu_6=RCM Mode (does not work?)=enterrcm\0" \
	"bootmenu_delay=3\0"

/* Board-specific serial config */
#define CFG_SYS_NS16550_COM1		NV_PA_APB_UARTA_BASE

#include "tegra-common-post.h"

#endif /* __CONFIG_H */
