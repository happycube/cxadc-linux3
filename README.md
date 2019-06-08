cxadc-linux3
============

CX2388x direct ADC capture driver, updated for Linux 3.11

Notes for Linux 3.x version 0.2 (cxadc 0.4+) 18 Dec 2013
========================================================

This version has been retargeted for ubuntu 13.10 Linux 3.11.

While still a mess, the driver has been simplified a bit.  Data is now read
using standard read() semantics, so no capture program is needed like the original
version.

For the first time, it also runs on 64-bit Linux, and *seems* to be OK under
SMP.

Caveats:

- Still need to mknod
- Does not support runtime parameters yet

- generally nowhere near linux coding standards

- Chad Page
Chad.Page@gmail.com

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


----
Notes for cxadc.c and cxcap.c  24 Mac 2007
==========================================

The purpose of these program is to get continuous  8 bit ADC raw data from
the CX2388x based TV cards. TV tuner functions are not supported. Register to
initialized sampling rate is not used. By default, after reset, sampling rate is
27 Mhz.

These program are developed on Fedora Core 6. (kernel 2.6.18)


cxadc.c is the driver for CX2388x TV card
cxcap.c is the program and get the raw data from the driver and store it into file.

These codes are a bit messy since I have no time to clean up. It might contain bugs
but at least it runs (on my machine).

Quick procedure to get it running (capture from video in of card)
=================================================================

Instruction may be specific to PixelView PlayTV Pro Ultra TV card

WARNING : Before trying, make sure you have backup your important data
just in case.

a) copy cxadc.c and cxcap.c to a new folder on the hard disk (mounted r/w)

b) run 'make' to compile cxadc.c

c) to compile cxcap, type in

       gcc cxcap.c -o cxcap

d) create device node with the following command. (make sure you are super user)

       mknod /dev/cxadc c 126 0

e) install driver

       insmod ./cxadc.ko

f) check 'dmesg' output to see whether it is similiar to the one
   shown in the cxadc.c source code

g) If it is ok, run cxcap to capture 8Mbyte of raw data. The signal source is feed into the
   video input of the CX2388x tv card.

        ./cxcap

h) Check 'dmesg' output to see the result similiar to one shown in cxadc.c

i) The output is saved in raw.pcm as 8 bit unsigned.

j) stop the driver by using the command

	rmmod cxadc

k) to get another capture , repeat e), g), and j).
   (note this step might not be necessary but for the mean time, we'll
    just stop and start the driver again to be safe).

   note : after installing driver, DMA is always running. To stop DMA, you need to remove driver
          if you don't stop, you can run cxcap to capture again.


These codes runs are meant for Intel 32 bit single processor only. My machine is a Dell Pentium III
800 MHz PC.

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



Version 0.2 or higher of the driver let you set the ADC input source during driver loading. Use "vmux=X"  to do this,
X=0 to 3

//vmux=2 is taken from 2nd IF (after hardware mod)
//vmux=1 is taken from video in

On the Pixelview PlayTVPro Ultracard:
audsel:
	3=audio in to audio out
	2=fm stereo tuner out?
	1=silence ?
	0=tuner tv audio out?

Hew How Chee
how_chee@yahoo.com
