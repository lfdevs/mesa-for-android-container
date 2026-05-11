# Copyright © 2017 Dylan Baker
# SPDX-License-Identifier: MIT

si_tracepoints = custom_target(
  'si_tracepoints.[ch]',
  input: 'si_tracepoints.py',
  output: ['si_tracepoints.c', 'si_tracepoints_perfetto.h', 'si_tracepoints.h'],
  command: [
    prog_python, '@INPUT@',
    '-p', join_paths(dir_source_root, 'src/util/perf/'),
    '-C', '@OUTPUT0@',
    '--perfetto-hdr', '@OUTPUT1@',
    '-H', '@OUTPUT2@'
  ],
  depend_files: u_trace_py,
)

idep_si_tracepoints = declare_dependency(
  sources: si_tracepoints,
)

files_libradeonsi = files(
  'driinfo_radeonsi.h',
  'gfx11_query.c',
  'si_barrier.c',
  'si_buffer.c',
  'si_build_pm4.h',
  'si_clear.c',
  'si_context.c',
  'si_cp_dma.c',
  'si_cp_utils.c',
  'si_debug.c',
  'si_descriptors.c',
  'si_fence.c',
  'si_get.c',
  'si_gfx_cs.c',
  'si_gpu_load.c',
  'si_perfcounter.c',
  'si_pipe.c',
  'si_pipe.h',
  'si_pm4.c',
  'si_pm4.h',
  'si_public.h',
  'si_query.c',
  'si_query.h',
  'si_sdma_copy_image.c',
  'si_state.c',
  'si_state.h',
  'si_state_binning.c',
  'si_state_msaa.c',
  'si_state_streamout.c',
  'si_state_viewport.c',
  'si_texture.c',
  'si_utrace.c',
  'si_utrace.h',
  'si_vpe.c',
  'si_vpe.h',
  'gfx/si_gfx.h',
  'mm/si_mm.h',
  'mm/radeon_bitstream.c',
  'mm/radeon_bitstream.h',
  'mm/radeon_uvd_enc.c',
  'mm/radeon_uvd_enc.h',
  'mm/radeon_vce.c',
  'mm/radeon_vce.h',
  'mm/radeon_vcn_enc_1_2.c',
  'mm/radeon_vcn_enc_2_0.c',
  'mm/radeon_vcn_enc_3_0.c',
  'mm/radeon_vcn_enc_4_0.c',
  'mm/radeon_vcn_enc_5_0.c',
  'mm/radeon_vcn_enc.c',
  'mm/radeon_vcn_enc.h',
  'mm/radeon_vcn.c',
  'mm/radeon_vcn.h',
  'mm/si_mm_context.c',
  'mm/si_mm_screen.c',
  'mm/si_mm.h',
  'mm/si_video_dec.c',
  'mm/si_video_dec.h',
  'mm/si_video.c',
  'mm/si_video.h',
)

radeonsi_include_dirs = [inc_src, inc_include, inc_gallium, inc_gallium_aux, inc_amd_common,
                         inc_gallium_drivers, inc_compiler, inc_amd, inc_amd_vpe, inc_virtio_gpu]
radeonsi_deps = [dep_clock, dep_libdrm, idep_nir_headers, idep_amd_generated_headers,
                 idep_mesautil, idep_aco, idep_u_tracepoints, idep_si_tracepoints]

if with_perfetto
  radeonsi_deps += dep_perfetto
endif

files_libradeonsi += ['si_perfetto.cpp', 'si_perfetto.h']

amd_common_libs = [libamd_common]

if amd_with_llvm and with_gfx_compute
  files_libradeonsi += files(
    'gfx/si_shader_llvm.c',
    'gfx/si_shader_llvm.h',
    'gfx/si_shader_llvm_gs.c',
    'gfx/si_shader_llvm_ps.c',
    'gfx/si_shader_llvm_tess.c',
  )

  radeonsi_include_dirs += [inc_amd_common_llvm]
  radeonsi_deps += [dep_llvm]
  amd_common_libs += [libamd_common_llvm]
endif

radeonsi_gfx_libs = []
if with_gfx_compute
  files_libradeonsi+= files(
    'gfx/si_blit.c',
    'gfx/si_cp_reg_shadowing.c',
    'gfx/si_compute_blit.c',
    'gfx/si_compute.c',
    'gfx/si_debug_gfx_compute.c',
    'gfx/si_gfx_context.c',
    'gfx/si_gfx_screen.c',
    'gfx/si_shader_cache.c',
    'gfx/si_nir_mediump.c',
    'gfx/si_mesh_shader.c',
    'gfx/si_shader_aco.c',
    'gfx/si_shader_args.c',
    'gfx/si_shader_binary.c',
    'gfx/si_shader_info.c',
    'gfx/si_shader_info.h',
    'gfx/si_shader_internal.h',
    'gfx/si_shader_nir.c',
    'gfx/si_shader_variant_info.c',
    'gfx/si_shader.c',
    'gfx/si_shader.h',
    'gfx/si_shaderlib_nir.c',
    'gfx/si_state_shaders.cpp',
    'gfx/si_sqtt.c',
    'gfx/si_nir_clamp_shadow_comparison_value.c',
    'gfx/si_nir_kill_outputs.c',
    'gfx/si_nir_lower_abi.c',
    'gfx/si_nir_lower_intrinsics_early.c',
    'gfx/si_nir_lower_polygon_stipple.c',
    'gfx/si_nir_lower_color_flatshade_twoside.c',
    'gfx/si_nir_lower_resource.c',
    'gfx/si_nir_lower_vs_inputs.c',
    'gfx/si_nir_mark_divergent_texture_non_uniform.c',
    'gfx/si_nir_optim.c',
    'tests/si_tests.c',
    'tests/si_test_dma_perf.c',
    'tests/si_test_image_copy_region.c',
    'tests/si_test_vm_fault.c',
  )

  foreach ver : ['6', '7', '8', '9', '10', '103', '11', '115', '117', '12']
    radeonsi_gfx_libs += static_library(
      'radeonsi_gfx@0@'.format(ver),
      ['si_state_draw.cpp'],
      include_directories : radeonsi_include_dirs,
      cpp_args : ['-DGFX_VER=@0@'.format(ver)],
      gnu_symbol_visibility : 'hidden',
      dependencies : radeonsi_deps,
    )
  endforeach
endif

libradeonsi_cflags = []
if with_amdgpu_virtio
  libradeonsi_cflags += ['-DHAVE_AMDGPU_VIRTIO']
endif

radeonsi_build_id = get_option('radeonsi-build-id')
if radeonsi_build_id != ''
  libradeonsi_cflags += '-DRADEONSI_BUILD_ID_OVERRIDE="' + radeonsi_build_id + '"'
endif

libradeonsi = static_library(
  'radeonsi',
  [files_libradeonsi],
  c_args : libradeonsi_cflags,
  include_directories : radeonsi_include_dirs,
  gnu_symbol_visibility : 'hidden',
  dependencies : radeonsi_deps,
)

driver_radeonsi = declare_dependency(
  compile_args : ['-DGALLIUM_RADEONSI'] + libradeonsi_cflags,
  link_with : radeonsi_gfx_libs + [
    libradeonsi, libradeonwinsys, libamdgpuwinsys, amd_common_libs, libvpe, libgm, liblanczos
  ],
  dependencies : idep_nir,
)
