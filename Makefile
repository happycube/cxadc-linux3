CONFIG_MODULE_SIG=n
KDIR ?= /lib/modules/`uname -r`/build
CC ?= cc
CFLAGS ?=-O3 -march=native

.PHONY: all
all: cxadc leveladj levelmon

# leveladj
LEVELADJ_SRCS = leveladj.c utils.c
LEVELADJ_OBJS = $(LEVELADJ_SRCS:.c=.o)

leveladj: $(LEVELADJ_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# levelmon
LEVELMON_SRCS = levelmon.c utils.c
LEVELMON_OBJS = $(LEVELMON_SRCS:.c=.o)

levelmon: $(LEVELMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

cxadc:
	$(MAKE) -C $(KDIR) M=$$PWD

%:
	$(MAKE) -C $(KDIR) M=$$PWD $@

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean
	rm -f $(LEVELADJ_OBJS) leveladj $(LEVELMON_OBJS) levelmon