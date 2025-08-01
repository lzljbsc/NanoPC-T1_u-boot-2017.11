/*
 * (C) Copyright 2008 - 2009
 * Windriver, <www.windriver.com>
 * Tom Rix <Tom.Rix@windriver.com>
 *
 * Copyright 2011 Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * Copyright 2014 Linaro, Ltd.
 * Rob Herring <robh@kernel.org>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <config.h>
#include <common.h>
#include <environment.h>
#include <errno.h>
#include <fastboot.h>
#include <malloc.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/composite.h>
#include <linux/compiler.h>
#include <version.h>
#include <g_dnl.h>
#include <decompress_ext4.h>
#ifdef CONFIG_FASTBOOT_FLASH_MMC_DEV
#include <fb_mmc.h>
#endif
#ifdef CONFIG_FASTBOOT_FLASH_NAND_DEV
#include <fb_nand.h>
#endif

#define FASTBOOT_VERSION		"0.4"

#define FASTBOOT_INTERFACE_CLASS	0xff
#define FASTBOOT_INTERFACE_SUB_CLASS	0x42
#define FASTBOOT_INTERFACE_PROTOCOL	0x03

#define RX_ENDPOINT_MAXIMUM_PACKET_SIZE_2_0  (0x0200)
#define RX_ENDPOINT_MAXIMUM_PACKET_SIZE_1_1  (0x0040)
#define TX_ENDPOINT_MAXIMUM_PACKET_SIZE      (0x0040)

#define EP_BUFFER_SIZE			4096
/*
 * EP_BUFFER_SIZE must always be an integral multiple of maxpacket size
 * (64 or 512 or 1024), else we break on certain controllers like DWC3
 * that expect bulk OUT requests to be divisible by maxpacket size.
 */

int do_mmcops(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[]);
int do_mmc_write(cmd_tbl_t *cmdtp, int flag,int argc, char * const argv[]);
int do_mem_md(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[]);
int do_mmc_dev(cmd_tbl_t *cmdtp, int flag,int argc, char * const argv[]);
u64 get_mmcinfo_capacity(void);
//int get_mmcinfo_capacity(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[]);

struct f_fastboot {
	struct usb_function usb_function;

	/* IN/OUT EP's and corresponding requests */
	struct usb_ep *in_ep, *out_ep;
	struct usb_request *in_req, *out_req;
};

static inline struct f_fastboot *func_to_fastboot(struct usb_function *f)
{
	return container_of(f, struct f_fastboot, usb_function);
}

static struct f_fastboot *fastboot_func;
static unsigned int download_size;
static unsigned int download_bytes;

static struct usb_endpoint_descriptor fs_ep_in = {
	.bLength            = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType    = USB_DT_ENDPOINT,
	.bEndpointAddress   = USB_DIR_IN,
	.bmAttributes       = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize     = cpu_to_le16(64),
};

static struct usb_endpoint_descriptor fs_ep_out = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize		= cpu_to_le16(64),
};

static struct usb_endpoint_descriptor hs_ep_in = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize		= cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_ep_out = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize		= cpu_to_le16(512),
};

static struct usb_interface_descriptor interface_desc = {
	.bLength		= USB_DT_INTERFACE_SIZE,
	.bDescriptorType	= USB_DT_INTERFACE,
	.bInterfaceNumber	= 0x00,
	.bAlternateSetting	= 0x00,
	.bNumEndpoints		= 0x02,
	.bInterfaceClass	= FASTBOOT_INTERFACE_CLASS,
	.bInterfaceSubClass	= FASTBOOT_INTERFACE_SUB_CLASS,
	.bInterfaceProtocol	= FASTBOOT_INTERFACE_PROTOCOL,
};

static struct usb_descriptor_header *fb_fs_function[] = {
	(struct usb_descriptor_header *)&interface_desc,
	(struct usb_descriptor_header *)&fs_ep_in,
	(struct usb_descriptor_header *)&fs_ep_out,
};

static struct usb_descriptor_header *fb_hs_function[] = {
	(struct usb_descriptor_header *)&interface_desc,
	(struct usb_descriptor_header *)&hs_ep_in,
	(struct usb_descriptor_header *)&hs_ep_out,
	NULL,
};

static struct usb_endpoint_descriptor *
fb_ep_desc(struct usb_gadget *g, struct usb_endpoint_descriptor *fs,
	    struct usb_endpoint_descriptor *hs)
{
	if (gadget_is_dualspeed(g) && g->speed == USB_SPEED_HIGH)
		return hs;
	return fs;
}

/*
 * static strings, in UTF-8
 */
static const char fastboot_name[] = "Android Fastboot";

static struct usb_string fastboot_string_defs[] = {
	[0].s = fastboot_name,
	{  }			/* end of list */
};

static struct usb_gadget_strings stringtab_fastboot = {
	.language	= 0x0409,	/* en-us */
	.strings	= fastboot_string_defs,
};

static struct usb_gadget_strings *fastboot_strings[] = {
	&stringtab_fastboot,
	NULL,
};

static void rx_handler_command(struct usb_ep *ep, struct usb_request *req);
static int strcmp_l1(const char *s1, const char *s2);


static char *fb_response_str;

void fastboot_fail(const char *reason)
{
	strncpy(fb_response_str, "FAIL\0", 5);
	strncat(fb_response_str, reason, FASTBOOT_RESPONSE_LEN - 4 - 1);
}

void fastboot_okay(const char *reason)
{
	strncpy(fb_response_str, "OKAY\0", 5);
	strncat(fb_response_str, reason, FASTBOOT_RESPONSE_LEN - 4 - 1);
}

static void fastboot_complete(struct usb_ep *ep, struct usb_request *req)
{
	int status = req->status;
	if (!status)
		return;
	printf("status: %d ep '%s' trans: %d\n", status, ep->name, req->actual);
}

static int fastboot_bind(struct usb_configuration *c, struct usb_function *f)
{
	int id;
	struct usb_gadget *gadget = c->cdev->gadget;
	struct f_fastboot *f_fb = func_to_fastboot(f);
	const char *s;

	/* DYNAMIC interface numbers assignments */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	interface_desc.bInterfaceNumber = id;

	id = usb_string_id(c->cdev);
	if (id < 0)
		return id;
	fastboot_string_defs[0].id = id;
	interface_desc.iInterface = id;

	f_fb->in_ep = usb_ep_autoconfig(gadget, &fs_ep_in);
	if (!f_fb->in_ep)
		return -ENODEV;
	f_fb->in_ep->driver_data = c->cdev;

	f_fb->out_ep = usb_ep_autoconfig(gadget, &fs_ep_out);
	if (!f_fb->out_ep)
		return -ENODEV;
	f_fb->out_ep->driver_data = c->cdev;

	f->descriptors = fb_fs_function;

	if (gadget_is_dualspeed(gadget)) {
		/* Assume endpoint addresses are the same for both speeds */
		hs_ep_in.bEndpointAddress = fs_ep_in.bEndpointAddress;
		hs_ep_out.bEndpointAddress = fs_ep_out.bEndpointAddress;
		/* copy HS descriptors */
		f->hs_descriptors = fb_hs_function;
	}

	s = env_get("serial#");
	if (s)
		g_dnl_set_serialnumber((char *)s);

	return 0;
}

static void fastboot_unbind(struct usb_configuration *c, struct usb_function *f)
{
	memset(fastboot_func, 0, sizeof(*fastboot_func));
}

static void fastboot_disable(struct usb_function *f)
{
	struct f_fastboot *f_fb = func_to_fastboot(f);

	usb_ep_disable(f_fb->out_ep);
	usb_ep_disable(f_fb->in_ep);

	if (f_fb->out_req) {
		free(f_fb->out_req->buf);
		usb_ep_free_request(f_fb->out_ep, f_fb->out_req);
		f_fb->out_req = NULL;
	}
	if (f_fb->in_req) {
		free(f_fb->in_req->buf);
		usb_ep_free_request(f_fb->in_ep, f_fb->in_req);
		f_fb->in_req = NULL;
	}
}

static struct usb_request *fastboot_start_ep(struct usb_ep *ep)
{
	struct usb_request *req;

	req = usb_ep_alloc_request(ep, 0);
	if (!req)
		return NULL;

	req->length = EP_BUFFER_SIZE;
	req->buf = memalign(CONFIG_SYS_CACHELINE_SIZE, EP_BUFFER_SIZE);
	if (!req->buf) {
		usb_ep_free_request(ep, req);
		return NULL;
	}

	memset(req->buf, 0, req->length);
	return req;
}

static int fastboot_set_alt(struct usb_function *f,
			    unsigned interface, unsigned alt)
{
	int ret;
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_gadget *gadget = cdev->gadget;
	struct f_fastboot *f_fb = func_to_fastboot(f);
	const struct usb_endpoint_descriptor *d;

	debug("%s: func: %s intf: %d alt: %d\n",
	      __func__, f->name, interface, alt);

	d = fb_ep_desc(gadget, &fs_ep_out, &hs_ep_out);
	ret = usb_ep_enable(f_fb->out_ep, d);
	if (ret) {
		puts("failed to enable out ep\n");
		return ret;
	}

	f_fb->out_req = fastboot_start_ep(f_fb->out_ep);
	if (!f_fb->out_req) {
		puts("failed to alloc out req\n");
		ret = -EINVAL;
		goto err;
	}
	f_fb->out_req->complete = rx_handler_command;

	d = fb_ep_desc(gadget, &fs_ep_in, &hs_ep_in);
	ret = usb_ep_enable(f_fb->in_ep, d);
	if (ret) {
		puts("failed to enable in ep\n");
		goto err;
	}

	f_fb->in_req = fastboot_start_ep(f_fb->in_ep);
	if (!f_fb->in_req) {
		puts("failed alloc req in\n");
		ret = -EINVAL;
		goto err;
	}
	f_fb->in_req->complete = fastboot_complete;

	ret = usb_ep_queue(f_fb->out_ep, f_fb->out_req, 0);
	if (ret)
		goto err;

	return 0;
err:
	fastboot_disable(f);
	return ret;
}

static int fastboot_add(struct usb_configuration *c)
{
	struct f_fastboot *f_fb = fastboot_func;
	int status;

	debug("%s: cdev: 0x%p\n", __func__, c->cdev);

	if (!f_fb) {
		f_fb = memalign(CONFIG_SYS_CACHELINE_SIZE, sizeof(*f_fb));
		if (!f_fb)
			return -ENOMEM;

		fastboot_func = f_fb;
		memset(f_fb, 0, sizeof(*f_fb));
	}

	f_fb->usb_function.name = "f_fastboot";
	f_fb->usb_function.bind = fastboot_bind;
	f_fb->usb_function.unbind = fastboot_unbind;
	f_fb->usb_function.set_alt = fastboot_set_alt;
	f_fb->usb_function.disable = fastboot_disable;
	f_fb->usb_function.strings = fastboot_strings;

	status = usb_add_function(c, &f_fb->usb_function);
	if (status) {
		free(f_fb);
		fastboot_func = f_fb;
	}

	return status;
}
DECLARE_GADGET_BIND_CALLBACK(usb_dnl_fastboot, fastboot_add);

static int fastboot_tx_write(const char *buffer, unsigned int buffer_size)
{
	struct usb_request *in_req = fastboot_func->in_req;
	int ret;

	memcpy(in_req->buf, buffer, buffer_size);
	in_req->length = buffer_size;

	usb_ep_dequeue(fastboot_func->in_ep, in_req);

	ret = usb_ep_queue(fastboot_func->in_ep, in_req, 0);
	if (ret)
		printf("Error %d on queue\n", ret);
	return 0;
}

static int fastboot_tx_write_str(const char *buffer)
{
	return fastboot_tx_write(buffer, strlen(buffer));
}

static void compl_do_reset(struct usb_ep *ep, struct usb_request *req)
{
	do_reset(NULL, 0, 0, NULL);
}

int __weak fb_set_reboot_flag(void)
{
	return -ENOSYS;
}

static void cb_reboot(struct usb_ep *ep, struct usb_request *req)
{
	char *cmd = req->buf;
	if (!strcmp_l1("reboot-bootloader", cmd)) {
		if (fb_set_reboot_flag()) {
			fastboot_tx_write_str("FAILCannot set reboot flag");
			return;
		}
	}
	fastboot_func->in_req->complete = compl_do_reset;
	fastboot_tx_write_str("OKAY");
}

static int strcmp_l1(const char *s1, const char *s2)
{
	if (!s1 || !s2)
		return -1;
	return strncmp(s1, s2, strlen(s1));
}

static void cb_getvar(struct usb_ep *ep, struct usb_request *req)
{
	char *cmd = req->buf;
	char response[FASTBOOT_RESPONSE_LEN];
	const char *s;
	size_t chars_left;

	strcpy(response, "OKAY");
	chars_left = sizeof(response) - strlen(response) - 1;

	strsep(&cmd, ":");
	if (!cmd) {
		pr_err("missing variable");
		fastboot_tx_write_str("FAILmissing var");
		return;
	}

	if (!strcmp_l1("version", cmd)) {
		strncat(response, FASTBOOT_VERSION, chars_left);
	} else if (!strcmp_l1("bootloader-version", cmd)) {
		strncat(response, U_BOOT_VERSION, chars_left);
	} else if (!strcmp_l1("downloadsize", cmd) ||
		!strcmp_l1("max-download-size", cmd)) {
		char str_num[12];

		sprintf(str_num, "0x%08x", CONFIG_FASTBOOT_BUF_SIZE);
		strncat(response, str_num, chars_left);
	} else if (!strcmp_l1("serialno", cmd)) {
		s = env_get("serial#");
		if (s)
			strncat(response, s, chars_left);
		else
			strcpy(response, "FAILValue not set");
	} else {
		char *envstr;

		envstr = malloc(strlen("fastboot.") + strlen(cmd) + 1);
		if (!envstr) {
			fastboot_tx_write_str("FAILmalloc error");
			return;
		}

		sprintf(envstr, "fastboot.%s", cmd);
		s = env_get(envstr);
		if (s) {
			strncat(response, s, chars_left);
		} else {
			printf("WARNING: unknown variable: %s\n", cmd);
			strcpy(response, "FAILVariable not implemented");
		}

		free(envstr);
	}
	fastboot_tx_write_str(response);
}

static unsigned int rx_bytes_expected(struct usb_ep *ep)
{
	int rx_remain = download_size - download_bytes;
	unsigned int rem;
	unsigned int maxpacket = ep->maxpacket;

	if (rx_remain <= 0)
		return 0;
	else if (rx_remain > EP_BUFFER_SIZE)
		return EP_BUFFER_SIZE;

	/*
	 * Some controllers e.g. DWC3 don't like OUT transfers to be
	 * not ending in maxpacket boundary. So just make them happy by
	 * always requesting for integral multiple of maxpackets.
	 * This shouldn't bother controllers that don't care about it.
	 */
	rem = rx_remain % maxpacket;
	if (rem > 0)
		rx_remain = rx_remain + (maxpacket - rem);

	return rx_remain;
}

#define BYTES_PER_DOT	0x20000
static void rx_handler_dl_image(struct usb_ep *ep, struct usb_request *req)
{
	char response[FASTBOOT_RESPONSE_LEN];
	unsigned int transfer_size = download_size - download_bytes;
	const unsigned char *buffer = req->buf;
	unsigned int buffer_size = req->actual;
	unsigned int pre_dot_num, now_dot_num;

	if (req->status != 0) {
		printf("Bad status: %d\n", req->status);
		return;
	}

	if (buffer_size < transfer_size)
		transfer_size = buffer_size;

	memcpy((void *)CONFIG_FASTBOOT_BUF_ADDR + download_bytes,
	       buffer, transfer_size);

	pre_dot_num = download_bytes / BYTES_PER_DOT;
	download_bytes += transfer_size;
	now_dot_num = download_bytes / BYTES_PER_DOT;

	if (pre_dot_num != now_dot_num) {
		putc('.');
		if (!(now_dot_num % 74))
			putc('\n');
	}

	/* Check if transfer is done */
	if (download_bytes >= download_size) {
		/*
		 * Reset global transfer variable, keep download_bytes because
		 * it will be used in the next possible flashing command
		 */
		download_size = 0;
		req->complete = rx_handler_command;
		req->length = EP_BUFFER_SIZE;

		strcpy(response, "OKAY");
		fastboot_tx_write_str(response);

		printf("\ndownloading of %d bytes finished\n", download_bytes);
	} else {
		req->length = rx_bytes_expected(ep);
	}

	req->actual = 0;
	usb_ep_queue(ep, req, 0);
}

static void cb_download(struct usb_ep *ep, struct usb_request *req)
{
	char *cmd = req->buf;
	char response[FASTBOOT_RESPONSE_LEN];

	strsep(&cmd, ":");
	download_size = simple_strtoul(cmd, NULL, 16);
	download_bytes = 0;

	printf("Starting download of %d bytes\n", download_size);

	if (0 == download_size) {
		strcpy(response, "FAILdata invalid size");
	} else if (download_size > CONFIG_FASTBOOT_BUF_SIZE) {
		download_size = 0;
		strcpy(response, "FAILdata too large");
	} else {
		sprintf(response, "DATA%08x", download_size);
		req->complete = rx_handler_dl_image;
		req->length = rx_bytes_expected(ep);
	}
	fastboot_tx_write_str(response);
}

static void do_bootm_on_complete(struct usb_ep *ep, struct usb_request *req)
{
	char boot_addr_start[12];
	char *bootm_args[] = { "bootm", boot_addr_start, NULL };

	puts("Booting kernel..\n");

	sprintf(boot_addr_start, "0x%lx", (long)CONFIG_FASTBOOT_BUF_ADDR);
	do_bootm(NULL, 0, 2, bootm_args);

	/* This only happens if image is somehow faulty so we start over */
	do_reset(NULL, 0, 0, NULL);
}

static void cb_boot(struct usb_ep *ep, struct usb_request *req)
{
	fastboot_func->in_req->complete = do_bootm_on_complete;
	fastboot_tx_write_str("OKAY");
}

static void do_exit_on_complete(struct usb_ep *ep, struct usb_request *req)
{
	g_dnl_trigger_detach();
}

static void cb_continue(struct usb_ep *ep, struct usb_request *req)
{
	fastboot_func->in_req->complete = do_exit_on_complete;
	fastboot_tx_write_str("OKAY");
}

#ifdef CONFIG_FASTBOOT_FLASH
#define SECTOR_BITS             9       /* 512B */
#define ext4_printf(args, ...)

int write_raw_chunk(char* data, unsigned int sector, unsigned int sector_size) 
{
    char run_cmd[64];
    int err ;
    ext4_printf("write raw data in %d size %d \n", sector, sector_size);
    sprintf(run_cmd,"mmc write  0x%x 0x%x 0x%x",(int)data, sector, sector_size);
    err = run_command(run_cmd, 0);


    return (1-err); //mj
}
int write_compressed_ext4(char* img_base, unsigned int sector_base) 
{
    unsigned int sector_size;
    int total_chunks;
    ext4_chunk_header *chunk_header;
    ext4_file_header *file_header;
    int err;
    (void)err;

    file_header = (ext4_file_header*)img_base;
    total_chunks = file_header->total_chunks;

    ext4_printf("total chunk = %d \n", total_chunks);

    img_base += EXT4_FILE_HEADER_SIZE;

    while(total_chunks) {
        chunk_header = (ext4_chunk_header*)img_base;
        sector_size = (chunk_header->chunk_size * file_header->block_size) >> SECTOR_BITS;

        switch(chunk_header->type)
        {
            case EXT4_CHUNK_TYPE_RAW:
                ext4_printf("raw_chunk \n");
                err = write_raw_chunk(img_base + EXT4_CHUNK_HEADER_SIZE,
                        sector_base, sector_size);
                //if(err)//mj for emergency
                //{
                //      printf("[ERROR] System image write fail.please try again..\n");
                //    return err;
                //}
                //else
                //{
                sector_base += sector_size;
                break;
                //}
            case EXT4_CHUNK_TYPE_FILL:
                ext4_printf("fill_chunk \n");
                sector_base += sector_size;
                break;

            case EXT4_CHUNK_TYPE_NONE:
                ext4_printf("none chunk \n");
                sector_base += sector_size;
                break;

            default:
                ext4_printf("unknown chunk type \n");
                sector_base += sector_size;
                break;
        }
        total_chunks--;
        ext4_printf("remain chunks = %d \n", total_chunks);

        img_base += chunk_header->total_size;
    };

    ext4_printf("write done \n");
    return 0;
}

/* 分区方式参考 include/configs/nanopc_t1.h */
char *argv_fwbl1[5]  = { "mmc","write", "40000000","0","10"};
char *argv_bl2[5]  = { "mmc","write", "40000000","10","20"};
char *argv_bootloader[5]  = { "mmc","write", "40000000","30","400"};
char *argv_kernel[5]  = { "mmc","write", "40000000","800","F000"};
char *argv_dtb[5]  = { "mmc","write", "40000000","F800","200"};
char *argv_dtb2[5]  = { "mmc","write", "40000000","FA00","200"};
char *argv_dtb3[5]  = { "mmc","write", "40000000","FC00","200"};
char *argv_dtb4[5]  = { "mmc","write", "40000000","FE00","200"};
char *argv_ramdisk[5]  = { "mmc","write", "40000000","10000","10000"};
char *argv_system[5]  = { "mmc","write", "40000000","20CC7","100000"};
/* 用于计算待写入的扇区数量
 * download_bytes  是下载的镜像字节数 */
char write_size[16];
static unsigned int write_sectors;
static void cb_flash(struct usb_ep *ep, struct usb_request *req)
{
    char *cmd = req->buf;
    char response[FASTBOOT_RESPONSE_LEN];

    //char *argv[2]  = { "md","40000000"};
    char *open_emmc[2]  = { "open","   "};
    char *close_emmc[2]  = { "close","   "};

    static cmd_tbl_t cmdtp;
    char value[] = {"mmc"};
    //char run_cmd[64];

    unsigned int addr = 0x40000000;

    /* start_8G start_16G 是向emmc中写入 system 分区时的起始扇区
     * 在 include/configs/nanopc_t1.h 中定义的 CFG_PARTITION_START
     * 为什么 start_8G 不是一个对齐的扇区呢？
     * 因为在 drivers/usb/gadget/f_fastboot.c 中，小于8G的emmc
     * 会按照 CHS 访问方式进行分区，所以不会整好对齐
     * 这里是使用 CFG_PARTITION_START 计算出 分区的 CHS 对应的扇区号 */
    unsigned int start_8G = 0x20CC7;
    unsigned int start_16G = 0x10000;

    cmdtp.name = &value[0];
    cmdtp.maxargs = 29;
    cmdtp.repeatable = 1;
    cmdtp.cmd = do_mmc_write;
    cmdtp.usage = NULL;


    strsep(&cmd, ":");
    if (!cmd) {
        pr_err("missing partition name");
        fastboot_tx_write_str("FAILmissing partition name");
        fastboot_fail("missing");
        return;
    }

    /* initialize the response buffer */
    fb_response_str = response;

    //	fastboot_fail("no flash device defined");
#ifdef CONFIG_FASTBOOT_FLASH_MMC_DEV
    //	fb_mmc_flash_write(cmd, (void *)CONFIG_FASTBOOT_BUF_ADDR,
    //			   download_bytes);

    printf("---%s---\n",cmd);

    if(strstr(cmd,"fwbl1") != NULL)
    {
        do_mmc_dev(NULL,0,2,open_emmc);//open emmc
        do_mmcops(&cmdtp,0,5,argv_fwbl1);
        do_mmc_dev(NULL,0,2,close_emmc);//close emmc
        fastboot_okay("fwbl1");
    }else if(strstr(cmd,"bl2") != NULL)
    {
        do_mmc_dev(NULL,0,2,open_emmc);//open emmc
        do_mmcops(&cmdtp,0,5,argv_bl2);
        do_mmc_dev(NULL,0,2,close_emmc);//close emmc
        fastboot_okay("bl2");
    }else if(strstr(cmd,"bootloader") != NULL)
    {
        do_mmc_dev(NULL,0,2,open_emmc);//open emmc
        do_mmcops(&cmdtp,0,5,argv_bootloader);
        do_mmc_dev(NULL,0,2,close_emmc);//close emmc
        fastboot_okay("bootloader");
    }else if(strstr(cmd, "kernel") != NULL)
    {
        /* 向上对齐计算扇区数量 */
        write_sectors = (download_bytes + 512) / 512;
        sprintf(write_size, "0x%X", write_sectors);
        env_set("kernel_sector_num", write_size);
        env_save();
        argv_kernel[4] = write_size;
        do_mmcops(&cmdtp,0,5,argv_kernel);
        fastboot_okay("kernel");
    }else if(strcmp(cmd,"dtb") == 0)
    {
        do_mmcops(&cmdtp,0,5,argv_dtb);
        fastboot_okay("dtb");
    }else if(strcmp(cmd,"dtb2") == 0)
    {
        do_mmcops(&cmdtp,0,5,argv_dtb2);
        fastboot_okay("dtb2");
    }else if(strcmp(cmd,"dtb3") == 0)
    {
        do_mmcops(&cmdtp,0,5,argv_dtb3);
        fastboot_okay("dtb3");
    }else if(strcmp(cmd,"dtb4") == 0)
    {
        do_mmcops(&cmdtp,0,5,argv_dtb4);
        fastboot_okay("dtb4");
    }else if(strstr(cmd,"ramdisk") != NULL)
    {
        /* 向上对齐计算扇区数量 */
        write_sectors = (download_bytes + 512) / 512;
        sprintf(write_size, "0x%X", write_sectors);
        env_set("ramdisk_sector_num", write_size);
        env_save();
        argv_ramdisk[4] = write_size;
        do_mmcops(&cmdtp,0,5,argv_ramdisk);
        fastboot_okay("ramdisk");
    }else if(strstr(cmd,"system") != NULL)
    {
        //      do_mmcops(&cmdtp,0,5,argv_system);
        if(get_mmcinfo_capacity() > 8){
            write_compressed_ext4((char *)addr,start_16G);
        }
        else{
            write_compressed_ext4((char *)addr,start_8G);
        }
        fastboot_okay("system");
    }else
    {
        fastboot_fail("error");
    }

    printf("\n\n");

#endif
#ifdef CONFIG_FASTBOOT_FLASH_NAND_DEV
    fb_nand_flash_write(cmd,
            (void *)CONFIG_FASTBOOT_BUF_ADDR,
            download_bytes);
#endif
    fastboot_tx_write_str(response);
}
#endif

static void cb_oem(struct usb_ep *ep, struct usb_request *req)
{
	char *cmd = req->buf;
#ifdef CONFIG_FASTBOOT_FLASH_MMC_DEV
	if (strncmp("format", cmd + 4, 6) == 0) {
		char cmdbuf[32];
                sprintf(cmdbuf, "gpt write mmc %x $partitions",
			CONFIG_FASTBOOT_FLASH_MMC_DEV);
                if (run_command(cmdbuf, 0))
			fastboot_tx_write_str("FAIL");
                else
			fastboot_tx_write_str("OKAY");
	} else
#endif
	if (strncmp("unlock", cmd + 4, 8) == 0) {
		fastboot_tx_write_str("FAILnot implemented");
	}
	else {
		fastboot_tx_write_str("FAILunknown oem command");
	}
}

#ifdef CONFIG_FASTBOOT_FLASH
static void cb_erase(struct usb_ep *ep, struct usb_request *req)
{
	char *cmd = req->buf;
	char response[FASTBOOT_RESPONSE_LEN];

	strsep(&cmd, ":");
	if (!cmd) {
		pr_err("missing partition name");
		fastboot_tx_write_str("FAILmissing partition name");
		return;
	}

	/* initialize the response buffer */
	fb_response_str = response;

	fastboot_fail("no flash device defined");
#ifdef CONFIG_FASTBOOT_FLASH_MMC_DEV
	fb_mmc_erase(cmd);
#endif
#ifdef CONFIG_FASTBOOT_FLASH_NAND_DEV
	fb_nand_erase(cmd);
#endif
	fastboot_tx_write_str(response);
}
#endif

struct cmd_dispatch_info {
	char *cmd;
	void (*cb)(struct usb_ep *ep, struct usb_request *req);
};

static const struct cmd_dispatch_info cmd_dispatch_info[] = {
	{
		.cmd = "reboot",
		.cb = cb_reboot,
	}, {
		.cmd = "getvar:",
		.cb = cb_getvar,
	}, {
		.cmd = "download:",
		.cb = cb_download,
	}, {
		.cmd = "boot",
		.cb = cb_boot,
	}, {
		.cmd = "continue",
		.cb = cb_continue,
	},
#ifdef CONFIG_FASTBOOT_FLASH
	{
		.cmd = "flash",
		.cb = cb_flash,
	}, {
		.cmd = "erase",
		.cb = cb_erase,
	},
#endif
	{
		.cmd = "oem",
		.cb = cb_oem,
	},
};

static void rx_handler_command(struct usb_ep *ep, struct usb_request *req)
{
	char *cmdbuf = req->buf;
	void (*func_cb)(struct usb_ep *ep, struct usb_request *req) = NULL;
	int i;

	if (req->status != 0 || req->length == 0)
		return;

	for (i = 0; i < ARRAY_SIZE(cmd_dispatch_info); i++) {
		if (!strcmp_l1(cmd_dispatch_info[i].cmd, cmdbuf)) {
			func_cb = cmd_dispatch_info[i].cb;
			break;
		}
	}

	if (!func_cb) {
		pr_err("unknown command: %.*s", req->actual, cmdbuf);
		fastboot_tx_write_str("FAILunknown command");
	} else {
		if (req->actual < req->length) {
			u8 *buf = (u8 *)req->buf;
			buf[req->actual] = 0;
			func_cb(ep, req);
		} else {
			pr_err("buffer overflow");
			fastboot_tx_write_str("FAILbuffer overflow");
		}
	}

	*cmdbuf = '\0';
	req->actual = 0;
	usb_ep_queue(ep, req, 0);
}
