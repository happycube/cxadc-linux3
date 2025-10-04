CONFIG_MODULE_SIG=n
KDIR ?= /lib/modules/`uname -r`/build
CC ?= cc
CFLAGS ?=-O3 -march=native

default:
	$(MAKE) -C $(KDIR) M=$$PWD
	$(CC) leveladj.c $(CFLAGS) -o leveladj
	$(CC) levelmon.c $(CFLAGS) -o levelmon

%:
	$(MAKE) -C $(KDIR) M=$$PWD $@

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean
	rm -f leveladj levelmon
