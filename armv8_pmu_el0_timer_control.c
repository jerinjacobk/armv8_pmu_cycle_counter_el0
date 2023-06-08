// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2023 NXP
 * Add support for Counter-timer Kernel Control register EL0 access
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include "pmuctl.h"

#if !defined(__aarch64__)
#error Module can only be compiled on ARM64 machines.
#endif

static void
enable_timer_ctl_el0(void *data)
{
	u64 val;

	/* Enable per cpu Physical/Virtual Timer Control EL0 access */
	asm volatile("mrs %0, CNTKCTL_EL1" : "=r" (val));
	asm volatile("isb" : :);
	asm volatile("msr CNTKCTL_EL1, %0" : : "r"(val | BIT(9) | BIT(8)));
}

static void
disable_timer_ctl_el0(void *data)
{
	u64 val;

	/* Enable per cpu Physical/Virtual Timer Control EL0 access */
	asm volatile("mrs %0, CNTKCTL_EL1" : "=r" (val));
	asm volatile("isb" : :);
	asm volatile("msr CNTKCTL_EL1, %0" : : "r"(val & 0xFF));
}

ssize_t
pmcntkctl_show(char *arg, size_t size)
{
	u64 val;
	int ret;

	asm volatile("mrs %0, CNTKCTL_EL1" : "=r" (val));
	ret = snprintf(arg, size, "CNTKCTL EL0 access = %1d\n",
		       ((val & (BIT(8) | BIT(9))) != 0 ? 1 : 0));
	return (ret < size) ? ret : size;
}

int
pmcntkctl_modify(const char *arg, size_t size)
{
	long val;

	if (kstrtol(arg, 0, &val))
		return -EINVAL;

	if (val != 0)
		on_each_cpu(enable_timer_ctl_el0, NULL, 1);
	else
		on_each_cpu(disable_timer_ctl_el0, NULL, 1);
	return 0;
}

void
pm_cntkctl_handler(int enable)
{
	if (enable)
		on_each_cpu(enable_timer_ctl_el0, NULL, 1);
	else
		on_each_cpu(disable_timer_ctl_el0, NULL, 1);
}

void
pm_cntkctl_fini(void)
{
	/* Restore the physical timer control register to default state */
	on_each_cpu(disable_timer_ctl_el0, NULL, 1);

}

MODULE_DESCRIPTION("Enable Physical Timer Control EL0 access");
MODULE_LICENSE("GPL");
