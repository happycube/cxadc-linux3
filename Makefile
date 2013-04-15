
xyyCFLAGS = -I/usr/src/kernels/2.6.18-1.2798.fc6-i586/include -I/usr/src/kernels/2.6.18-1.2798.fc6-i586/include/asm-i386/mach-default
xxxCFLAGS =  -I/usr/src/kernels/2.6.18-1.2798.fc6-i586/include -I/usr/include/asm/mach-default



UNAME := $(shell uname -r)
#KERNEL26 := 2.6
#KERNELVERSION := $(findstring $(KERNEL26),$(UNAME))

obj-m	:= cxadc.o

INCLUDE	:= -I/usr/include/asm/mach-default/
KDIR	:= /lib/modules/$(shell uname -r)/build
PWD		:= $(shell pwd)

all::
	$(MAKE) -C $(KDIR) $(INCLUDE) SUBDIRS=$(PWD) modules

clean::
	$(MAKE) -C $(KDIR) $(INCLUDE) SUBDIRS=$(PWD) clean 
	

