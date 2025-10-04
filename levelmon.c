#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/time.h>

// this needs to be one over the ring buffer size to work
#define READ_LEN (40000000 / 4)
unsigned char buf[READ_LEN + 1];
unsigned short *wbuf = (void *)buf;

int readlen = READ_LEN;

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
	int level = 0;
	int go_on = 1; // 2 after going over
	char *device;
	char *device_path;
	char str[512];
	struct timeval t1, t2;
    double elapsedTime;

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

	fd = open(device_path, O_RDONLY);
	
	while (1) {
		unsigned int lo_count = 0, clip_lo = 0;
		unsigned int hi_count = 0, clip_hi = 0;

		unsigned int lo = 0, hi = 0, average = 0;
		unsigned int min, center, max;
		unsigned short low, high;


		// read a bit
		gettimeofday(&t1, NULL);
		size_t total_samples = read(fd, buf, readlen);
		gettimeofday(&t2, NULL);

    	        elapsedTime = (t2.tv_sec - t1.tv_sec) * 1000.0;      // sec to ms
    	        elapsedTime += (t2.tv_usec - t1.tv_usec) / 1000.0;   // us to ms

		if (tenbit) {
			min = 1;
			max = 0x400;
			total_samples /= 2;

			// shift unused bits
			for (unsigned int i = 0; i < total_samples; i++) {
				wbuf[i] = wbuf[i] >> 6;
			}
		} else {
			min = 1;
			max = 0x100;
		}
		
		center = max / 2;
		low = max;
		high = min;

		for (unsigned int i = 0; i < total_samples; i++) {
			unsigned short value = (tenbit ? wbuf[i] : buf[i]) + 1;
			
			// find the average
			average += value;

			// find the min and max
			if (value < low)
				low = value;
			if (value > high)
			        high = value;
			
			// find the average of low and high samples
			if (value < center) {
				lo += value;
			        lo_count++;
			} else {
				hi += value;
			        hi_count++;
			}

			// flag any clipping
			if (value == min)
				clip_lo++;

			if (value == max)
				clip_hi++;
		}

		double rate = total_samples / elapsedTime / 1000;
		double avg_lo = lo / (double)max / lo_count * 100;
		double avg_hi = hi / (double)max / hi_count * 100;
		double avg_center = average / (double)total_samples / max * 100 - 50;
		double low_pct = low / (double)max * 100;
		double high_pct  = high / (double)max * 100;

		printf("lo |%d| [%7.3f%%] (%7.3f%%) center %.2f%% hi (%7.3f%%) [%7.3f%%] |%d|\tnsamp %d\trate %.2f\n", clip_lo, low_pct, avg_lo, avg_center, avg_hi, high_pct, clip_hi, total_samples, rate);

	}
	
	close(fd);

	if (device_path)
		free(device_path);
	if (device)
		free(device);
	return 0;
}

