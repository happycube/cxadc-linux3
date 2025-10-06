#include "utils.h"
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#define READ_SECONDS 0.25

int main(int argc, char *argv[]) {
	int fd;
	char device[64];
	char device_path[128];
	char str[512];
	struct timeval t1, t2;
	double elapsedTime;

	int tenbit = 0;
	int crystal = 0;
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
		case '?':
			// clang-format off
			fputs("levelmon continuously monitors levels from the specified cxadc card.\n", stderr);
			fputs("\n", stderr);
			fputs("levelmon -d <cxadc_device>\n", stderr);
			fputs("\n", stderr);
			fputs("Output format:\n", stderr);
			fputs(".   / clipped samples low\n", stderr);
			fputs(".   |     / min amplitude\n", stderr);
			fputs(".   |     |         / avg amplitude (negative side of center)\n", stderr);
			fputs(".   |     |         |                / dc offset\n", stderr);
			fputs(".   |     |         |                |\n", stderr);
			fputs("lo |0| [  3.906%] ( 33.429%) center -0.54% hi ( 65.764%) [ 95.312%] |0|	nsamp 10000000	rate 44.58\n", stderr);
			fputs(".   ^     ^^^^^     ^^^^^^           ^^^^       ^^^^^^     ^^^^^^    ^        ^^^^^^^^       ^^^^^\n", stderr);
			fputs(".                                               |          |         |        |              \\ calculated sample rate in msps\n", stderr);
			fputs(".                                               |          |         |        \\ total number of samples collected\n", stderr);
			fputs(".                                               |          |         \\ clipped samples high\n", stderr);
			fputs(".                                               |          \\ max amplitude\n", stderr);
			// clang-format on
			return -1;
		}
	}

	// test if the cxadc device is available
	fd = open(device_path, O_RDWR);
	if (fd <= 0) {
		fprintf(stderr, "%s not found\n", device_path);
		return -1;
	}
	close(fd);

	if (read_cxadc_param("tenbit", device, &tenbit)) {
		return -1;
	}

	if (read_cxadc_param("crystal", device, &crystal)) {
		return -1;
	}

	int readlen = crystal * READ_SECONDS;
	uint8_t *buf = malloc(readlen + 1);
	if (!buf) {
		fprintf(stderr, "failed to allocate %d bytes of memory\n", readlen + 1);
		return -1;
	}
	uint16_t *wbuf = (void *)buf;

	uint16_t min, center, max;
	if (tenbit) {
		min = 1;
		max = 0x400;
	} else {
		min = 1;
		max = 0x100;
	}
	center = max / 2;

	fd = open(device_path, O_RDONLY);

	while (1) {
		uint32_t lo_count = 0, clip_lo = 0;
		uint32_t hi_count = 0, clip_hi = 0;
		uint64_t lo = 0, hi = 0, average = 0;
		uint16_t low, high;

		gettimeofday(&t1, NULL);
		size_t total_samples = read(fd, buf, readlen);
		if (total_samples < 0) {
			printf("failed to read from device %s\n", device);
			free(buf);
			close(fd);
			return -1;
		}
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

	free(buf);
	close(fd);
	return 0;
}
