# Copyright © 2022 Collabora Ltd. and Red Hat Inc.
# SPDX-License-Identifier: MIT

prog_cbindgen = find_program(
  'cbindgen',
  required : true,
  native : true,
  version : '>= 0.25'
)

dep_paste = dependency(
  'paste',
  version: '>= 1.0.14',
  fallback: ['paste', 'dep_paste'],
  required: true,
)

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
  'nvk_cmd_video.c',
  'nvk_cubin.c',
  'nvk_cubin.h',
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
  'nvk_nir_lower_mesh_shader.c',
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
  'nvk_video_session.c',
  'nvk_video_session.h',
  'nvk_wsi.c',
  'nvk_wsi.h',
  'layers/nvk_app_workarounds.c',
  'layers/nvk_app_workarounds.h',
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
  'cl/nvk_copy_indirect.cl',
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
  input : [nvkcl_files, libnvidia_headers],
  output : 'nvkcl.spv',
  command : [
    prog_mesa_clc, '-o', '@OUTPUT@', '--depfile', '@DEPFILE@', nvkcl_files, '--',
    '-I' + join_paths(meson.project_source_root(), 'src/compiler/libcl'),
    '-I' + join_paths(meson.current_source_dir(), '.'),
    '-I' + join_paths(meson.project_source_root(), 'src'),
    '-I' + join_paths(meson.project_source_root(), 'src/nouveau/headers'),
    '-I' + join_paths(meson.project_source_root(), 'src/nouveau/headers/nvidia/classes'),
    '-I' + join_paths(meson.project_build_root(), 'src/nouveau/headers'),
    cl_args,
    ],
  depfile : 'nvkcl.spv.d',
)

nvkcl = custom_target(
  input : nvkcl_spv,
  output : ['nvkcl.cpp', 'nvkcl.h'],
  command : [prog_vtn_bindgen2, '@INPUT@', '@OUTPUT@'],
)

nvk_drirc = custom_target(
  'nvk_drirc',
  input  : [files('nvk_drirc_gen.py'), drirc_gen, '00-nvk-defaults.conf'],
  output : ['nvk_drirc.c', 'nvk_drirc.h'],
  command : [
    prog_python, '@INPUT0@',
    '--import-path', join_paths(dir_source_root, 'src/util'),
    '--drirc-src', '@OUTPUT0@',
    '--drirc-hdr', '@OUTPUT1@',
    '--validate', '@INPUT2@',
  ],
)

nvk_deps = [
  dep_libdrm,
  dep_elf,
  idep_nak,
  idep_nil,
  idep_nir,
  idep_nir_headers,
  idep_mesautil,
  idep_nouveau_cubin,
  idep_nouveau_mme,
  idep_nouveau_ws,
  idep_nvidia_headers,
  idep_vulkan_runtime,
  idep_vulkan_runtime_headers,
  idep_vulkan_util,
  idep_vulkan_util_headers,
  idep_vulkan_wsi,
  idep_vulkan_wsi_headers,
]

nvk_include_directories = [
  inc_include,
  inc_src,
]

nvk_bindings_rs_gen = rust.bindgen(
  input: ['rust/bindings.h', nvk_drirc[1]],
  output: 'nvk_bindings.rs',
  output_inline_wrapper : 'nvk_inline_wrappers.c',
  c_args: [
    pre_args,
  ],
  args: [
    bindgen_output_args,
    '--no-prepend-enum-name',
    '--with-derive-default',
    '--allowlist-type', 'Std.*|vk_video.*|.*nv.*|Vk.*|vk.*',
    '--allowlist-function', '.*vk_find.*|.*vk_video.*|nvk.*',
    '--allowlist-function', 'vk_buffer_address',
    '--allowlist-var', '.*SUBC.*',
  ],
  include_directories : include_directories('.'),
  dependencies : nvk_deps,
)

libnvk_bindings_rs_extern = static_library(
  'nvk_bindings_rs_extern',
  nvk_bindings_rs_gen[1],
  c_args : [
    no_override_init_args,
    '-Wno-missing-prototypes',
  ],
  include_directories : nvk_include_directories,
  dependencies : nvk_deps,
)

libnvk_bindings_rs = static_library(
  'nvk_bindings_rs',
  nvk_bindings_rs_gen[0],
  link_with : [libnvk_bindings_rs_extern],
  gnu_symbol_visibility: 'hidden',
  rust_abi: 'rust',
)

libnvk_rs = static_library(
  'nvk_rs',
  files('rust/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  rust_abi: 'c',
  rust_args: nouveau_rust_args,
  link_with: [
    libnvk_bindings_rs,
  ],
  dependencies: [
    dep_paste,
    dep_rustc_hash,
    idep_nv_push_rs,
    idep_nvidia_headers_rs,
  ],
)

# TODO: Linking Rust executables (such as unit tests) doesn't play nicely
# with the sanitizers because meson doesn't know to pass -fsanitize to the
# Rust linker.  See also https://github.com/mesonbuild/meson/issues/11741
if with_tests and get_option('b_sanitize') == 'none' and not with_platform_android
  rust.test(
    'nvk_rs',
    libnvk_rs,
    suite : ['nouveau'],
    # This is needed to ensure we link against glibc
    # See also https://gitlab.freedesktop.org/mesa/mesa/-/issues/11632
    rust_args: ['-C', 'default-linker-libraries'],
    link_with: [
      _libacorn_rs,
    ],
  )
endif

nvk_rust_h = custom_target(
  'nvk_rust.h',
  input : [files('rust/cbindgen.toml'), 'rust/lib.rs'],
  output : ['nvk_rust.h'],
  command : [
    prog_cbindgen, '-q', '--config', '@INPUT0@', '--lang', 'c',
    '--output', '@OUTPUT0@', '--depfile', '@DEPFILE@',
    '--', '@INPUT1@',
  ],
  depfile : 'nvk_rust.h.d',
)

nvk_flags = []

nvk_build_id = get_option('nvk-build-id')
if nvk_build_id != ''
  nvk_flags += '-DNVK_BUILD_ID_OVERRIDE="' + nvk_build_id + '"'
endif

if with_platform_android
  nvk_files += files('nvk_android.c')
endif

libnvk = static_library(
  'nvk',
  [
    nvk_entrypoints,
    nvk_drirc,
    cl_generated,
    nvk_files,
    nvk_rust_h,
    nvkcl,
    sha1_h,
  ],
  include_directories : nvk_include_directories,
  dependencies : nvk_deps,
  link_with : [libnvk_rs],
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
_dev_icd = custom_target(
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

devenv.append('VK_DRIVER_FILES', _dev_icd.full_path())

if with_tests and not with_platform_android
  test(
    'nvk_mme',
    executable(
      'test_nvk_mme',
      files('test_nvk_mme.c'),
      dependencies : [nvk_deps],
      link_with : [libnvk],
    ),
    suite : ['nouveau'],
  )
endif
