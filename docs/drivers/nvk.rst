NVK
===

NVK is a Vulkan driver for NVIDIA GPUs.

Hardware support
----------------

NVK currently supports Kepler (GeForce 600 and 700 series) and later GPUs up to
and including Ada (RTX 4000 series), as well as consumer Blackwell GPUs
(RTX 5000 series).

Conformance status:
-------------------

NVK is a conformant Vulkan 1.4 implementation for all officially supported
GPUs.

OpenGL support through Zink:
----------------------------

Starting with Mesa 25.1, all Turing (RTX 2000 series and GTX 16xx) and
later GPUs will get NVK+Zink as their OpenGL implementation by default
instead of the old Nouveau GL driver.  NVK+Zink is a conformant OpenGL 4.6
implementation. The Nouveau GL driver is no longer supported on these cards.

Kernel requirements
-------------------

NVK requires at least a Linux 6.6 kernel

Debugging
---------

Here are a few environment variable debug environment variables
specific to NVK:

.. envvar:: NAK_DEBUG

   a comma-separated list of named flags affecting the NVK back-end shader
   compiler:

   ``print``
      Prints the shader at various stages of the compile pipeline
   ``serial``
      Forces serial instruction execution; this is often useful for
      debugging or working around dependency bugs
   ``spill``
      Forces the GPR file to a minimal size to test the spilling code
   ``annotate``
      Adds extra annotation instructions to the IR to track information
      from various compile passes

.. envvar:: NVK_DEBUG

   a comma-separated list of named flags, which do various things:

   ``push``
      Dumps all pushbufs to stderr on submit.  This requires that
      ``push_sync`` also be set.
   ``push_sync``
      Waits for submit to complete before continuing
   ``zero_memory``
      Zeros all VkDeviceMemory objects upon creation
   ``trash_memory``
      Write repeating nonzero patterns to client memory allocations
   ``vm``
      Logs VM binds and unbinds
   ``no_cbuf``
      Disables automatic promotion of UBOs to constant buffers
   ``edb_bview``
      Forces the driver to use the VK_EXT_descriptor_buffer path for buffer
      views.
   ``gart``
      Forces all memory to be allocated from system RAM (GART)
   ``coherent``
      Forces all memory maps to be coherent with the CPU caches.  This only
      applies to Tegra devices.

.. envvar:: NVK_I_WANT_A_BROKEN_VULKAN_DRIVER

   If defined to ``1`` or ``true``, this will enable enumeration of all
   GPUs Kepler and later, including GPUs for which hardware support is
   poorly tested or completely broken.  This is intended for developer use
   only.

Developer info
--------------

.. toctree::
   :glob:

   nvk/*
