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

files_i965 = files(
  'brw_binding_tables.c',
  'brw_blorp.c',
  'brw_blorp.h',
  'brw_bufmgr.c',
  'brw_bufmgr.h',
  'brw_clear.c',
  'brw_clip.c',
  'brw_compute.c',
  'brw_conditional_render.c',
  'brw_context.c',
  'brw_context.h',
  'brw_cs.c',
  'brw_cs.h',
  'brw_curbe.c',
  'brw_defines.h',
  'brw_disk_cache.c',
  'brw_draw.c',
  'brw_draw.h',
  'brw_draw_upload.c',
  'brw_ff_gs.c',
  'brw_ff_gs_emit.c',
  'brw_ff_gs.h',
  'brw_formatquery.c',
  'brw_generate_mipmap.c',
  'brw_gs.c',
  'brw_gs.h',
  'brw_gs_surface_state.c',
  'brw_link.cpp',
  'brw_meta_util.c',
  'brw_meta_util.h',
  'brw_misc_state.c',
  'brw_multisample_state.h',
  'brw_nir_uniforms.cpp',
  'brw_object_purgeable.c',
  'brw_pipe_control.c',
  'brw_performance_query.c',
  'brw_program.c',
  'brw_program.h',
  'brw_program_binary.c',
  'brw_program_cache.c',
  'brw_primitive_restart.c',
  'brw_queryobj.c',
  'brw_reset.c',
  'brw_sf.c',
  'brw_state.h',
  'brw_state_upload.c',
  'brw_structs.h',
  'brw_surface_formats.c',
  'brw_sync.c',
  'brw_tcs.c',
  'brw_tcs_surface_state.c',
  'brw_tes.c',
  'brw_tes_surface_state.c',
  'brw_urb.c',
  'brw_util.c',
  'brw_util.h',
  'brw_vs.c',
  'brw_vs.h',
  'brw_vs_surface_state.c',
  'brw_wm.c',
  'brw_wm.h',
  'brw_wm_surface_state.c',
  'gen4_blorp_exec.h',
  'gen6_clip_state.c',
  'gen6_constant_state.c',
  'gen6_multisample_state.c',
  'gen6_queryobj.c',
  'gen6_sampler_state.c',
  'gen6_sol.c',
  'gen6_urb.c',
  'gen7_l3_state.c',
  'gen7_sol_state.c',
  'gen7_urb.c',
  'gen8_depth_state.c',
  'gen8_multisample_state.c',
  'hsw_queryobj.c',
  'hsw_sol.c',
  'intel_batchbuffer.c',
  'intel_batchbuffer.h',
  'intel_blit.c',
  'intel_blit.h',
  'intel_buffer_objects.c',
  'intel_buffer_objects.h',
  'intel_buffers.c',
  'intel_buffers.h',
  'intel_copy_image.c',
  'intel_extensions.c',
  'intel_fbo.c',
  'intel_fbo.h',
  'intel_image.h',
  'intel_mipmap_tree.c',
  'intel_mipmap_tree.h',
  'intel_pixel_bitmap.c',
  'intel_pixel.c',
  'intel_pixel_copy.c',
  'intel_pixel_draw.c',
  'intel_pixel.h',
  'intel_pixel_read.c',
  'intel_screen.c',
  'intel_screen.h',
  'intel_state.c',
  'intel_tex.c',
  'intel_tex_copy.c',
  'intel_tex.h',
  'intel_tex_image.c',
  'intel_tex_obj.h',
  'intel_tex_validate.c',
  'intel_upload.c',
  'libdrm_macros.h',
)

i965_gen_libs = []
foreach v : ['40', '45', '50', '60', '70', '75', '80', '90', '110']
  i965_gen_libs += static_library(
    'i965_gen@0@'.format(v),
    ['genX_blorp_exec.c', 'genX_boilerplate.h', 'genX_pipe_control.c',
     'genX_state_upload.c', gen_xml_pack],
    include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_intel, inc_dri_common],
    c_args : [
      no_override_init_args, c_sse2_args,
      '-DGEN_VERSIONx10=@0@'.format(v),
    ],
    gnu_symbol_visibility : 'hidden',
    dependencies : [dep_libdrm, idep_nir_headers],
  )
endforeach


libi965 = static_library(
  'i965',
  [files_i965, ir_expression_operation_h],
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_intel, inc_dri_common,
  ],
  c_args : [no_override_init_args, c_sse2_args],
  cpp_args : [c_sse2_args],
  gnu_symbol_visibility : 'hidden',
  link_with : [
    i965_gen_libs, libintel_dev, libisl, libintel_compiler, libblorp,
    libintel_perf
  ],
  dependencies : [
    dep_libdrm, dep_valgrind, idep_libintel_common, idep_nir_headers, idep_genxml,
    idep_xmlconfig,
  ],
)
