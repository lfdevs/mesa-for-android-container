Benchmarking Panfrost
=====================

Obtaining reproducible benchmark timings can sometimes be tricky on Mali
based SoCs. There are a number of things that can be done to make the timings
more predictable.

Make sure the device is actively cooled (to eliminate thermal throttling)
-------------------------------------------------------------------------

This isn't strictly speaking necessary, if you use low enough frequencies
in the steps below, but it is recommended.

Fix the GPU frequency
---------------------

If power management is allowed to vary the GPU frequency, this will lead to
uncertainty in benchmark figures, particularly for shorter benchmark runs.

On most systems, the GPU frequency can be set to a fixed value via a command
like the following (run it as root, sudo alone won't allow the redirection):

.. code-block:: sh

   cat /sys/devices/platform/fb000000.gpu/devfreq/fb000000.gpu/min_freq > /sys/devices/platform/fb000000.gpu/devfreq/fb000000.gpu/max_freq

This forces the GPU to always use its minimum frequency. You could reverse
`min_freq` and `max_freq` to fix it to its maximum frequency, if your system
is adequately cooled.

Fix the CPU frequency
---------------------

It is easy to overlook the role the CPU plays in driver performance. To set
its frequency to a fixed value, install the `linux-cpupower` package and
run something like:

.. code-block:: sh

   sudo cpupower frequency-info
   sudo cpupower frequency-set -d 1.20
   sudo cpupower frequency-set -u 1.20
   sudo cpupower frequency-info

Adjust the numbers based on what the board actually supports (seen in
the first `frequency-info`. It is best to choose fairly low settings
unless you're confident of the board's cooling.

Pin the benchmark program to a particular core
----------------------------------------------

This is especially important on devices with a big.LITTLE architecture, where
having the benchmark be scheduled on a different kind of core can drastically
change the performance. But even without this, variations in cache residency
and scheduling of kernel tasks can lead to changes in benchmark timing.

Experience shows that using a little core for the benchmark program
tends to give more reproducible results (probably because they have smaller
caches, and also are less likely to be scheduled to by the kernel).
However, forcing the benchmark to run on a little core could change the
bottleneck from GPU to CPU, so watch out for that.

Pinning a task to a particular core is done with the `taskset` command. An
example for the Rock5b (rk3588) board is:

.. code-block:: sh

   taskset 0x04 glmark2-es2-wayland

Replace `0x04` with an appropriate mask for your board, and
`glmark2-es2-wayland` with the benchmark program to be used.
