#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>

// this needs to be one over the ring buffer size to work
const int bufsize = 1024*1024*65;
unsigned char buf[bufsize];

int readlen = 256 * 1024;

int main(void)
{
	int fd;
	int level = 0x10;
	int go_on = 1; // 2 after going over

	fd = open("/dev/cxadc", O_RDWR);
	if (fd <= 0) {
		fprintf(stderr, "nope.\n");
		return -1;
	}

	while (go_on) {
		int over = 0;
		unsigned char low = 255, high = 0;
		ioctl(fd, 0x12345670, level); 

		printf("testing level %x\n", level);

		// dump cache
		read(fd, buf, bufsize);
	
		// read a bit
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
	}

	close(fd);

	return 0;
}

