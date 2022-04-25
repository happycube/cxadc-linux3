Tony Anderson 2022
tandersn@uw.edu

Convenient scripts to manipulate cx card parameters.

# To install: (Requires sudo to install)

sudo ./inst_scripts

inst_scripts = create simlinks to these scripts in /usr/bin.

To make the command list executable from anywhere on the system via symlinks

ls |xargs -I % bash -c 'ln -s `pwd`/% /usr/bin/%'

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

cxlvlcavdd.py = A capture script to use instead of `dd` that adjust the gain down automatically if clipping is detected during capture.

# Change CXADC defaults

Defaults cxadc3-linux/cxadc.c file to have the defaults you like. at stock, it will look like this:

static int latency = -1; (leave this alone)
static int audsel = -1;  (leave this alone)
static int vmux = 2;
static int level = 16;
static int tenbit;
static int tenxfsc;
static int sixdb = 1;

But you could change it to:
static int latency = -1; (leave this alone)
static int audsel = -1; (leave this alone)
static int vmux = 1;
static int level = 0;
static int tenbit = 1;
static int tenxfsc = 1;
static int sixdb = 0;

Then redo the make and sudo make modules_install commands. Then next reboot, it will come up with those settings as the default
