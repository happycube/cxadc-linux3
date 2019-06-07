/*
    cxadc - cx2388x adc dma driver for linux 3.x
    version 0.5

    Copyright (c) 2005-2007 Hew How Chee <how_chee@yahoo.com>
    Copyright (c) 2013-2015 Chad Page <Chad.Page@gmail.com>
    Copyright (c) 2019 Adam Sampson <ats@offog.org>
 
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

/*
Make sure you log in as super user

To compile     : make
Output file    : cxadc.ko	
Create node    : mknod /dev/cxadc c 126 0 
Install driver : insmod ./cxadc.ko

read /dev/cxadc to get data

Reference      : btaudio driver, mc4020 driver , cxadc driver v0.3
*/

/* 'dmesg' log after insmod ./cxadc.ko   (Addresses/Numbers might be different)

If it is not something like this, then there might be memory allocation error.

cxadc: mem addr 0xfd000000 size 0x0
cxadc: risc inst buff allocated at virt 0xd4f40000 phy 0x14f40000 size 132 kbytes
cxadc: total DMA size allocated = 32768 kb
cxadc: end of risc inst 0xd4f6000c total size 128 kbyte
cxadc: IRQ used 11
cxadc: MEM :fd000000 MMIO :d8f80000
cxadc: dev 0x8800 (rev 5) at 02:0b.0, cxadc: irq: 11, latency: 255, mmio: 0xfd000000
cxadc: char dev register ok
cxadc: audsel = 2

*/

/* dmesg log after running cxcap to capture data 
   note that once cxadc driver is loaded, DMA is always running
   to stop driver use 'rmmod cxadc'
 
cxadc: open [0] private_data c7aa0000
cxadc: vm end b7f8b000 vm start b5f8b000  size 2000000
cxadc: vm pg off 0
cxadc: mmap private data c7aa0000
cxadc: enable interrupt
cxadc: release
*/


/*
  History

  25 sep 2005 - added support for i2c, use i2c.c to tune
              - set registers to lower gain
              - set registers so that no signal is nearer to sample value 128
              - added vmux and audsel as parms during driver loading
                (for 2nd IF hardware modification, load driver using vmux=2 )
                By default audsel=2 is to route tv tuner audio signal to
                audio out of TV card, vmux=1 use the signal from video in of tv card
                     
  Feb-Mac 2007 - change code to compile and run in kernel 2.6.18		
               - clean up mess in version 0.2 code
               - it still a mess in this code
*/

#include <linux/version.h>
#include <linux/cdev.h>
#include <linux/debugfs.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
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

static int debug = 0;
static int level = 16;
static int tenbit = 0;
static int tenxfsc = 0;

#define cx_read(adr)         readl(ctd->mmio+((adr)>>2))
#define cx_write(dat,adr)    writel((dat),(ctd->mmio+((adr)>>2)))

#define CX_SRAM_BASE 0x180000
#define CX_VIDEO_VBI_CMDS_BASE  (CX_SRAM_BASE+0x100)

#define CXADC_IOCTL_WRITE_PLL_REG 0x12345676

/* ------------------------------------------------------------------------- */
/* CX registers                                                              */
#define CX_PLL_REG              0x310168 //PLL register
#define CX_SAMP_RATE_CONV_REG   0x310170 //sample rate conversion register
#define CX_DEV_CNTRL2       	0x200034 //  Device control
#define CX_VID_INT_STAT     	0x200054 //  Video interrupt status
#define CX_DMA24_PTR1       	0x30008C //  DMA Current Ptr : Ch#24_ir
#define CX_DMA24_PTR2       	0x3000CC //  DMA Tab Ptr     : Ch#24
#define CX_DMA24_CNT1       	0x30010C //  DMA Buffer Size : Ch#24
#define CX_DMA24_CNT2       	0x30014C //  DMA Table Size  : Ch#24
#define CX_VID_DMA_CNTRL       	0x31c040 //  Video DMA control
#define CX_VBI_GP_CNT        	0x31C02C //  VBI general purpose counter
#define VID_CAPTURE_CONTROL  	0x310180
#define CX_PCI_INT_STAT		0x200044
#define CX_PCI_INT_MSK		0x200040

#define CX_GPIO                 0x350010
#define CX_GPOE			0x350014	
#define CX_PINMUX_IO            0x35c044
#define CX_GP3_IO               0x35001C

#define CX_I2C_COMP_DATACNTRL   0x368000
/* ------------------------------------------------------------------------ */

#define RISC_SYNC		 0x80000000
#define RISC_WRITE		 0x10000000
#define RISC_JUMP		 0x70000000
#define RISC_WRITERM		 0xB0000000
#define RISC_WRITECM		 0xC0000000
#define RISC_WRITECR		 0xD0000000

/* ------------------------------------------------------------------------ */

//64 Mbytes VBI DMA BUFF
#define VBI_DMA_BUFF_SIZE (1024*1024*64) 
//corresponds to 8192 DMA pages of 4k bytes
#define MAX_DMA_PAGE (VBI_DMA_BUFF_SIZE/PAGE_SIZE)

/* -------------------------------------------------------------- */

#define WRITE_INST_PER_PAGE 510 
// WRITE_INST_PER_PAGE -  	1 sync instruction (first page only) followed by 510 write instruction and finally a
//  		       		1 jmp instruction to next page/reloop  
// 				this will give 4 + 510*8 +  8 = 4092 bytes, which fits into a page

#define NUMBER_OF_WRITE_PER_PAGE 510
//number of write per page must be even
//each write risc instruction is 8 byte
//and the end , there is a jmp to next page which is 8 byte
//giving a total of 510 * 8 + 8 = 4088

#define CLUSTER_BUFFER_SIZE 2048 

#define NUMBER_OF_WRITE_NEEDED (VBI_DMA_BUFF_SIZE/CLUSTER_BUFFER_SIZE)
#define NUMBER_OF_RISC_PAGE ((NUMBER_OF_WRITE_NEEDED/NUMBER_OF_WRITE_PER_PAGE)+1+1)
//+1 page for additional one write page if NUMBER_OF_WRITE_NEEDED/NUMBER_OF_WRITE_PER_PAGE is not integer
//+1 page for first sync and jmp instruction (we used 12 bytes of 4 kbytes only)

struct risc_page {
	struct risc_page * next;
	char buffer [PAGE_SIZE-sizeof(struct risc_page*)];
};
struct cxadc {
	/* linked list */
	struct cxadc *next;

	/* device info */
	int            char_dev;
	struct pci_dev *pci;
	unsigned int   irq;
	unsigned int  mem;
	unsigned int  *mmio;

	/* locking */
	int            users;
	struct mutex lock;
	
	unsigned int    risc_inst_buff_size;
	unsigned int	*risc_inst_virt;
	dma_addr_t    	risc_inst_phy;
	
	wait_queue_head_t readQ;
	
	void *pgvec_virt[MAX_DMA_PAGE+1];
	dma_addr_t pgvec_phy[MAX_DMA_PAGE+1];

	unsigned int *pgrisc_virt[NUMBER_OF_RISC_PAGE];
	unsigned int *pgrisc_phy[NUMBER_OF_RISC_PAGE];
	struct risc_page *risc_ptr;
	int newpage;

	int initial_page;
};

static struct cxadc *cxadcs  = NULL;
static unsigned int cxcount      = 0;
#ifdef METHOD2
static unsigned int risc_start_phy_addr=0;
#endif
/* -------------------------------------------------------------- */

#define NUMBER_OF_CLUSTER_BUFFER 8 
#define CX_SRAM_BASE 0x180000

#define CDT_BASE  		(CX_SRAM_BASE+0x1000)
#define CLUSTER_BUFFER_BASE 	(CX_SRAM_BASE+0x4000)
#define RISC_BUFFER_BASE        (CX_SRAM_BASE+0x2000)
#define RISC_INST_QUEUE		(CX_SRAM_BASE+0x800)
#define CHN24_CMDS_BASE	0x180100
#define DMA_BUFFER_SIZE (256*1024)

#define CX_VID_INT_MSTAT 0x200058
#define CX_VID_INT_STAT 0x200054
#define CX_VID_INT_MSK 0x200050
#define INTERRUPT_MASK 0x18888

static struct pci_device_id cxadc_pci_tbl[] = 
{
	{
		.vendor       = 0x14f1, //conexant
		.device       = 0x8800, 
		.subvendor    = PCI_ANY_ID,
		.subdevice    = PCI_ANY_ID,
	},
	{
	
	}
};

static void disable_card(struct cxadc *ctd)
{
	/* turn off all DMA / IRQs */
	//turn off pci interrupt
	cx_write(0,CX_PCI_INT_MSK);
	//turn off interrupt
	cx_write(0,CX_VID_INT_MSK);
	cx_write(~(u32)0,CX_VID_INT_STAT);
	//disable fifo and risc
	cx_write(0,CX_VID_DMA_CNTRL);
	//disable risc
	cx_write(0,CX_DEV_CNTRL2);
}


/*
numbuf   - number of buffer
buffsize - buffer size in bytes
buffptr  - CX sram start addr for buffer e.g. 0x182000 as in example pg 2-62 of CX23880/1/2/3 datasheet
cdtptr   - CX sram start addr for CDT    e.g. 0x181000 as in example pg 2-62 of CX23880/1/2/3 datasheet
*/

static void create_cdt_table(struct cxadc *ctd,unsigned int numbuf,unsigned int buffsize,unsigned int buffptr,unsigned int cdtptr )
{
	int i;
	unsigned int pp,qq;
	pp=buffptr;
	qq=cdtptr;

	for (i=0;i<numbuf;i++)
	{
		//printk("cdtptr [%x]= %x\n",qq,pp);
		
		writel(pp,ctd->mmio+(qq>>2));
		qq+=4;
		
		writel(0,ctd->mmio+(qq>>2)); /*not needed but we do it anyway*/
		qq+=4;
		
		writel(0,ctd->mmio+(qq>>2)); /*not needed but we do it anyway*/
		qq+=4;
		
		writel(0,ctd->mmio+(qq>>2)); /*not needed but we do it anyway*/
		qq+=4;
		
		pp+=buffsize;
	}

}

static void free_dma_buffer(struct cxadc *ctd)
{
	int i;
	for(i=0;i<MAX_DMA_PAGE;i++)
	{
		if(ctd->pgvec_virt[i]) 
		{
			dma_free_coherent(&ctd->pci->dev, 4096, ctd->pgvec_virt[i],ctd->pgvec_phy[i]);
		}
	}
}
		
static int alloc_risc_inst_buffer(struct cxadc *ctd) //used
{
	//unsigned int size=
	ctd->risc_inst_buff_size=(VBI_DMA_BUFF_SIZE/CLUSTER_BUFFER_SIZE)*8+PAGE_SIZE; //add 1 page for sync instruct and jump
	ctd->risc_inst_virt=pci_alloc_consistent(ctd->pci,ctd->risc_inst_buff_size,&ctd->risc_inst_phy);
	if (ctd->risc_inst_virt==NULL)
		return -ENOMEM;
	memset(ctd->risc_inst_virt,0,ctd->risc_inst_buff_size);
	
	printk("cxadc: risc inst buff allocated at virt 0x%p phy 0x%lx size %d kbytes\n",
	ctd->risc_inst_virt,(unsigned long)ctd->risc_inst_phy,ctd->risc_inst_buff_size/1024);
	
	return 0;
}

static void free_risc_inst_buffer(struct cxadc *ctd) //used
{
	if(ctd->risc_inst_virt!=NULL)
	pci_free_consistent(ctd->pci,ctd->risc_inst_buff_size,ctd->risc_inst_virt,ctd->risc_inst_phy);
}

static int make_risc_instructions(struct cxadc *ctd)//,unsigned int cl_size,unsigned int max_page) //used
{
	int i;
	int irqt;
	unsigned int loop_addr;
	unsigned int dma_addr;
	unsigned int *pp=(unsigned int *)ctd->risc_inst_virt;

	loop_addr=ctd->risc_inst_phy+4;
	
	*pp++=RISC_SYNC|(3<<16);
	
	irqt=0;
	for(i=0;i<MAX_DMA_PAGE;i++)
	{
		irqt++;	
		irqt&=0x1ff;
		*pp++=RISC_WRITE |CLUSTER_BUFFER_SIZE|(3<<26)|(0<<16);	
		
//		dma_addr=virt_to_bus(ctd->pgvec[i]);
		dma_addr=ctd->pgvec_phy[i];
		//printk("dma addr %x pgvec %x\n",dma_addr,ctd->pgvec[i]);
		*pp++=dma_addr;
		if(i!=MAX_DMA_PAGE-1)
		{	
			*pp++=RISC_WRITE |CLUSTER_BUFFER_SIZE|(((irqt==0)?1:0)<<24)|(3<<26)|(1<<16);	
			*pp++=dma_addr+CLUSTER_BUFFER_SIZE;
		}
		else
		{
			*pp++=RISC_WRITE |CLUSTER_BUFFER_SIZE|(((irqt==0)?1:0)<<24)|(3<<26)|(3<<16);	//reset cnt to 0
			*pp++=dma_addr+CLUSTER_BUFFER_SIZE;
		}
	}
	
#if 0	
	//test stop FIFO and RISC
	*pp++=RISC_WRITECR|1;
	*pp++=CX_VID_DMA_CNTRL;
	*pp++=0;
	*pp++=0xffffffff;
	//test
#endif	
	//1<<24 = irq , 11<<16 = cnt
	*pp++=RISC_JUMP|(0<<24)|(0<<16); //interrupt and increment counter
	*pp++=loop_addr;
	
	printk("cxadc: end of risc inst 0x%p total size %ld kbyte\n",pp,((void *)pp-(void *)ctd->risc_inst_virt)/1024);
	return 0;
}

static int cxadc_char_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct cxadc *ctd;

	for (ctd = cxadcs; ctd != NULL; ctd = ctd->next)
		if (ctd->char_dev == minor)
			break;
	if (NULL == ctd)
		return -ENODEV;

	if (debug)
		printk("cxadc: open [%d] private_data %p\n",minor,ctd);

	// re-set the level, clock speed, and bit size

	if (level < 0) level = 0;	
	if (level > 31) level = 31;	
	cx_write((1<<23)|(0<<22)|(0<<21)|(level<<16)|(0xff<<8)|(0x0<<0),0x310220);//control gain also bit 16

	// set higher clock rate	
	if (tenxfsc) {
		cx_write(131072*4/5,0x310170);//set SRC to 1.25x/10fsc  
	} else {
		cx_write(131072,0x310170);//set SRC to 8xfsc 
	}
	if (tenxfsc) {
		cx_write(131072*4/5,0x310170);//set SRC to 1.25x/10fsc  
		cx_write(0x01400000,CX_PLL_REG);//set PLL to 1.25x/10fsc 
	} else {
		cx_write(131072,0x310170);//set SRC to 8xfsc 
		cx_write(0x11000000,CX_PLL_REG);//set PLL to 1:1
	}
		
	if (tenbit) {
		cx_write(((1<<6)|(3<<1)|(1<<5)),0x310180); //capture 16 bit raw
	} else {
		cx_write(((1<<6)|(3<<1)|(0<<5)),0x310180); //capture 8 bit raw
	}

	file->private_data = ctd;

	ctd->initial_page = cx_read(CX_VBI_GP_CNT) - 1;
	cx_write(1,CX_PCI_INT_MSK); //enable interrupt

	return 0;
}

static int cxadc_char_release(struct inode *inode, struct file *file)
{
	struct cxadc *ctd = file->private_data;

	cx_write(0,CX_PCI_INT_MSK);

	if (debug)
		printk("cxadc: release\n");

	return 0;
}

static ssize_t cxadc_char_read(struct file *file, char __user *tgt, size_t count, loff_t *offset )
{
	struct cxadc *ctd = file->private_data;
	unsigned int rv = 0;
	unsigned int pnum;
	int gp_cnt;

	//printk("read pos %ld cur %d len %d\n", *offset, cx_read(CX_VBI_GP_CNT), count);

	pnum = (*offset % VBI_DMA_BUFF_SIZE) / PAGE_SIZE;
	pnum += ctd->initial_page;
	pnum %= MAX_DMA_PAGE;

	gp_cnt = cx_read(CX_VBI_GP_CNT);
	gp_cnt = (!gp_cnt) ? (MAX_DMA_PAGE - 1) : (gp_cnt - 1);

//	printk("read pos %ld cur %d len %d pnum %d gp_cnt %d\n", *offset, cx_read(CX_VBI_GP_CNT), count, pnum, gp_cnt);

	if ((pnum == gp_cnt) && (file->f_flags & O_NONBLOCK)) return rv; 

	while (count) {
		while ((count > 0) && (pnum != gp_cnt)) {
			unsigned len;

			// handle partial pages for either reason 
			len = (*offset % 4096) ? (4096 - (*offset % 4096)) : 4096;
			if (len > count) len = count;

		//	if (len != 4096) printk("do read rv %d count %d cur %d len %d pnum %d\n", rv, count, cx_read(CX_VBI_GP_CNT), len, pnum);
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
		};

		if (count) {
			if (file->f_flags & O_NONBLOCK) return rv; 

			ctd->newpage=0;
                	wait_event_interruptible(ctd->readQ,ctd->newpage);
	
			gp_cnt = cx_read(CX_VBI_GP_CNT);
			gp_cnt = (!gp_cnt) ? (MAX_DMA_PAGE - 1) : (gp_cnt - 1);
		}
	};

	return rv;
}

static long cxadc_char_ioctl(struct file *file,
                               unsigned int cmd, unsigned long arg)
{
        struct cxadc *ctd = file->private_data;
	int ret=0;

	if (cmd == 0x12345670) {
		int gain = arg;

		if (gain < 0) gain = 0;
		if (gain > 31) gain = 31;
		cx_write((1<<23)|(0<<22)|(0<<21)|(gain<<16)|(0xff<<8)|(0x0<<0),0x310220);//control gain also bit 16
	}

	return ret;
}

static struct file_operations cxadc_char_fops = {
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
	u32 stat,astat,ostat,allstat;
	struct cxadc *ctd = dev_id;
	
	allstat = cx_read(CX_VID_INT_STAT);
	stat  = cx_read(CX_VID_INT_MSK);
	astat = stat & allstat;
	ostat = astat;

	if (ostat!=8 && allstat!=0 && ostat!=0) {
		printk(KERN_INFO "cxadc : Interrupt stat 0x%x Masked 0x%x\n",allstat,ostat);
	}

	if (!astat) {
		 return IRQ_RETVAL(0); //if no interrupt bit set we return
	}	

	for (count = 0; count < 20; count++) {
		if (astat&1) {
			if(count==3) {
//				unsigned int uu=cx_read(0x310220);
//				printk("%8.8x %x %x %x %x\n",
//			uu,uu>>16,(uu>>16)&0x1f,(uu>>8)&0xff,uu&0xff);
//			printk("gadj1 %x stip3 %x gadj3 %x gadj4 %x stat %x wc %x\n ",cx_read(0x310214),cx_read(0x310210),cx_read(0x31021c),cx_read(0x310220),cx_read(0x310100),cx_read(0x31011c));
//				printk("cxadc: wake up %x\n",cx_read(CX_VBI_GP_CNT));
//				
				ctd->newpage=1;
				wake_up_interruptible(&ctd->readQ);
			}
//		printk("IRQ %s\n",irq_name[count]);
		}
		astat>>=1;
	}
	cx_write(ostat,CX_VID_INT_STAT);
	
	return IRQ_RETVAL(1);
}

/* -------------------------------------------------------------- */

#define CXADC_MAX 1

static int latency = -1;
static int audsel=2;
static int vmux=2;

static int cxadc_probe(struct pci_dev *pci_dev,
				   const struct pci_device_id *pci_id)
{
	u32 i;
	struct cxadc *ctd;
	unsigned char revision,lat;
	int rc;
	unsigned int total_size;
	u64 startaddr,pcilen;
	unsigned int pgsize;

	if (PAGE_SIZE != 4096)
	{
		printk(KERN_ERR "cxadc: only page size of 4096 is supported\n");
		return -EIO;
	}

	if (CXADC_MAX == cxcount)
	{
		printk(KERN_ERR "cxadc: only 1 card is supported\n");
		return -EBUSY;
	}

	if (pci_enable_device(pci_dev))
	{
		printk(KERN_ERR "cxadc: enable device failed\n");
		return -EIO;
	}

	if (!request_mem_region(startaddr=pci_resource_start(pci_dev,0),
				pcilen=pci_resource_len(pci_dev,0),
				"cxadc")) {
		printk(KERN_ERR "cxadc: request memory region failed\n");
		return -EBUSY;
	}
	
	ctd = kmalloc(sizeof(*ctd),GFP_ATOMIC);
	if (!ctd) {
		rc = -ENOMEM;
		printk(KERN_ERR "cxadc: kmalloc failed\n");
		goto fail0;
	}
	memset(ctd,0,sizeof(*ctd));

	ctd->pci = pci_dev;
	ctd->irq = pci_dev->irq;
	
	if (alloc_risc_inst_buffer(ctd))
	{
		printk(KERN_ERR "cxadc: cannot alloc risc buffer\n");
		rc=-ENOMEM;
		goto fail1;
	}	
	
	for (i = 0; i < (MAX_DMA_PAGE+1); i++) {
		ctd->pgvec_virt[i]=0;
		ctd->pgvec_phy[i]=0;
	}
	
	total_size=0;
	pgsize=4096;
	
	for (i=0; i<MAX_DMA_PAGE; i++) {
		dma_addr_t dma_handle;

		ctd->pgvec_virt[i]=(void*)dma_zalloc_coherent(&ctd->pci->dev,4096,&dma_handle,GFP_KERNEL);
		if(ctd->pgvec_virt[i]!=0) {
			ctd->pgvec_phy[i]=dma_handle;
			total_size+=pgsize;
 		} else {	
			printk("cxadc: alloc dma buffer failed. index = %d\n",i);
			
			rc=-ENOMEM;
			goto fail1x;
		}
		
	}
	
	printk(KERN_INFO "cxadc: total DMA size allocated = %d kb\n",total_size/1024);

	make_risc_instructions(ctd);
	
//	printk("cxadc: IRQ used %d\n",ctd->irq);
	ctd->mem  = pci_resource_start(pci_dev,0);
	
	ctd->mmio = ioremap(pci_resource_start(pci_dev,0),
			    pci_resource_len(pci_dev,0));
	printk("cxadc: MEM :%x MMIO :%p\n",ctd->mem,ctd->mmio);

//	ctd->rate = rate[cxcount];
	
	mutex_init(&ctd->lock);
	init_waitqueue_head(&ctd->readQ);

	if (-1 != latency) {
		printk(KERN_INFO "cxadc: setting pci latency timer to %d\n",
		       latency);
		pci_write_config_byte(pci_dev, PCI_LATENCY_TIMER, latency);
	} else {
		pci_write_config_byte(pci_dev, PCI_LATENCY_TIMER, 255);
	}
       
	pci_read_config_byte(pci_dev, PCI_CLASS_REVISION, &revision);
        pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER, &lat);
        printk("cxadc: dev 0x%X (rev %d) at %02x:%02x.%x, ",
	       pci_dev->device,revision,pci_dev->bus->number,
	       PCI_SLOT(pci_dev->devfn),PCI_FUNC(pci_dev->devfn));
        printk("cxadc: irq: %d, latency: %d, mmio: 0x%x\n",
	       ctd->irq, lat, ctd->mem);
	//printk("cxadc: using card config \"%s\"\n", card->name);

	/* init hw */
	pci_set_master(pci_dev);
	disable_card(ctd);	
	
	/* we use 16kbytes of FIFO buffer */
	create_cdt_table( ctd, NUMBER_OF_CLUSTER_BUFFER,CLUSTER_BUFFER_SIZE,CLUSTER_BUFFER_BASE,CDT_BASE);
	cx_write((CLUSTER_BUFFER_SIZE/8-1),CX_DMA24_CNT1); /* size of one buffer in qword -1 */
//	printk("cnt1:%x\n",CLUSTER_BUFFER_SIZE/8-1);
	
	cx_write(CDT_BASE,CX_DMA24_PTR2); /* ptr to cdt */
	cx_write(2*NUMBER_OF_CLUSTER_BUFFER,CX_DMA24_CNT2); /* size of cdt in qword */
	
//	if(ctd->tbuf!=NULL)
	{
		unsigned int xxx;
		xxx=cx_read(CX_VID_INT_STAT);
		cx_write(xxx,CX_VID_INT_STAT); //clear interrupt	
		
		cx_write(ctd->risc_inst_phy,CHN24_CMDS_BASE);//working 
		cx_write(CDT_BASE,CHN24_CMDS_BASE+4);
		cx_write(2*NUMBER_OF_CLUSTER_BUFFER,CHN24_CMDS_BASE+8);
		cx_write(RISC_INST_QUEUE,CHN24_CMDS_BASE+12);

		cx_write(0x40 ,CHN24_CMDS_BASE+16);
		
		//source select (see datasheet on how to change adc source)
		//1<<14 - video in
		
		vmux&=3;//default vmux=1
//		cx_write((vmux<<14)|0x01|0x10|(0x01<<12),0x310104); //pal-B, yadc =mux1 (<<14)
//		cx_write((vmux<<14)|(1<<13)|0x01|0x10|0x10000,0x310104); //pal-B, yadc =mux1 (<<14)
		cx_write((vmux<<14)|(1<<13)|0x01|0x10|0x10000,0x310104); //pal-B
		cx_write(0x0f, 0x310164); // output format:  allow full range
		
		cx_write(0xff00, 0x310110); // brightness and contrast 
		
		//vbi lenght CLUSTER_BUFFER_SIZE/2  work
				
		cx_write((((CLUSTER_BUFFER_SIZE)<<17)|(2<<11)),0x310188); //no of byte transferred from peripehral to fifo
								// if fifo buffer < this, it will still transfer this no of byte
								//must be multiple of 8, if not go haywire?
		
		//raw mode & byte swap <<8 (3<<8=swap)
		cx_write( ((0xe)|(0xe<<4)|(0<<8)) , 0x310184);

		if (tenbit) {
			cx_write(((1<<6)|(3<<1)|(1<<5)),0x310180); //capture 16 bit raw
		} else {
			cx_write(((1<<6)|(3<<1)|(0<<5)),0x310180); //capture 8 bit raw
		}
	
		// power down audio and chroma DAC+ADC	
		cx_write( 0x12, 0x35C04C);

		//cx_write(((1<<6)|(3<<1)|(1<<5)),0x310180); //capture 16 bit raw
//		cx_write(((1<<6)),0x310180);
		//run risc
//		wmb();
		cx_write(1<<5,CX_DEV_CNTRL2);
//		wmb();
		//enable fifo and risc
		cx_write(((1<<7)|(1<<3)),CX_VID_DMA_CNTRL);
//		wmb();
	
	//	for(i=0x4000;i<0x8000;i+=4)
	//	{	
	//		cx_write(0xaaaaaaaa,(CX_SRAM_BASE+i));
	//	}	
	}

	if ((rc = request_irq(ctd->irq, cxadc_irq, IRQF_SHARED,
			      "cxadc",(void *)ctd)) < 0) {
		printk(KERN_WARNING
		       "cxadc: can't request irq (rc=%d)\n",rc);
		goto fail1x;
	}

#if 1
	/* register devices */
#define CX2388XADC_MAJOR  126		       
  	if (register_chrdev (CX2388XADC_MAJOR, "cxadc", &cxadc_char_fops))
	{
    		printk (KERN_ERR "cxadc: failed to register device\n");
		rc=-EIO;
		goto fail2;
	}
	else
	{
		printk("cxadc: char dev register ok\n");	       
	}	
#endif

//	cx_write(0x20000,0x31016C);
//  cx_write(0x11000000,CX_PLL_REG);//set PLL to 1:1
//  cx_write(0x01400000,CX_PLL_REG);//set PLL to 1.25x/10fsc 
	cx_write(0x01000000,CX_PLL_REG);//set PLL to 8xfsc 

	if (tenxfsc) {
		cx_write(131072*4/5,0x310170);//set SRC to 1.25x/10fsc  
		cx_write(0x01400000,CX_PLL_REG);//set PLL to 1.25x/10fsc 
	} else {
		cx_write(131072,0x310170);//set SRC to 8xfsc 
		cx_write(0x11000000,CX_PLL_REG);//set PLL to 1:1
	}

	//set audio multiplexer
	
	//set vbi agc
//	cx_write((0<<21)|(0<<20)|(0<<19)|(4<<16)|(0x60<<8)|(0x1c<<0),0x310204);
	cx_write(0x0,0x310204);
	
	if (level < 0) level = 0;	
	if (level > 31) level = 31;	
			
	cx_write((0<<27)|(0<<26) |(1<<25)| (0x100<<16) |(0xfff<<0),   0x310200);
	cx_write((1<<23)|(0<<22)|(0<<21)|(level<<16)|(0xff<<8)|(0x0<<0),0x310220);//control gain also bit 16
// for 'cooked' composite
	cx_write((0x1c0<<17)|(0x0<<9)|(0<<7)|(0xf<<0),0x310208);
	cx_write((0x20<<17)|(0x0<<9)|(0<<7)|(0xf<<0),0x31020c);
	cx_write((0x1e48<<16)|(0xff<<8)|(0x8),0x310210);
	cx_write((0xe0<<17)|(0xe<<9)|(0x0<<7)|(0x7<<0),0x310214);
	cx_write((0x28<<16)|(0x28<<8)|(0x50<<0),0x31021c);//set gain of agc but not offset
	
	//==========Pixelview PlayTVPro Ultracard specific============
	//select which output is redirected to audio output jack
	//
	cx_write(1<<25,CX_GP3_IO); //use as 24 bit GPIO/GPOE 
	cx_write(0x0b,CX_GPOE); //bit 3 is to enable 4052 , bit 0-1 4052's AB
	cx_write(audsel&3,CX_GPIO); //3=audio in to audio out
	                            //2=fm out?
				    //1=silence ?
				    //0=tuner tv audio out?
	printk("cxadc: audsel = %d\n",audsel&3);
	//=================================================
	
	//i2c sda/scl set to high and use software control
	
	cx_write(3,CX_I2C_COMP_DATACNTRL);
	//=================================================
	
	/* hook into linked list */
	ctd->next = cxadcs;
	cxadcs = ctd;
	cxcount++;

	pci_set_drvdata(pci_dev,ctd);
	cx_write(INTERRUPT_MASK,CX_VID_INT_MSK);
	//cx_write(1,CX_PCI_INT_MSK); //enable interrupt
//	printk("page size %d\n",PAGE_SIZE);
        return 0;

// fail4:
	//unregister_sound_dsp(ctd->dsp_analog);
// fail3:
	//if (digital)
	//	unregister_sound_dsp(ctd->dsp_digital);
 fail2:
        free_irq(ctd->irq,ctd);	
 fail1x:
	free_dma_buffer(ctd);
	free_risc_inst_buffer(ctd);
 fail1:

	kfree(ctd);

//	if(ctd->mmio!=NULL) iounmap(ctd->mmio);	
 fail0:

	release_mem_region(pci_resource_start(pci_dev,0),
			   pci_resource_len(pci_dev,0));
	return rc;
}

static void cxadc_remove(struct pci_dev *pci_dev)
{
	struct cxadc *ctd = pci_get_drvdata(pci_dev);
	struct cxadc *walk;

	disable_card(ctd);		
//		wmb();
	/* unregister devices */
	/* Next, unregister ourselves with the character device driver handler */

	unregister_chrdev(CX2388XADC_MAJOR, "cxadc");
	
	/* free resources */
	free_risc_inst_buffer(ctd);
        free_irq(ctd->irq,ctd);
//	printk("cxadc: irq freed\n");
	free_dma_buffer(ctd);
//	printk("cxadc: dma page freed\n");
//	printk("cxadc: try release mem region\n");
	iounmap(ctd->mmio);
	release_mem_region(pci_resource_start(pci_dev,0),
			   pci_resource_len(pci_dev,0));
//	printk("cxadc: release mem region ok\n");

	/* remove from linked list */
	if (ctd == cxadcs) {
		cxadcs = NULL;
	} else {
		for (walk = cxadcs; walk->next != ctd; walk = walk->next)
			; /* if (NULL == walk->next) BUG(); */
		walk->next = ctd->next;
	}
	cxcount--;

	printk("cxadc: reset drv data\n");
	pci_set_drvdata(pci_dev, NULL);
	printk("cxadc: reset drv ok\n");
	kfree(ctd);

	return;
}

MODULE_DEVICE_TABLE(pci, cxadc_pci_tbl);

/* -------------------------------------------------------------- */
static struct pci_driver cxadc_pci_driver = {
        .name     = "cxadc",
        .id_table = cxadc_pci_tbl,
        .probe    = cxadc_probe,
        .remove   = cxadc_remove,
};

static int __init cxadc_init_module(void)
{
	int rv = pci_register_driver(&cxadc_pci_driver);

	return rv;
}

static void __exit cxadc_cleanup_module(void)
{
	pci_unregister_driver(&cxadc_pci_driver);
	return;
}

module_init(cxadc_init_module);
module_exit(cxadc_cleanup_module);

module_param(latency, int, 0644);
module_param(audsel, int, 0644);
module_param(vmux, int, 0644);
module_param(level, int, 0644);
module_param(tenbit, int, 0644);
module_param(tenxfsc, int, 0644);

//audsel=2 is taken from tuner stereo out ?
//vmux=2 is taken from 2nd IF (after hardware mod)
//vmux=1 is taken from video in

MODULE_DESCRIPTION("cx2388xx adc driver");
MODULE_AUTHOR("Hew How Chee");
MODULE_LICENSE("GPL");

