/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022, Svyatoslav Ryhel <clamor95@gmail.com>.
 */

#ifndef __TRANSFORMER_COMMON_H
#define __TRANSFORMER_COMMON_H

/* High-level configuration options */
#define CFG_TEGRA_BOARD_STRING		"ASUS Transformer"

/*
 * SOS and LNX offset is relative to
 * mmcblk0 start on both t20 and t30
 */

#define TRANSFORMER_T20_EMMC_LAYOUT \
	"sos_offset_m=0x1C00\0" \
	"sos_size=0x2800\0" \
	"lnx_offset_m=0x4400\0" \
	"lnx_size=0x4000\0"

#define TRANSFORMER_T30_EMMC_LAYOUT \
	"sos_offset_m=0x3C00\0" \
	"sos_size=0x4000\0" \
	"lnx_offset_m=0x7C00\0" \
	"lnx_size=0x4000\0"

#define TRANSFORMER_BOOT_SOS \
	"boot_sos=echo Reading SOS partition;" \
		"mmc dev;" \
		"if mmc read ${kernel_addr_r} ${sos_offset_m} ${sos_size};" \
		"then echo Booting Kernel;" \
			"bootm ${kernel_addr_r};" \
		"else echo Reading SOS failed;" \
		"pause 'Press ANY key to return to bootmenu';" \
		"bootmenu; fi\0"

#define TRANSFORMER_BOOT_LNX \
	"boot_lnx=echo Reading LNX partition;" \
		"mmc dev;" \
		"if mmc read ${kernel_addr_r} ${lnx_offset_m} ${lnx_size};" \
		"then echo Booting Kernel;" \
			"bootm ${kernel_addr_r};" \
		"else echo Reading LNX failed;" \
		"pause 'Press ANY key to return to bootmenu';" \
		"bootmenu; fi\0"

#define TRANSFORMER_BRICKSAFE_HOOK \
	"bricksafe_hook=echo Loading bricksafe.img;" \
		"if load mmc 1:1 0x81000000 bricksafe.img;" \
		"then echo Restoring bricksafe.img;" \
			"mmc dev 0 1;" \
			"mmc write 0x81000000 0 0x1000;" \
			"mmc dev 0 2;" \
			"mmc write 0x81200000 0 0x1000;" \
			"mmc dev;" \
			"mmc write 0x81400000 0 0x3C00;" \
			"echo Restoration of bricksafe.img completed;" \
			"echo Rebooting...;" \
			"sleep 3;" \
			"reset;" \
		"else echo Reading bricksafe.img;" \
			"mmc dev 0 1;" \
			"mmc read 0x81000000 0 0x1000;" \
			"mmc dev 0 2;" \
			"mmc read 0x81200000 0 0x1000;" \
			"mmc dev;" \
			"mmc read 0x81400000 0 0x3C00;" \
			"if fatwrite mmc 1:1 0x81000000 bricksafe.img 0xB80000;" \
			"then echo bricksafe.img dumped successfully;" \
				"pause 'Press ANY key to turn off device'; poweroff;" \
			"else bricksafe.img dump FAILED! ABORTING...;" \
				"pause 'Press ANY key to return to bootmenu'; bootmenu; fi; fi\0"

#define TRANSFORMER_FASTBOOT_ALIAS \
	"fastboot_raw_partition_boot=${lnx_offset_m} ${lnx_size} mmcpart 0\0" \
	"fastboot_raw_partition_recovery=${sos_offset_m} ${sos_size} mmcpart 0\0" \
	"fastboot_partition_alias_system=APP\0" \
	"fastboot_partition_alias_cache=CAC\0" \
	"fastboot_partition_alias_misc=MSC\0" \
	"fastboot_partition_alias_staging=USP\0" \
	"fastboot_partition_alias_vendor=VDR\0" \
	"fastboot_partition_alias_userdata=UDA\0"

#define TRANSFORMER_BOOTMENU \
	TRANSFORMER_T30_EMMC_LAYOUT \
	TRANSFORMER_BOOT_SOS \
	TRANSFORMER_BOOT_LNX \
	TRANSFORMER_BRICKSAFE_HOOK \
	TRANSFORMER_FASTBOOT_ALIAS \
	"bootmenu_0=boot LNX=run boot_lnx\0" \
	"bootmenu_1=boot SOS=run boot_sos\0" \
	"bootmenu_2=mount external storage=usb start && ums 0 mmc 1; bootmenu\0" \
	"bootmenu_3=fastboot=echo Starting Fastboot protocol ...; fastboot usb 0; bootmenu\0" \
	"bootmenu_4=bricksafe=run bricksafe_hook\0" \
	"bootmenu_5=enter console=usb start; setenv skip_boot 1; exit\0" \
	"bootmenu_6=reboot RCM=enterrcm\0" \
	"bootmenu_7=reboot=reset\0" \
	"bootmenu_8=power off=poweroff\0" \
	"bootmenu_delay=-1\0"

#define BOARD_EXTRA_ENV_SETTINGS \
	"bootloader_file=u-boot-dtb-tegra.bin\0" \
	TRANSFORMER_BOOTMENU

#endif /* __CONFIG_H */
