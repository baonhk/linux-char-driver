# Makefile - builds hello.ko out-of-tree against the running kernel headers.
#
# Usage:
#   make            # build hello.ko
#   make clean      # remove build artifacts
#   make load       # insmod hello.ko
#   make unload     # rmmod hello
#   make reload     # unload + load
#   make node       # show the created /dev node + major/minor
#   make log        # tail kernel log (dmesg) for this driver
#
# Requires: linux-headers-$(uname -r) installed on the build machine.

obj-m += hello.o

KVERSION := $(shell uname -r)
KDIR     := /lib/modules/$(KVERSION)/build
PWD      := $(shell pwd)

DEVICE   := mydevice

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

load:
	sudo insmod hello.ko
	@sleep 0.2
	@dmesg | tail -n 5

unload:
	sudo rmmod hello || true
	@dmesg | tail -n 5

reload: unload load

node:
	@ls -l /dev/$(DEVICE) 2>/dev/null || echo "/dev/$(DEVICE) not found - is the module loaded?"

log:
	sudo dmesg -w | grep --line-buffered mydevice

.PHONY: all clean load unload reload node log
