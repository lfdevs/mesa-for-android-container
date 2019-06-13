# Copyright © 2017-2019 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

files_libiris = files(
  'driinfo_iris.h',
  'iris_batch.c',
  'iris_batch.h',
  'iris_binder.c',
  'iris_binder.h',
  'iris_blit.c',
  'iris_border_color.c',
  'iris_bufmgr.c',
  'iris_bufmgr.h',
  'iris_clear.c',
  'iris_context.c',
  'iris_context.h',
  'iris_draw.c',
  'iris_fence.c',
  'iris_fence.h',
  'iris_formats.c',
  'iris_pipe.h',
  'iris_pipe_control.c',
  'iris_program.c',
  'iris_program_cache.c',
  'iris_query.c',
  'iris_resolve.c',
  'iris_resource.c',
  'iris_resource.h',
  'iris_screen.c',
  'iris_screen.h',
)

iris_driinfo_h = custom_target(
  'iris_driinfo.h',
  input : files(
    '../../../util/merge_driinfo.py',
    '../../auxiliary/pipe-loader/driinfo_gallium.h', 'driinfo_iris.h'
  ),
  output : 'iris_driinfo.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
)

iris_gen_libs = []
foreach v : ['80', '90', '100', '110']
  iris_gen_libs += static_library(
    'libiris_gen@0@'.format(v),
    ['iris_blorp.c', 'iris_state.c', gen_xml_pack],
    include_directories : [inc_common, inc_intel, inc_nir],
    c_args : [
      c_vis_args, no_override_init_args, c_sse2_args,
      '-DGEN_VERSIONx10=@0@'.format(v),
    ],
    dependencies : [dep_libdrm, dep_valgrind],
  )
endforeach

libiris = static_library(
  'iris',
  [files_libiris, gen_xml_pack, nir_opcodes_h, nir_builder_opcodes_h,
   iris_driinfo_h],
  include_directories : [
    inc_src, inc_include, inc_gallium, inc_gallium_aux, inc_intel, inc_nir,
    inc_gallium_drivers,
    # these should not be necessary, but main/macros.h...
    inc_mesa, inc_mapi
  ],
  c_args : [c_vis_args, c_sse2_args],
  cpp_args : [cpp_vis_args, c_sse2_args],
  dependencies : [dep_libdrm, dep_valgrind, idep_genxml],
  link_with : [
    iris_gen_libs, libintel_common, libintel_compiler, libintel_dev, libisl,
    libblorp
  ],
)

driver_iris = declare_dependency(
  compile_args : '-DGALLIUM_IRIS',
  sources : iris_driinfo_h,
  link_with : [libiris, libiriswinsys],
)
