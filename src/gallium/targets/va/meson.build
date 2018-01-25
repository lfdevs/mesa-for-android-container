# Copyright © 2017 Intel Corporation

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# TODO: support non-static targets
# Static targets are always enabled in autotools (unless you modify
# configure.ac)

va_deps = []
va_link_args = []
va_link_depends = []
va_drivers = []

if with_ld_version_script
  va_link_args += ['-Wl,--version-script', join_paths(meson.current_source_dir(), 'va.sym')]
  va_link_depends += files('va.sym')
endif
if with_platform_x11
  va_deps += [dep_xcb, dep_x11_xcb, dep_xcb_dri2, dep_xcb_dri3]
endif

libva_gallium = shared_library(
  'gallium_drv_video',
  'target.c',
  c_args : c_vis_args,
  cpp_args : cpp_vis_args,
  link_args : [va_link_args, ld_args_gc_sections],
  include_directories : [
    inc_common, inc_util, inc_gallium_winsys, inc_gallium_drivers,
  ],
  link_with : [
    libva_st, libgalliumvlwinsys, libgalliumvl, libgallium, libmesa_util,
    libpipe_loader_static, libws_null, libwsw,
  ],
  dependencies : [
    dep_libdrm, dep_thread, va_deps, driver_r600, driver_radeonsi, driver_nouveau,
  ],
  link_depends : va_link_depends,
  install : true,
  install_dir : va_drivers_path,
)

foreach d : [[with_gallium_r600, 'r600'],
             [with_gallium_radeonsi, 'radeonsi'],
             [with_gallium_nouveau, 'nouveau']]
  if d[0]
    va_drivers += '@0@_drv_video.so'.format(d[1])
  endif
endforeach

meson.add_install_script(
  join_paths(meson.source_root(), 'bin/install_megadrivers.py'),
  libva_gallium.full_path(),
  va_drivers_path,
  va_drivers,
)
