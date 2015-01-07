#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>

// this needs to be one over the ring buffer size to work
#define bufsize (1024*1024*65)
unsigned char buf[bufsize];

int readlen = 256 * 1024;

void set_level(int level) 
{
	int fd = open("/sys/module/cxadc/parameters/level", O_WRONLY);
	char str[512];

	sprintf(str, "%d", level); 
	write(fd, str, strlen(str) + 1);

	close(fd);
}

int main(int argc, char *argv[])
{
	int fd;
	int level = 0x10;
	int go_on = 1; // 2 after going over

	fd = open("/dev/cxadc", O_RDWR);
	if (fd <= 0) {
		fprintf(stderr, "nope.\n");
		return -1;
	}
	close(fd);

	if (argc >= 2) {
		level = atoi(argv[1]);

		set_level(level);
		return 0;
	}

	while (go_on) {
		int over = 0;
		unsigned char low = 255, high = 0;
		set_level(level);
	
		fd = open("/dev/cxadc", O_RDWR);

		printf("testing level %d\n", level);

		// read a bit
		read(fd, buf, (1024 * 1024) * 4);
		read(fd, buf, readlen);	
		for (int i = 0; i < readlen && !over; i++) {
			if (buf[i] < low) low = buf[i]; 
			if (buf[i] > high) high = buf[i]; 

			if ((buf[i] < 0x08) || (buf[i] > 0xf8)) {
				over = 1;
			}
		}

		printf("low %d high %d\n", (int)low, (int)high);

		if (over) {
			go_on = 2;
		} else {
			if (go_on == 2) go_on = 0;
		}

		if (go_on == 1) level++;
		else if (go_on == 2) level--;

		if ((level < 0) || (level > 0x1f)) go_on = 0;
		close(fd);
	}

	return 0;
}

