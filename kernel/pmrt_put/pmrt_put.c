// SPDX-License-Identifier: GPL-2.0
/*
 * Route R probe helper (test only). Panfrost's remove callback takes a runtime-PM reference
 * (pm_runtime_get_sync) and never drops it, so after `unbind` the GPU device keeps
 * usage_count 1 and mali_kbase's devicetree platform refuses to probe ("Device runtime usage
 * count unexpectedly non zero 1"). This puts the device back in the never-bound state
 * (usage_count 0, runtime PM disabled once, status suspended) and stays loaded doing nothing.
 *   insmod pmrt_put.ko [devname=1800000.gpu]   (the device must be unbound)
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

static char *devname = "1800000.gpu";
module_param(devname, charp, 0444);

static void report(struct device *dev, const char *when)
{
	pr_info("pmrt_put: %s %s: usage_count %d disable_depth %u runtime_status %d\n", devname, when,
		atomic_read(&dev->power.usage_count), dev->power.disable_depth, dev->power.runtime_status);
}

static int __init pmrt_put_init(void)
{
	struct device *dev = bus_find_device_by_name(&platform_bus_type, NULL, devname);

	if (!dev) {
		pr_err("pmrt_put: %s not found\n", devname);
		return -ENODEV;
	}
	if (dev->driver) {
		pr_err("pmrt_put: %s is bound to %s; unbind it first\n", devname, dev->driver->name);
		put_device(dev);
		return -EBUSY;
	}
	report(dev, "before");
	while (atomic_read(&dev->power.usage_count) > 0)
		pm_runtime_put_noidle(dev);
	if (dev->power.disable_depth == 0)
		pm_runtime_disable(dev); /* a failed probe can leave it enabled */
	if (dev->power.runtime_status != RPM_SUSPENDED)
		pm_runtime_set_suspended(dev);
	report(dev, "after");
	put_device(dev);
	return 0;
}

static void __exit pmrt_put_exit(void)
{
}

module_init(pmrt_put_init);
module_exit(pmrt_put_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Route R test helper: drop the runtime-PM reference Panfrost's remove leaves");
