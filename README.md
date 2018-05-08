ARMv8 Performance Counters management module
==========================================

This module allows user to control access to ARMv8 PMU counters from userspace.

It was initially created just for enabling userspace access to *Performance Monitors Cycle Count Register* (**PMCCNTR_EL0**) for use in dataplane software such as [DPDK](http://dpdk.org/dev/patchwork/patch/15225/) framework. It has been later extended to provide a general purpose interface for managing ARMv8 PMU counters.

## Compilation

```sh
git clone https://github.com/jerinjacobk/armv8_pmu_cycle_counter_el0
cd armv8_pmu_cycle_counter_el0
# If compiling natively on ARMv8 host
make
# If cross compiling pass arguments as make arguments, not env vars
make CROSS_COMPILE={cross_compiler_prefix} ARCH=arm64 KDIR=/path/to/kernel/sources
```

## Usage

Next module has to be copied to the target board in question if it was cross-compiled. Next load it:

```sh
sudo insmod pmu_el0_cycle_counter.ko
```

Loading the module will enable userspace access to **PMCCNTR** counter. Unloading this module will disable userspace access to **PMCCNTR**.

The **PMCCNTR** can be read in the application with:

```c
static inline uint64_t
read_pmccntr(void)
{
	uint64_t val;
	asm volatile("mrs %0, pmccntr_el0" : "=r"(val));
	return val;
}
```

Additionally module creates a device (`/dev/pmuctl`) which can be used to enable/disable access to PMU counters (currently only **PMCCNTR** is supported). This device supports the following interfaces:

1. `read()` - Dump current counter configuration. It is preferred to read all data in one call as the data may change in between `read` syscalls:

    ```sh
    $ cat /dev/pmuctl
    PMCCNTR=1
    ```

2. `write()` - Modify the configuration of a particular counter. The write buffer should have the `name=value` format. Both `name` and `value` are counter specific and described in the next chapter. Below is an example of how to use this:

    ```sh
    echo "PMCCNTR=1" > /dev/pmuctl
    ```

3. `ioctl()` - Similar to `write()` but intended for use in user applications rather than scripts. The list of supported ioctls is located in `pmuctl.h` header. Below is an example of how to use this interface:

    ```c
    struct pmuctl_pmccntr_data arg = { .enable = 0 };
    int fd = open("/dev/pmuctl", O_RDONLY);
    if (ioctl(fd, PMU_IOC_PMCCNTR, &arg)) {
        /* error handling */
    }
    ```

### Supported PMU counters

1. **Performance Monitors Cycle Count Register**: `name` is `PMCCNTR`, `value` is `0` to disable EL0 access, `1` to enable EL0 access.

## Adding support for new counters

To add support for managing a new counter, developer should do the following:

1. Add a new value to `enum pmu_ctls` at the end, just before `PM_CTL_CNT`. This will be used to identify the new counter in read and write operations. I.e.:

    ```c
    enum pmu_ctls {
        PM_CTL_PMCCNTR,
        PM_CTL_NEW_CNTR, /* Short description */ // <- new entry
        PM_CTL_CNT,
    };
    ```

2. Write `read()` and `write()` handlers for the new counter and add a descriptor to the `struct pmu_ctl_cfg pmu_ctls` array in `pmu_el0_cycle_counter.c` file. New entry should be placed at the index matching the new entry in `enum pmu_ctls`. I.e.:

    ```c
    static ssize_t
    new_cntr_show(char *arg, size_t size)
    {
        /* Dump configuration to arg buffer, up to size characters.
         * Return number of written characters or negative error code.
         */
    }
    static int
    new_cntr_modify(const char *arg, size_t size)
    {
        /* Modify config according to value parsed from arg buffer
         * of size length.
         * Return 0 on success or a negative error code.
         */
    }
    /* ... */
    static struct pmu_ctl_cfg pmu_ctls[PM_CTL_CNT] = {
        /* ... */
        [PM_CTL_NEW_CNTR] = {
            .name	= "NEW_CNTR",
            .show	= new_cntr_show,
            .modify	= new_cntr_modify
        }
    };
    ```

3. For `ioctl()` support, add the definition of new ioctl and its arguments to the `pmuctl.h` file using `PMUCTL_IOC_MAGIC` as the ioctl magic and new `enum pmu_ctls` as a sequence number and using macros in `<linux/ioctl.h>`. I.e.:

    ```c
    struct pmuctl_new_cntr_arg {
        int some_argument;
    };
    /* ... */
    #define PMU_IOC_NEW_CNTR \
        _IOW(PMUCTL_IOC_MAGIC, PM_CTL_NEW_CNTR, struct pmuctl_new_cntr_arg)
    ```

4. Next add a case statement in `pmuctl_ioctl()` function in `pmu_el0_cycle_counter.c` file to handle the new ioctl, i.e.:

    ```c
    static long
    pmuctl_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
    {
        /* ... */
        mutex_lock(&pmuctl_lock);
        switch (cmd) {
            /* ... */
        case PMU_IOC_NEW_CNTR:
            /* handle the ioctl() */
            break;
        /* ...*/
        }
        mutex_unlock(&pmuctl_lock);
        return ret;
    }
    ```
