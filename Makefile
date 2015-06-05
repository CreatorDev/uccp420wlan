uccp420wlan-objs		:= src/80211_if.o src/core.o src/umac_if.o src/tx.o src/hal_hostport.o src/fwldr.o
obj-$(CONFIG_UCCP420WLAN)	+= uccp420wlan.o
ccflags-y += -I$(src)/inc


