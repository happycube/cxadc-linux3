// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cxadc - CX2388x ADC DMA driver for Linux, version 0.5
 *
 * Copyright (C) 2005-2007 Hew How Chee <how_chee@yahoo.com>
 * Copyright (C) 2013-2015 Chad Page <Chad.Page@gmail.com>
 * Copyright (C) 2019-2024 Adam Sampson <ats@offog.org>
 * Copyright (C) 2020-2022 Tony Anderson  <tandersn@cs.washington.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "cx88-reg.h"

#include <linux/version.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/mm.h>

/*
 * From Linux 4.21, dma_alloc_coherent always returns zeroed memory,
 * and dma_zalloc_coherent was removed later.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 21, 0)
#define dma_zalloc_coherent dma_alloc_coherent
#endif

#define default_latency			-1
#define default_audsel			-1
#define default_vmux		        1	
#define default_level			16
#define default_tenbit			0
#define default_tenxfsc			0
#define default_sixdb			0
#define default_crystal			28636363
#define default_center_offset	8

#define cx_read(reg)         readl(ctd->mmio + ((reg) >> 2))
#define cx_write(reg, value) writel((value), ctd->mmio + ((reg) >> 2))

#define cx_err(fmt, ...) \
	dev_err(&ctd->pci->dev, fmt, ##__VA_ARGS__)
#define cx_info(fmt, ...) \
	dev_info(&ctd->pci->dev, fmt, ##__VA_ARGS__)

/* 64 Mbytes VBI DMA BUFF */
#define VBI_DMA_BUFF_SIZE (1024*1024*64)
/* corresponds to 8192 DMA pages of 4k bytes */
#define MAX_DMA_PAGE (VBI_DMA_BUFF_SIZE/PAGE_SIZE)

#define CLUSTER_BUFFER_SIZE 2048

/* Must be a power of 2 */
#define IRQ_PERIOD_IN_PAGES (0x200000 >> PAGE_SHIFT)

struct cxadc {
	/* linked list */
	struct cxadc *next;
	/* device info */
	struct cdev cdev;
	struct pci_dev *pci;
	unsigned int   irq;
	unsigned int  mem;
	unsigned int  *mmio;
	struct kref refcnt;

	/* locking */
	bool in_use;
	struct mutex lock;

	unsigned int    risc_inst_buff_size;
	unsigned int	*risc_inst_virt;
	dma_addr_t	risc_inst_phy;

	wait_queue_head_t readQ;

	void *pgvec_virt[MAX_DMA_PAGE+1];
	dma_addr_t pgvec_phy[MAX_DMA_PAGE+1];

	atomic_t lgpcnt;
	int initial_page;
	/* device attributes */
	int latency;
	int audsel;
	int vmux;
	int level;
	int tenbit;
	int tenxfsc;
	int sixdb;
	int crystal;
	int center_offset;
};

/*
 * boiler plate for device attributes
 * show/store for latency
 */

static ssize_t mycxadc_latency_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cxadc *mycxadc = dev_get_drvdata(dev);
	int len;

	len = sprintf(buf, "%d\n", mycxadc->latency);
	if (len <= 0)
		dev_err(dev, "cxadc: Invalid sprintf len: %d\n", len);
	return len;
}

static ssize_t mycxadc_latency_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	struct cxadc *mycxadc = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 10, &mycxadc->latency);
	return count;
}

/*
 * show/store for audsel
 */

static ssize_t mycxadc_audsel_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cxadc *mycxadc = dev_get_drvdata(dev);
	int len;

	len = sprintf(buf, "%d\n", mycxadc->audsel);
	if (len <= 0)
		dev_err(dev, "cxadc: Invalid sprintf len: %d\n", len);
	return len;
}

static ssize_t mycxadc_audsel_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	struct cxadc *mycxadc = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 10, &mycxadc->audsel);
	return count;
}

/*
 * show/store for level
 */

static ssize_t mycxadc_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cxadc *mycxadc = dev_get_drvdata(dev);
	int len;

	len = sprintf(buf, "%d\n", mycxadc->level);
	if (len <= 0)
		dev_err(dev, "cxadc: Invalid sprintf len: %d\n", len);
	return len;
}

static ssize_t mycxadc_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	struct cxadc *mycxadc = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 10, &mycxadc->level);
	return count;
}

/*
 * show/store for vmux
 */

static ssize_t mycxadc_vmux_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cxadc *mycxadc = dev_get_drvdata(dev);
	int len;

	len = sprintf(buf, "%d\n", mycxadc->vmux);
	if (len <= 0)
		dev_err(dev, "cxadc: Invalid sprintf len: %d\n", len);
	return len;
}

static ssize_t mycxadc_vmux_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	struct cxadc *mycxadc = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 10, &mycxadc->vmux);
	return count;
}

/*
 * show/store for tenbit
 */

static ssize_t mycxadc_tenbit_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cxadc *mycxadc = dev_get_drvdata(dev);
	int len;

	len = sprintf(buf, "%d\n", mycxadc->tenbit);
	if (len <= 0)
		dev_err(dev, "cxadc: Invalid sprintf len: %d\n", len);
	return len;
}

static ssize_t mycxadc_tenbit_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	struct cxadc *mycxadc = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 10, &mycxadc->tenbit);
	return count;
}

/*
 * show/store for tenfsc
 */

static ssize_t mycxadc_tenxfsc_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cxadc *mycxadc = dev_get_drvdata(dev);
	int len;

	len = sprintf(buf, "%d\n", mycxadc->tenxfsc);
	if (len <= 0)
		dev_err(dev, "cxadc: Invalid sprintf len: %d\n", len);
	return len;
}

static ssize_t mycxadc_tenxfsc_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	struct cxadc *mycxadc = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 10, &mycxadc->tenxfsc);
	return count;
}

/*
 * show/store for sixdb
 */

static ssize_t mycxadc_sixdb_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cxadc *mycxadc = dev_get_drvdata(dev);
	int len;

	len = sprintf(buf, "%d\n", mycxadc->sixdb);
	if (len <= 0)
		dev_err(dev, "cxadc: Invalid sprintf len: %d\n", len);
	return len;
}

static ssize_t mycxadc_sixdb_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	struct cxadc *mycxadc = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 10, &mycxadc->sixdb);
	return count;
}

/*
 * show/store for crystal
 */

static ssize_t mycxadc_crystal_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cxadc *mycxadc = dev_get_drvdata(dev);
	int len;

	len = sprintf(buf, "%d\n", mycxadc->crystal);
	if (len <= 0)
		dev_err(dev, "cxadc: Invalid sprintf len: %d\n", len);
	return len;
}

static ssize_t mycxadc_crystal_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	struct cxadc *mycxadc = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 10, &mycxadc->crystal);
	return count;
}

/*
 * show/store for center_offset
 */

static ssize_t mycxadc_center_offset_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cxadc *mycxadc = dev_get_drvdata(dev);
	int len;

	len = sprintf(buf, "%d\n", mycxadc->center_offset);
	if (len <= 0)
		dev_err(dev, "cxadc: Invalid sprintf len: %d\n", len);
	return len;
}

static ssize_t mycxadc_center_offset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	struct cxadc *mycxadc = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 10, &mycxadc->center_offset);
	return count;
}

static struct device_attribute dev_attr_latency = {
	.attr = {
		.name = "latency",
		.mode = 0664,
	},
	.show = mycxadc_latency_show,
	.store = mycxadc_latency_store,
};

static struct device_attribute dev_attr_audsel = {
	.attr = {
		.name = "audsel",
		.mode = 0664,
	},
	.show = mycxadc_audsel_show,
	.store = mycxadc_audsel_store,
};

static struct device_attribute dev_attr_vmux = {
	.attr = {
		.name = "vmux",
		.mode = 0664,
	},
	.show = mycxadc_vmux_show,
	.store = mycxadc_vmux_store,
};

static struct device_attribute dev_attr_level = {
	.attr = {
		.name = "level",
		.mode = 0664,
	},
	.show = mycxadc_level_show,
	.store = mycxadc_level_store,
};

static struct device_attribute dev_attr_tenbit = {
	.attr = {
		.name = "tenbit",
		.mode = 0664,
	},
	.show = mycxadc_tenbit_show,
	.store = mycxadc_tenbit_store,
};

static struct device_attribute dev_attr_tenxfsc = {
	.attr = {
		.name = "tenxfsc",
		.mode = 0664,
	},
	.show = mycxadc_tenxfsc_show,
	.store = mycxadc_tenxfsc_store,
};

static struct device_attribute dev_attr_sixdb = {
	.attr = {
		.name = "sixdb",
		.mode = 0664,
	},
	.show = mycxadc_sixdb_show,
	.store = mycxadc_sixdb_store,
};

static struct device_attribute dev_attr_crystal = {
	.attr = {
		.name = "crystal",
		.mode = 0664,
	},
	.show = mycxadc_crystal_show,
	.store = mycxadc_crystal_store,
};

static struct device_attribute dev_attr_center_offset = {
	.attr = {
		.name = "center_offset",
		.mode = 0664,
	},
	.show = mycxadc_center_offset_show,
	.store = mycxadc_center_offset_store,
};

static struct attribute *mycxadc_attrs[] = {
	&dev_attr_latency.attr,
	&dev_attr_audsel.attr,
	&dev_attr_vmux.attr,
	&dev_attr_level.attr,
	&dev_attr_tenbit.attr,
	&dev_attr_tenxfsc.attr,
	&dev_attr_sixdb.attr,
	&dev_attr_crystal.attr,
	&dev_attr_center_offset.attr,
	NULL
};

static struct attribute_group mycxadc_group = {
	.name = "parameters",
	.attrs = mycxadc_attrs,
};

/*
 * end boiler plate
 */

static struct cxadc *cxadcs;
static unsigned int cxcount;
/*
 * linux supports 32 devices per bus, 8 functions per device
 */
#define CXCOUNT_MAX 256

static struct class *cxadc_class;
static int cxadc_major;

#define NUMBER_OF_CLUSTER_BUFFER 8
#define CX_SRAM_BASE	0x180000

#define CDT_BASE			(CX_SRAM_BASE+0x1000)
#define CLUSTER_BUFFER_BASE	(CX_SRAM_BASE+0x4000)
#define RISC_BUFFER_BASE	(CX_SRAM_BASE+0x2000)
#define RISC_INST_QUEUE		(CX_SRAM_BASE+0x800)
#define CHN24_CMDS_BASE		0x180100
#define DMA_BUFFER_SIZE		(256*1024)

#define INTERRUPT_MASK	0x18888

static struct pci_device_id cxadc_pci_tbl[] = {
	{
		.vendor       = 0x14f1,
		.device       = 0x8800,
		.subvendor    = PCI_ANY_ID,
		.subdevice    = PCI_ANY_ID,
	}, {
		/* --- end of list --- */
	}
};

/* turn off all DMA / IRQs */
static void disable_card(struct cxadc *ctd)
{
	/* turn off pci interrupt */
	cx_write(MO_PCI_INTMSK, 0);
	/* turn off interrupt */
	cx_write(MO_VID_INTMSK, 0);
	cx_write(MO_VID_INTSTAT, ~(u32)0);
	/* disable fifo and risc */
	cx_write(MO_VID_DMACNTRL, 0);
	/* disable risc */
	cx_write(MO_DEV_CNTRL2, 0);
}

/*
 * numbuf   - number of buffer
 * buffsize - buffer size in bytes
 * buffptr  - CX sram start addr for buffer
 *            e.g. 0x182000 as in example pg 2-62 of CX23880/1/2/3 datasheet
 * cdtptr   - CX sram start addr for CDT
 *            e.g. 0x181000 as in example pg 2-62 of CX23880/1/2/3 datasheet
 */
static void create_cdt_table(struct cxadc *ctd,
			unsigned int numbuf, unsigned int buffsize,
			unsigned int buffptr, unsigned int cdtptr)
{
	int i;
	unsigned int pp, qq;

	pp = buffptr;
	qq = cdtptr;

	for (i = 0; i < numbuf; i++) {
		writel(pp, ctd->mmio+(qq>>2));
		qq += 4;

		writel(0, ctd->mmio+(qq>>2)); /*not needed but we do it anyway*/
		qq += 4;

		writel(0, ctd->mmio+(qq>>2)); /*not needed but we do it anyway*/
		qq += 4;

		writel(0, ctd->mmio+(qq>>2)); /*not needed but we do it anyway*/
		qq += 4;

		pp += buffsize;
	}

}

static void free_dma_buffer(struct cxadc *ctd)
{
	int i;

	for (i = 0; i < MAX_DMA_PAGE; i++) {
		if (ctd->pgvec_virt[i])
			dma_free_coherent(&ctd->pci->dev, PAGE_SIZE, ctd->pgvec_virt[i], ctd->pgvec_phy[i]);
	}
}

static int alloc_risc_inst_buffer(struct cxadc *ctd)
{
	/* add 1 page for sync instruct and jump */
	ctd->risc_inst_buff_size = (VBI_DMA_BUFF_SIZE/CLUSTER_BUFFER_SIZE)*8+PAGE_SIZE;
	ctd->risc_inst_virt = dma_alloc_coherent(&ctd->pci->dev, ctd->risc_inst_buff_size, &ctd->risc_inst_phy, GFP_KERNEL);
	if (ctd->risc_inst_virt == NULL)
		return -ENOMEM;
	memset(ctd->risc_inst_virt, 0, ctd->risc_inst_buff_size);

	cx_info("risc inst buff allocated at virt 0x%p phy 0x%lx size %u kbytes\n",
		ctd->risc_inst_virt, (unsigned long)ctd->risc_inst_phy, ctd->risc_inst_buff_size / 1024);

	return 0;
}

static void free_risc_inst_buffer(struct cxadc *ctd)
{
	if (ctd->risc_inst_virt != NULL)
		dma_free_coherent(&ctd->pci->dev, ctd->risc_inst_buff_size, ctd->risc_inst_virt, ctd->risc_inst_phy);
}

static int make_risc_instructions(struct cxadc *ctd)
{
	int page, wr;
	unsigned int dma_addr;
	unsigned int *pp = (unsigned int *)ctd->risc_inst_virt;

	/* The RISC program is just a long sequence of WRITEs that fill each DMA page in
	   sequence. It begins with a SYNC and ends with a JUMP back to the first WRITE. */

	*pp++ = RISC_SYNC|RISC_CNT_RESET;

	for (page = 0; page < MAX_DMA_PAGE; page++) {
		dma_addr = ctd->pgvec_phy[page];

		/* Each WRITE is CLUSTER_BUFFER_SIZE bytes so each DMA page requires
		   n = (PAGE_SIZE / CLUSTER_BUFFER_SIZE) WRITEs to fill it. */

		/* Generate n - 1 WRITEs. */
		for (wr = 0; wr < (PAGE_SIZE / CLUSTER_BUFFER_SIZE) - 1; wr++) {
			*pp++ = RISC_WRITE|CLUSTER_BUFFER_SIZE|RISC_SOL|RISC_EOL|RISC_CNT_NONE;
			*pp++ = dma_addr;
			dma_addr += CLUSTER_BUFFER_SIZE;
		}

		/* Generate the final write which may trigger side effects. */
		*pp++ = RISC_WRITE|CLUSTER_BUFFER_SIZE|RISC_SOL|RISC_EOL|
			/* If this is the last DMA page, reset counter, otherwise increment it. */
			(page == (MAX_DMA_PAGE - 1) ? RISC_CNT_RESET : RISC_CNT_INC)|
			/* If we've filled enough pages, trigger IRQ1. */
			((((page + 1) % IRQ_PERIOD_IN_PAGES) == 0) ? RISC_IRQ1 : 0);
		*pp++ = dma_addr;
	}

	*pp++ = RISC_JUMP; /* Jump back to first WRITE (+4 skips the SYNC command.) */
	*pp++ = ctd->risc_inst_phy + 4;

	cx_info("end of risc inst 0x%p total size %lu kbyte\n",
		pp, (unsigned long)((char *)pp - (char *)ctd->risc_inst_virt) / 1024);
	return 0;
}

static int cxadc_char_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct cxadc *ctd = container_of(inode->i_cdev, struct cxadc, cdev);
	unsigned long longtenxfsc, longPLLboth, longPLLint;
	int PLLint, PLLfrac, PLLfin, SConv, rv;

	for (ctd = cxadcs; ctd != NULL; ctd = ctd->next)
		if (MINOR(ctd->cdev.dev) == minor)
			break;
	if (ctd == NULL)
		return -ENODEV;

	mutex_lock(&ctd->lock);
	if (ctd->in_use) {
		mutex_unlock(&ctd->lock);
		return -EBUSY;
	}

	kref_get(&ctd->refcnt);
	file->private_data = ctd;

	ctd->in_use = true;
	mutex_unlock(&ctd->lock);

	/* source select (see datasheet on how to change adc source) */
	ctd->vmux &= 3;/* default vmux=1 */
	/* pal-B */
	cx_write(MO_INPUT_FORMAT, (ctd->vmux<<14)|(1<<13)|0x01|0x10|0x10000);

	/* capture 16 bit or 8 bit raw samples */
	if (ctd->tenbit)
		cx_write(MO_CAPTURE_CTRL, ((1<<6)|(3<<1)|(1<<5)));
	else
		cx_write(MO_CAPTURE_CTRL, ((1<<6)|(3<<1)|(0<<5)));

	/* re-set the level, clock speed, and bit size */

	if (ctd->level < 0)
		ctd->level = 0;
	if (ctd->level > 31)
		ctd->level = 31;
	/* control gain also bit 16 */
	cx_write(MO_AGC_GAIN_ADJ4, (ctd->sixdb<<23)|(0<<22)|(0<<21)|(ctd->level<<16)|(0xff<<8)|(0x0<<0));
	cx_write(MO_AGC_SYNC_TIP3, (0x1e48<<16)|(0xff<<8)|(ctd->center_offset));

	if (ctd->tenxfsc < 10) {
		//old code for old parameter compatibility
		switch (ctd->tenxfsc) {
		case 0:
			/* clock speed equal to crystal speed, unmodified card = 28.6 mhz */
			cx_write(MO_SCONV_REG, 131072); /* set SRC to 8xfsc */
			cx_write(MO_PLL_REG, 0x11000000); /* set PLL to 1:1 */
			break;
		case 1:
			/* clock speed equal to 1.25 x crystal speed, unmodified card = 35.8 mhz */
			cx_write(MO_SCONV_REG, 131072*4/5); /* set SRC to 1.25x/10fsc */
			cx_write(MO_PLL_REG, 0x01400000); /* set PLL to 1.25x/10fsc */
			break;
		case 2:
			/* clock speed equal to ~1.4 x crystal speed, unmodified card = 40 mhz */
			cx_write(MO_SCONV_REG, 131072*0.715909072483);
			cx_write(MO_PLL_REG, 0x0165965A); /* 40000000.1406459 */
			break;
		default:
			/* if someone sets value out of range, default to crystal speed */
			/* clock speed equal to crystal speed, unmodified card = 28.6 mhz */
			cx_write(MO_SCONV_REG, 131072); /* set SRC to 8xfsc */
			cx_write(MO_PLL_REG, 0x11000000); /* set PLL to 1:1 */
		}
	} else {
		if (ctd->tenxfsc < 100)
			ctd->tenxfsc = ctd->tenxfsc * 1000000;  //if number 11-99, conver to 11,000,000 to 99,000,000
		PLLint = ctd->tenxfsc/(ctd->crystal/40);  //always use PLL_PRE of 5 (=64)
		longtenxfsc = (long)ctd->tenxfsc * 1000000;
		longPLLboth = (long)(longtenxfsc/(long)(ctd->crystal/40));
		longPLLint = (long)PLLint * 1000000;
		PLLfrac = ((longPLLboth-longPLLint)*1048576)/1000000;
		PLLfin =  ((PLLint+64)*1048576)+PLLfrac;
		if (PLLfin < 81788928)
			PLLfin = 81788928; // 81788928 lowest possible value
		if (PLLfin > 119537664)
			PLLfin = 119537664 ; //133169152 is highest possible value with PLL_PRE = 5 but above 119537664 may crash
		cx_write(MO_PLL_REG,  PLLfin);
		//cx_write(MO_SCONV_REG, 131072 * (crystal / tenxfsc));
		SConv = (long)(131072 * (long)ctd->crystal) / (long)ctd->tenxfsc;
		cx_write(MO_SCONV_REG, SConv);
	}


	/* capture 16 bit or 8 bit raw samples */
	if (ctd->tenbit)
		cx_write(MO_CAPTURE_CTRL, ((1<<6)|(3<<1)|(1<<5)));
	else
		cx_write(MO_CAPTURE_CTRL, ((1<<6)|(3<<1)|(0<<5)));

	file->private_data = ctd;

	atomic_set(&ctd->lgpcnt, -1);
	cx_write(MO_PCI_INTMSK, 1); /* enable interrupt */

	rv = wait_event_interruptible(ctd->readQ, atomic_read(&ctd->lgpcnt) != -1);
	if (rv) {
		cx_write(MO_PCI_INTMSK, 0);

		mutex_lock(&ctd->lock);
		ctd->in_use = false;
		mutex_unlock(&ctd->lock);

		return rv;
	}

	ctd->initial_page = atomic_read(&ctd->lgpcnt);

	return 0;
}

static int cxadc_char_release(struct inode *inode, struct file *file)
{
	struct cxadc *ctd = file->private_data;

	cx_write(MO_PCI_INTMSK, 0);

	mutex_lock(&ctd->lock);
	ctd->in_use = false;
	mutex_unlock(&ctd->lock);
	return 0;
}

static ssize_t cxadc_char_read(struct file *file, char __user *tgt,
		size_t count, loff_t *offset)
{
	struct cxadc *ctd = file->private_data;
	unsigned int rv = 0;
	unsigned int pnum;
	int gp_cnt;

	pnum = (*offset % VBI_DMA_BUFF_SIZE) / PAGE_SIZE;
	pnum += ctd->initial_page;
	pnum %= MAX_DMA_PAGE;

	gp_cnt = atomic_read(&ctd->lgpcnt);

	if ((pnum == gp_cnt) && (file->f_flags & O_NONBLOCK))
		return rv;

	while (count) {
		while ((count > 0) && (pnum != gp_cnt)) {
			unsigned int len;

			/* handle partial pages for either reason */
			len = (*offset % PAGE_SIZE) ? (PAGE_SIZE - (*offset % PAGE_SIZE)) : PAGE_SIZE;
			if (len > count)
				len = count;

			if (copy_to_user(tgt, ctd->pgvec_virt[pnum] + (*offset % PAGE_SIZE), len))
				return -EFAULT;
			memset(ctd->pgvec_virt[pnum] + (*offset % PAGE_SIZE), 0, len);

			count -= len;
			tgt += len;
			*offset += len;
			rv += len;

			pnum = (*offset % VBI_DMA_BUFF_SIZE) / PAGE_SIZE;
			pnum += ctd->initial_page;
			pnum %= MAX_DMA_PAGE;
		}
		/*
		 * adding code to allow level change during read, have tested, works with CAV capture
		 * script i have been working on
		 */
		if (ctd->level < 0)
			ctd->level = 0;
		if (ctd->level > 31)
			ctd->level = 31;
		cx_write(MO_AGC_GAIN_ADJ4, (ctd->sixdb<<23)|(0<<22)|(0<<21)|(ctd->level<<16)|(0xff<<8)|(0x0<<0));
		cx_write(MO_AGC_SYNC_TIP3, (0x1e48<<16)|(0xff<<8)|(ctd->center_offset));

		if (count) {
			int rv2;

			if (file->f_flags & O_NONBLOCK)
				return rv;

			rv2 = wait_event_interruptible(ctd->readQ, atomic_read(&ctd->lgpcnt) != gp_cnt);
			if (rv2) {
				return rv ? rv : rv2;
			}

			gp_cnt = atomic_read(&ctd->lgpcnt);
		}
	};

	return rv;
}

static long cxadc_char_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct cxadc *ctd = file->private_data;
	int ret = 0;

	if (cmd == 0x12345670) {
		int gain = arg;

		if (gain < 0)
			gain = 0;
		if (gain > 31)
			gain = 31;

		/* control gain also bit 16 */
		cx_write(MO_AGC_GAIN_ADJ4, (ctd->sixdb<<23)|(0<<22)|(0<<21)|(gain<<16)|(0xff<<8)|(0x0<<0));
	}

	return ret;
}

static const struct file_operations cxadc_char_fops = {
	.owner    = THIS_MODULE,
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
	.llseek   = no_llseek,
#endif
	.unlocked_ioctl = cxadc_char_ioctl,
	.open     = cxadc_char_open,
	.release  = cxadc_char_release,
	.read     = cxadc_char_read,
};

static irqreturn_t cxadc_irq(int irq, void *dev_id)
{
	struct cxadc *ctd = dev_id;
	u32 allstat = cx_read(MO_VID_INTSTAT);
	u32 stat  = cx_read(MO_VID_INTMSK);
	u32 astat = stat & allstat;
	u32 ostat = astat;

	if (ostat != 8 && allstat != 0 && ostat != 0)
		cx_info("interrupt stat 0x%x masked 0x%x\n", allstat, ostat);

	if (!astat)
		return IRQ_RETVAL(0); /* if no interrupt bit set we return */

	if (astat & 0x8) {
		int gp_cnt = cx_read(MO_VBI_GPCNT);
		/* NB: MO_VBI_GPCNT is not guaranteed to be in-sync with resident pages.
		   i.e. we can get gpcnt == 1 but the first page may not yet have been transferred
		   to main memory. on the other hand, if an interrupt has occurred, we are guaranteed to have the page
		   in main memory. so we only retrieve MO_VBI_GPCNT after an interrupt has occurred and then round
		   it down to the last page that we know should have triggered an interrupt. */
		gp_cnt &= ~(IRQ_PERIOD_IN_PAGES - 1);
		atomic_set(&ctd->lgpcnt, gp_cnt);
		wake_up_interruptible(&ctd->readQ);
	}
	cx_write(MO_VID_INTSTAT, ostat);

	return IRQ_RETVAL(1);
}

static int cxadc_probe(struct pci_dev *pci_dev,
			const struct pci_device_id *pci_id)
{
	u32 i, intstat;
	struct cxadc *ctd;
	unsigned char revision, lat;
	int rc;
	unsigned int total_size;
	unsigned long longtenxfsc, longPLLboth, longPLLint;
	int PLLint, PLLfrac, PLLfin, SConv;

	if (pci_enable_device(pci_dev)) {
		dev_err(&pci_dev->dev, "cxadc: enable device failed\n");
		return -EIO;
	}

	if (!request_mem_region(pci_resource_start(pci_dev, 0),
				pci_resource_len(pci_dev, 0),
				"cxadc")) {
		dev_err(&pci_dev->dev, "cxadc: request memory region failed\n");
		return -EBUSY;
	}

	ctd = kmalloc(sizeof(*ctd), GFP_KERNEL);
	if (!ctd) {
		rc = -ENOMEM;
		dev_err(&pci_dev->dev, "cxadc: kmalloc failed\n");
		goto fail0;
	}
	memset(ctd, 0, sizeof(*ctd));

	if (cxcount >= CXCOUNT_MAX) {
		dev_err(&pci_dev->dev, "cxadc: only 256 cards are supported\n");
		return -EBUSY;
	}

	ctd->pci = pci_dev;
	ctd->irq = pci_dev->irq;

	/* set default device attributes */
	ctd->latency = default_latency;
	ctd->audsel = default_audsel;
	ctd->vmux = default_vmux;
	ctd->level = default_level;
	ctd->tenbit = default_tenbit;
	ctd->tenxfsc = default_tenxfsc;
	ctd->sixdb = default_sixdb;
	ctd->crystal = default_crystal;
	ctd->center_offset = default_center_offset;

	/*
	 * creates our device attributs in
	 * /sys/class/cxadc/cxadc[0-7]/device/parameters
	 */

	if (sysfs_create_group(&pci_dev->dev.kobj, &mycxadc_group)) {
		cx_err("cannot create sysfs attributes\n");
		/* something is very wrong if we can't create sysfs files */
		rc = -ENOMEM;
		goto fail1;
	}

	/* We can use cx_err/cx_info from here, now ctd has been set up. */

	if (alloc_risc_inst_buffer(ctd)) {
		cx_err("cannot alloc risc buffer\n");
		rc = -ENOMEM;
		goto fail1s;
	}

	for (i = 0; i < (MAX_DMA_PAGE+1); i++) {
		ctd->pgvec_virt[i] = 0;
		ctd->pgvec_phy[i] = 0;
	}

	total_size = 0;

	for (i = 0; i < MAX_DMA_PAGE; i++) {
		dma_addr_t dma_handle;

		ctd->pgvec_virt[i] = dma_zalloc_coherent(&ctd->pci->dev, PAGE_SIZE,
				&dma_handle, GFP_KERNEL);

		if (ctd->pgvec_virt[i] != 0) {
			ctd->pgvec_phy[i] = dma_handle;
			total_size += PAGE_SIZE;
		} else {
			cx_err("alloc dma buffer failed. index = %u\n", i);

			rc = -ENOMEM;
			goto fail1x;
		}

	}

	cx_info("total DMA size allocated = %u kb\n", total_size / 1024);

	make_risc_instructions(ctd);

	ctd->mem = pci_resource_start(pci_dev, 0);

	ctd->mmio = ioremap(pci_resource_start(pci_dev, 0),
			pci_resource_len(pci_dev, 0));

	cx_info("MEM :%x MMIO :%p\n", ctd->mem, ctd->mmio);

	ctd->in_use = false;
	mutex_init(&ctd->lock);
	kref_init(&ctd->refcnt);

	init_waitqueue_head(&ctd->readQ);

	if (ctd->latency != -1) {
		cx_info("setting pci latency timer to %d\n", ctd->latency);
		pci_write_config_byte(pci_dev, PCI_LATENCY_TIMER, ctd->latency);
	} else {
		pci_write_config_byte(pci_dev, PCI_LATENCY_TIMER, 255);
	}

	pci_read_config_byte(pci_dev, PCI_CLASS_REVISION, &revision);
	pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER, &lat);

	cx_info("dev 0x%X (rev %d) at %02x:%02x.%x, ",
		pci_dev->device, revision, pci_dev->bus->number,
		PCI_SLOT(pci_dev->devfn), PCI_FUNC(pci_dev->devfn));

	cx_info("irq: %d, latency: %d, mmio: 0x%x\n",
		ctd->irq, lat, ctd->mem);

	/* init hw */
	pci_set_master(pci_dev);
	disable_card(ctd);

	/* we use 16kbytes of FIFO buffer */
	create_cdt_table(ctd, NUMBER_OF_CLUSTER_BUFFER, CLUSTER_BUFFER_SIZE,
		CLUSTER_BUFFER_BASE, CDT_BASE);

	/* size of one buffer in qword -1 */
	cx_write(MO_DMA24_CNT1, (CLUSTER_BUFFER_SIZE/8-1));

	/* ptr to cdt */
	cx_write(MO_DMA24_PTR2, CDT_BASE);
	/* size of cdt in qword */
	cx_write(MO_DMA24_CNT2, 2*NUMBER_OF_CLUSTER_BUFFER);

	/* clear interrupt */
	intstat = cx_read(MO_VID_INTSTAT);
	cx_write(MO_VID_INTSTAT, intstat);

	cx_write(CHN24_CMDS_BASE, ctd->risc_inst_phy); /* working */
	cx_write(CHN24_CMDS_BASE+4, CDT_BASE);
	cx_write(CHN24_CMDS_BASE+8, 2*NUMBER_OF_CLUSTER_BUFFER);
	cx_write(CHN24_CMDS_BASE+12, RISC_INST_QUEUE);

	cx_write(CHN24_CMDS_BASE+16, 0x40);

	/* source select (see datasheet on how to change adc source) */
	ctd->vmux &= 3;/* default vmux=1 */
	/* pal-B */
	cx_write(MO_INPUT_FORMAT, (ctd->vmux<<14)|(1<<13)|0x01|0x10|0x10000);
	cx_write(MO_OUTPUT_FORMAT, 0x0f); /* allow full range */

	cx_write(MO_CONTR_BRIGHT, 0xff00);

	/* vbi lenght CLUSTER_BUFFER_SIZE/2  work */

	/*
	 * no of byte transferred from peripehral to fifo
	 * if fifo buffer < this, it will still transfer this no of byte
	 * must be multiple of 8, if not go haywire?
	 */
	cx_write(MO_VBI_PACKET, (((CLUSTER_BUFFER_SIZE)<<17)|(2<<11)));

	/* raw mode & byte swap <<8 (3<<8=swap) */
	cx_write(MO_COLOR_CTRL, ((0xe)|(0xe<<4)|(0<<8)));

	/* capture 16 bit or 8 bit raw samples */
	if (ctd->tenbit)
		cx_write(MO_CAPTURE_CTRL, ((1<<6)|(3<<1)|(1<<5)));
	else
		cx_write(MO_CAPTURE_CTRL, ((1<<6)|(3<<1)|(0<<5)));

	/* power down audio and chroma DAC+ADC */
	cx_write(MO_AFECFG_IO, 0x12);

	/* run risc */
	cx_write(MO_DEV_CNTRL2, 1<<5);
	/* enable fifo and risc */
	cx_write(MO_VID_DMACNTRL, ((1<<7)|(1<<3)));

	rc = request_irq(ctd->irq, cxadc_irq, IRQF_SHARED, "cxadc", ctd);
	if (rc < 0) {
		cx_err("can't request irq (rc=%d)\n", rc);
		goto fail1x;
	}

	/* register devices */
	cdev_init(&ctd->cdev, &cxadc_char_fops);
	if (cdev_add(&ctd->cdev, MKDEV(cxadc_major, cxcount), 1)) {
		cx_err("failed to register device\n");
		rc = -EIO;
		goto fail2;
	}

	if (IS_ERR(device_create(cxadc_class, &pci_dev->dev,
			 MKDEV(cxadc_major, cxcount), NULL,
			 "cxadc%u", cxcount)))
		dev_err(&pci_dev->dev, "can't create device\n");

	cx_info("char dev register ok\n");

	if (ctd->tenxfsc < 10) {
		//old code for old parameter compatibility
		switch (ctd->tenxfsc) {
		case 0:
			/* clock speed equal to crystal speed, unmodified card = 28.6 mhz */
			cx_write(MO_SCONV_REG, 131072); /* set SRC to 8xfsc */
			cx_write(MO_PLL_REG, 0x11000000); /* set PLL to 1:1 */
			break;
		case 1:
			/* clock speed equal to 1.25 x crystal speed, unmodified card = 35.8 mhz */
			cx_write(MO_SCONV_REG, 131072*4/5); /* set SRC to 1.25x/10fsc */
			cx_write(MO_PLL_REG, 0x01400000); /* set PLL to 1.25x/10fsc */
			break;
		case 2:
			/* clock speed equal to ~1.4 x crystal speed, unmodified card = 40 mhz */
			cx_write(MO_SCONV_REG, 131072*0.715909072483);
			cx_write(MO_PLL_REG, 0x0165965A); /* 40000000.1406459 */
			break;
		default:
			/* if someone sets value out of range, default to crystal speed */
			/* clock speed equal to crystal speed, unmodified card = 28.6 mhz */
			cx_write(MO_SCONV_REG, 131072); /* set SRC to 8xfsc */
			cx_write(MO_PLL_REG, 0x11000000); /* set PLL to 1:1 */
		}
	} else {
		if (ctd->tenxfsc < 100)
			ctd->tenxfsc = ctd->tenxfsc * 1000000;  //if number 11-99, conver to 11,000,000 to 99,000,000
		PLLint = ctd->tenxfsc/(ctd->crystal/40);  //always use PLL_PRE of 5 (=64)
		longtenxfsc = (long)ctd->tenxfsc * 1000000;
		longPLLboth = (long)(longtenxfsc/(long)(ctd->crystal/40));
		longPLLint = (long)PLLint * 1000000;
		PLLfrac = ((longPLLboth-longPLLint)*1048576)/1000000;
		PLLfin =  ((PLLint+64)*1048576)+PLLfrac;
		if (PLLfin < 81788928)
			PLLfin = 81788928; // 81788928 lowest possible value
		if (PLLfin > 119537664)
			PLLfin = 119537664 ; //133169152 is highest possible value with PLL_PRE = 5 but above 119537664 may crash
		cx_write(MO_PLL_REG,  PLLfin);
		SConv = (long)(131072 * (long)ctd->crystal) / (long)ctd->tenxfsc;
		cx_write(MO_SCONV_REG, SConv);
	}



	/* set vbi agc */
	cx_write(MO_AGC_SYNC_SLICER, 0x0);

	if (ctd->level < 0)
		ctd->level = 0;
	if (ctd->level > 31)
		ctd->level = 31;

	cx_write(MO_AGC_BACK_VBI, (0<<27)|(0<<26)|(1<<25)|(0x100<<16)|(0xfff<<0));
	/* control gain also bit 16 */
	cx_write(MO_AGC_GAIN_ADJ4, (ctd->sixdb<<23)|(0<<22)|(0<<21)|(ctd->level<<16)|(0xff<<8)|(0x0<<0));
	/* for 'cooked' composite */
	cx_write(MO_AGC_SYNC_TIP1, (0x1c0<<17)|(0x0<<9)|(0<<7)|(0xf<<0));
	cx_write(MO_AGC_SYNC_TIP2, (0x20<<17)|(0x0<<9)|(0<<7)|(0xf<<0));
	cx_write(MO_AGC_SYNC_TIP3, (0x1e48<<16)|(0xff<<8)|(ctd->center_offset));
	cx_write(MO_AGC_GAIN_ADJ1, (0xe0<<17)|(0xe<<9)|(0x0<<7)|(0x7<<0));
	cx_write(MO_AGC_GAIN_ADJ2, (0x20<<17)|(2<<7)|0x0f);
	/* set gain of agc but not offset */
	cx_write(MO_AGC_GAIN_ADJ3, (0x28<<16)|(0x28<<8)|(0x50<<0));

	if (ctd->audsel != -1) {
		/*
		 * Pixelview PlayTVPro Ultracard specific
		 * select which output is redirected to audio output jack
		 * GPIO bit 3 is to enable 4052 , bit 0-1 4052's AB
		 */
		cx_write(MO_GP3_IO, 1<<25); /* use as 24 bit GPIO/GPOE */
		cx_write(MO_GP1_IO, 0x0b);
		cx_write(MO_GP0_IO, ctd->audsel&3);
		cx_info("audsel = %d\n", ctd->audsel&3);
	}

	/* i2c sda/scl set to high and use software control */
	cx_write(MO_I2C, 3);

	/* hook into linked list */
	ctd->next = cxadcs;
	cxadcs = ctd;
	cxcount++;

	pci_set_drvdata(pci_dev, ctd);
	cx_write(MO_VID_INTMSK, INTERRUPT_MASK);

	return 0;

fail2:
	free_irq(ctd->irq, ctd);
fail1x:
	free_dma_buffer(ctd);
	free_risc_inst_buffer(ctd);
fail1s:
	sysfs_remove_group(&pci_dev->dev.kobj, &mycxadc_group);
fail1:
	kfree(ctd);
fail0:
	release_mem_region(pci_resource_start(pci_dev, 0),
			   pci_resource_len(pci_dev, 0));
	return rc;
}

static void agc_reset(struct cxadc *ctd)
{
	/*
	 * Set AGC registers back to their default values, as per the CX23833
	 * datasheet. This is in case you want to load cx8800 after unloading
	 * cxadc; cx8800 doesn't know about all of these.
	 */
	cx_write(MO_AGC_BACK_VBI,
		(0xe0<<16)|0x555);
	cx_write(MO_AGC_SYNC_SLICER,
		(1<<21)|(1<<20)|(1<<19)|(0x4<<16)|(0x60<<8)|0x1c);
	cx_write(MO_AGC_SYNC_TIP1,
		(0x1c0<<17)|0x0f);
	cx_write(MO_AGC_SYNC_TIP2,
		(0x20<<17)|(1<<7)|0x3f);
	cx_write(MO_AGC_SYNC_TIP3,
		(0x1e48<<16)|(0xe0<<8)|0x40);
	cx_write(MO_AGC_GAIN_ADJ1,
		(0xe0<<17)|(0x0e<<9)|0x07);
	cx_write(MO_AGC_GAIN_ADJ2,
		(0x20<<17)|(2<<7)|0x0f);
	cx_write(MO_AGC_GAIN_ADJ3,
		(0x28<<16)|(0x38<<8)|0xc0);
	cx_write(MO_AGC_GAIN_ADJ4,
		(1<<22)|(1<<21)|(0xa<<16)|(0x2c<<8)|0x34);
}

static void cxadc_remove(struct pci_dev *pci_dev)
{
	struct cxadc *ctd = pci_get_drvdata(pci_dev);
	/* struct cxadc *walk; */

	disable_card(ctd);

	/* removes our sysfs files */
	sysfs_remove_group(&pci_dev->dev.kobj, &mycxadc_group);
	agc_reset(ctd);
	device_destroy(cxadc_class, MKDEV(cxadc_major, ctd->cdev.dev));
	cdev_del(&ctd->cdev);

	/* free resources */
	free_risc_inst_buffer(ctd);
	free_irq(ctd->irq, ctd);
	free_dma_buffer(ctd);
	iounmap(ctd->mmio);
	release_mem_region(pci_resource_start(pci_dev, 0),
			   pci_resource_len(pci_dev, 0));

	/* remove from linked list */
	/* CAUSES KERNEL PANIC
	 * if (ctd == cxadcs) {
	 *	cxadcs = NULL;
	 * } else {
	 *	for (walk = cxadcs; walk->next != ctd; walk = walk->next)
	 *		;
	 *	walk->next = ctd->next;
	 * }
	 */

	cxcount--;

	cx_info("reset drv data\n");
	pci_set_drvdata(pci_dev, NULL);
	cx_info("reset drv ok\n");
	kfree(ctd);
	pci_disable_device(pci_dev);
}

static int cxadc_suspend(struct pci_dev *pci_dev, pm_message_t state)
{
	struct cxadc *ctd = pci_get_drvdata(pci_dev);

	disable_card(ctd);
	agc_reset(ctd);
	pci_save_state(pci_dev);
	pci_set_power_state(pci_dev, pci_choose_state(pci_dev, state));
	pci_disable_device(pci_dev);
	return 0;
}

static int cxadc_resume(struct pci_dev *pci_dev)
{
/*
 * I have no idea what state this card is in after resume
 * so re-init the hardware and re-sync our settings
 */
	struct cxadc *ctd = pci_get_drvdata(pci_dev);
	unsigned long longtenxfsc, longPLLboth, longPLLint;
	int PLLint, PLLfrac, PLLfin, SConv, intstat, ret;

	ret = pci_enable_device(pci_dev);
	pci_set_power_state(pci_dev, PCI_D0);
	pci_restore_state(pci_dev);
	/* init hw */
	pci_set_master(pci_dev);
	disable_card(ctd);

	/* we use 16kbytes of FIFO buffer */
	create_cdt_table(ctd, NUMBER_OF_CLUSTER_BUFFER, CLUSTER_BUFFER_SIZE,
			CLUSTER_BUFFER_BASE, CDT_BASE);
	/* size of one buffer in qword -1 */
	cx_write(MO_DMA24_CNT1, (CLUSTER_BUFFER_SIZE/8-1));

	/* ptr to cdt */
	cx_write(MO_DMA24_PTR2, CDT_BASE);
	/* size of cdt in qword */
	cx_write(MO_DMA24_CNT2, 2*NUMBER_OF_CLUSTER_BUFFER);

	/* clear interrupt */
	intstat = cx_read(MO_VID_INTSTAT);
	cx_write(MO_VID_INTSTAT, intstat);

	cx_write(CHN24_CMDS_BASE, ctd->risc_inst_phy); /* working */
	cx_write(CHN24_CMDS_BASE+4, CDT_BASE);
	cx_write(CHN24_CMDS_BASE+8, 2*NUMBER_OF_CLUSTER_BUFFER);
	cx_write(CHN24_CMDS_BASE+12, RISC_INST_QUEUE);

	cx_write(CHN24_CMDS_BASE+16, 0x40);

	/* source select (see datasheet on how to change adc source) */
	ctd->vmux &= 3;/* default vmux=1 */
	/* pal-B */
	cx_write(MO_INPUT_FORMAT, (ctd->vmux<<14)|(1<<13)|0x01|0x10|0x10000);
	cx_write(MO_OUTPUT_FORMAT, 0x0f); /* allow full range */

	cx_write(MO_CONTR_BRIGHT, 0xff00);

	/* vbi lenght CLUSTER_BUFFER_SIZE/2  work */

	/*
	 * no of byte transferred from peripehral to fifo
	 * if fifo buffer < this, it will still transfer this no of byte
	 * must be multiple of 8, if not go haywire?
	 */
	cx_write(MO_VBI_PACKET, (((CLUSTER_BUFFER_SIZE)<<17)|(2<<11)));

	/* raw mode & byte swap <<8 (3<<8=swap) */
	cx_write(MO_COLOR_CTRL, ((0xe)|(0xe<<4)|(0<<8)));

	/* capture 16 bit or 8 bit raw samples */
	if (ctd->tenbit)
		cx_write(MO_CAPTURE_CTRL, ((1<<6)|(3<<1)|(1<<5)));
	else
		cx_write(MO_CAPTURE_CTRL, ((1<<6)|(3<<1)|(0<<5)));

	/* power down audio and chroma DAC+ADC */
	cx_write(MO_AFECFG_IO, 0x12);

	/* run risc */
	cx_write(MO_DEV_CNTRL2, 1<<5);
	/* enable fifo and risc */
	cx_write(MO_VID_DMACNTRL, ((1<<7)|(1<<3)));
	if (ctd->tenxfsc < 10) {
	//old code for old parameter compatibility
		switch (ctd->tenxfsc) {
		case 0:
			/* clock speed equal to crystal speed, unmodified card = 28.6 mhz */
			cx_write(MO_SCONV_REG, 131072); /* set SRC to 8xfsc */
			cx_write(MO_PLL_REG, 0x11000000); /* set PLL to 1:1 */
			break;
		case 1:
			/* clock speed equal to 1.25 x crystal speed, unmodified card = 35.8 mhz */
			cx_write(MO_SCONV_REG, 131072*4/5); /* set SRC to 1.25x/10fsc */
			cx_write(MO_PLL_REG, 0x01400000); /* set PLL to 1.25x/10fsc */
			break;
		case 2:
			/* clock speed equal to ~1.4 x crystal speed, unmodified card = 40 mhz */
			cx_write(MO_SCONV_REG, 131072*0.715909072483);
			cx_write(MO_PLL_REG, 0x0165965A); /* 40000000.1406459 */
			break;
		default:
			/* if someone sets value out of range, default to crystal speed */
			/* clock speed equal to crystal speed, unmodified card = 28.6 mhz */
			cx_write(MO_SCONV_REG, 131072); /* set SRC to 8xfsc */
			cx_write(MO_PLL_REG, 0x11000000); /* set PLL to 1:1 */
		}
	} else {
		if (ctd->tenxfsc < 100)
			ctd->tenxfsc = ctd->tenxfsc * 1000000;  //if number 11-99, conver to 11,000,000 to 99,000,000
		PLLint = ctd->tenxfsc/(ctd->crystal/40);  //always use PLL_PRE of 5 (=64)
		longtenxfsc = (long)ctd->tenxfsc * 1000000;
		longPLLboth = (long)(longtenxfsc/(long)(ctd->crystal/40));
		longPLLint = (long)PLLint * 1000000;
		PLLfrac = ((longPLLboth-longPLLint)*1048576)/1000000;
		PLLfin =  ((PLLint+64)*1048576)+PLLfrac;
		if (PLLfin < 81788928)
			PLLfin = 81788928; // 81788928 lowest possible value
		if (PLLfin > 119537664)
			PLLfin = 119537664 ; //133169152 is highest possible value with PLL_PRE = 5 but above 119537664 may crash
		cx_write(MO_PLL_REG,  PLLfin);
		SConv = (long)(131072 * (long)ctd->crystal) / (long)ctd->tenxfsc;
		cx_write(MO_SCONV_REG, SConv);
	}

	/* set vbi agc */
	cx_write(MO_AGC_SYNC_SLICER, 0x0);

	if (ctd->level < 0)
		ctd->level = 0;
	if (ctd->level > 31)
		ctd->level = 31;

	cx_write(MO_AGC_BACK_VBI, (0<<27)|(0<<26)|(1<<25)|(0x100<<16)|(0xfff<<0));
	/* control gain also bit 16 */
	cx_write(MO_AGC_GAIN_ADJ4, (ctd->sixdb<<23)|(0<<22)|(0<<21)|(ctd->level<<16)|(0xff<<8)|(0x0<<0));
	/* for 'cooked' composite */
	cx_write(MO_AGC_SYNC_TIP1, (0x1c0<<17)|(0x0<<9)|(0<<7)|(0xf<<0));
	cx_write(MO_AGC_SYNC_TIP2, (0x20<<17)|(0x0<<9)|(0<<7)|(0xf<<0));
	cx_write(MO_AGC_SYNC_TIP3, (0x1e48<<16)|(0xff<<8)|(ctd->center_offset));
	cx_write(MO_AGC_GAIN_ADJ1, (0xe0<<17)|(0xe<<9)|(0x0<<7)|(0x7<<0));
	cx_write(MO_AGC_GAIN_ADJ2, (0x20<<17)|(2<<7)|0x0f);
	/* set gain of agc but not offset */
	cx_write(MO_AGC_GAIN_ADJ3, (0x28<<16)|(0x28<<8)|(0x50<<0));

	if (ctd->audsel != -1) {
		/*
		 * Pixelview PlayTVPro Ultracard specific
		 * select which output is redirected to audio output jack
		 * GPIO bit 3 is to enable 4052 , bit 0-1 4052's AB
		 */
		cx_write(MO_GP3_IO, 1<<25); /* use as 24 bit GPIO/GPOE */
		cx_write(MO_GP1_IO, 0x0b);
		cx_write(MO_GP0_IO, ctd->audsel&3);
	}

	/* i2c sda/scl set to high and use software control */
	cx_write(MO_I2C, 3);

	ret = request_irq(ctd->irq, cxadc_irq, IRQF_SHARED, "cxadc", ctd);
	cx_write(MO_VID_INTMSK, INTERRUPT_MASK);
	return 0;
}

MODULE_DEVICE_TABLE(pci, cxadc_pci_tbl);

static struct pci_driver cxadc_pci_driver = {
	.name     = "cxadc",
	.id_table = cxadc_pci_tbl,
	.probe    = cxadc_probe,
	.remove   = cxadc_remove,
	.shutdown = cxadc_remove,
	.suspend  = cxadc_suspend,
	.resume   = cxadc_resume,
};

static int __init cxadc_init_module(void)
{
	int retval;
	dev_t dev;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	cxadc_class = class_create("cxadc");
#else
	cxadc_class = class_create(THIS_MODULE, "cxadc");
#endif
	if (IS_ERR(cxadc_class)) {
		retval = PTR_ERR(cxadc_class);
		printk(KERN_ERR "cxadc: can't register cxadc class\n");
		goto err;
	}

	retval = alloc_chrdev_region(&dev, 0, CXCOUNT_MAX, "cxadc");
	if (retval) {
		printk(KERN_ERR "cxadc: can't register character device\n");
		goto err_class;
	}
	cxadc_major = MAJOR(dev);

	retval = pci_register_driver(&cxadc_pci_driver);
	if (retval) {
		printk(KERN_ERR "cxadc: can't register pci driver\n");
		goto err_unchr;
	}

	printk(KERN_INFO "cxadc driver loaded\n");

	return 0;

err_unchr:
	unregister_chrdev_region(dev, CXCOUNT_MAX);
err_class:
	class_destroy(cxadc_class);
err:
	return retval;
}

static void __exit cxadc_cleanup_module(void)
{
	pci_unregister_driver(&cxadc_pci_driver);

	unregister_chrdev_region(MKDEV(cxadc_major, 0), CXCOUNT_MAX);

	class_destroy(cxadc_class);
}

module_init(cxadc_init_module);
module_exit(cxadc_cleanup_module);

MODULE_DESCRIPTION("cx2388xx adc driver");
MODULE_AUTHOR("Hew How Chee");
MODULE_LICENSE("GPL");

