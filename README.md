# cxadc (CX - Analogue Digital Converter )

cxadc is an alternative Linux driver for the Conexant CX2388x video
capture chips used on many PCI TV cards and cheep PCIE (with 1x bridge chip) capture cards It configures the CX2388x to
capture raw 8bit or 16bit unsigned samples from the video input ports, allowing these cards to be
used as a low-cost 28-54Mhz 10bit ADC for SDR and similar applications.

The regular cx88 driver in Linux provides support for capturing composite
video, digital video, audio and the other normal features of these
chips. You shouldn't load both drivers at the same time.

## Ware to find current PCIE CX CX23881 cards and notes

https://www.aliexpress.com/item/1005003461248897.html - White Variation

https://www.aliexpress.com/item/1005003092946304.html - Green Variation

https://www.aliexpress.com/item/4001286595482.html    - Blue Variation

Note 01: For reliable 40Mhz 8-bit and 20mhz 16-bit samples the recommended crystal is the `ABLS2-40.000MHZ-D4YF-T`.

Note 02: Asmedia PCI to PCIE 1x bridge chips may have support issues on some older PCH chipsets intel 3rd gen, for example, white cards use ITE chips which might not have said issue.

Note 03: Added cooling can provide stability more so with 40-54mhz crystal mods, but within 10° Celsius of room temperature is always preferable for silicone hardware but currently only 40mhz mods have been broadly viable in testing.

Note 04: For crystals over 54mhz it might be possible to use higher crystals with self temperature regulated isolated chamber models but this is still to have proper testing.

Note 05: List of tested working crystals: Native SMD crystal on current PCIE 1x cards is a `HC-49/US` type

Note 06: The CX chip verient with the least self-noise is the cx23883 mostly found on the White Variation card with most clean captures at the 6dB off and Digital Gain at 0-10.

`40MHz` - ABRACON ABLS2-40.000MHZ-D4YF-T 18pF 30ppm `HC-49/US`

`40MHz` - Diodes Incorporated FL400WFQA1 7pF 10ppm `SMD Package`

`48MHz` - ECS ECS-480-18-33-AGM-TR 18pF 25ppm  `SMD Package`

`50MHz` - ECS ECS-500-18-33-AGM-TR 18pF 30ppm  `SMD Package`

`54MHz` - ECS ECS-540-8-33-JTN-TR 8pF 20ppm `SMD Package`

## Getting started

Open the directory that you wish to install into git pull to pull down the driver into a directory of your choice and use the following:

`git clone https://github.com/happycube/cxadc-linux`

You can then use `git pull` to update later

or

Click code then download zip and extract files to the directory you wish to use CXADC in then open a terminal in said directory

Once files have been acquired then proceed to do the following:

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

`depmod -a` enables auto load on start-up

Build the level adjustment tool:

	gcc -o leveladj leveladj.c

Install PV to allow real-time monitoring of the runtime & datarate. (may have dropped samples on lower end setups)

    sudo apt install pv

## Configuration and Capturing

Connect a signal to the input you've selected, and run `leveladj` to
adjust the gain automatically:

	./leveladj

To use PV modify command with `/dev/cxadc0 |pv >` it will look like this when in use:

    cat /dev/cxadc0 |pv > output.raw
    0:00:04 [38.1MiB/s] [        <=>  

Open Terminal in the directory you wish to write the data this to capture 10 seconds of test samples:

    timeout 10s dd if=/dev/cxadc0 |pv > of=out.raw

`dd` and `cat` can also be used to trigger captures and the `|pv >` argument enables data readout.

Use CTRL+C to manually stop capture.

`timeout 30s` at the start of the command will run capture for 30 seconds for example.

`sox -r 28636363` etc can be used to resample to the sample rate specified ware as cat/dd will just do whatever has been pre-defined by parameters set bellow.

Note: Use with (S)VHS & LD-Decode projects .u16 for 16-bit samples and .8u for 8-bit samples should replace .raw extention this allows the software to detect the data and use it before flac compression to .vhs/.svhs & .ldf and so on.

## Module Parameters

Most of these parameters (except `latency`) can be changed using sysfs
after the module has been loaded. Re-opening the device will update the
CX2388x's registers.

To change configuration open the terminal and use the following command to change driver config settings.

X = Number Setting i.e  `0`  `1`  `2`  `3`  etc

Y = Parameter setting i.e `vmux` `level` etc

sudo echo X >/sys/module/cxadc/parameters/Y

Example: `sudo echo 1 >/sys/module/cxadc/parameters/vmux`

### `vmux` (0 to 3, default 2)

### How to select the physical input to capture.

A typical TV card has a tuner,
a composite input with RCA/BNC ports and S-Video inputs tied to three of these inputs; you
may need to experiment the quickest way is to attach a video signal and see a white flash on signal hook-up and change vmux until you get something.

PAL:
`sudo ffplay -hide_banner -async 1 -f rawvideo -pixel_format gray8 -video_size 2291x625 -i /dev/cxadc0 -vf scale=1135x625,eq=gamma=0.5:contrast=1.5`

NTSC:
`ffplay -hide_banner -async 1 -f rawvideo -pix_fmt gray8 -video_size 2275x525 -i /dev/cxadc0 -vf scale=910x525,eq=gamma=0.5:contrast=1.5`

### `audsel` (0 to 3, default none)

Some TV cards (e.g. the PixelView PlayTV Pro Ultra) have an external
multiplexer attached to the CX2388x's GPIO pins to select an audio
channel. If your card has one, you can select an input using this
parameter.

On the PlayTV Pro Ultra:
- `audsel=0`: tuner tv audio out?
- `audsel=1`: silence?
- `audsel=2`: FM stereo tuner out?
- `audsel=3`: audio in to audio out

### `latency` (0 to 255, default 255)

The PCI latency timer value for the device.

### `sixdb` (0 or 1, default 1)
Enables or disables a default 6db gain applied to input signal (can result in cleaner capture)

`1` = On

`0` = Off

### `level` (0 to 31, default 16)

The fixed digital gain to be applied by the CX2388x

(`INT_VGA_VAL` in the datasheet).

Adjust to minimise clipping; `./leveladj` will do this
for you automatically.

### `tenxfsc` (0 to 2, default 0)

By default, cxadc captures at a rate of 8 x fSc (8 * 315 / 88 Mhz, approximately 28.6 MHz)

tenxfsc - Sets sampling rate of the ADC based off the crystal's frequency

`0` = Native crystal frequency i.e 28 (default), 40, 50, 54, etc

`1` = Native crystal frequency times 1.25

`2` = Native crystal frequency times 1.4

With the Stock 28Mhz Crystal the modes are the following:

`0` = 28.6 MHz 8bit

`1` = 35.8 MHz 8bit

`2` = 40.04 MHz 8bit

### `tenbit` (0 or 1, default 0)

By default, cxadc captures unsigned 8-bit samples.

In mode 1, unsigned 16-bit mode, the data is resampled (down converted) by 50%

`0` = 8xFsc 8-bit data mode (Raw Unsigned Data)

`1` = 4xFsc 16-bit data mode (Filtered Vertical Blanking Interval Data)

When in 16bit sample modes change to the following:

`14.3 MHz 16-bit` - Stock Card

`17.9 MHz 16-bit` - Stock Card

`20.02 MHz 16-bit` - Stock Card

Note!

`40/20mhz modes` have a rare chance of working on stock cards its recommended to just replace the stock crystal with ABLS2-40.000MHZ-D4YF-T for more reliable results.

## Other Tips

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

### 2021-12-14 - Updated Documentation

- Change 10bit to the correct 16bit as that's what's stated in RAW16 under the datasheet and that's what the actual samples are in format-wise.
- Cleaned up and added examples for adjusting module parameters and basic real-time readout information.
- Added notations of ABLS2-40.000MHZ-D4YF-T a drop-in replacement crystal that adds 40mhz ability at low cost for current market PCIE cards.
- Added documentation for sixdb mode selection.
- Added links to find current CX cards
- Added issues that have been found
- Added crystal list of working replacements
