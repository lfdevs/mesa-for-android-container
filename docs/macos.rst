Notes for macOS
================

Mesa builds on macOS without modifications. However, there are some details to
be aware of.

-  Mesa has a number of build-time dependencies. Most dependencies, including
   Meson itself, are available in `homebrew <https://brew.sh>`, which has a
   Mesa package for reference. The exception seems to be Mako, a Python module
   used for templating, which you can install as `pip3 install mako`.
-  macOS's default C compiler doesn't play nice with some C11 idioms used in
   Mesa. To workaround, set `-Dc_std=c11`.
-  macOS is picky about its build-time environment. Type `brew sh` before
   building to get the Homebrew dependencies in your path.

At the moment, only the software rasterizers are supported on macOS. Stay tuned
for updates here!
