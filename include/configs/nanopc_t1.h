/*
 * Copyright (C) 2011 Samsung Electronics
 *
 * Configuration settings for the SAMSUNG NANOPC_T1 (EXYNOS4412) board.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __CONFIG_NANOPC_T1_H
#define __CONFIG_NANOPC_T1_H

#include <configs/exynos4-common.h>

#define CONFIG_SUPPORT_EMMC_BOOT 1

/* High Level Configuration Options */
#define CONFIG_EXYNOS4210		1	/* which is a EXYNOS4210 SoC */
#define CONFIG_NANOPC_T1		1	/* working with NANOPC_T1 */

/* 启动方式，设置到 INF_REG3 寄存器
 * 在内核启动时可以读取该寄存器获取启动方式 */
#define BOOT_MMCSD              0x3
#define BOOT_EMMC               0x6 
#define BOOT_EMMC_4_4           0x7 

/* legacy image */
#define CONFIG_IMAGE_FORMAT_LEGACY /* enable also legacy image format */

#define CONFIG_SETUP_MEMORY_TAGS
#define CONFIG_CMDLINE_TAG
#define CONFIG_INITRD_TAG

#define CONFIG_SYS_DCACHE_OFF		1

/* NANOPC_T1 has 4 bank of DRAM */
#define CONFIG_NR_DRAM_BANKS		4
#define CONFIG_SYS_SDRAM_BASE		0x40000000
#define PHYS_SDRAM_1			CONFIG_SYS_SDRAM_BASE
#define SDRAM_BANK_SIZE			(256 << 20)	/* 256 MB */

/* memtest works on */
#define CONFIG_SYS_MEMTEST_START	CONFIG_SYS_SDRAM_BASE
#define CONFIG_SYS_MEMTEST_END		(CONFIG_SYS_SDRAM_BASE + 0x6000000)
#define CONFIG_SYS_LOAD_ADDR		(CONFIG_SYS_SDRAM_BASE + 0x00100000)

#define CONFIG_SYS_TEXT_BASE		0x43E00000

#define CONFIG_MACH_TYPE		MACH_TYPE_NANOPC_T1

/* select serial console configuration */
#define CONFIG_SERIAL0

/* Console configuration */
#define CONFIG_DEFAULT_CONSOLE		"console=ttySAC0,115200n8 \0"

#define CONFIG_SYS_MEM_TOP_HIDE	(1 << 20)	/* ram console */

#define CONFIG_SYS_MONITOR_BASE	0x00000000

/* Power Down Modes */
#define S5P_CHECK_SLEEP			0x00000BAD
#define S5P_CHECK_DIDLE			0xBAD00000
#define S5P_CHECK_LPA			0xABAD0000

#define CONFIG_SUPPORT_RAW_INITRD

/* MMC SPL */
#define COPY_BL2_FNPTR_ADDR	0x02020030
#define CONFIG_SPL_TEXT_BASE	0x02023400 /* 0x02021410 */

#define CONFIG_SYS_BOOTM_LEN    (30 << 20)      /* Increase max gunzip size */

/* 
 * machid 设置为 0x1200(4608) 与老版本uboot兼容
 *
 * bootm_read: 从mmc中，按照固定扇区方式读取镜像，并使用bootm启动
 * bootm_load: 从 ${mmcdev}:${mmcpart} 分区中读取镜像文件，并使用bootm启动
 * bootz_read: 从mmc中，按照固定扇区方式读取镜像，并使用bootz启动
 *
 * bootm不支持 zImage 方式启动
 * bootz支持 zImage 方式启动 - 为了支持 Linux-3.5 版本启动
 * */
#define CONFIG_EXTRA_ENV_SETTINGS \
    "loadaddr=0x40008000 \0" \
    "kernel_name=uImage \0" \
    "kernel_sector_start=0x800 \0" \
    "kernel_sector_num=0xF000 \0" \
    "dtb_addr=0x41000000 \0" \
    "dtb_name=exynos4412-nanopc_t1.dtb \0" \
    "dtb1_sector_start=0xF800 \0" \
    "dtb2_sector_start=0xFA00 \0" \
    "dtb3_sector_start=0xFC00 \0" \
    "dtb4_sector_start=0xFE00 \0" \
    "dtb_sector_start=0xF800 \0" \
    "dtb_sector_num=0x200 \0" \
    "ramdiskaddr=0x48000000 \0" \
    "ramdisk_name=rootfs.cpio.uboot \0" \
    "ramdisk_sector_start=0x10000 \0" \
    "ramdisk_sector_num=0x10000 \0" \
    CONFIG_DEFAULT_CONSOLE \
    "bootargs=root=/dev/mmcblk0p2 rw rootfstype=ext4 rootwait init=/linuxrc earlyprintk " \
    CONFIG_DEFAULT_CONSOLE " \0" \
    "mmcdev=0 \0" \
    "mmcpart=2 \0" \
    "bootm_read=" \
    "mmc read ${loadaddr} ${kernel_sector_start} ${kernel_sector_num}; " \
    "mmc read ${dtb_addr} ${dtb_sector_start} ${dtb_sector_num}; " \
    "bootm ${loadaddr} - ${dtb_addr} \0" \
    "bootm_ramdisk_read=" \
    "mmc read ${loadaddr} ${kernel_sector_start} ${kernel_sector_num}; " \
    "mmc read ${dtb_addr} ${dtb_sector_start} ${dtb_sector_num}; " \
    "mmc read ${ramdiskaddr} ${ramdisk_sector_start} ${ramdisk_sector_num}; " \
    "bootm ${loadaddr} ${ramdiskaddr} ${dtb_addr} \0" \
    "bootm_load=" \
    "load mmc ${mmcdev}:${mmcpart} ${loadaddr} ${kernel_name}; " \
    "load mmc ${mmcdev}:${mmcpart} ${dtb_addr} ${dtb_name}; " \
    "bootm ${loadaddr} - ${dtb_addr} \0" \
    "bootm_ramdisk_load=" \
    "load mmc ${mmcdev}:${mmcpart} ${loadaddr} ${kernel_name}; " \
    "load mmc ${mmcdev}:${mmcpart} ${dtb_addr} ${dtb_name}; " \
    "load mmc ${mmcdev}:${mmcpart} ${ramdiskaddr} ${ramdisk_name}; " \
    "bootm ${loadaddr} ${ramdiskaddr} ${dtb_addr} \0" \
    "bootz_read=" \
    "mmc read ${loadaddr} ${kernel_sector_start} ${kernel_sector_num}; " \
    "bootz ${loadaddr} \0" \
    "bootz_ramdisk_read=" \
    "mmc read ${loadaddr} ${kernel_sector_start} ${kernel_sector_num}; " \
    "mmc read ${ramdiskaddr} ${ramdisk_sector_start} ${ramdisk_sector_num}; " \
    "bootz ${loadaddr} ${ramdiskaddr} \0"

#define CONFIG_BOOTCOMMAND \
    "run bootm_read"


#define CONFIG_CLK_1000_400_200

/* MIU (Memory Interleaving Unit) */
#define CONFIG_MIU_2BIT_21_7_INTERLEAVED

/* MMC 空间划分方式
 * 本配置是针对 NANOPC_T1 开发板的板载 MMC芯片
 * 因为在 drivers/usb/gadget/f_fastboot.c 的 cb_flash 函数中，
 * 已经固定了分区写入地址，所以这里的 fastboot 命令并不适用于 SD卡
 * 注意，老版本的 uboot_tiny4412 中， bootloader 只写入 328KB
 * 而本版本的 uboot 超过了 400KB，所以，也不能使用老版本的 uboot 升级
 * 如果需要更新本版本的uboot，有两种情况：
 * 1、单板可以正常启动本版本uboot，那直接使用fastboot升级即可；
 * 2、单板不能启动本版本uboot，那就SD卡启动到一个系统，使用 dd 命令将
 *    新版本的uboot 写入到 mmc中
 * */
/* fwbl1 bl2 bootloader env 占用 emmc 最前面的 1MB区域
 * 其中 fwbl1 bl2 bootloader 是在 emmc 的 boot0 分区中
 * kernel dtb 占用 1MB - 32MB区域 kernel 30MB, dtb 1MB
 * 其中 dtb 分四个空间, 每个空间 256KB 默认从第一个启动
 * ramdisk 占用 32MB - 64MB 区域
 * system及其它的数据分区 占用 64MB 以后区域
 * */
/* 分区名       起始扇区(512)       扇区数量        分区大小
 * fwbl1:       0                   16              8KB
 * bl2:         16                  32              16KB
 * bootloader:  48                  1024            512KB
 * env:         1072                16              8KB
 *
 * kernel:      2048                61440           30MB
 * dtb:         63488               2048            1MB
 *
 * ramdisk:     65536               65536           32MB
 *
 * 数据分区(默认在 fdisk.c 中设置)
 * 分区名       分区大小
 * system:      512MB
 * userdata:    512MB
 * cache:       512MB
 * fat:         ~~~~
 * */

/* mmc 分区开始位置 */
/* 这是在 fdisk.c 中使用 fdisk -c 命令进行emmc分区时使用的
 * 这里需要特别注意，emmc小于8GB，使用的 CHS模式
 * 所以分区起始扇区并不会与这里的 CFG_PARTITION_START 相同
 * 而是一个 CHS 对齐的值计算的 逻辑扇区地址
 * 这里计算的逻辑扇区地址为：0x20CC7
 * 需要与 f_fastboot.c 中的 system 分区的值相同(start_8G) */
#define CFG_PARTITION_START         (0x4000000)

#define CONFIG_SYS_MMC_ENV_DEV		0
#define CONFIG_ENV_SIZE				(8 << 10) /* 8 KB */
#define RESERVE_BLOCK_SIZE			(512)
#define BL1_SIZE					(8 << 10) /* 8 K reserved for BL1 */
#define BL2_SIZE					(16 << 10) /* 16 K reserved for BL2 */
#define CONFIG_ENV_OFFSET			(RESERVE_BLOCK_SIZE + BL1_SIZE + BL2_SIZE + COPY_BL2_SIZE)

#define CONFIG_SPL_MAX_FOOTPRINT	(14 * 1024)

#define CONFIG_SPL_STACK			0x02040000
#define UBOOT_SIZE					(2 << 20)
#define CONFIG_SYS_INIT_SP_ADDR		(CONFIG_SYS_TEXT_BASE + UBOOT_SIZE - 0x1000)

/* U-Boot copy size from boot Media to DRAM. */
#define COPY_BL2_SIZE		0x80000
#define BL2_START_OFFSET	((RESERVE_BLOCK_SIZE + BL1_SIZE + BL2_SIZE)/512)
#define BL2_SIZE_BLOC_COUNT	(COPY_BL2_SIZE / 512)

#endif	/* __CONFIG_H */
