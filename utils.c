#include "utils.h"

int set_cxadc_param(char *name, char *device, int level)
{
	char str[512];

	sprintf(str, "/sys/class/cxadc/%s/device/parameters/%s", device, name);

	int fd = open(str, O_WRONLY);
	if (fd <= 0) {
		fprintf(stderr, "failed to open %s\n", str);
		return -1;
	}

	sprintf(str, "%d", level);
    if (write(fd, str, strlen(str) + 1) < 0) {
		fprintf(stderr, "failed to set parameter %s\n", str);
	    close(fd);
		return -1;
    }
	
	close(fd);
    return 0;
}

int read_cxadc_param(char *param_name, char *device, int *param_value) {
	char str[512];
	FILE *syssfys;

	sprintf(str, "/sys/class/cxadc/%s/device/parameters/%s", device, param_name);
	syssfys = fopen(str, "r");

	if (syssfys == NULL) {
		fprintf(stderr, "failed to open %s\n", str);
		return -1;
	}
	
    if (!fscanf(syssfys, "%d", param_value)) {
		fprintf(stderr, "failed to read %s\n", param_name);
	    fclose(syssfys);
		return -1;
	}
	fclose(syssfys);
    return 0;
}