
Notes for Linux 3.5 version 0.1 (cxadc 0.4) 15 Apr 2013
=======================================================

This version has been retargeted for ubuntu 12.04 on 32-bit x86 running Linux 3.5.

While still a mess, the driver has been simplified a bit.  Data is now read
using standard read() semantics, no capture program is needed like the original 
version.

Caveats:

- Still need to mknod
- Does not support runtime parameters
- Not tested (and probably doesn't work) under 64-bit

- generally nowhere near linux coding standards

- Chad Page
Chad.Page@gmail.com

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

Version 0.2 or higher of the driver let you set the ADC input source during driver loading. Use "vmux=X"  to do this,
X=0 to 3


Hew How Chee
how_chee@yahoo.com
