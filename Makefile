CONFIG_MODULE_SIG=n
KDIR ?= /lib/modules/`uname -r`/build

default:
	$(MAKE) -C $(KDIR) M=$$PWD
	cc leveladj.c -o leveladj

%:
	$(MAKE) -C $(KDIR) M=$$PWD $@

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean
	rm -f leveladj
