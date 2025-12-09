# Copyright © 2022 Collabora Ltd. and Red Hat Inc.
# SPDX-License-Identifier: MIT
nvk_files = files(
  'nvk_acceleration_structure.c',
  'nvk_buffer.c',
  'nvk_buffer.h',
  'nvk_buffer_view.c',
  'nvk_buffer_view.h',
  'nvk_cmd_buffer.c',
  'nvk_cmd_buffer.h',
  'nvk_cmd_clear.c',
  'nvk_cmd_copy.c',
  'nvk_cmd_dispatch.c',
  'nvk_cmd_draw.c',
  'nvk_cmd_indirect.c',
  'nvk_cmd_meta.c',
  'nvk_cmd_pool.c',
  'nvk_cmd_pool.h',
  'nvk_debug.h',
  'nvk_descriptor_set.h',
  'nvk_descriptor_set.c',
  'nvk_descriptor_set_layout.c',
  'nvk_descriptor_set_layout.h',
  'nvk_descriptor_table.c',
  'nvk_descriptor_table.h',
  'nvk_descriptor_types.h',
  'nvk_device.c',
  'nvk_device.h',
  'nvk_device_memory.c',
  'nvk_device_memory.h',
  'nvk_edb_bview_cache.c',
  'nvk_edb_bview_cache.h',
  'nvk_event.c',
  'nvk_event.h',
  'nvk_format.c',
  'nvk_format.h',
  'nvk_heap.c',
  'nvk_heap.h',
  'nvk_host_copy.c',
  'nvk_image.c',
  'nvk_image.h',
  'nvk_image_view.c',
  'nvk_image_view.h',
  'nvk_indirect_execution_set.c',
  'nvk_indirect_execution_set.h',
  'nvk_instance.c',
  'nvk_instance.h',
  'nvk_mem_arena.c',
  'nvk_mem_arena.h',
  'nvk_mem_stream.c',
  'nvk_mem_stream.h',
  'nvk_mme.c',
  'nvk_mme.h',
  'nvk_nir_lower_descriptors.c',
  'nvk_physical_device.c',
  'nvk_physical_device.h',
  'nvk_private.h',
  'nvk_query_pool.c',
  'nvk_query_pool.h',
  'nvk_queue.c',
  'nvk_queue.h',
  'nvk_sampler.c',
  'nvk_sampler.h',
  'nvk_shader.c',
  'nvk_shader.h',
  'nvk_upload_queue.c',
  'nvk_upload_queue.h',
  'nvk_wsi.c',
  'nvk_wsi.h',
  'nvkmd/nouveau/nvkmd_nouveau.h',
  'nvkmd/nouveau/nvkmd_nouveau_ctx.c',
  'nvkmd/nouveau/nvkmd_nouveau_dev.c',
  'nvkmd/nouveau/nvkmd_nouveau_mem.c',
  'nvkmd/nouveau/nvkmd_nouveau_pdev.c',
  'nvkmd/nouveau/nvkmd_nouveau_va.c',
  'nvkmd/nvkmd.c',
  'nvkmd/nvkmd.h',
)

nvkcl_files = files(
  'cl/nvk_query.cl',
)

nvk_entrypoints = custom_target(
  'nvk_entrypoints',
  input : [vk_entrypoints_gen, vk_api_xml],
  output : ['nvk_entrypoints.h', 'nvk_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--proto', '--weak',
    '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@', '--prefix', 'nvk',
    '--beta', with_vulkan_beta.to_string(),
  ],
  depend_files : vk_entrypoints_gen_depend_files,
)

nvkcl_spv = custom_target(
  input : nvkcl_files,
  output : 'nvkcl.spv',
  command : [
    prog_mesa_clc, '-o', '@OUTPUT@', '--depfile', '@DEPFILE@', nvkcl_files, '--',
    '-I' + join_paths(meson.project_source_root(), 'src/compiler/libcl'),
    '-I' + join_paths(meson.current_source_dir(), '.'),
    '-I' + join_paths(meson.project_source_root(), 'src'),
    cl_args,
    ],
  depfile : '@PLAINNAME@.d',
)

nvkcl = custom_target(
  input : nvkcl_spv,
  output : ['nvkcl.cpp', 'nvkcl.h'],
  command : [prog_vtn_bindgen2, '@INPUT@', '@OUTPUT@'],
)

nvk_deps = [
  dep_libdrm,
  idep_nak,
  idep_nil,
  idep_nir,
  idep_mesautil,
  idep_nouveau_mme,
  idep_nouveau_ws,
  idep_nvidia_headers,
  idep_vulkan_runtime,
  idep_vulkan_util,
  idep_vulkan_wsi,
  idep_vulkan_wsi_headers,
]

nvk_flags = []

if with_platform_android
  nvk_files += files('nvk_android.c')
endif

libnvk = static_library(
  'nvk',
  [
    nvk_entrypoints,
    cl_generated,
    nvk_files,
    nvkcl,
    sha1_h,
  ],
  include_directories : [
    inc_include,
    inc_src,
  ],
  dependencies : nvk_deps,
  c_args : [no_override_init_args, nvk_flags],
  gnu_symbol_visibility : 'hidden',
)

libvulkan_nouveau = shared_library(
  'vulkan_nouveau',
  link_whole : [libnvk],
  link_args: [ld_args_build_id, ld_args_bsymbolic, ld_args_gc_sections],
  gnu_symbol_visibility : 'hidden',
  install : true,
)

icd_file_name = libname_prefix + 'vulkan_nouveau.' + libname_suffix

nouveau_icd = custom_target(
  'nouveau_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : 'nouveau_icd.' + vulkan_manifest_suffix,
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.4', '--xml', '@INPUT1@',
    '--sizeof-pointer', sizeof_pointer,
    '--icd-lib-path', vulkan_icd_lib_path,
    '--icd-filename', icd_file_name,
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
  install_dir : with_vulkan_icd_dir,
  install_tag : 'runtime',
  install : true,
)

_dev_icdname = 'nouveau_devenv_icd.@0@.json'.format(host_machine.cpu())
custom_target(
  'nouveau_devenv_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : _dev_icdname,
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.4', '--xml', '@INPUT1@',
    '--sizeof-pointer', sizeof_pointer,
    '--icd-lib-path', meson.current_build_dir(),
    '--icd-filename', icd_file_name,
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
)

devenv.append('VK_DRIVER_FILES', meson.current_build_dir() / _dev_icdname)
# Deprecated: replaced by VK_DRIVER_FILES above
devenv.append('VK_ICD_FILENAMES', meson.current_build_dir() / _dev_icdname)

if with_tests and not with_platform_android
  test(
    'nvk_mme',
    executable(
      'test_nvk_mme',
      files('test_nvk_mme.c'),
      dependencies : [nvk_deps],
      link_with : [libnvk],
    )
  )
endif
