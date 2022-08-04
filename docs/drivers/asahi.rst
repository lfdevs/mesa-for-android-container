Asahi
=====

The Asahi driver aims to provide an OpenGL implementation for the Apple M1.

Testing on macOS
-----------------

On macOS, the experimental Asahi driver may built with options:

    -Dosmesa=true -Dglx=xlib -Dgallium-drivers=asahi,swrast

To use, set the ``DYLD_LIBRARY_PATH`` environment variable:

   DYLD_LIBRARY_PATH=/Users/nobody/mesa/build/src/gallium/targets/libgl-xlib/ glmark2 --reuse-context

Only X11 apps are supported. XQuartz must be setup separately.

Wrap (macOS only)
-----------------

Mesa includes a library that wraps the key IOKit entrypoints used in the macOS
UABI for AGX. The wrapped routines print information about the kernel calls made
and dump work submitted to the GPU using agxdecode.

This library allows debugging Mesa, particularly around the undocumented macOS
user-kernel interface. Logs from Mesa may compared to Metal to check that the
UABI is being used correcrly.

Furthermore, it allows reverse-engineering the hardware, as glue to get at the
"interesting" GPU memory.

The library is only built if ``-Dtools=asahi`` is passed. It builds a single
``wrap.dylib`` file, which should be inserted into a process with the
``DYLD_INSERT_LIBRARIES`` environment variable.

For example, to trace an app ``./app``, run:

    DYLD_INSERT_LIBRARIES=~/mesa/build/src/asahi/lib/libwrap.dylib ./app
