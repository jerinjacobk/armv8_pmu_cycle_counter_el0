obj-m := pmu_el0_cycle_counter.o
pmu_el0_cycle_counter-objs += armv8_pmu_el0_cycle_counter.o armv8_pmu_el0_timer_control.o
KDIR	:= /lib/modules/$(shell uname -r)/build
PWD	:= $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
