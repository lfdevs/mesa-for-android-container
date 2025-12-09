PowerVR
=======

PowerVR is a Vulkan driver for Imagination Technologies PowerVR GPUs, starting
with those based on the Rogue architecture.

The driver is conformant to Vulkan 1.0 on `BXS-4-64 <https://www.khronos.org/conformance/adopters/conformant-products#submission_936>`__,
but **not yet on other GPUs and Vulkan versions**, so it requires exporting
``PVR_I_WANT_A_BROKEN_VULKAN_DRIVER=1`` to the environment before running any
Vulkan content.

The following hardware is currently in active development:

========= =========== ============== =======
Product   Series      B.V.N.C        Vulkan
========= =========== ============== =======
AXE-1-16M A-Series    33.15.11.3     1.2
BXS-4-64  B-Series    36.53.104.796  1.2
BXM-4-64  B-Series    36.52.104.182  1.2
BXM-4-64  B-Series    36.56.104.183  1.2
========= =========== ============== =======

The following hardware is partially supported and not currently
under active development:

========= =========== ============== ======= ==========
Product   Series      B.V.N.C        Vulkan  Notes
========= =========== ============== ======= ==========
GX6250    Series 6XT  4.40.2.51      1.2     [#GX6250]_
========= =========== ============== ======= ==========

.. [#GX6250]
   Various core-specific texture, compute, and other workarounds are
   currently unimplemented for this device. Some very simple Vulkan applications
   may run unhindered, but instability and corruption are to be expected until
   the aforementioned workarounds are in place.

The following hardware is unsupported and not under active development:

========= =========== ==============
Product   Series      B.V.N.C
========= =========== ==============
GX6250    Series 6XT  4.45.2.58
GX6650    Series 6XT  4.46.6.62
G6110     Series 6XE  5.9.1.46
GE7800    Series 7XE  15.5.1.64
GE8300    Series 8XE  22.67.54.30
GE8300    Series 8XE  22.68.54.30
GE8300    Series 8XE  22.102.54.38
BXE-2-32  B-Series    36.29.52.182
BXE-4-32  B-Series    36.50.54.182
========= =========== ==============

Device info and firmware_ have been made available for these devices, typically
due to community requests or interest, but no support is guaranteed beyond this.

.. _firmware: https://gitlab.freedesktop.org/imagination/linux-firmware

In some cases, a product name is shared across multiple BVNCs so to check for
support make sure the BVNC matches the one listed. As the feature set and
hardware issues can vary between BVNCs, additional driver changes might be
necessary even for devices sharing the same product name.

Hardware documentation can be found at: https://docs.imgtec.com/

Note: GPUs prior to Series6 do not have the hardware capabilities required to
support Vulkan and therefore cannot be supported by this driver.

Chat
----

PowerVR developers and users hang out on IRC at ``#powervr`` on OFTC. Note
that registering and authenticating with ``NickServ`` is required to prevent
spam. `Join the chat. <https://webchat.oftc.net/?channels=powervr>`_

Hardware glossary
-----------------

.. glossary:: :sorted:

   BVNC
      Set of four numbers used to uniquely identify each GPU (Series6 onwards).
      This is used to determine the GPU feature set, along with any known
      hardware issues.
