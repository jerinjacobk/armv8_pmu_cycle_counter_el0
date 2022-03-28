/*
 * Enable user-mode ARM performance counter access.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include "pmuctl.h"

#if !defined(__aarch64__)
#error Module can only be compiled on ARM64 machines.
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
#define pmu_access_ok(x,y,z) access_ok((y),(z))
#else
#define pmu_access_ok(x,y,z) access_ok((x),(y),(z))
#endif

struct pmu_ctl_cfg {
	const char	*name;
	/* Display PMU ctl value/status. Include newline.
	 * Return written chars on success.
	 */
	ssize_t		(*show)(char *arg, size_t size);
	/* Modify PMU ctl according to user value. Return 0 on success */
	int		(*modify)(const char *arg, size_t size);
};

/* ops for each pmu control */
static ssize_t
pmccntr_show(char *arg, size_t size);
static int
pmccntr_modify(const char *arg, size_t size);

/* file ops */
static ssize_t
pmuctl_read(struct file *f, char __user *userbuf, size_t count, loff_t *ppos);
static ssize_t
pmuctl_write(struct file *f, const char __user *userbuf, size_t count, loff_t *ppos);
static long
pmuctl_ioctl(struct file *f, unsigned int cmd, unsigned long arg);

static struct pmu_ctl_cfg pmu_ctls[PM_CTL_CNT] = {
	[PM_CTL_PMCCNTR] = {
		.name	= "PMCCNTR",
		.show	= pmccntr_show,
		.modify	= pmccntr_modify
	}
};

static const struct file_operations pmuctl_fops = {
	.owner		= THIS_MODULE,
	.read		= pmuctl_read,
	.write		= pmuctl_write,
	.llseek		= generic_file_llseek,
	.unlocked_ioctl	= pmuctl_ioctl,
};

static struct miscdevice pmuctl_dev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "pmuctl",
	.fops		= &pmuctl_fops
};
DEFINE_MUTEX(pmuctl_lock);

static void
enable_cycle_counter_el0(void* data)
{
	u64 val;
	/* Disable cycle counter overflow interrupt */
	asm volatile("msr pmintenclr_el1, %0" : : "r" ((u64)(1 << 31)));
	/* Enable cycle counter */
	asm volatile("msr pmcntenset_el0, %0" :: "r" BIT(31));
	/* Enable user-mode access to cycle counters. */
	asm volatile("msr pmuserenr_el0, %0" : : "r"(BIT(0) | BIT(2)));
	/* Clear cycle counter and start */
	asm volatile("mrs %0, pmcr_el0" : "=r" (val));
	val |= (BIT(0) | BIT(2));
	isb();
	asm volatile("msr pmcr_el0, %0" : : "r" (val));
	val = BIT(27);
	asm volatile("msr pmccfiltr_el0, %0" : : "r" (val));
}

static void
disable_cycle_counter_el0(void* data)
{
	/* Disable cycle counter */
	asm volatile("msr pmcntenset_el0, %0" :: "r" (0 << 31));
	/* Disable user-mode access to counters. */
	asm volatile("msr pmuserenr_el0, %0" : : "r"((u64)0));

}

static ssize_t
pmccntr_show(char *arg, size_t size)
{
	u64 val;
	int ret;

	asm volatile("mrs %0, pmuserenr_el0" : "=r" (val));
	ret = snprintf(arg, size, "PMCCNTR=%1d\n",
		       ((val & (BIT(0) | BIT(2))) != 0 ? 1 : 0));
	return (ret < size) ? ret : size;
}

static int
pmccntr_modify(const char *arg, size_t size)
{
	long int val;

	if (kstrtol(arg, 0, &val)) {
		return -EINVAL;
	}
	if (val != 0)
		on_each_cpu(enable_cycle_counter_el0, NULL, 1);
	else
		on_each_cpu(disable_cycle_counter_el0, NULL, 1);
	return 0;
}

static ssize_t
pmuctl_read(struct file *f, char __user *userbuf, size_t count, loff_t *ppos)
{
	char *buf, *cur;
	ssize_t size;
	int i;

	if (*ppos > 0)
		return 0;
	if (count > PAGE_SIZE)
		return -E2BIG;
	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;
	cur = buf;

	mutex_lock(&pmuctl_lock);
	for (i = 0; i < PM_CTL_CNT; i++) {
		if (pmu_ctls[i].show == NULL)
			continue;

		size = pmu_ctls[i].show(cur, count);
		if (size < 0) {
			goto err_free;
		}
		cur += size;
		count -= size;
	}
	mutex_unlock(&pmuctl_lock);
	size = simple_read_from_buffer(userbuf, count, ppos, buf, cur - buf);

err_free:
	kfree(buf);

	return size;
}

static ssize_t
pmuctl_write(struct file *f, const char __user *userbuf, size_t count, loff_t *ppos)
{
	char *buf, *name, *val;
	int i, ret = -EIO;

	if (count > PAGE_SIZE)
		return -E2BIG;
	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;
	if (copy_from_user(buf, userbuf, count)) {
		ret = -EIO;
		goto err_copy;
	}

	val = buf;
	name = strim(strsep(&val, "="));
	if (val == NULL) {
		dev_err(pmuctl_dev.this_device, "Invalid write: %s\n", buf);
		ret = -EINVAL;
		goto err_parse;
	}
	val = strim(val);
	for (i = 0; i < PM_CTL_CNT; i++) {
		if (strcmp(pmu_ctls[i].name, name))
			continue;
		if (pmu_ctls[i].modify != NULL) {
			ret = pmu_ctls[i].modify(val, count - (val - name));
			if (ret == 0)
				ret = count;
		} else {
			dev_err(pmuctl_dev.this_device,
				"PMU %s not modifiable\n", pmu_ctls[i].name);
			ret = -ENOTSUPP;
		}
		break;
	}
	if (i == PM_CTL_CNT) {
		dev_err(pmuctl_dev.this_device, "Unknown PMU CTL: %s\n", name);
		ret = -EINVAL;
	}
err_parse:
err_copy:
	kfree(buf);
	return ret;
}

static long
pmuctl_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	struct pmuctl_pmccntr_data pmccntr;
	int ret = 0;

	if (_IOC_TYPE(cmd) != PMUCTL_IOC_MAGIC)
		return -ENOTTY;

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !pmu_access_ok(VERIFY_WRITE, (void __user *)arg,
				 _IOC_SIZE(cmd));
	else if (_IOC_TYPE(cmd) & _IOC_WRITE)
		err = !pmu_access_ok(VERIFY_READ, (void __user *)arg,
				 _IOC_SIZE(cmd));

	if (err)
		return -EFAULT;

	mutex_lock(&pmuctl_lock);
	switch (cmd) {
	case PMU_IOC_PMCCNTR: /* enable/disable pmccntr */
		if (copy_from_user(&pmccntr, (void *)arg, _IOC_SIZE(cmd))) {
			ret = -EIO;
			break;
		}
		if (pmccntr.enable)
			on_each_cpu(enable_cycle_counter_el0, NULL, 1);
		else
			on_each_cpu(disable_cycle_counter_el0, NULL, 1);
		break;
	default:
		ret = -ENOTTY;
	}
	mutex_unlock(&pmuctl_lock);
	return ret;
}

static int __init
init(void)
{
	int ret;

	ret = misc_register(&pmuctl_dev);
	if (ret) {
		printk(KERN_ERR "pmuctl - misc_register failed, err = %d\n",
		       ret);
		goto err_mist_register;
	}

	on_each_cpu(enable_cycle_counter_el0, NULL, 1);

	return 0;

err_mist_register:
	return ret;
}

static void __exit
fini(void)
{
	on_each_cpu(disable_cycle_counter_el0, NULL, 1);

	misc_deregister(&pmuctl_dev);
}

MODULE_DESCRIPTION("Enables user-mode access to ARMv8 PMU counters");
MODULE_LICENSE("GPL");
module_init(init);
module_exit(fini);
