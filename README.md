## CXADC (CX - Analogue-Digital Converter)


cxadc is an alternative Linux driver for the Conexant CX2388x series of video decoder/encoder chips used on many PCI TV tuner and capture cards.

The new driver configures the CX2388x to capture in its raw output mode in 8-bit or 16-bit unsigned samples from the video input ports, allowing these cards to be used as a low-cost 28-54mhz 10bit ADC for SDR and similar applications.

> [!NOTE]  
> `CX23885-xx` & `CX23888-xx` are incompatible chips, however the `CX25800` is a compatible chip.


<img src="https://github.com/happycube/cxadc-linux3/wiki/assets/images/CX-Cards/CX-Card-White-Frount-High-Res-Scaled-2022.12.21.png"  width="500" height="">

<img src="https://raw.githubusercontent.com/wiki/happycube/cxadc-linux3/assets/images/Diagrams/CXADC-Driver-Basic.png"  width="600" height="">


Today the cheap PCIe (with 1x bridge chip) capture card market uses these chips at 16-35USD prices per card, directly from China.

The regular cx88 driver in Linux provides support for capturing composite
video, digital video, audio and the other normal features of these chips.

> [!WARNING]  
>  You shouldn't load both drivers at the same time.


# Wiki


There is now a [wiki](https://github.com/happycube/cxadc-linux3/wiki) about the cards variants and helpful information on modifications cabling and amplification.


## Where to find current PCIe 1x CX2388x cards & notes:



Links to buy a CX Card: 

- Current CX White CX25800 Card Order Links [Link 1](https://s.click.aliexpress.com/e/_olUXYFh) / [Link 2](https://s.click.aliexpress.com/e/_DBBRKPR) / [Link 3](https://s.click.aliexpress.com/e/_Dkhwebf) (16~30 USD) (Recommended as it has the better CX25800 IC)
- [Blue Variant](https://s.click.aliexpress.com/e/_DFDQaJh)

**Note 00:** While `Mhz` is used and is accurate due to the crystal used, in reality, it should be called `MSPS` (million samples per second) as the actual effective sampled is half the Mhz number of the defined crystal/clock rate.

**Note 01:** The CX chip variant with the least self-noise is the CX25800, mostly found on the White Variation card; most clean captures are at 6dB off, and Digital Gain at 0-10 with external amplification and or proper impedance matching.

**Note 02:** For reliable 40mhz 8-bit & 20mhz 16-bit samples, it is recommended to replace the stock crystal with a `ABLS2-40.000MHZ-D4YF-T` or equivalent fundamental crystal.

[For the full list of working crystal replacements, check the wiki page here!](https://github.com/happycube/cxadc-linux3/wiki/Crystal-Upgrades)

**Note 03:** Asmedia PCI to PCIe 1x bridge chips may have support issues on some older PCH chipsets based on Intel 3rd gen; for example, white cards use ITE chips which might not have said issue.

**Note 04:** Added cooling can provide additional stability, more so with 40-54mhz crystal mods, but within 10° Celsius of room temperature is always preferable for silicone hardware. Currently, only 40-54mhz crystal mods have been broadly viable in testing for current white PCIe cards.

**Note 05:** For crystals over 54mhz: it might be possible to use higher crystals with self-temperature regulated isolated chamber models, but this is still to have proper testing.

**Note 06:** While the term Mhz is used and is hardware accurate, to be clear with Nyquist sampling the crystal frequency should be noted as the MSPS or million samples per second rating, the number is always halved to equal its effective bandwidth of whatever its sampling i.e 28mhz is 28msps with 14mhz of bandwidth and so on you want a 2:1 ratio or higher of whatever your capturing to correctly sample it.

**Note 07:** When using lower-end older systems (Pentium 4 and before era), if there are not enough system resources, you may have dropped samples, this also applies to any use of SoX or FLAC in real-time.


# Getting Started & Installation


## Install Dependencies


</details>

<details closed>
<summary>Ubuntu 22.04</summary>
<br>

Update your package manager

    sudo apt update 

Install build essentials 

    sudo apt install build-essential

Install Linux Headers

    sudo apt install linux-headers-generic

Install PV for real-time monitoring of the runtime & datarate output:

    sudo apt install pv

Install Sox key for manipulating data in real-time or more usefully after captures:

    sudo apt install sox

Install FFmpeg (If you don't already have it!)

    sudo apt install ffmpeg

Install FLAC (If you don't already have it!)

    sudo apt install flac 

</details>

<details closed>
<summary>Raspberry Pi OS on Raspberry Pi 4 or 5 with PCIe adapter</summary>
<br>

As Ubuntu 22.04, but install `raspberrypi-kernel-headers` instead of `linux-headers-generic`,
and then add the following to the end of `/boot/firmware/config.txt`:

    [all]
    dtoverlay=pcie-32bit-dma


</details>


## Install CXADC


Pull the driver via terminal

Open a terminal window and git clone the repository:

    git clone https://github.com/happycube/cxadc-linux3 cxadc

For offline install simply, click `Code` on the GitHub page and then download the zip. 

Move the zip to your home directory into a folder called `cxadc` and extract the files.

Afterward, open a terminal in the said directory and continue below.


## How to Update 


You can then use `git pull` inside the directory to update later and then re-build the driver with the steps below again or likewise manually re-download the files.


## Build The Driver


If not already inside of the CXADC directory

    cd cxadc

Build and install the out-of-tree module:

    make && sudo make modules_install && sudo depmod -a

If you see the following error, ignore it:

    At main.c:160:
    - SSL error:02001002:system library:fopen:No such file or directory: ../crypto/bio/bss_file.c:69
    - SSL error:2006D080:BIO routines:BIO_new_file:no such file: ../crypto/bio/bss_file.c:76
    sign-file: certs/signing_key.pem: No such file or directory
    Warning: modules_install: missing 'System.map' file. Skipping depmod.

This error just means the module could not be signed. It will still be installed.

Install configuration files:

    sudo cp cxadc.rules /etc/udev/rules.d
    sudo cp cxadc.conf /etc/modprobe.d

Now reboot and the modules will be loaded automatically. The device node will
be called `/dev/cxadc0`. The default cx88 driver will be blacklisted by cxadc.conf.
Module parameters can also be configured in that file.

If there is an issue just re-load the CXADC module from the install directory via terminal

    sudo rmmod cxadc
    make
    sudo make modules_install
    sudo depmod -a

`depmod -a` enables auto load on start-up

You can then install scripted commands to help operate and verify your configuration.

Check the `utils folder` and the associated README for quicker and more simplified commands.

To enable short system wide commands, first change into the utils directory from the cxadc source folder:

    cd utils

Then install the system links with:

    sudo ./inst_scripts 


## Troubleshooting

> [!WARNING]  
> Secure boot is the most common issue with many PCI/PCIe devices on Linux very much so video class devices such as BMD SDI hardware. 

If the kernal is updated, the driver will need a re-install unless [DKMS](https://askubuntu.com/questions/408605/what-does-dkms-do-how-do-i-use-it) is setup.

If you see this error:

    arch/x86/Makefile:142: CONFIG_X86_X32 enabled but no binutils support
      INSTALL /lib/modules/5.15.0-92-generic/extra/cxadc.ko
      SIGN    /lib/modules/5.15.0-92-generic/extra/cxadc.ko
      DEPMOD  /lib/modules/5.15.0-92-generic
    Warning: modules_install: missing 'System.map' file. Skipping depmod.
    make[1]: Leaving directory '/usr/src/linux-headers-5.15.0-92-generic'


> [!CAUTION]  
> Ensure secure boot is disabled before doing anything else.

Try Install Binutils 

    apt install binutils

If issues with binutills persists just use a different Kernel like [Xanmod](https://xanmod.org/) has been tested as a fix to the issue.


# Configuration of Capture Settings


Most of these parameters (except `latency`) can be changed using sysfs
after the module has been loaded. Re-opening the device will update the
CX2388x's registers. If you wish to be able to change module parameters
as a regular users (e.g. without `sudo`), you need to run the command:

    sudo usermod -a -G video YourUbuntuUserName

To change configuration open the terminal and use the following command to change driver config settings.


## Module Parameters


> [!CAUTION]  
> - Configuration will reset on every re-boot of the system.
> - If using a fixed input/gain configuration update the `cxadc.rules` file inside the `etc/udev/rules.d` directory, this will save you from having to manually copy-paste change values on each system restart. 

> [!NOTE]  
> You can use `cxvalues` to check your current configuration state at anytime globally on the terminal. 

X = Number Setting i.e  `0`  `1`  `2`  `3`  etc

Y = Parameter setting i.e `vmux`, `level` etc

echo X >/sys/class/cxadc/cxadc0/device/parameters/Y

Example: `echo 1 >/sys/class/cxadc/cxadc0/device/parameters/vmux`

> [!WARNING]  
> Also see the `utils` folders for scripts to manipulate these values; sudo will be required unless you add your local user to the `video` group as mentioned above.


## `Multi Card Usage`


In single card capture mode this is `cat /dev/cxadc0` 

With multi card this will be `cat /dev/cxadc1` and so on for card 2 and 3 etc 

Same for parameters

`sudo echo 1 >/sys/class/cxadc/cxadc0/device/parameters/vmux` 

This changes to 

`sudo echo 1 >/sys/class/cxadc/cxadc1/device/parameters/vmux`

This can go up to 256, but real world use we don't expect more then 8-16 per system.

> [!NOTE]  
> Each card has a separate set of entries in the `cxadc.rules` file also.


## `vmux` (0 to 3, default 2) select physical input to capture.


[Check the Wiki](https://github.com/happycube/cxadc-linux3/wiki/Types-Of-CX2388x-Cards) for the optimal way to connect your card type!

A typical TV card has a tuner, a composite input with RCA or BNC ports and S-Video input, tied to three of these inputs; you
may need to experiment with inputs. The quickest way is to attach a video signal and see a white flash on signal hook-up, and change vmux until you get something.


> [!TIP]  
> On current White CX cards `VMUX 0 is S-Video` and `VMUX 1 is RCA input`. 

## Commands to Check for Signal Burst


Create a video preview of signal. Depending on the RF signal type, you will get an unstable video or just a white flash on cable connection. 

(Using video_size values to give approximately the correct resolution for the default 28.64 Mhz sample rate)

PAL:

`sudo ffplay -hide_banner -async 1 -f rawvideo -pixel_format gray8 -video_size 1832x625 -i /dev/cxadc0 -vf scale=1135x625,eq=gamma=0.5:contrast=1.5`

NTSC:

`sudo ffplay -hide_banner -async 1 -f rawvideo -pixel_format gray8 -video_size 1820x525 -i /dev/cxadc0 -vf scale=910x525,eq=gamma=0.5:contrast=1.5`


## `audsel` (0 to 3, default none)


Some TV cards (e.g. the PixelView PlayTV Pro Ultra) have an external
multiplexer attached to the CX2388x's GPIO pins to select an audio
channel. If your card has one, you can select the input using this
parameter.

On the PlayTV Pro Ultra:
- `audsel=0`: tuner tv audio out?
- `audsel=1`: silence?
- `audsel=2`: FM stereo tuner out?
- `audsel=3`: audio in to audio out


## `latency` (0 to 255, default 255)


The PCI latency timer value for the device.


## `sixdb` (0 or 1, default 1)


Enables or disables a default 6db gain applied to the input signal (Disabling this can result in cleaner capture but may require an external amplifier)

`1` = On

`0` = Off


## `level` (0 to 31, default 16)


The fixed digital gain to be applied by the CX2388x

(`INT_VGA_VAL` in the datasheet).

Adjust to minimise clipping; `./leveladj` will do this
for you automatically.

To change the card witch add the `-h` flag followed by the card so `./leveladj -h 1` for card 2 for example.


## `tenxfsc` (0 to 2, 10 to 99, or 10022728 to "see below", default 0)


By default, cxadc captures at a rate of 8 x fsc (8 * 315 / 88 Mhz, approximately 28.6 MHz)

tenxfsc - Sets sampling rate of the ADC based on the crystal's native frequency

`0` = Native crystal frequency i.e 28MHz (default), 40, 50, 54, (if hardware modified)

`1` = Native crystal frequency times 1.25


With the Stock 28.6Mhz Crystal the modes are the following:

`0` = 28.6 MHz 8-bit

`1` = 35.8 MHz 8-bit (Upsampled)


> [!NOTE]  
> For 40mhz 8-bit native or 20mhz 16-bit (10-bit scaled to 16-bits) sampling please use the [clockgen or crystal replacement](https://github.com/happycube/cxadc-linux3/wiki/Modifications) hardware mods for reliable results & lower noise.

Alternatively, enter 2 digit values (like 20), that will then be
multiplied by 1,000,000 (so 20 = 20,000,000sps), with the caveat
that the lowest possible rate is a little more than 1/3 the actual
`HW Crystal` rate (HW crystal / 40 * 14). For stock 28.6mhz crystal,
this is about 10,022,728sps.

For a 40mhz crystal card, the lowest
rate will be 14,000,000sps. The highest rate is capped at the
10fsc rate, or:  HW crystal / 8 * 10.

Full-range sample values can also be entered: 14318181 for instance.
Again, the caveat is that the lowest possible rate is:
HW crystal / 40 * 14 and the highest allowed rate is:
HW crystal / 8 * 10.

Values outside the range will be converted to the lowest / highest
value appropriately. Higher rates may work, with the max rate depending
on individual card and cooling, but can cause system crash for others,
so are prevented by the driver code (increase at your own risk).


## `tenbit` (0 or 1, default 0)


By default, cxadc captures unsigned 8-bit samples.

In mode 1, unsigned 16-bit mode, the data is resampled (down-converted) by 50%

`0` = 8xFsc 8-bit data mode (Raw Unsigned Data)

`1` = 4xFsc 16-bit data mode (Filtered Vertical Blanking Interval Data)

When in 16bit sample modes, change to the following:

`14.3 MHz 16-bit` - Stock Card

`17.9 MHz 16-bit` - Stock Card


## `crystal` (? - 54000000,  default 28636363)


The Mhz of the physical XTAL crystal on your CX Card.
The stock crystal is usually a 28636363 (28.6Mhz) fundamental type, but a 40mhz replacement crystal is easily available and crystals as high as 54mhz have been shown to work (with
extra cooling required above 40mhz).  

This value is ONLY used to compute the sample rates entered for the tenxfsc parameters other than 0, 1, 2.


## `center_offset` (0 to 255, default 2)


This option allows you to manually adjust DC center offset or the centering of the RF signal you wish to capture.

> [!TIP]  
> You can visually adjust the DC offset line with this handy [GNURadio Script](https://github.com/tandersn/GNRC-Flowgraphs/tree/main/test_cxadc)!

Manual calculation: If the "highest" and "lowest" values returned are equidistant from 0 and 255 respectively, it's cantered.

Use leveladj to obtain level and centring information 

    ./leveladj

Example:

`low 121 high 133 clipped 0 nsamp 2097152`

121-121=0  133+121 = 254 = centred, but: low

`110 high 119 clipped 0 nsamp 2097152`

110-110=0  119+110 = 229 = not centred.


# Capture


## Gain Adjustment


Connect a live or playing signal to the input you've selected, and run `leveladj` to adjust the gain automatically:

    ./leveladj

To use this on multiple different cards 

`./leveladj -d 1` (1 means for device 2/3/4 and so on device 0 is assumed when `-d` is not used)


### Fixed Gain & External Amplifyer Use


You can manually set a fixed gain setting after centering the signal with

`sudo echo 0 >/sys/class/cxadc/cxadc0/device/parameters/level` - Internal Gain (`0`~`31`)

`sudo echo 0 >/sys/class/cxadc/cxadc0/device/parameters/sixdb` - Digital Gain Boost (`1` On / `0` Off)

This is critical to note when trying capture [RAW CVBS](https://github.com/oyvindln/vhs-decode/wiki/CVBS-Composite-Decode) or using an [AD4857](https://github.com/happycube/cxadc-linux3/wiki/Modifications#external-amplification) amplifier with a videotape deck.


### Level Monitoring

Connect a live or playing signal to the input you've selected, and run `levelmon` to monitor the levels:

    ./levelmon

The levels are read from the card and printed to the terminal every 1/4 second.

#### Example Output
```
    / clipped samples low
    |     / min amplitude
    |     |         / avg amplitude (positive side of center)
    |     |         |                / dc offset
    |     |         |                |
lo |0| [  3.906%] ( 33.429%) center -0.54% hi ( 65.764%) [ 95.312%] |0|	nsamp 10000000	rate 44.58
    ^     ^^^^^     ^^^^^^           ^^^^       ^^^^^^     ^^^^^^    ^        ^^^^^^^^       ^^^^^
                                                |          |         |        |              \ calculated sample rate in msps
                                                |          |         |        \ total number of samples collected
                                                |          |         \ clipped samples high
                                                |          \ max amplitude
                                                \ avg amplitude (positive side of center)
```

To use this on multiple different cards 

`./levelmon -d 1` (1 means for device 2/3/4 and so on device 0 is assumed when `-d` is not used)


## Command Line Capture (CLI)


Open a terminal in the directory you wish to write the data to, and use the following example command to capture 10 seconds of test samples.

    timeout 10s cat /dev/cxadc0 |pv > CX_Card_28msps_8-bit.u8

Press <kbd>Ctrl</kbd>+<kbd>C</kbd> to copy then <kbd>Ctrl</kbd>+<kbd>P</kbd> to past the command use <kbd><</kbd>+<kbd>></kbd> to move edit position on the command line to edit the name or command and <kbd>Enter</kbd> to run the command.

`cat` is the default due to user issues with `dd`

To use the PV argument that enables data rate/runtime readout, modify the command with `|pv >`

It will look like this when in use:

    cat /dev/cxadc0 |pv > CX_Card_28msps_8-bit.u8
    0:00:04 [38.1MiB/s] [        <=>  

<kbd>Ctrl</kbd>+<kbd>C</kbd> Will kill the current process, use this to stop the capture manually.

`timeout 10s` defines the capture duration of 10 seconds, this can be defined in `h`ours `m`inutes or `s`econds if a timeout duration is not set it will capture until storage space runs out or is stopped manually.

`sox -r 28636363` etc can be used to resample to the sample rate specified, whereas cat/dd will just do whatever has been pre-defined by the parameters set above.

Note: For use with the decode projects, filetypes `.u8` for 8-bit & `.u16` for 16-bit samples are used instead of `.raw` extension. 

This allows the software to correctly detect the data and use it for decoding or flac compression and renaming to `.vhs`/`.svhs` etc.


### Real-Time FLAC Compressed Capture


Optional but **not optimal** due to risk of dropped samples even with high-end hardware, on the fly FLAC compressed captures are possible with the following commands; edit rates and modes as needed.

> [!WARNING]  
> You need to be on FLAC 1.5.0 or newer to leverage multi-threading for higher reliability capture and real-time compression. 

8-bit Mode (Stock 28.6 MSPS)

    cat /dev/cxadc0 | flac --threads 64 -6 --sample-rate=28636 --sign=unsigned --channels=1 --endian=little --bps=8 --blocksize=65535 --lax -f - -o media-name-28msps-8bit-cx-card.flac

16-bit Mode (Stock 17.8 MSPS)

    cat /dev/cxadc0 | flac --threads 64 -6 --sample-rate=17898 --sign=unsigned --channels=1 --endian=little --bps=16 --blocksize=65535 --lax -f - -o media-name-17.8msps-16bit-cx-card.flac


# Issues & Debugging


Secure boot can cause issues.

Kernel updates will break the driver and require a full re-installation, unless DKMS is setup. 

`rules.config` - Inside this file are your defined base settings every time the driver loads.


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
[Chad Page](https://github.com/happycube/) (Chad.Page@gmail.com).

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

Information Additions Documentation Cleanup by [Harry Munday](https://github.com/harrypm) (harry@opcomedia.com)

- Change 10bit to the correct 16bit as that's what's stated in RAW16 under the datasheet and that's what the actual samples are in format-wise.
- Cleaned up and added examples for adjusting module parameters and basic real-time readout information.
- Added notations of ABLS2-40.000MHZ-D4YF-T a drop-in replacement crystal that adds 40mhz ability at low cost for current market PCIe cards.
- Added documentation for sixdb mode selection.
- Added links to find current CX cards
- Added issues that have been found
- Added crystal list of working replacements

### 2022-01-21 - v0.6 - Usability Improvements

New additions by [Tony Anderson](https://github.com/tandersn) (tandersn@uw.edu)

- Fixed ./leveladj script from re-setting module parameters
- Added new command scripts
- Added new level adjustment tool cxlvlcavdd
- Added dedicated readme for new scripts and future tools

### 2022-04-26 - More Usability Improvements & Tools

- Documentation Cleanup
- More utils additons
- Added cxlevel (utils/README.md)
- Added cxfreq  (utils/README.md)
- Added cxvalues shows the current configuration.
- Added fortycryst 0 for no, 1 for yes, and then added sample rates 11-27 (14-27 on 40cryst)
- Added warning messages for high & low gain states

### 2023-01-12 - v0.7 Multi Card Support

New multi-card support added by [Adam R](https://github.com/AR1972)

- Multi card support up to 256 cards per system
- Individual card settings support
- Documentation & Scripts updated 

### 2024 - Hardware Notes

[Clockgen Mod](https://github.com/happycube/cxadc-linux3/wiki/Modifications#clockgen-mod---external-clock) Established.

- Software defined 20/28.6/40/50msps modes
- Shared clock source synchronised capture
- Raspberry Pi 4 & 5 support added by [Alistair Buxton](https://github.com/ali1234)

### 2024-12-11

- [Windows Port](https://github.com/JuniorIsAJitterbug/cxadc-win) of CXADC established by Jitterbug

### 2025-02-11

- [FLAC V1.5.0 released](https://github.com/xiph/flac/releases/tag/1.5.0) enabling CPU multi-threading for FM RF archival capture with FLAC.

### 2025-10-04 - Add `levelmon`, support kernel 6.12

Add `levelmon` [Ethan Halsall](https://github.com/eshaz) (ethanshalsall@gmail.com)

- Add `levelmon` tool that monitors the levels and clipping
- Small fix to support kernel 6.12.0 and up
