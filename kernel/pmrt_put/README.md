# pmrt_put

Test-only helper for the prototype switch on a kernel that has Panfrost built in. Panfrost's remove
callback leaves a runtime-PM reference on the GPU device, and `mali_kbase` then refuses to probe
("Device runtime usage count unexpectedly non zero"). Loading this module after the unbind puts the
device back in its never-bound state. An image that builds Panfrost as a module does not need it.

Build it against the device's exact kernel tree:

    make -C /path/to/linux M=$PWD ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules

Use: `insmod pmrt_put.ko [devname=1800000.gpu]` (the device must be unbound).
