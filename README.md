# cxadc

cxadc is an alternative Linux driver for the Conexant CX2388x video
capture chips used on many PCI TV cards and cheep PCIE (with 1x bridige chip) capture cards It configures the CX2388x to
capture raw 8bit or 16bit unsigned samples from the input ports, allowing these cards to be
used as a low-cost 28-40Mhz 10bit ADC for SDR and similar applications.

The regular cx88 driver in Linux provides support for capturing composite
video, digital video, audio and the other normal features of these
chips. You shouldn't load both drivers at the same time.

## Getting started

Click code then download zip and extract files to directory you wish to use CXADC in then open a terminal in said directory

    sudo -s 
	
After inputing your password this terminal will be in root mode this saves typing in your password sevral times.
	
Build and install the out-of-tree module:

	make && sudo make modules_install && sudo depmod -a

If you see the following error, ignore it:

        At main.c:160:
        - SSL error:02001002:system library:fopen:No such file or directory: ../crypto/bio/bss_file.c:69
        - SSL error:2006D080:BIO routines:BIO_new_file:no such file: ../crypto/bio/bss_file.c:76
        sign-file: certs/signing_key.pem: No such file or directory
        Warning: modules_install: missing 'System.map' file. Skipping depmod.

This error just means the module could not be signed. It will still be installed.

Install configuration files::

	sudo cp cxadc.rules /etc/udev/rules.d
	sudo cp cxadc.conf /etc/modprobe.d

Now reboot and the modules will be loaded automatically. The device node will
be called `/dev/cxadc0`. The default cx88 driver will be blacklisted by cxadc.conf.
Module parameters can also be configured in that file.

If there is an issue just re-load the CXADC module from the install directory via terminal

`sudo rmmod cxadc
make
make modules_install
depmod -a`

`depmod -a` makes it auto load on startup

Build the level adjustment tool:

	gcc -o leveladj leveladj.c

Install PV to allow real-time monitoring of the runtime & datarate. (may have droped samples on lower end setups) 

    sudo apt install pv

## Configiration and Capturing

Connect a signal to the input you've selected, and run `leveladj` to
adjust the gain automatically:

	./leveladj

Open Terminal in the directory you wish to write the data this to capture 10 seconds of test samples:

	sox -r 28636363 -b 8 -c 1 -e unsigned -t raw /dev/cxadc0 capture.wav trim 0 10

To use PV modify command with `/dev/cxadc0 |pv >` it will look like this when in use:

     cat /dev/cxadc0 |pv > output.wav
     0:00:04 [38.1MiB/s] [        <=>  

`dd` and `cat` can also be used to trigger captures for example:

     timeout 10s dd if=/dev/cxadc0 |pv > of=out.wav

Use CTRL+C to manualy stop capture.

`timeout 30s` at the start of the command will run capture for 30seconds for example.

## Module parameters

Most of these parameters (except `latency`) can be changed using sysfs
after the module has been loaded. Re-opening the device will update the
CX2388x's registers.

To change configirtation open the terminal and use the following command to change driver config settings.

X = Number Setting i.e  `0`  `1`  `2`  `3`  etc

Y = Parameter seting i.e `vmux` `level` etc

sudo echo X >/sys/module/cxadc/parameters/Y

Example: `sudo echo 1 >/sys/module/cxadc/parameters/vmux`

### `vmux` (0 to 3, default 2)

Select the physical input to capture. 

A typical TV card has a tuner,
composite input with RCA/BNC ports and S-Video inputs tied to three of these inputs; you
may need to experiment quickest way is to attach a video signal and run 

### `audsel` (0 to 3, default none)

Some TV cards (e.g. the PixelView PlayTV Pro Ultra) have an external
multiplexer attached to the CX2388x's GPIO pins to select an audio
channel. If your card has one, you can select an input using this
parameter.

On the PlayTV Pro Ultra:
- `audsel=0`: tuner tv audio out?
- `audsel=1`: silence?
- `audsel=2`: fm stereo tuner out?
- `audsel=3`: audio in to audio out

### `latency` (0 to 255, default 255)

The PCI latency timer value for the device.

### `sixdb` (0 or 1, default 1)
Enables or disabled a defualt 6db gain applied to input signal

`1` = On 

`0` = Off 

### `level` (0 to 31, default 16)

The fixed digital gain to be applied by the CX2388x (`INT_VGA_VAL` in
the datasheet). Adjust to minimise clipping; `leveladj` will do this
for you automatically.

### `tenxfsc` (0 to 2, default 0)

By default, cxadc captures at a rate of 8 x fSc (8 * 315 / 88 Mhz
tenxfsc - sets the sample rate

`0` = 28.6 MHz 8bit 

`1` = 35.8 MHz 8bit

`2` = 40.0 Mhz 8bit with ABLS2-40.000MHZ-D4YF-T crystal via tap de/solder (stock cards have rare chance of working)

### `tenbit` (0 or 1, default 0)

By default, cxadc captures unsigned 8-bit samples.

In mode 1 unsigned 16-bit mode, the sample rate is halved.

`0` = 8xFsc 8-bit data mode (Raw Data)
 
`1` = 4xFsc 16-bit data mode (Filtered VBI data)

When in 16bit sample modes change to the following:

14.3 MHz 16-bit

17.9 MHz 16-bit

20.0 MHz 16-bit with ABLS2-40.000MHZ-D4YF-T crystal via tap de/solder (stock cards have rare chance of working)

## Other Tips

### Ware to find current PCIE CX CX23881 cards 
https://www.aliexpress.com/item/1005003461248897.html?spm - White Veriation
https://www.aliexpress.com/item/1005003133382186.html?spm - Green Veriation
https://www.aliexpress.com/item/4001286595482.html?spm    - Blue Veriation

Note: Asmedia PCI to PCIE 1x bridge chips may have drivers issues on some older PCH chipsets.

### Accessing registers directly from userspace

This can be done with the pcimem tool. Get it from here:

    https://github.com/billfarrow/pcimem

Build it with `make`. To use, consult `lspci` for the PCI address of
your card. This will be different depending on motherboard and slot.
Example output:

    03:00.0 Multimedia video controller: Conexant Systems, Inc. CX23880/1/2/3 PCI Video and Audio Decoder (rev 05)
    03:00.4 Multimedia controller: Conexant Systems, Inc. CX23880/1/2/3 PCI Video and Audio Decoder [IR Port] (rev 05)

Here we want the video controller at address `03:00.0` - not the
IR port. Next you need to find that device in sysfs. Due to topology
it can be nested under other devices. The quickest way to find it:

    find /sys/devices -iname '*03:00.0'

Output:

    /sys/devices/pci0000:00/0000:00:1c.5/0000:02:00.0/0000:03:00.0

To use pcimem we take that filename and add "/resource0" to the end.
Then to read a register we do this:

    ./pcimem /sys/devices/pci0000:00/0000:00:1c.5/0000:02:00.0/0000:03:00.0/resource0 0x2f0000

0x2f0000 is the device ID register and it should begin with 0x88.
Output:

    0x2F0000: 0x880014F1

To write to a register, specify a value after the address.

## History

### 2005-09-25 - v0.2

cxadc was originally written by Hew How Chee (<how_chee@yahoo.com>).
See [SDR using a CX2388x TV+FM card](http://web.archive.org/web/20091027150612/http://geocities.com/how_chee/cx23881fc6.htm) for more details.

- added support for i2c, use `i2c.c` to tune
- set registers to lower gain
- set registers so that no signal is nearer to sample value 128
- added `vmux` and `audsel` as params during driver loading
  (for 2nd IF hardware modification, load driver using `vmux=2`).
  By default `audsel=2` is to route tv tuner audio signal to
  audio out of TV card, `vmux=1` to use the signal from video in of tv card.

### 2007-03-24 - v0.3

- change code to compile and run in kernel 2.6.18 (Fedora Core 6)
  for Intel 32 bit single processor only
- clean up mess in version 0.2 code

### 2013-12-18 - v0.4

This version has been retargeted for Ubuntu 13.10 Linux 3.11 by
Chad Page (<Chad.Page@gmail.com>).

While still a mess, the driver has been simplified a bit.  Data is now read
using standard `read()` semantics, so no capture program is needed like the original
version.

For the first time, it also runs on 64-bit Linux, and *seems* to be OK under
SMP.

### 2019-06-09 - v0.5

- Update to work with Linux 5.1; older versions should still work.
- Tidy up the code to get rid of most of the warnings from checkpatch,
  and bring it closer in style to the normal cx88 driver.
- Make `audsel` optional.
- Don't allow `/dev/cxadc` to be opened multiple times.
- When unloading cxadc, reset the AGC registers to their default values.
  as cx88 expects. This lets you switch between cxadc and cx88 without
  rebooting.

### 2021-11-28 - Updated Documentaton

- Change 10bit to the correct 16bit as thats whats stated in RAW16 under the datasheet and thats what the actual samples are in format wise.
- Cleaned up and added examples for ajusting module parameters and basic real time readout information.
- Added notations of ABLS2-40.000MHZ-D4YF-T a drop in repalcement crystal that adds 40mhz abbility at low cost for current market PCIE cards.
- Added doumentation for sixdb mode selection.