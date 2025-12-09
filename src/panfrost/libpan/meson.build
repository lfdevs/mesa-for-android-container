# Copyright © 2024 Collabora
# SPDX-License-Identifier: MIT


libpan_shader_files = files(
  'clear_afbc_metadata.cl',
  'query_pool.cl',
  'draw_helper.cl',
  'indirect_dispatch.cl',
)

idep_libpan_per_arch = {}

foreach ver : ['4', '5', '6', '7', '9', '10', '12', '13']
  libpan_spv = custom_target(
    input : libpan_shader_files,
    output : 'libpan_v' + ver + '.spv',
    command : [
        prog_mesa_clc, '-o', '@OUTPUT@',  '--depfile', '@DEPFILE@',
        libpan_shader_files, '--',
        '-DPAN_ARCH=@0@'.format(ver),
        '-I' + join_paths(meson.project_source_root(), 'include'),
        '-I' + join_paths(meson.project_source_root(), 'src/compiler/libcl'),
        '-I' + join_paths(meson.current_source_dir(), '.'),
        '-I' + join_paths(meson.current_source_dir(), '../../'),
        '-I' + join_paths(meson.current_source_dir(), '../'),
        '-I' + join_paths(meson.current_build_dir(), '../'),
        cl_args,
        ],
    depends : [pan_packers],
    depfile : 'libpan_shaders_v' + ver + '.h.d',
  )

  libpan_nir_cur_ver = custom_target(
    input : libpan_spv,
    output : ['libpan_v' + ver + '.cpp', 'libpan_v' + ver + '.h'],
    command : [prog_vtn_bindgen2, libpan_spv, '@OUTPUT@'],
  )

  libpan_shaders_cur_ver = custom_target(
    input : libpan_spv,
    output : ['libpan_shaders_v' + ver + '.h', 'libpan_shaders_v' + ver + '.c'],
    command : [prog_panfrost_compile, 'libpan_shaders', ver, '@INPUT0@', '@OUTPUT@'],
  )

  idep_libpan_cur_ver = declare_dependency(
    sources: [libpan_shaders_cur_ver, libpan_nir_cur_ver],
    include_directories : include_directories('.'),
  )
  idep_libpan_per_arch += {ver: idep_libpan_cur_ver}
endforeach

