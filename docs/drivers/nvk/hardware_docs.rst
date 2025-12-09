
Hardware docs
=============

Command buffers
---------------

Command format
^^^^^^^^^^^^^^

Each command sent to the GPU contains a method and some data. Method names are
documented in corresponding header files copied from NVIDIA, eg. the fermi
graphics methods are in src/nouveau/headers/nvidia/classes/cl9097.h

P_IMMD
""""""

A lot of the time, you will want to issue a single method with its data, which
can be done with P_IMMD::

    P_IMMD(p, NV9097, WAIT_FOR_IDLE, 0);

P_IMMD will emit either a single `immediate-data method`_, which takes a single
word, or a pair of words that's equivalent to P_MTHD + the provided data. Code
must count P_IMMD as possibly 2 words as a result.

 .. _immediate-data method: https://github.com/NVIDIA/open-gpu-doc/blob/87ba53e0c385285a3aa304b864dccb975a9a0dd4/manuals/turing/tu104/dev_ram.ref.txt#L1214

P_MTHD
""""""

`P_MTHD`_ is a convenient way to execute multiple consecutive methods without
repeating the method header. For example, the code::

      P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
      P_NV9097_SET_REPORT_SEMAPHORE_A(p, addr >> 32);
      P_NV9097_SET_REPORT_SEMAPHORE_B(p, addr);
      P_NV9097_SET_REPORT_SEMAPHORE_C(p, value);

generates four words - one word for the method header (defaulting to 1INC) and
then the next three words for data. 1INC will automatically increment the method
id by one word for each data value, which is why the example can advance from
SET_REPORT_SEMAPHORE_A to B to C.

 .. _P_MTHD: https://github.com/NVIDIA/open-gpu-doc/blob/87ba53e0c385285a3aa304b864dccb975a9a0dd4/manuals/turing/tu104/dev_ram.ref.txt#L1042

P_0INC
""""""

`0INC`_ will issue the same method repeatedly for each following data word.

 .. _0INC: https://github.com/NVIDIA/open-gpu-doc/blob/87ba53e0c385285a3aa304b864dccb975a9a0dd4/manuals/turing/tu104/dev_ram.ref.txt#L1096

P_1INC
""""""

`1INC`_ will increment after one word and then issue the following method
repeatedly. For example, the code::

      P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_SET_PRIV_REG));
      P_INLINE_DATA(p, 0);
      P_INLINE_DATA(p, BITFIELD_BIT(3));

issues one NV9097_CALL_MME_MACRO command, then increments the method and issues
two NV9097_CALL_MME_DATA commands.

.. _1INC: https://github.com/NVIDIA/open-gpu-doc/blob/87ba53e0c385285a3aa304b864dccb975a9a0dd4/manuals/turing/tu104/dev_ram.ref.txt#L1149

Execution barriers
^^^^^^^^^^^^^^^^^^

Commands within a command buffer can be synchronized in a few different ways.

 * Explicit WFI - Idles all engines before executing the next command eg. via
   `NVA16F_WFI` or `NV9097_WAIT_FOR_IDLE`
 * Semaphores - Delay execution based on values in a memory location. See
   `open-gpu-doc on semaphores`_
 * A subchannel switch - Causes the hardware to execute an implied WFI

 .. _open-gpu-doc on semaphores: https://github.com/NVIDIA/open-gpu-doc/blob/master/manuals/turing/tu104/dev_pbdma.ref.txt#L3231

Subchannel switches
"""""""""""""""""""

A subchannel switch occurs when the hardware receives a command for a different
subchannel than the one that it's currently executing. For example, if the
hardware is currently executing commands on the 3D engine (`SUBC_NV9097 == 0`), a
command executed on the compute engine (`SUBC_NV90C0 == 1`) will cause a
subchannel switch. Host methods (class \*6F) are an exception to this - they can
be issued to any subchannel and will not trigger a subchannel switch
[#fhostsubcswitch]_ [#foverlapnvk]_.

Subchannel switches act the same way that an explicit WFI does - they fully idle
the channel before issuing commands to the next engine [#fnsight]_
[#foverlapnvk]_.

This works the same on Blackwell. Some NVIDIA documentation contradicts this:
"On NVIDIA Blackwell Architecture GPUs and newer, subchannel switches do not
occur between 3D and compute workloads"[#fnsight]_. This documentation appears
to be wrong or inapplicable for some reason - tests do not reproduce this
behavior [#foverlapnvk]_ [#foverlapprop]_, and the blob does not change its
event implementation for blackwell [#feventprop]_.


.. [#fnsight] https://docs.nvidia.com/nsight-graphics/UserGuide/index.html#subchannel-switch-overlay
.. [#foverlapnvk] Based on reverse engineering by running
    https://gitlab.freedesktop.org/mhenning/re/-/tree/main/vk_test_overlap_exec
    under nvk, possibly with alterations to the way nvk generates commands for
    pipeline barriers.
.. [#foverlapprop] By running
    https://gitlab.freedesktop.org/mhenning/re/-/tree/main/vk_test_overlap_exec
    under the proprietary driver and examining the main output (eg. execution
    overlaps without pipeline barriers)
.. [#feventprop] For example, the event commands generated here only wait on a
    single engine
    https://gitlab.freedesktop.org/mhenning/re/-/blob/6ce8c860da65fbf2ab26d124d25f907dea2cf33a/vk_event/blackwell.out#L20612-20793
.. [#fhostsubcswitch] https://github.com/NVIDIA/open-gpu-doc/blob/87ba53e0c385285a3aa304b864dccb975a9a0dd4/manuals/turing/tu104/dev_ram.ref.txt#L1009

Copy engine
^^^^^^^^^^^

The copy engine's `PIPELINED` mode allows a new transfer to start before the
previous transfer finishes, while `NON_PIPELINED` acts as an execution barrier
between the current copy and the previous one [#fpipelined]_.

 .. [#fpipelined] https://github.com/NVIDIA/open-gpu-kernel-modules/blob/2b436058a616676ec888ef3814d1db6b2220f2eb/src/common/sdk/nvidia/inc/ctrl/ctrl0050.h#L75-L83
