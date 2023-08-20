Tony Anderson 2022
tandersn@uw.edu

Convenient scripts to manipulate cx card parameters.

# To install: (Requires sudo to install)

sudo ./inst_scripts

inst_scripts = create simlinks to these scripts in /usr/bin.

## IMPORTANT:  About capturing a CAV laserdisc

CAV laserdiscs increase in gain as they play, in order to get a good quality capture, you have to adjust the CX card gain *down* as the disc plays (unless using the RMS amp). This means the physical amp must be set as such, that at the end of the disc, the CX card gain is about 1 or 2. If you were, to say,  run level adjust at the beginning of the disc and adjust the amp until it landed at 1 or 2, after about 5 minutes the entire capture would be clipping. ⚡ 🔥 🤕 The **cxlvlcavdd** script is designed to capture the data and turn down the CX card gain as the capture progresses.  

The user which you are capturing with must have read-write access to the sysfs parameters.

There's a few steps to using cxlvlcavdd:

1. Go to near the end of the disk, use leveladj and the gain POT of the amp to get leveladj to land at 1 or 2.
2. Go to the beginning of the disc and run leveladj again.
3. Finally, start the capture with `cxlvlcavdd CaptureFileName.r8`.


## Command Arguments

cx8fsc = set 8fsc sample rate mode.  1x crystal speed.

cx10fsc = set 10fsc sample rate mode. 1.25x crystal speed (only recommended when also using 16bit mode).

cx16bit = set cxadc to return unsigned 16 bit samples.

cx6off = set sixdb to off. This is a 6db gain setting for the AFE. Kind of like a "loud" button on your stereo.

cx6on = set sixdb to on.  (see above).

cx8bit = set cxadc to return unsigned 8 bit samples.

cxvx0 = set cxadc to work off vmux0

cxvx1 = set cxadc to work off vmux1

cxvx2 = set cxadc to work off vmux2

cxfreq = set cx card desired frequency, (same as echo 'somenumber' > tenxfsc. See main wiki for tenxfsc parameter).

cxlevel = set cx card level 0-31.

cxlvlcavdd = A capture script to use that adjusts the gain automatically.

cxvalues = display current values of cx card module parameters.


## Example of CX Values with 4 Cards


    harry@Decode-Station:~$ cxvalues
    /sys/class/cxadc/cxadc0/device/parameters/sixdb 1
    /sys/class/cxadc/cxadc0/device/parameters/tenbit 0
    /sys/class/cxadc/cxadc0/device/parameters/audsel -1
    /sys/class/cxadc/cxadc0/device/parameters/center_offset 8
    /sys/class/cxadc/cxadc0/device/parameters/latency -1
    /sys/class/cxadc/cxadc0/device/parameters/crystal 28636363
    /sys/class/cxadc/cxadc0/device/parameters/vmux 2
    /sys/class/cxadc/cxadc0/device/parameters/tenxfsc 0
    /sys/class/cxadc/cxadc0/device/parameters/level 16
    /sys/class/cxadc/cxadc1/device/parameters/sixdb 1
    /sys/class/cxadc/cxadc1/device/parameters/tenbit 0
    /sys/class/cxadc/cxadc1/device/parameters/audsel -1
    /sys/class/cxadc/cxadc1/device/parameters/center_offset 8
    /sys/class/cxadc/cxadc1/device/parameters/latency -1
    /sys/class/cxadc/cxadc1/device/parameters/crystal 28636363
    /sys/class/cxadc/cxadc1/device/parameters/vmux 2
    /sys/class/cxadc/cxadc1/device/parameters/tenxfsc 0
    /sys/class/cxadc/cxadc1/device/parameters/level 16
    /sys/class/cxadc/cxadc2/device/parameters/sixdb 1
    /sys/class/cxadc/cxadc2/device/parameters/tenbit 0
    /sys/class/cxadc/cxadc2/device/parameters/audsel -1
    /sys/class/cxadc/cxadc2/device/parameters/center_offset 8
    /sys/class/cxadc/cxadc2/device/parameters/latency -1
    /sys/class/cxadc/cxadc2/device/parameters/crystal 28636363
    /sys/class/cxadc/cxadc2/device/parameters/vmux 2
    /sys/class/cxadc/cxadc2/device/parameters/tenxfsc 0
    /sys/class/cxadc/cxadc2/device/parameters/level 16
    /sys/class/cxadc/cxadc3/device/parameters/sixdb 1
    /sys/class/cxadc/cxadc3/device/parameters/tenbit 0
    /sys/class/cxadc/cxadc3/device/parameters/audsel -1
    /sys/class/cxadc/cxadc3/device/parameters/center_offset 8
    /sys/class/cxadc/cxadc3/device/parameters/latency -1
    /sys/class/cxadc/cxadc3/device/parameters/crystal 28636363
    /sys/class/cxadc/cxadc3/device/parameters/vmux 2
    /sys/class/cxadc/cxadc3/device/parameters/tenxfsc 0
    /sys/class/cxadc/cxadc3/device/parameters/level 16
