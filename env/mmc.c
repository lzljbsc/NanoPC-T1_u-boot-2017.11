/*
 * (C) Copyright 2008-2011 Freescale Semiconductor, Inc.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

/* #define DEBUG */

#include <common.h>

#include <command.h>
#include <environment.h>
#include <fdtdec.h>
#include <linux/stddef.h>
#include <malloc.h>
#include <memalign.h>
#include <mmc.h>
#include <search.h>
#include <errno.h>
#include <asm/arch/power.h>

#if defined(CONFIG_ENV_SIZE_REDUND) &&  \
	(CONFIG_ENV_SIZE_REDUND != CONFIG_ENV_SIZE)
#error CONFIG_ENV_SIZE_REDUND should be the same as CONFIG_ENV_SIZE
#endif

DECLARE_GLOBAL_DATA_PTR;

#if !defined(CONFIG_ENV_OFFSET)
#define CONFIG_ENV_OFFSET 0
#endif

#if CONFIG_IS_ENABLED(OF_CONTROL)
static inline s64 mmc_offset(int copy)
{
	const char *propname = "u-boot,mmc-env-offset";
	s64 defvalue = CONFIG_ENV_OFFSET;

#if defined(CONFIG_ENV_OFFSET_REDUND)
	if (copy) {
		propname = "u-boot,mmc-env-offset-redundant";
		defvalue = CONFIG_ENV_OFFSET_REDUND;
	}
#endif

	return fdtdec_get_config_int(gd->fdt_blob, propname, defvalue);
}
#else
static inline s64 mmc_offset(int copy)
{
	s64 offset = CONFIG_ENV_OFFSET;

#if defined(CONFIG_ENV_OFFSET_REDUND)
	if (copy)
		offset = CONFIG_ENV_OFFSET_REDUND;
#endif
	return offset;
}
#endif

__weak int mmc_get_env_addr(struct mmc *mmc, int copy, u32 *env_addr)
{
	s64 offset = mmc_offset(copy);

	if (offset < 0)
		offset += mmc->capacity;

	*env_addr = offset;

	return 0;
}

__weak int mmc_get_env_dev(void)
{
	unsigned int boot_mode;

	boot_mode = get_boot_mode();
	if (boot_mode == BOOT_MODE_SD)
		return 1;
	else if ((boot_mode == BOOT_MODE_EMMC_SD) || (boot_mode == BOOT_MODE_EMMC))
		return 0;
	return CONFIG_SYS_MMC_ENV_DEV;
}

#ifdef CONFIG_SYS_MMC_ENV_PART
__weak uint mmc_get_env_part(struct mmc *mmc)
{
	return CONFIG_SYS_MMC_ENV_PART;
}

static unsigned char env_mmc_orig_hwpart;

static int mmc_set_env_part(struct mmc *mmc)
{
	uint part = mmc_get_env_part(mmc);
	int dev = mmc_get_env_dev();
	int ret = 0;

	env_mmc_orig_hwpart = mmc_get_blk_desc(mmc)->hwpart;
	ret = blk_select_hwpart_devnum(IF_TYPE_MMC, dev, part);
	if (ret)
		puts("MMC partition switch failed\n");

	return ret;
}
#else
static inline int mmc_set_env_part(struct mmc *mmc) {return 0; };
#endif

static const char *init_mmc_for_env(struct mmc *mmc)
{
	if (!mmc)
		return "!No MMC card found";

#ifdef CONFIG_BLK
	struct udevice *dev;

	if (blk_get_from_parent(mmc->dev, &dev))
		return "!No block device";
#else
	if (mmc_init(mmc))
		return "!MMC init failed";
#endif
	if (mmc_set_env_part(mmc))
		return "!MMC partition switch failed";

	return NULL;
}

static void fini_mmc_for_env(struct mmc *mmc)
{
#ifdef CONFIG_SYS_MMC_ENV_PART
	int dev = mmc_get_env_dev();

	blk_select_hwpart_devnum(IF_TYPE_MMC, dev, env_mmc_orig_hwpart);
#endif
}

#if defined(CONFIG_CMD_SAVEENV) && !defined(CONFIG_SPL_BUILD)
static inline int write_env(struct mmc *mmc, unsigned long size,
			    unsigned long offset, const void *buffer)
{
	uint blk_start, blk_cnt, n;
	struct blk_desc *desc = mmc_get_blk_desc(mmc);

	blk_start	= ALIGN(offset, mmc->write_bl_len) / mmc->write_bl_len;
	blk_cnt		= ALIGN(size, mmc->write_bl_len) / mmc->write_bl_len;

	n = blk_dwrite(desc, blk_start, blk_cnt, (u_char *)buffer);

	return (n == blk_cnt) ? 0 : -1;
}

static int env_mmc_save(void)
{
	ALLOC_CACHE_ALIGN_BUFFER(env_t, env_new, 1);
	int dev = mmc_get_env_dev();
	struct mmc *mmc = find_mmc_device(dev);
	u32	offset;
	int	ret, copy = 0;
	const char *errmsg;

	errmsg = init_mmc_for_env(mmc);
	if (errmsg) {
		printf("%s\n", errmsg);
		return 1;
	}

	ret = env_export(env_new);
	if (ret)
		goto fini;

#ifdef CONFIG_ENV_OFFSET_REDUND
	if (gd->env_valid == ENV_VALID)
		copy = 1;
#endif

	if (mmc_get_env_addr(mmc, copy, &offset)) {
		ret = 1;
		goto fini;
	}

	printf("Writing to %sMMC(%d)... ", copy ? "redundant " : "", dev);
	if (write_env(mmc, CONFIG_ENV_SIZE, offset, (u_char *)env_new)) {
		puts("failed\n");
		ret = 1;
		goto fini;
	}

	puts("done\n");
	ret = 0;

#ifdef CONFIG_ENV_OFFSET_REDUND
	gd->env_valid = gd->env_valid == ENV_REDUND ? ENV_VALID : ENV_REDUND;
#endif

fini:
	fini_mmc_for_env(mmc);
	return ret;
}
#endif /* CONFIG_CMD_SAVEENV && !CONFIG_SPL_BUILD */

static inline int read_env(struct mmc *mmc, unsigned long size,
			   unsigned long offset, const void *buffer)
{
	uint blk_start, blk_cnt, n;
	struct blk_desc *desc = mmc_get_blk_desc(mmc);

	blk_start	= ALIGN(offset, mmc->read_bl_len) / mmc->read_bl_len;
	blk_cnt		= ALIGN(size, mmc->read_bl_len) / mmc->read_bl_len;

	n = blk_dread(desc, blk_start, blk_cnt, (uchar *)buffer);

	return (n == blk_cnt) ? 0 : -1;
}

#ifdef CONFIG_ENV_OFFSET_REDUND
static int env_mmc_load(void)
{
#if !defined(ENV_IS_EMBEDDED)
	struct mmc *mmc;
	u32 offset1, offset2;
	int read1_fail = 0, read2_fail = 0;
	int ret;
	int dev = mmc_get_env_dev();
	const char *errmsg = NULL;

	ALLOC_CACHE_ALIGN_BUFFER(env_t, tmp_env1, 1);
	ALLOC_CACHE_ALIGN_BUFFER(env_t, tmp_env2, 1);

	mmc = find_mmc_device(dev);

	errmsg = init_mmc_for_env(mmc);
	if (errmsg) {
		ret = -EIO;
		goto err;
	}

	if (mmc_get_env_addr(mmc, 0, &offset1) ||
	    mmc_get_env_addr(mmc, 1, &offset2)) {
		ret = -EIO;
		goto fini;
	}

	read1_fail = read_env(mmc, CONFIG_ENV_SIZE, offset1, tmp_env1);
	read2_fail = read_env(mmc, CONFIG_ENV_SIZE, offset2, tmp_env2);

	if (read1_fail && read2_fail)
		puts("*** Error - No Valid Environment Area found\n");
	else if (read1_fail || read2_fail)
		puts("*** Warning - some problems detected "
		     "reading environment; recovered successfully\n");

	if (read1_fail && read2_fail) {
		errmsg = "!bad CRC";
		ret = -EIO;
		goto fini;
	} else if (!read1_fail && read2_fail) {
		gd->env_valid = ENV_VALID;
		env_import((char *)tmp_env1, 1);
	} else if (read1_fail && !read2_fail) {
		gd->env_valid = ENV_REDUND;
		env_import((char *)tmp_env2, 1);
	} else {
		env_import_redund((char *)tmp_env1, (char *)tmp_env2);
	}

	ret = 0;

fini:
	fini_mmc_for_env(mmc);
err:
	if (ret)
		set_default_env(errmsg);

#endif
	return ret;
}
#else /* ! CONFIG_ENV_OFFSET_REDUND */
static int env_mmc_load(void)
{
#if !defined(ENV_IS_EMBEDDED)
	ALLOC_CACHE_ALIGN_BUFFER(char, buf, CONFIG_ENV_SIZE);
	struct mmc *mmc;
	u32 offset;
	int ret;
	int dev = mmc_get_env_dev();
	const char *errmsg;

	mmc = find_mmc_device(dev);

	errmsg = init_mmc_for_env(mmc);
	if (errmsg) {
		ret = -EIO;
		goto err;
	}

	if (mmc_get_env_addr(mmc, 0, &offset)) {
		ret = -EIO;
		goto fini;
	}

	if (read_env(mmc, CONFIG_ENV_SIZE, offset, buf)) {
		errmsg = "!read failed";
		ret = -EIO;
		goto fini;
	}

	env_import(buf, 1);
	ret = 0;

fini:
	fini_mmc_for_env(mmc);
err:
	if (ret)
		set_default_env(errmsg);
#endif
	return ret;
}
#endif /* CONFIG_ENV_OFFSET_REDUND */

U_BOOT_ENV_LOCATION(mmc) = {
	.location	= ENVL_MMC,
	ENV_NAME("MMC")
	.load		= env_mmc_load,
#ifndef CONFIG_SPL_BUILD
	.save		= env_save_ptr(env_mmc_save),
#endif
};
