// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cxadc - CX2388x ADC DMA driver for Linux, version 0.5
 *
 * Copyright (C) 2005-2007 Hew How Chee <how_chee@yahoo.com>
 * Copyright (C) 2013-2015 Chad Page <Chad.Page@gmail.com>
 * Copyright (C) 2019-2022 Adam Sampson <ats@offog.org>
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

static int latency = -1;
static int audsel = -1;
static int vmux = 2;
static int level = 16;
static int tenbit = 0;
static int tenxfsc = 0;
static int sixdb = 1;
static int crystal = 28636363;
static int center_offset = 8;

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

/*
 * 1 sync instruction (first page only) followed by 510 write instruction
 * and finally a 1 jmp instruction to next page/reloop
 * this will give 4 + 510*8 +  8 = 4092 bytes, which fits into a page
 */
#define WRITE_INST_PER_PAGE 510

/*
 * number of write per page must be even
 * each write risc instruction is 8 byte
 * and the end , there is a jmp to next page which is 8 byte
 * giving a total of 510 * 8 + 8 = 4088
 */
#define NUMBER_OF_WRITE_PER_PAGE 510

#define CLUSTER_BUFFER_SIZE 2048

#define NUMBER_OF_WRITE_NEEDED (VBI_DMA_BUFF_SIZE/CLUSTER_BUFFER_SIZE)

/*
 * +1 page for additional one write page if
 *   NUMBER_OF_WRITE_NEEDED/NUMBER_OF_WRITE_PER_PAGE is not integer
 * +1 page for first sync and jmp instruction
 *   (we used 12 bytes of 4 kbytes only)
 */
#define NUMBER_OF_RISC_PAGE ((NUMBER_OF_WRITE_NEEDED/NUMBER_OF_WRITE_PER_PAGE)+1+1)

struct risc_page {
	struct risc_page *next;
	char buffer[PAGE_SIZE - sizeof(struct risc_page *)];
};

struct cxadc {
	/* linked list */
	struct cxadc *next;

	/* device info */
	struct cdev cdev;
	struct pci_dev *pci;
	unsigned int   irq;
	unsigned int  mem;
	unsigned int  *mmio;

	/* locking */
	bool in_use;
	struct mutex lock;

	unsigned int    risc_inst_buff_size;
	unsigned int	*risc_inst_virt;
	dma_addr_t	risc_inst_phy;

	wait_queue_head_t readQ;

	void *pgvec_virt[MAX_DMA_PAGE+1];
	dma_addr_t pgvec_phy[MAX_DMA_PAGE+1];

	int newpage;
	int initial_page;
};

static struct cxadc *cxadcs;
static unsigned int cxcount;
#define CXCOUNT_MAX 1

static struct class *cxadc_class;
static int cxadc_major;

#define NUMBER_OF_CLUSTER_BUFFER 8
#define CX_SRAM_BASE 0x180000

#define CDT_BASE		(CX_SRAM_BASE+0x1000)
#define CLUSTER_BUFFER_BASE	(CX_SRAM_BASE+0x4000)
#define RISC_BUFFER_BASE        (CX_SRAM_BASE+0x2000)
#define RISC_INST_QUEUE		(CX_SRAM_BASE+0x800)
#define CHN24_CMDS_BASE	0x180100
#define DMA_BUFFER_SIZE (256*1024)

#define INTERRUPT_MASK 0x18888

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
		if (ctd->pgvec_virt[i]) {
			dma_free_coherent(&ctd->pci->dev, 4096,
					  ctd->pgvec_virt[i],
					  ctd->pgvec_phy[i]);
		}
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
	int i;
	int irqt;
	unsigned int loop_addr;
	unsigned int dma_addr;
	unsigned int *pp = (unsigned int *)ctd->risc_inst_virt;

	loop_addr = ctd->risc_inst_phy+4;

	*pp++ = RISC_SYNC|(3<<16);

	irqt = 0;
	for (i = 0; i < MAX_DMA_PAGE; i++) {
		irqt++;
		irqt &= 0x1ff;
		*pp++ = RISC_WRITE|CLUSTER_BUFFER_SIZE|(3<<26)|(0<<16);

		dma_addr = ctd->pgvec_phy[i];
		*pp++ = dma_addr;

		if (i != MAX_DMA_PAGE - 1) {
			*pp++ = RISC_WRITE|CLUSTER_BUFFER_SIZE|(((irqt == 0) ? 1 : 0)<<24)|(3<<26)|(1<<16);
			*pp++ = dma_addr+CLUSTER_BUFFER_SIZE;
		} else {
			/* reset cnt to 0 */
			*pp++ = RISC_WRITE|CLUSTER_BUFFER_SIZE|(((irqt == 0) ? 1 : 0)<<24)|(3<<26)|(3<<16);
			*pp++ = dma_addr+CLUSTER_BUFFER_SIZE;
		}
	}

	/* 1<<24 = irq , 11<<16 = cnt */
	*pp++ = RISC_JUMP|(0<<24)|(0<<16); /* interrupt and increment counter */
	*pp++ = loop_addr;

	cx_info("end of risc inst 0x%p total size %lu kbyte\n",
		pp, (unsigned long)((char *)pp - (char *)ctd->risc_inst_virt) / 1024);
	return 0;
}

static int cxadc_char_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct cxadc *ctd;
        unsigned long longtenxfsc, longPLLboth, longPLLint;
        int PLLint, PLLfrac, PLLfin, SConv;
  
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
	ctd->in_use = true;
	mutex_unlock(&ctd->lock);

	/* source select (see datasheet on how to change adc source) */
	vmux &= 3;/* default vmux=1 */
	/* pal-B */
	cx_write(MO_INPUT_FORMAT, (vmux<<14)|(1<<13)|0x01|0x10|0x10000);

	/* capture 16 bit or 8 bit raw samples */
	if (tenbit)
		cx_write(MO_CAPTURE_CTRL, ((1<<6)|(3<<1)|(1<<5)));
	else
		cx_write(MO_CAPTURE_CTRL, ((1<<6)|(3<<1)|(0<<5)));

	/* re-set the level, clock speed, and bit size */

	if (level < 0)
		level = 0;
	if (level > 31)
		level = 31;
	/* control gain also bit 16 */
	cx_write(MO_AGC_GAIN_ADJ4, (sixdb<<23)|(0<<22)|(0<<21)|(level<<16)|(0xff<<8)|(0x0<<0));
	cx_write(MO_AGC_SYNC_TIP3, (0x1e48<<16)|(0xff<<8)|(center_offset));

       if (tenxfsc < 10) {
        //old code for old parameter compatibility
        switch (tenxfsc) {
                case 0 :
                        /* clock speed equal to crystal speed, unmodified card = 28.6 mhz */
                        cx_write(MO_SCONV_REG, 131072); /* set SRC to 8xfsc */
                        cx_write(MO_PLL_REG, 0x11000000); /* set PLL to 1:1 */
                        break;
                case 1 :
                        /* clock speed equal to 1.25 x crystal speed, unmodified card = 35.8 mhz */
                        cx_write(MO_SCONV_REG, 131072*4/5); /* set SRC to 1.25x/10fsc */
                        cx_write(MO_PLL_REG, 0x01400000); /* set PLL to 1.25x/10fsc */
                        break;
                case 2 :
                        /* clock speed equal to ~1.4 x crystal speed, unmodified card = 40 mhz */
                        cx_write(MO_SCONV_REG, 131072*0.715909072483);
                        cx_write(MO_PLL_REG, 0x0165965A); /* 40000000.1406459 */
                        break;
      		default :
			/* if someone sets value out of range, default to crystal speed */
                        /* clock speed equal to crystal speed, unmodified card = 28.6 mhz */
                        cx_write(MO_SCONV_REG, 131072); /* set SRC to 8xfsc */
                        cx_write(MO_PLL_REG, 0x11000000); /* set PLL to 1:1 */
       }
      } else {
           if (tenxfsc < 100) {
	     tenxfsc = tenxfsc * 1000000;  //if number 11-99, conver to 11,000,000 to 99,000,000
           }
	   PLLint = tenxfsc/(crystal/40);  //always use PLL_PRE of 5 (=64)
	   longtenxfsc = (long)tenxfsc * 1000000; 
           longPLLboth = (long)(longtenxfsc/(long)(crystal/40));
	   longPLLint = (long)PLLint * 1000000;
	   PLLfrac = ((longPLLboth-longPLLint)*1048576)/1000000;
           PLLfin =  ((PLLint+64)*1048576)+PLLfrac;
           if (PLLfin < 81788928) {
             PLLfin = 81788928; // 81788928 lowest possible value
           }
           if (PLLfin > 119537664 ) {
             PLLfin = 119537664 ; //133169152 is highest possible value with PLL_PRE = 5 but above 119537664 may crash  
           }
           cx_write(MO_PLL_REG,  PLLfin); 
           //cx_write(MO_SCONV_REG, 131072 * (crystal / tenxfsc));
           SConv = (long)(131072 * (long)crystal) / (long)tenxfsc;
           cx_write(MO_SCONV_REG, SConv ); 
           
      }


	/* capture 16 bit or 8 bit raw samples */
	if (tenbit)
		cx_write(MO_CAPTURE_CTRL, ((1<<6)|(3<<1)|(1<<5)));
	else
		cx_write(MO_CAPTURE_CTRL, ((1<<6)|(3<<1)|(0<<5)));

	file->private_data = ctd;

	ctd->initial_page = cx_read(MO_VBI_GPCNT) - 1;
	cx_write(MO_PCI_INTMSK, 1); /* enable interrupt */

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

static ssize_t cxadc_char_read(struct file *file, char __user *tgt, size_t count,
			       loff_t *offset)
{
	struct cxadc *ctd = file->private_data;
	unsigned int rv = 0;
	unsigned int pnum;
	int gp_cnt;
        
	pnum = (*offset % VBI_DMA_BUFF_SIZE) / PAGE_SIZE;
	pnum += ctd->initial_page;
	pnum %= MAX_DMA_PAGE;

	gp_cnt = cx_read(MO_VBI_GPCNT);
	gp_cnt = (!gp_cnt) ? (MAX_DMA_PAGE - 1) : (gp_cnt - 1);

	if ((pnum == gp_cnt) && (file->f_flags & O_NONBLOCK))
		return rv;

	while (count) {
		while ((count > 0) && (pnum != gp_cnt)) {
			unsigned int len;

			/* handle partial pages for either reason */
			len = (*offset % 4096) ? (4096 - (*offset % 4096)) : 4096;
			if (len > count)
				len = count;

			if (copy_to_user(tgt, ctd->pgvec_virt[pnum] + (*offset % 4096), len))
				return -EFAULT;
			memset(ctd->pgvec_virt[pnum] + (*offset % 4096), 0, len);

			count -= len;
			tgt += len;
			*offset += len;
			rv += len;

			pnum = (*offset % VBI_DMA_BUFF_SIZE) / PAGE_SIZE;
			pnum += ctd->initial_page;
			pnum %= MAX_DMA_PAGE;
		}
		/* adding code to allow level change during read, have tested, works with CAV capture 
		 * script i have been working on */
                     if (level < 0)
                        level = 0;
	             if (level > 31)
                        level = 31;
	             cx_write(MO_AGC_GAIN_ADJ4, (sixdb<<23)|(0<<22)|(0<<21)|(level<<16)|(0xff<<8)|(0x0<<0));
	             cx_write(MO_AGC_SYNC_TIP3, (0x1e48<<16)|(0xff<<8)|(center_offset));


		if (count) {
			if (file->f_flags & O_NONBLOCK)
				return rv;

			ctd->newpage = 0;
			wait_event_interruptible(ctd->readQ, ctd->newpage);

			gp_cnt = cx_read(MO_VBI_GPCNT);
			gp_cnt = (!gp_cnt) ? (MAX_DMA_PAGE - 1) : (gp_cnt - 1);
		}
	};

	return rv;
}

static long cxadc_char_ioctl(struct file *file,
			     unsigned int cmd, unsigned long arg)
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
		cx_write(MO_AGC_GAIN_ADJ4, (sixdb<<23)|(0<<22)|(0<<21)|(gain<<16)|(0xff<<8)|(0x0<<0));
	}

	return ret;
}

static const struct file_operations cxadc_char_fops = {
	.owner    = THIS_MODULE,
	.llseek   = no_llseek,
	.unlocked_ioctl = cxadc_char_ioctl,
	.open     = cxadc_char_open,
	.release  = cxadc_char_release,
	.read     = cxadc_char_read,
};

static irqreturn_t cxadc_irq(int irq, void *dev_id)
{
	int count = 0;
	struct cxadc *ctd = dev_id;
	u32 allstat = cx_read(MO_VID_INTSTAT);
	u32 stat  = cx_read(MO_VID_INTMSK);
	u32 astat = stat & allstat;
	u32 ostat = astat;

	if (ostat != 8 && allstat != 0 && ostat != 0)
		cx_info("interrupt stat 0x%x masked 0x%x\n", allstat, ostat);

	if (!astat)
		return IRQ_RETVAL(0); /* if no interrupt bit set we return */

	for (count = 0; count < 20; count++) {
		if (astat & 1) {
			if (count == 3) {
				ctd->newpage = 1;
				wake_up_interruptible(&ctd->readQ);
			}
		}
		astat >>= 1;
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
	unsigned int pgsize;
        unsigned long longtenxfsc, longPLLboth, longPLLint;
        int PLLint, PLLfrac, PLLfin, SConv;

	if (PAGE_SIZE != 4096) {
		dev_err(&pci_dev->dev, "cxadc: only page size of 4096 is supported\n");
		return -EIO;
	}

	if (cxcount == CXCOUNT_MAX) {
		dev_err(&pci_dev->dev, "cxadc: only 1 card is supported\n");
		return -EBUSY;
	}

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

	ctd->pci = pci_dev;
	ctd->irq = pci_dev->irq;

	/* We can use cx_err/cx_info from here, now ctd has been set up. */

	if (alloc_risc_inst_buffer(ctd)) {
		cx_err("cannot alloc risc buffer\n");
		rc = -ENOMEM;
		goto fail1;
	}

	for (i = 0; i < (MAX_DMA_PAGE+1); i++) {
		ctd->pgvec_virt[i] = 0;
		ctd->pgvec_phy[i] = 0;
	}

	total_size = 0;
	pgsize = 4096;

	for (i = 0; i < MAX_DMA_PAGE; i++) {
		dma_addr_t dma_handle;

		ctd->pgvec_virt[i] = dma_zalloc_coherent(&ctd->pci->dev, 4096, &dma_handle, GFP_KERNEL);
		if (ctd->pgvec_virt[i] != 0) {
			ctd->pgvec_phy[i] = dma_handle;
			total_size += pgsize;
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

	init_waitqueue_head(&ctd->readQ);

	if (latency != -1) {
		cx_info("setting pci latency timer to %d\n", latency);
		pci_write_config_byte(pci_dev, PCI_LATENCY_TIMER, latency);
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
	create_cdt_table(ctd, NUMBER_OF_CLUSTER_BUFFER, CLUSTER_BUFFER_SIZE, CLUSTER_BUFFER_BASE, CDT_BASE);
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
	vmux &= 3;/* default vmux=1 */
	/* pal-B */
	cx_write(MO_INPUT_FORMAT, (vmux<<14)|(1<<13)|0x01|0x10|0x10000);
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
	if (tenbit)
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

      if (tenxfsc < 10) {
        //old code for old parameter compatibility
        switch (tenxfsc) {
                case 0 :
                        /* clock speed equal to crystal speed, unmodified card = 28.6 mhz */
                        cx_write(MO_SCONV_REG, 131072); /* set SRC to 8xfsc */
                        cx_write(MO_PLL_REG, 0x11000000); /* set PLL to 1:1 */
                        break;
                case 1 :
                        /* clock speed equal to 1.25 x crystal speed, unmodified card = 35.8 mhz */
                        cx_write(MO_SCONV_REG, 131072*4/5); /* set SRC to 1.25x/10fsc */
                        cx_write(MO_PLL_REG, 0x01400000); /* set PLL to 1.25x/10fsc */
                        break;
                case 2 :
                        /* clock speed equal to ~1.4 x crystal speed, unmodified card = 40 mhz */
                        cx_write(MO_SCONV_REG, 131072*0.715909072483);
                        cx_write(MO_PLL_REG, 0x0165965A); /* 40000000.1406459 */
                        break;
                default :
                        /* if someone sets value out of range, default to crystal speed */
                        /* clock speed equal to crystal speed, unmodified card = 28.6 mhz */
                        cx_write(MO_SCONV_REG, 131072); /* set SRC to 8xfsc */
                        cx_write(MO_PLL_REG, 0x11000000); /* set PLL to 1:1 */
        }
      } else {
           if (tenxfsc < 100) {
             tenxfsc = tenxfsc * 1000000;  //if number 11-99, conver to 11,000,000 to 99,000,000
           }
           PLLint = tenxfsc/(crystal/40);  //always use PLL_PRE of 5 (=64)
           longtenxfsc = (long)tenxfsc * 1000000;
           longPLLboth = (long)(longtenxfsc/(long)(crystal/40));
           longPLLint = (long)PLLint * 1000000;
           PLLfrac = ((longPLLboth-longPLLint)*1048576)/1000000;
           PLLfin =  ((PLLint+64)*1048576)+PLLfrac;
           if (PLLfin < 81788928) {
             PLLfin = 81788928; // 81788928 lowest possible value
           }
           if (PLLfin > 119537664 ) {
             PLLfin = 119537664 ; //133169152 is highest possible value with PLL_PRE = 5 but above 119537664 may crash  
           }
           cx_write(MO_PLL_REG,  PLLfin); 
           SConv = (long)(131072 * (long)crystal) / (long)tenxfsc;
           cx_write(MO_SCONV_REG, SConv ); 
      }

 

	/* set vbi agc */
        /* Set the sample delays to maximum (issue #2) */
	cx_write(MO_AGC_SYNC_SLICER, (0xff << 8) || 0xff);

	if (level < 0)
		level = 0;
	if (level > 31)
		level = 31;

	cx_write(MO_AGC_BACK_VBI, (0<<27)|(0<<26)|(1<<25)|(0x100<<16)|(0xfff<<0));
	/* control gain also bit 16 */
	cx_write(MO_AGC_GAIN_ADJ4, (sixdb<<23)|(0<<22)|(0<<21)|(level<<16)|(0xff<<8)|(0x0<<0));
	/* for 'cooked' composite */
	cx_write(MO_AGC_SYNC_TIP1, (0x1c0<<17)|(0x0<<9)|(0<<7)|(0xf<<0));
	cx_write(MO_AGC_SYNC_TIP2, (0x20<<17)|(0x0<<9)|(0<<7)|(0xf<<0));
	cx_write(MO_AGC_SYNC_TIP3, (0x1e48<<16)|(0xff<<8)|(center_offset));
	cx_write(MO_AGC_GAIN_ADJ1, (0xe0<<17)|(0xe<<9)|(0x0<<7)|(0x7<<0));
	/* set gain of agc but not offset */
	cx_write(MO_AGC_GAIN_ADJ3, (0x28<<16)|(0x28<<8)|(0x50<<0));

        /* Disable PLL adjust (stabilizes output when video is detected by chip) */
	cx_write(MO_PLL_ADJ_CTRL, cx_read(MO_PLL_ADJ_CTRL) & ~(0x0<<25));

	if (audsel != -1) {
		/*
		 * Pixelview PlayTVPro Ultracard specific
		 * select which output is redirected to audio output jack
		 * GPIO bit 3 is to enable 4052 , bit 0-1 4052's AB
		 */
		cx_write(MO_GP3_IO, 1<<25); /* use as 24 bit GPIO/GPOE */
		cx_write(MO_GP1_IO, 0x0b);
		cx_write(MO_GP0_IO, audsel&3);
		cx_info("audsel = %d\n", audsel&3);
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
fail1:
	kfree(ctd);
fail0:
	release_mem_region(pci_resource_start(pci_dev, 0),
			   pci_resource_len(pci_dev, 0));
	return rc;
}

static void cxadc_remove(struct pci_dev *pci_dev)
{
	struct cxadc *ctd = pci_get_drvdata(pci_dev);
	struct cxadc *walk;

	disable_card(ctd);

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
	if (ctd == cxadcs) {
		cxadcs = NULL;
	} else {
		for (walk = cxadcs; walk->next != ctd; walk = walk->next)
			;
		walk->next = ctd->next;
	}
	cxcount--;

	cx_info("reset drv data\n");
	pci_set_drvdata(pci_dev, NULL);
	cx_info("reset drv ok\n");
	kfree(ctd);
}

MODULE_DEVICE_TABLE(pci, cxadc_pci_tbl);

static struct pci_driver cxadc_pci_driver = {
	.name     = "cxadc",
	.id_table = cxadc_pci_tbl,
	.probe    = cxadc_probe,
	.remove   = cxadc_remove,
};

static int __init cxadc_init_module(void)
{
	int retval;
	dev_t dev;

	cxadc_class = class_create(THIS_MODULE, "cxadc");
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

module_param(latency, int, 0664);
module_param(audsel, int, 0664);
module_param(vmux, int, 0664);
module_param(level, int, 0664);
module_param(tenbit, int, 0664);
module_param(tenxfsc, int, 0664);
module_param(sixdb, int, 0664);
module_param(crystal, int, 0664);
module_param(center_offset, int, 0664);

MODULE_DESCRIPTION("cx2388xx adc driver");
MODULE_AUTHOR("Hew How Chee");
MODULE_LICENSE("GPL");

