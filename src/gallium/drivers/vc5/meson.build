# Copyright © 2017 Broadcom
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

files_libvc5 = files(
  'vc5_blit.c',
  'vc5_bufmgr.c',
  'vc5_bufmgr.h',
  'vc5_cl.c',
  'vc5_cl.h',
  'vc5_context.c',
  'vc5_context.h',
  'vc5_fence.c',
  'vc5_formats.c',
  'vc5_job.c',
  'vc5_program.c',
  'vc5_query.c',
  'vc5_resource.c',
  'vc5_resource.h',
  'vc5_screen.c',
  'vc5_screen.h',
  'vc5_simulator.c',
  'vc5_simulator_wrapper.cpp',
  'vc5_tiling.c',
  'vc5_tiling.h',
  'vc5_uniforms.c',
)

files_per_version = files(
  'v3dx_format_table.c',
  'v3dx_job.c',
  'v3dx_simulator.c',
  'vc5_draw.c',
  'vc5_emit.c',
  'vc5_rcl.c',
  'vc5_state.c',
)

v3dv3_c_args = []
dep_v3dv3 = dependency('v3dv3')
if dep_v3dv3.found()
  v3dv3_c_args = '-DUSE_VC5_SIMULATOR'
endif

vc5_versions = ['33', '41']

per_version_libs = []
foreach ver : vc5_versions
  per_version_libs += static_library(
    'vc5-v' + ver,
    [files_per_version, v3d_xml_pack, nir_opcodes_h, nir_builder_opcodes_h],
    include_directories : [
      inc_src, inc_include, inc_gallium, inc_gallium_aux, inc_broadcom,
      inc_gallium_drivers, inc_drm_uapi,
    ],
    c_args : [c_vis_args, v3dv3_c_args, '-DV3D_VERSION=' + ver],
    cpp_args : [cpp_vis_args],
    dependencies : [dep_v3dv3, dep_libdrm, dep_valgrind],
)

endforeach

libvc5 = static_library(
  'vc5',
  [files_libvc5, v3d_xml_pack],
  include_directories : [
    inc_src, inc_include, inc_gallium, inc_gallium_aux, inc_broadcom,
    inc_gallium_drivers, inc_drm_uapi,
  ],
  c_args : [c_vis_args, v3dv3_c_args],
  cpp_args : [cpp_vis_args, v3dv3_c_args],
  dependencies : [dep_v3dv3, dep_libdrm, dep_valgrind, idep_nir_headers],
  link_with: per_version_libs,
)

driver_vc5 = declare_dependency(
  compile_args : '-DGALLIUM_VC5',
  link_with : [libvc5, libvc5winsys, libbroadcom_cle, libbroadcom_vc5],
  dependencies : idep_nir,
)
