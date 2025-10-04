#include <fcntl.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include "utils.h"

// this needs to be one over the ring buffer size to work
#define READ_LEN (40000000 / 4)
unsigned char buf[READ_LEN + 1];
unsigned short *wbuf = (void *)buf;

int readlen = READ_LEN;

int main(int argc, char *argv[]) {
	int fd;
	char device[64];
	char device_path[128];
	char str[512];
	struct timeval t1, t2;
	double elapsedTime;

	int tenbit = 0;
	int c;

	opterr = 0;
	sprintf(device, "cxadc0");
	sprintf(device_path, "/dev/cxadc0");

	while ((c = getopt(argc, argv, "d:bx")) != -1) {
		switch (c) {
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

	// read tenbit
    if (read_cxadc_param("tenbit", device, &tenbit)) {
		return -1;
	}

	fd = open(device_path, O_RDONLY);

	uint16_t min, center, max;
	if (tenbit) {
		min = 1;
		max = 0x400;
	} else {
		min = 1;
		max = 0x100;
	}
	center = max / 2;

	while (1) {
		uint32_t lo_count = 0, clip_lo = 0;
		uint32_t hi_count = 0, clip_hi = 0;

		uint64_t lo = 0, hi = 0, average = 0;
		uint16_t low, high;

		// read a bit
		gettimeofday(&t1, NULL);
		size_t total_samples = read(fd, buf, readlen);
		gettimeofday(&t2, NULL);

		elapsedTime = (t2.tv_sec - t1.tv_sec) * 1000.0;	   // sec to ms
		elapsedTime += (t2.tv_usec - t1.tv_usec) / 1000.0; // us to ms

		if (tenbit) {
			total_samples /= 2;
		}

		low = max;
		high = min;

		for (size_t i = 0; i < total_samples; i++) {
            uint16_t value;
            if (tenbit) {
                value = wbuf[i] >> 6;
            } else {
                value = buf[i];
            }
            value += 1;

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
		double high_pct = high / (double)max * 100;

		printf(
			"lo |%d| [%7.3f%%] (%7.3f%%) center %.2f%% hi (%7.3f%%) [%7.3f%%] "
			"|%d|\tnsamp %ld\trate %.2f\n",
			clip_lo, low_pct, avg_lo, avg_center, avg_hi, high_pct, clip_hi,
			total_samples, rate);
	}

	close(fd);
	return 0;
}
