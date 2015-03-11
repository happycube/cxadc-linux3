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

int readlen = 256 * 1024;

void set(char *name, int level) 
{
	char str[512];
	sprintf(str, "/sys/module/cxadc/parameters/%s", name); 
	int fd = open(str, O_WRONLY);

	sprintf(str, "%d", level); 
	write(fd, str, strlen(str) + 1);

	close(fd);
}

int main(int argc, char *argv[])
{
	int fd;
	int level = 30;
	int go_on = 1; // 2 after going over

	fd = open("/dev/cxadc", O_RDWR);
	if (fd <= 0) {
		fprintf(stderr, "/dev/cxadc not found\n");
		return -1;
	}
	close(fd);

	int tenbit  = 0;
	int tenxfsc = 0;  

	int c;
        opterr = 0;

        while ((c = getopt(argc, argv, "bx")) != -1) {
		switch (c) {
			case 'b':
				tenbit = 1;
				break;	
			case 'x':
				tenxfsc = 1;
				break;	
		}; 
	}
		
	set("tenbit", tenbit);
	set("tenxfsc", tenxfsc);

	if (argc > optind) {
		level = atoi(argv[optind]);

		set("level", level);
		return 0;
	}

	while (go_on) {
		int over = 0;
		unsigned int low = tenbit ? 65535 : 255, high = 0;
		set("level", level);
	
		fd = open("/dev/cxadc", O_RDWR);

		printf("testing level %d\n", level);

		// read a bit
		read(fd, buf, (1024 * 1024) * 4);
		read(fd, buf, readlen);	

		if (tenbit) {
			unsigned short *wbuf = (void *)buf;
			for (int i = 0; i < (readlen / 2) && !over; i++) {
				if (wbuf[i] < low) low = wbuf[i]; 
				if (wbuf[i] > high) high = wbuf[i]; 

				if ((wbuf[i] < 0x0200) || (wbuf[i] > 0xfe00)) {
					over++;
				}
			}
		} else {
			for (int i = 0; i < readlen && !over; i++) {
				if (buf[i] < low) low = buf[i]; 
				if (buf[i] > high) high = buf[i]; 

				if ((buf[i] < 0x04) || (buf[i] > 0xfc)) {
					over++;
				}
			}
		}

		printf("low %d high %d clipped %d nsamp %d\n", (int)low, (int)high, over, readlen);

		if (over > (readlen / 10000)) {
			go_on = 2;
		} else {
			if (go_on == 2) go_on = 0;
		}

		if (go_on == 1) level++;
		else if (go_on == 2) level--;

		if ((level < 0) || (level > 31)) go_on = 0;
		close(fd);
	}

	return 0;
}

