## Other Tips

### Change CXADC defaults

Defaults cxadc3-linux/cxadc.c file to have the defaults you like. at stock, it will look like this:

`static int latency = -1;` (leave this alone)

`static int audsel = -1;`

`static int vmux = 2;`

`static int level = 16;`

`static int tenbit;`

`static int tenxfsc;`

`static int sixdb = 1;`

`static int crystal = 28636363;`

But you could change it to:

`static int latency = -1;` (leave this alone)

`static int audsel = -1;`

`static int vmux = 1;`

`static int level = 0;`

`static int tenbit = 1;`

`static int tenxfsc = 1;`

`static int sixdb = 0;`

`static int crystal = 40000000;`

Then redo the make and sudo make modules_install commands. Then next reboot, it will come up with those settings as the default.

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