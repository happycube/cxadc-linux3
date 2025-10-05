#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int set_cxadc_param(char *param_name, char *device, int param_value);
int read_cxadc_param(char *param_name, char *device, int *param_value);