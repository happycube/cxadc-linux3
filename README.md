# cxadc

cxadc is an alternative Linux driver for the Conexant CX2388x video
capture chips used on many PCI TV cards. It configures the CX2388x to
capture raw samples from the input ports, allowing these cards to be
used as a low-cost 28Mhz ADC for SDR and similar applications.

The regular cx88 driver in Linux provides support for capturing composite
video, digital video, audio and the other normal features of these
chips. You shouldn't load both drivers at the same time.

## Getting started

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

Build the level adjustment tool:

	gcc -o leveladj leveladj.c

Connect a signal to the input you've selected, and run `leveladj` to
adjust the gain automatically:

	./leveladj

Open `/dev/cxadc0` and read samples. For example, to capture 10 seconds
of samples:

	sox -r 28636363 -b 8 -c 1 -e unsigned -t raw /dev/cxadc0 capture.wav trim 0 10

## Module parameters

Most of these parameters (except `latency`) can be changed using sysfs
after the module has been loaded. Re-opening the device will update the
CX2388x's registers.

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

### `level` (0 to 31, default 16)

The fixed digital gain to be applied by the CX2388x (`INT_VGA_VAL` in
the datasheet). Adjust to minimise clipping; `leveladj` will do this
for you automatically.

### `tenbit` (0 or 1, default 0)

By default, cxadc captures unsigned 8-bit samples. Set this to 1 to
capture 10-bit samples, which will be returned as unsigned 16-bit
values. In 10-bit mode, the sample rate is halved.

### `tenxfsc` (0 to 3, default 0)

By default, cxadc captures at a rate of 8 x fSc (8 * 315 / 88 Mhz,
approximately 28.6 MHz). Set this to 1 to capture at 10 x fSc
(approximately 35.8 MHz). Set this to 2 to capture at 40 Mhz. Set
this to 3 when using a 64 Mhz crystal to capture at 40 Mhz.

### `vmux` (0 to 3, default 2)

Select the CX2388x input to capture. A typical TV card has the tuner,
composite input and S-Video inputs tied to three of these inputs; you
may need to experiment (or look at the cx88 source) to work out which
input you need.

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
