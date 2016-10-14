obj-m += uccp420wlan.o

uccp420wlan-objs		:= src/80211_if.o src/core.o src/umac_if.o src/tx.o src/hal_hostport.o src/fwldr.o

AS		= $(CROSS_COMPILE)as
LD		= $(CROSS_COMPILE)ld
CC		= $(CROSS_COMPILE)gcc

EXTRA_CFLAGS+=-DMULTI_CHAN_SUPPORT
EXTRA_CFLAGS+=-I$(src)/inc
#Needed for Run Time Debug support
EXTRA_CFLAGS+= -DDEBUG

all:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules


clean:
	rm -f *.o *.a *.s *.ko *.ko.cmd *.o.cmd *.mod.* .mwlwifi.*
	rm -rf modules.order Module.symvers .tmp_versions
