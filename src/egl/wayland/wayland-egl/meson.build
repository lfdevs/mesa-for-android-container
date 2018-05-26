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


libwayland_egl = shared_library(
  'wayland-egl',
  'wayland-egl.c',
  c_args : [c_vis_args],
  link_args : ld_args_gc_sections,
  dependencies : dep_wayland_client,
  version : '1.0.0',
  install : true,
)

pkg.generate(
  name : 'wayland-egl',
  description : 'Mesa wayland-egl library',
  libraries : libwayland_egl,
  version : meson.project_version(),
  requires : 'wayland-client',
)

if with_tests
  test('wayland-egl-symbols-check',
    find_program('wayland-egl-symbols-check'),
    env : env_test,
    args : libwayland_egl
  )
  test(
    'wayland-egl-abi-check',
    executable('wayland-egl-abi-check', 'wayland-egl-abi-check.c')
  )
endif
