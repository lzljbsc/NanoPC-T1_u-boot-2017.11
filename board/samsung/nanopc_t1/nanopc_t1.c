/*
 * Copyright (C) 2011 Samsung Electronics
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <asm/arch/cpu.h>
#include <asm/arch/mmc.h>
#include <asm/arch/periph.h>
#include <asm/arch/pinmux.h>
#include <asm/arch/power.h>
#include <asm/mach-types.h>
#include <usb.h>
#include <usb/dwc2_udc.h>

DECLARE_GLOBAL_DATA_PTR;

u32 get_board_rev(void)
{
	return 0;
}

int exynos_init(void)
{
	/* arch number of NANOPC-T1 */
	gd->bd->bi_arch_number = MACH_TYPE_NANOPC_T1;
	return 0;
}

static int s5pc210_phy_control(int on)
{
	return 0;
}

/* 分区方式参考 include/configs/nanopc_t1.h */
extern char *argv_fwbl1[5];
extern char *argv_bl2[5];
extern char *argv_bootloader[5];
extern char *argv_kernel[5];
extern char *argv_dtb[5];
extern char *argv_dtb2[5];
extern char *argv_dtb3[5];
extern char *argv_dtb4[5];
extern char *argv_ramdisk[5];
extern char *argv_system[5];
void fastboot_ptn(void)
{
    printf("ptn 0 name='fwbl1'      start=0x%s \tlen=0x%s\r\n", argv_fwbl1[3], argv_fwbl1[4]);
    printf("ptn 1 name='bl2'        start=0x%s \tlen=0x%s\r\n", argv_bl2[3], argv_bl2[4]);
    printf("ptn 2 name='bootloader' start=0x%s \tlen=0x%s\r\n", argv_bootloader[3], argv_bootloader[4]);
    printf("ptn 3 name='kernel'     start=0x%s \tlen=0x%s\r\n", argv_kernel[3], argv_kernel[4]);
    printf("ptn 4 name='dtb'        start=0x%s \tlen=0x%s\r\n", argv_dtb[3], argv_dtb[4]);
    printf("ptn 4 name='dtb2'       start=0x%s \tlen=0x%s\r\n", argv_dtb2[3], argv_dtb2[4]);
    printf("ptn 4 name='dtb3'       start=0x%s \tlen=0x%s\r\n", argv_dtb3[3], argv_dtb3[4]);
    printf("ptn 4 name='dtb4'       start=0x%s \tlen=0x%s\r\n", argv_dtb4[3], argv_dtb4[4]);
    printf("ptn 4 name='ramdisk'    start=0x%s \tlen=0x%s\r\n", argv_ramdisk[3], argv_ramdisk[4]);
    printf("ptn 5 name='system'     start=0x%s \tlen=0x%s\r\n", argv_system[3], argv_system[4]);
}

struct dwc2_plat_otg_data s5pc210_otg_data = {
	.phy_control	= s5pc210_phy_control,
	.regs_phy	= EXYNOS4X12_USBPHY_BASE,
	.regs_otg	= EXYNOS4X12_USBOTG_BASE,
	.usb_phy_ctrl	= EXYNOS4X12_USBPHY_CONTROL,
	.usb_flags	= PHY0_SLEEP,
};

int board_usb_init(int index, enum usb_init_type init)
{
    fastboot_ptn();
	debug("USB_udc_probe\n");
	return dwc2_udc_probe(&s5pc210_otg_data);
}

#ifdef CONFIG_BOARD_EARLY_INIT_F
int exynos_early_init_f(void)
{
    /* 设置 启动方式 到 INF_REG3寄存器
     * 在内核启动时将会使用该值 */
    unsigned int bootmode = get_boot_mode();
    struct exynos4x12_power *power = 
        (struct exynos4x12_power *)samsung_get_base_power();

    bootmode &= (~0xffffffc1);
    if (BOOT_MODE_SD == bootmode) {
        writel(BOOT_MMCSD, &power->inform3);
        printf("\r\n\nBoot from SD.");
    } else if (BOOT_MODE_EMMC == bootmode) {
        writel(BOOT_EMMC, &power->inform3);
        printf("\r\n\nBoot from EMMC.");
    } else if (BOOT_MODE_EMMC_SD == bootmode) {
        writel(BOOT_EMMC_4_4, &power->inform3);
        printf("\r\n\nBoot from EMMC4.4.");
    } else {
        writel(0x00, &power->inform3);
        printf("\r\n\nUnknown.\r\n");
        return 1;
    }


    /* LED2 */
    writel(0x00001111, 0x110002E0);
    writel(0x0d, 0x110002E4);
	return 0;
}
#endif
