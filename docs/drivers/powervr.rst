PowerVR
=======

PowerVR is an open source Vulkan driver for Imagination Technologies PowerVR
GPUs, starting with those based on the Rogue architecture.

The driver is conformant to Vulkan 1.2 on `BXS-4-64 <https://www.khronos.org/conformance/adopters/conformant-products#submission_981>`__,
but **not yet on other GPUs and Vulkan versions**, so it requires exporting
``PVR_I_WANT_A_BROKEN_VULKAN_DRIVER=1`` to the environment for GPUs that aren't
conformant to any Vulkan version before running any Vulkan content.

The following hardware is currently in active development:

========= =========== ============== =======
Product   Series      B.V.N.C        Vulkan
========= =========== ============== =======
AXE-1-16M A-Series    33.15.11.3     1.2
BXM-4-64  B-Series    36.52.104.182  1.2
BXM-4-64  B-Series    36.56.104.183  1.2
BXS-4-64  B-Series    36.53.104.796  1.2
========= =========== ============== =======

The following hardware is partially supported and not currently
under active development:

========= =========== ============== =======
Product   Series      B.V.N.C        Vulkan
========= =========== ============== =======
GX6250    Series 6XT  4.40.2.51      1.2
========= =========== ============== =======

Various core-specific features and workarounds are likely to be unimplemented
for this hardware. Some very simple Vulkan applications may run unhindered, but
instability and corruption are to be expected until additional feature support
and workarounds are in place.

The following hardware is unsupported and not under active development:

========= =========== ==============
Product   Series      B.V.N.C
========= =========== ==============
G6110     Series 6XE  5.9.1.46
GX6250    Series 6XT  4.45.2.58
GX6650    Series 6XT  4.46.6.62
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

Multi-Architecture support
--------------------------

To support multiple distinct hardware generations without too much
spaghetti-code, the PowerVR build compiles a few files multiple times (once
per hardware architecture) and uses a system of macros and aliases to be
able to refer to the different versions.

The files that get compiled multiple times are those named
:file:`pvr_arch_*.c`. These files contain definitions of functions prefixed
with ``pvr_arch_`` (instead of the normal ``pvr_``-prefix). The ``arch``-bit
of that function is a placeholder, which gets replaced at compile-time, thanks
to a system of defines in the corresponding header file, supported by a set
of macros defined in :file:`pvr_macros.h`.

The intention is that these functions are mostly called from architecture
specific entrypoints, that are handled by the common vulkan dispatch-table
code. This means that an architecture specific function can easily call either
architecture specific or architecture agnostic code.

The tricky bit comes when architecture agnostic code calls architecture
specific code. In that case, we have the ``PVR_ARCH_DISPATCH`` and
``PVR_ARCH_DISPATCH_RET`` macros. These are a bit error-prone to use because
they need to see definitions for all architecture versions of each entrypoint,
which isn't something we have available. To work around this, we define a
``PER_ARCH_FUNCS(arch)`` macro in each source-file that needs to use these
dispatch macros and make sure to instantiate it once per architecture.

To avoid confusion, please do not add functions that are prefixed with
``pvr_arch_`` if they are not part of the system described here.

drm-shim
--------

PowerVR implements ``drm-shim``, stubbing out the powervr DRM kernel interface.
This allows for use-cases such as offline compiler testing and control-stream
dumping.

To build Mesa with the PowerVR drm-shim, configure Meson with
``-Dvulkan-drivers=imagination`` and ``-Dtools=drm-shim``.
The drm-shim binary will be built to
``build/src/imagination/drm-shim/libpowervr_noop_drm_shim.so``.

To use, set the ``LD_PRELOAD`` environment variable to the drm-shim binary.

By default, drm-shim mocks a BXS-4-64 with a BVNC of 36.53.104.796,
however this can be overridden by setting the ``PVR_SHIM_DEVICE_BVNC``
environment variable. The value to set should match those found in the
``struct pvr_device_ident`` instances defined in
``src/imagination/common/device_info/*.h``.

The reported quirks and enhancements can be overridden by setting the
``PVR_SHIM_MUSTHAVE_QUIRKS``, ``PVR_SHIM_QUIRKS``, and ``PVR_SHIM_ENHANCEMENTS``
environment variables. These should be set to a comma/semicolon-separated list
of quirk/enhancement IDs.

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
