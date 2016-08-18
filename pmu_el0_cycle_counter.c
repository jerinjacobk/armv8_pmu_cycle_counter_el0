/*
 * Enable user-mode ARM performance counter access.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/smp.h>

#if !defined(__aarch64__)
#error Module can only be compiled on ARM64 machines.
#endif

static void
enable_cycle_counter_el0(void* data)
{
        u64 val;
	/* Disable cycle counter overflow interrupt */
	asm volatile("msr pmintenset_el1, %0" : : "r" ((u64)(0 << 31)));
	/* Enable cycle counter */
	asm volatile("msr pmcntenset_el0, %0" :: "r" BIT(31));
	/* Enable user-mode access to cycle counters. */
	asm volatile("msr pmuserenr_el0, %0" : : "r"(BIT(0) | BIT(2)));
	/* Clear cycle counter and start */
	asm volatile("mrs %0, pmcr_el0" : "=r" (val));
	val |= (BIT(0) | BIT(2));
        isb();
        asm volatile("msr pmcr_el0, %0" : : "r" (val));
}

static void
disable_cycle_counter_el0(void* data)
{
	/* Disable cycle counter */
	asm volatile("msr pmcntenset_el0, %0" :: "r" (0 << 31));
	/* Disable user-mode access to counters. */
	asm volatile("msr pmuserenr_el0, %0" : : "r"((u64)0));

}

static int __init
init(void)
{
	on_each_cpu(enable_cycle_counter_el0, NULL, 1);
	return 0;
}

static void __exit
fini(void)
{
	on_each_cpu(disable_cycle_counter_el0, NULL, 1);
}

MODULE_DESCRIPTION("Enables user-mode access to ARMv8 PMU counters");
MODULE_LICENSE("GPL");
module_init(init);
module_exit(fini);
