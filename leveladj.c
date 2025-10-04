#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include "utils.h"

// this needs to be one over the ring buffer size to work
#define bufsize (1024*1024*65)
unsigned char buf[bufsize];

int readlen = 2048 * 1024;

int main(int argc, char *argv[])
{
	int ret = 0;
	int fd;
	int level = 20;
	int go_on = 1; // 2 after going over
	char device[64];
	char device_path[128];

	int tenbit  = -1;
	int tenxfsc = -1;

	int c;

	opterr = 0;
	sprintf(device, "cxadc0");
	sprintf(device_path, "/dev/cxadc0");

	while ((c = getopt(argc, argv, "d:bx")) != -1) {
		switch (c) {
		case 'b':
			tenbit = 1;
			break;
		case 'x':
			tenxfsc = 1;
			break;
		case 'd':
			if (strlen(optarg) <= 30) {
				sprintf(device_path, "/dev/%s", optarg);
				sprintf(device, "%s", optarg);
			}
			break;
		};
	}

	// test if the cxadc device is available
	fd = open(device_path, O_RDWR);
	if (fd <= 0) {
		fprintf(stderr, "%s not found\n", device_path);
		return -1;
	}
	close(fd);
    
	// set tenbit if supplied in args
	if (tenbit >= 0) {
		if (set_cxadc_param("tenbit", device, tenbit)) {
			return -1;
		}
	}

	// read tenbit
    if (read_cxadc_param("tenbit", device, &tenbit)) {
		return -1;
	}

	// set tenxfsc if supplied in args
	if (tenxfsc >= 0) {
		if (set_cxadc_param("tenxfsc", device, tenxfsc)) {
			return -1;
		}
	}

	// read tenxfsc
    if (read_cxadc_param("tenxfsc", device, &tenxfsc)) {
		return -1;
	}

	if (argc > optind) {
		level = atoi(argv[optind]);

		if (set_cxadc_param("level", device, level)) {
			return -1;
		}

		return 0;
	}

	while (go_on) {
		int over = 0;
		unsigned int low = tenbit ? 65535 : 255, high = 0;

		if (set_cxadc_param("level", device, level)) {
			return -1;
		}

		fd = open(device_path, O_RDWR);

		printf("testing level %d\n", level);

		// read a bit
		read(fd, buf, readlen);

		if (tenbit) {
			unsigned short *wbuf = (void *)buf;

			for (int i = 0; i < (readlen / 2) && (over < (readlen / 200000)); i++) {
				if (wbuf[i] < low)
					low = wbuf[i];
				if (wbuf[i] > high)
					high = wbuf[i];

				if ((wbuf[i] < 0x0800) || (wbuf[i] > 0xf800))
					over++;

				// auto fail on 0 and 65535
				if ((wbuf[i] == 0) || (wbuf[i] == 0xffff))
					over += (readlen / 50000);
			}
		} else {
			for (int i = 0; i < readlen && (over < (readlen / 100000)); i++) {
				if (buf[i] < low)
					low = buf[i];
				if (buf[i] > high)
					high = buf[i];

				if ((buf[i] < 0x08) || (buf[i] > 0xf8))
					over++;

				// auto fail on 0 and 255
				if ((buf[i] == 0) || (buf[i] == 0xff))
					over += (readlen / 50000);
			}
		}

		printf("low %d high %d clipped %d nsamp %d\n", (int)low, (int)high, over, readlen);

		if (over >= 20) {
			go_on = 2;
		} else {
			if (go_on == 2)
				go_on = 0;
		}

		if (go_on == 1)
			level++;
		else if (go_on == 2)
			level--;

		if ((level < 0) || (level > 31))
			go_on = 0;
		close(fd);
	}

	return 0;
}

