# Copyright 2025 LunarG, Inc.
# Copyright 2025 Google LLC
# SPDX-License-Identifier: MIT

kk_device_dispatch_table_dependencies = declare_dependency(
  sources : [
    kk_dispatch_trampolines[1],
  ],
)

# Unsure if needed, just making sure we don't royally mess up the dependencies
kk_device_dispatch_table_dependencies_list = [
  vulkan_lite_runtime_deps,
  kk_device_dispatch_table_dependencies,
  idep_nir,
  idep_mesautil,
  idep_vulkan_runtime,
  idep_vulkan_util,
  idep_vulkan_wsi,
  idep_vulkan_wsi_headers,
]

libkk_device_dispatch_table = static_library(
  'kk_device_dispatch_table',
  kk_dispatch_trampolines,
  include_directories : [inc_include, inc_src],
  dependencies : kk_device_dispatch_table_dependencies_list,
  c_args : c_msvc_compat_args,
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

idep_kk_device_dispatch_table = declare_dependency(
  # Instruct users of this library to link with --whole-archive.  Otherwise,
  # our weak function overloads may not resolve properly.
  link_whole : [libkk_device_dispatch_table]
)

kk_files = files(
  'kk_bo.h',
  'kk_bo.c',
  'kk_buffer_view.h',
  'kk_buffer_view.c',
  'kk_buffer.h',
  'kk_buffer.c',
  'kk_cmd_buffer.h',
  'kk_cmd_buffer.c',
  'kk_cmd_clear.c',
  'kk_cmd_copy.c',
  'kk_cmd_dispatch.c',
  'kk_cmd_draw.c',
  'kk_cmd_meta.c',
  'kk_cmd_pool.h',
  'kk_cmd_pool.c',
  'kk_debug.h',
  'kk_debug.c',
  'kk_descriptor_set_layout.h',
  'kk_descriptor_set_layout.c',
  'kk_descriptor_set.h',
  'kk_descriptor_set.c',
  'kk_device_lib.c',
  'kk_device_memory.h',
  'kk_device_memory.c',
  'kk_device.h',
  'kk_device.c',
  'kk_encoder.h',
  'kk_encoder.c',
  'kk_event.h',
  'kk_event.c',
  'kk_format.h',
  'kk_format.c',
  'kk_image_layout.h',
  'kk_image_layout.c',
  'kk_image_view.h',
  'kk_image_view.c',
  'kk_image.h',
  'kk_image.c',
  'kk_instance.h',
  'kk_instance.c',
  'kk_nir_lower_descriptors.c',
  'kk_nir_lower_multiview.c',
  'kk_nir_lower_textures.c',
  'kk_nir_lower_vbo.h',
  'kk_nir_lower_vbo.c',
  'kk_physical_device.h',
  'kk_physical_device.c',
  'kk_query_pool.h',
  'kk_query_pool.c',
  'kk_query_table.h',
  'kk_query_table.c',
  'kk_queue.h',
  'kk_queue.c',
  'kk_sampler.h',
  'kk_sampler.c',
  'kk_shader.h',
  'kk_shader.c',
  'kk_sync.h',
  'kk_sync.c',
  'kk_wsi.h',
  'kk_wsi.c',
)

kkcl_files = files(
  'cl/kk_query.cl',
  'cl/kk_triangle_fan.cl',
)

kk_entrypoints = custom_target(
  'kk_entrypoints',
  input : [vk_entrypoints_gen, vk_api_xml],
  output : ['kk_entrypoints.h', 'kk_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--proto', '--weak',
    '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@', '--prefix', 'kk',
    '--beta', with_vulkan_beta.to_string()
  ],
  depend_files : vk_entrypoints_gen_depend_files,
)

relative_dir = fs.relative_to(meson.global_source_root(), meson.global_build_root())

kkcl_spv = custom_target(
  input : kkcl_files,
  output : 'kkcl.spv',
  command : [
    prog_mesa_clc, '-o', '@OUTPUT@', '--depfile', '@DEPFILE@', kkcl_files, '--',
    '-I' + join_paths(meson.project_source_root(), 'src/compiler/libcl'),
    '-I' + join_paths(meson.current_source_dir(), '.'),
    '-I' + join_paths(meson.project_source_root(), 'src'),
    cl_args,
    ],
  depfile : 'libkk_shaders.h.d',
)

kkcl = custom_target(
  input : kkcl_spv,
  output : ['kkcl.cpp', 'kkcl.h'],
  command : [prog_vtn_bindgen2, '@INPUT@', '@OUTPUT@'],
)

kk_deps = [
  idep_nir,
  idep_mesautil,
  idep_vulkan_runtime,
  idep_vulkan_util,
  idep_vulkan_wsi,
  idep_vulkan_wsi_headers,
  kk_device_dispatch_table_dependencies,
  idep_kk_device_dispatch_table,
  idep_msl_to_nir,
  idep_mtl_bridge
]

libkk = static_library(
  'kk',
  [
    kk_files,
    kk_entrypoints,
    kkcl,
    sha1_h,
  ],
  include_directories : [
    inc_include,
    inc_src,
  ],
  dependencies : [kk_deps],
  gnu_symbol_visibility : 'hidden',
)

# Happens with macOS ld linker:
# "-Wl,-undefined,dynamic_lookup" required, otherwise, linker will complain about undefined symbols
libvulkan_kosmickrisp = shared_library(
  'vulkan_kosmickrisp',
  link_whole: [libkk],
  link_args: [ld_args_build_id, '-Wl,-undefined,dynamic_lookup'],
  gnu_symbol_visibility : 'hidden',
  install : true,
)

icd_file_name = libname_prefix + 'vulkan_kosmickrisp.' + libname_suffix

kosmickrisp_mesa_icd = custom_target(
  'kosmickrisp_mesa_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : 'kosmickrisp_mesa_icd.' + vulkan_manifest_suffix,
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.3', '--xml', '@INPUT1@',
    '--icd-lib-path', vulkan_icd_lib_path,
    '--icd-filename', icd_file_name,
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
  install_dir : with_vulkan_icd_dir,
  install_tag : 'runtime',
  install : true,
)

kosmickrisp_icd = custom_target(
  'kosmickrisp_mesa_devenv_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : 'kosmickrisp_mesa_devenv_icd.@0@.json'.format(host_machine.cpu()),
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.3', '--xml', '@INPUT1@',
    '--icd-lib-path', meson.current_build_dir(),
    '--icd-filename', icd_file_name,
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
)

devenv.append('VK_DRIVER_FILES', kosmickrisp_icd.full_path())
# Deprecated: replaced by VK_DRIVER_FILES above
devenv.append('VK_ICD_FILENAMES', kosmickrisp_icd.full_path())
