

obj-m += rpmsg_neo.o
rpmsg_neo-objs:= rpmsg_neoproxy.o rpmsg_neo_tty.o rpmsg_init_neo.o

KDIR  := /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	
	
	
clean:
	rm -f .tmp
	rm -f *.o*
	rm -f Mo*
	rm -f *.cmd
	rm -f mccmulti.mod*
	rm -fr .t* .mcc*
	rm -f *.ko
	rm -f *.mod.*
	rm -f *~

