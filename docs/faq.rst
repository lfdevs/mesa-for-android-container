Frequently Asked Questions
==========================

Last updated: 19 September 2018

1. High-level Questions and Answers
-----------------------------------

1.1 What is Mesa?
~~~~~~~~~~~~~~~~~

Mesa is an open-source implementation of the OpenGL specification.
OpenGL is a programming library for writing interactive 3D applications.
See the `OpenGL website <https://www.opengl.org/>`__ for more
information.

Mesa 9.x supports the OpenGL 3.1 specification.

1.2 Does Mesa support/use graphics hardware?
--------------------------------------------

Yes. Specifically, Mesa serves as the OpenGL core for the open-source
DRI drivers for X.org.

-  See the `DRI website <https://dri.freedesktop.org/>`__ for more
   information.
-  See `01.org <https://www.intel.com/content/www/us/en/developer/topic-technology/open/overview.html>`__
   for more information about Intel drivers.
-  See `nouveau.freedesktop.org <https://nouveau.freedesktop.org>`__ for
   more information about Nouveau drivers.
-  See `www.x.org/RadeonFeature <https://www.x.org/RadeonFeature>`__
   for more information about Radeon drivers.

2. Compilation and Installation Problems
----------------------------------------

2.1 What's the easiest way to install Mesa?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you're using a Linux-based system, your distribution
most likely already includes Mesa packages.

2.2 What's the proper place for the libraries and headers?
----------------------------------------------------------

On Linux-based systems you'll want to follow the `Linux
ABI <https://registry.khronos.org/OpenGL/ABI/>`__ standard.
Basically you'll want the following:

``/usr/include/GL/gl.h``
   the main OpenGL header
``/usr/include/GL/glu.h``
   the OpenGL GLU (utility) header
``/usr/include/GL/glx.h``
   the OpenGL GLX header
``/usr/include/GL/glext.h``
   the OpenGL extensions header
``/usr/include/GL/glxext.h``
   the OpenGL GLX extensions header
``/usr/lib/libGL.so``
   a symlink to ``libGL.so.1``
``/usr/lib/libGL.so.1``
   a symlink to ``libGL.so.1.xyz``
``/usr/lib/libGL.so.xyz``
   the actual OpenGL/Mesa library. xyz denotes the Mesa version number.

When configuring Mesa, there are three meson options that affect the
install location that you should take care with: ``--prefix``,
``--libdir``, and ``-D dri-drivers-path``. To install Mesa into the
system location where it will be available for all programs to use, set
``--prefix=/usr``. Set ``--libdir`` to where your Linux distribution
installs system libraries, usually either ``/usr/lib`` or
``/usr/lib64``. Set ``-D dri-drivers-path`` to the directory where your
Linux distribution installs DRI drivers. To find your system's DRI
driver directory, try executing ``find /usr -type d -name dri``. For
example, if the ``find`` command listed ``/usr/lib64/dri``, then set
``-D dri-drivers-path=/usr/lib64/dri``.

After determining the correct values for the install location, configure
Mesa with
``meson configure --prefix=/usr --libdir=xxx -D dri-drivers-path=xxx``
and then install with ``sudo meson install``.

3. Runtime / Rendering Problems
-------------------------------

3.1 Rendering is slow / why isn't my graphics hardware being used?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If Mesa can't use its hardware accelerated drivers it falls back on one
of its software renderers. (e.g. Softpipe or LLVMpipe)

You can run the ``glxinfo`` program to learn about your OpenGL library.
Look for the ``OpenGL vendor`` and ``OpenGL renderer`` values. That will
identify who's OpenGL library with which driver you're using and what
sort of hardware it has detected.

If you're using a hardware accelerated driver you want
``direct rendering: Yes``.

If your DRI-based driver isn't working, go to the `DRI
website <https://dri.freedesktop.org/>`__ for trouble-shooting
information.

3.2 I'm seeing errors in depth (Z) buffering. Why?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Make sure the ratio of the far to near clipping planes isn't too great.
Look
`here <https://www.opengl.org/archives/resources/faq/technical/depthbuffer.htm#0040>`__
for details.

Mesa uses a 16-bit depth buffer by default which is smaller and faster
to clear than a 32-bit buffer but not as accurate. If you need a deeper
you can modify the parameters to ``glXChooseVisual`` in your code.

3.3 Why Isn't depth buffering working at all?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Be sure you're requesting a depth buffered-visual. If you set the
``MESA_DEBUG`` environment variable it will warn you about trying to
enable depth testing when you don't have a depth buffer.

Specifically, make sure ``glutInitDisplayMode`` is being called with
``GLUT_DEPTH`` or ``glXChooseVisual`` is being called with a non-zero
value for ``GLX_DEPTH_SIZE``.

This discussion applies to stencil buffers, accumulation buffers and
alpha channels too.

3.4 Why does ``glGetString()`` always return ``NULL``?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Be sure you have an active/current OpenGL rendering context before
calling ``glGetString``.

3.5 ``GL_POINTS`` and ``GL_LINES`` don't touch the right pixels
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you're trying to draw a filled region by using ``GL_POINTS`` or
``GL_LINES`` and seeing holes or gaps it's because of a float-to-int
rounding problem. But this is not a bug. See Appendix H of the OpenGL
Programming Guide - "OpenGL Correctness Tips". Basically, applying a
translation of (0.375, 0.375, 0.0) to your coordinates will fix the
problem.

4. Developer Questions
----------------------

4.1 How can I contribute?
~~~~~~~~~~~~~~~~~~~~~~~~~

First, join the :doc:`mesa-dev mailing list <lists>`. That's where
Mesa development is discussed.

The `OpenGL Specification
<https://registry.khronos.org/OpenGL/index_gl.php#apispecs>`__ is the
bible for OpenGL implementation work. You should read it.

Most of the Mesa development work involves implementing new OpenGL
extensions, writing hardware drivers (for the DRI), and code
optimization.

4.2 How do I write a new device driver?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Unfortunately, writing a device driver isn't easy. It requires detailed
understanding of OpenGL, the Mesa code, and your target
hardware/operating system. 3D graphics are not simple.

The best way to get started is to use an existing driver as your
starting point. For a Gallium3D hardware driver, the r300g, r600g and the i915g
are good examples.

The DRI website has more information about writing hardware drivers. The
process isn't well document because the Mesa driver interface changes
over time, and we seldom have spare time for writing documentation. That
being said, many people have managed to figure out the process.

Joining the appropriate mailing lists and asking questions (and
searching the archives) is a good way to get information.

4.3 Why isn't :ext:`GL_EXT_texture_compression_s3tc` implemented in Mesa?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Oh but it is! Prior to 2nd October 2017, the Mesa project did not
include S3TC support due to intellectual property (IP) and/or patent
issues around the S3TC algorithm.

As of Mesa 17.3.0, Mesa now officially supports S3TC, as the patent has
expired.

In versions prior to this, a 3rd party `plug-in
library <https://dri.freedesktop.org/wiki/S3TC>`__ was required.
