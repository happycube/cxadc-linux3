#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>

// this needs to be one over the ring buffer size to work
#define bufsize (1024*1024*65)
unsigned char buf[bufsize];

int readlen = 2048 * 1024;

void set(char *name, char *device, int level)
{
	char str[512];

	sprintf(str, "/sys/class/cxadc/%s/device/parameters/%s", device, name);

	int fd = open(str, O_WRONLY);

	sprintf(str, "%d", level);
	write(fd, str, strlen(str) + 1);

	close(fd);
}

int main(int argc, char *argv[])
{

	FILE *syssfys;
	int fd;
	int level = 20;
	int go_on = 1; // 2 after going over
	char *device;
	char *device_path;
	char str[512];

	int tenbit  = 0;
	int tenxfsc = 0;

	int c;

	opterr = 0;
	device = (char *)malloc(64);
	device_path = (char *)malloc(128);
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

	fd = open(device_path, O_RDWR);
	if (fd <= 0) {
		fprintf(stderr, "%s not found\n", device_path);
		if (device_path)
			free(device_path);
		if (device)
			free(device);
		return -1;
	}
	close(fd);

		sprintf(str, "/sys/class/cxadc/%s/device/parameters/tenbit", device);
		syssfys = fopen(str, "r");

	if (syssfys == NULL) {
		fprintf(stderr, "no sysfs paramerters\n");
		if (device_path)
			free(device_path);
		if (device)
			free(device);
		return -1;
	}

	fscanf(syssfys, "%d", &tenbit);
	fclose(syssfys);

	sprintf(str, "/sys/class/cxadc/%s/device/parameters/tenxfsc", device);
	syssfys = fopen(str, "r");

	if (syssfys == NULL) {
		fprintf(stderr, "no sysfs paramerters\n");
		if (device_path)
			free(device_path);
		if (device)
			free(device);
		return -1;
	}

		fscanf(syssfys, "%d", &tenxfsc);
		fclose(syssfys);


	set("tenbit", device, tenbit);
	set("tenxfsc", device, tenxfsc);

	if (argc > optind) {
		level = atoi(argv[optind]);

		set("level", device, level);
		if (device_path)
			free(device_path);
		if (device)
			free(device);
		return 0;
	}

	while (go_on) {
		int over = 0;
		unsigned int low = tenbit ? 65535 : 255, high = 0;

		set("level", device, level);

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
	if (device_path)
		free(device_path);
	if (device)
		free(device);
	return 0;
}

