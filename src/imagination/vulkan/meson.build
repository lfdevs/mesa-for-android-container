# Copyright © 2022 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

subdir('pds')

pvr_entrypoints = custom_target(
  'pvr_entrypoints',
  input : [vk_entrypoints_gen, vk_api_xml],
  output : ['pvr_entrypoints.h', 'pvr_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--proto', '--weak',
    '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@', '--prefix', 'pvr',
    '--device-prefix', 'pvr_rogue',
    '--beta', with_vulkan_beta.to_string()
  ],
  depend_files : vk_entrypoints_gen_depend_files,
)

pvr_drirc = custom_target(
  'pvr_drirc',
  input  : [files('pvr_drirc_gen.py'), drirc_gen, '00-pvr-defaults.conf'],
  output : ['pvr_drirc.c', 'pvr_drirc.h'],
  command : [
    prog_python, '@INPUT0@',
    '--import-path', join_paths(dir_source_root, 'src/util'),
    '--drirc-src', '@OUTPUT0@',
    '--drirc-hdr', '@OUTPUT1@',
    '--validate', '@INPUT2@',
  ],
)

pvr_files = files(
  'winsys/powervr/pvr_drm.c',
  'winsys/powervr/pvr_drm_bo.c',
  'winsys/powervr/pvr_drm_job_compute.c',
  'winsys/powervr/pvr_drm_job_null.c',
  'winsys/powervr/pvr_drm_job_render.c',
  'winsys/powervr/pvr_drm_job_transfer.c',
  'winsys/pvr_winsys.c',
  'winsys/pvr_winsys_helper.c',
  'pvr_bo.c',
  'pvr_csb.c',
  'pvr_descriptor_set.c',
  'pvr_device.c',
  'pvr_dump_bo.c',
  'pvr_free_list.c',
  'pvr_formats.c',
  'pvr_image.c',
  'pvr_instance.c',
  'pvr_nir_lower_ycbcr.c',
  'pvr_physical_device.c',
  'pvr_transfer_frag_store.c',
  'pvr_query.c',
  'pvr_robustness.c',
  'pvr_rt_dataset.c',
  'pvr_spm.c',
  'pvr_usc.c',
  'pvr_wsi.c',
)

pvr_includes = [
  include_directories('winsys'),
  libpowervr_pds_includes,
]

pvr_deps = [
  dep_csbgen,
  dep_libdrm,
  dep_valgrind,
  idep_mesautil,
  idep_pco_uscgen_programs,
  idep_vulkan_runtime,
  idep_vulkan_util,
  idep_vulkan_wsi,
]

pvr_flags = [
  imagination_c_args,
  no_override_init_args,
]

common_per_arch_files = [
  pvr_entrypoints[0],
  pvr_drirc[1],
  sha1_h,
]

common_per_arch_files += files(
  'pvr_arch_border.c',
  'pvr_arch_cmd_buffer.c',
  'pvr_arch_cmd_query.c',
  'pvr_arch_csb.c',
  'pvr_arch_descriptor_set.c',
  'pvr_arch_device.c',
  'pvr_arch_formats.c',
  'pvr_arch_framebuffer.c',
  'pvr_arch_hw_pass.c',
  'pvr_arch_pass.c',
  'pvr_arch_pipeline.c',
  'pvr_arch_image.c',
  'pvr_arch_job_common.c',
  'pvr_arch_job_compute.c',
  'pvr_arch_job_context.c',
  'pvr_arch_job_render.c',
  'pvr_arch_job_transfer.c',
  'pvr_arch_mrt.c',
  'pvr_arch_queue.c',
  'pvr_arch_query_compute.c',
  'pvr_arch_sampler.c',
  'pvr_arch_spm.c',
  'pvr_arch_tex_state.c',
)

if with_imagination_srv
  pvr_files += files(
    'winsys/pvrsrvkm/pvr_srv.c',
    'winsys/pvrsrvkm/pvr_srv_bo.c',
    'winsys/pvrsrvkm/pvr_srv_bridge.c',
    'winsys/pvrsrvkm/pvr_srv_job_common.c',
    'winsys/pvrsrvkm/pvr_srv_job_compute.c',
    'winsys/pvrsrvkm/pvr_srv_job_null.c',
    'winsys/pvrsrvkm/pvr_srv_job_render.c',
    'winsys/pvrsrvkm/pvr_srv_job_transfer.c',
    'winsys/pvrsrvkm/pvr_srv_sync.c',
    'winsys/pvrsrvkm/pvr_srv_sync_prim.c',
  )
  common_per_arch_files += files(
    'winsys/pvrsrvkm/pvr_arch_srv_job_compute.c',
    'winsys/pvrsrvkm/pvr_arch_srv_job_render.c',
    'winsys/pvrsrvkm/pvr_arch_srv_job_transfer.c',
  )
  pvr_flags += '-DPVR_SUPPORT_SERVICES_DRIVER'
endif

rogue_files = files(
  'rogue/pvr_blit.c',
  'rogue/pvr_clear.c',
  'rogue/pvr_dump_csb.c',
)

powervr_per_arch_libs = []

foreach arch : ['rogue']
  per_arch_files = common_per_arch_files

  if arch == 'rogue'
    per_arch_files += rogue_files
  endif

  powervr_per_arch_libs += static_library(
    'powervr_@0@'.format(arch),
    per_arch_files,
    include_directories : [
      pvr_includes,
      inc_imagination,
      inc_include,
      inc_src,
    ],
    dependencies : [
      dep_valgrind,
      idep_nir_headers,
      idep_pco_uscgen_programs_headers,
      idep_vulkan_runtime_headers,
      idep_vulkan_util_headers,
      idep_vulkan_wsi_headers,
    ],
    c_args : [pvr_flags, '-DPVR_BUILD_ARCH_@0@'.format(arch.to_upper())],
    gnu_symbol_visibility : 'hidden',
  )
endforeach


libvulkan_powervr_mesa = shared_library(
  'vulkan_powervr_mesa',
  [pvr_files, pvr_entrypoints, pvr_drirc, u_format_pack_h],
  include_directories : [
    pvr_includes,
    inc_imagination,
    inc_include,
    inc_src,
  ],
  link_whole : [powervr_per_arch_libs],
  link_with : [
    libpowervr_common,
    libpowervr_compiler,
    libpowervr_pds,
    libpowervr_uscgen,
    libvulkan_wsi,
  ],
  dependencies : [
    pvr_deps,
    idep_nir_headers,
  ],
  c_args : pvr_flags,
  link_args : [
    vulkan_icd_link_args,
    ld_args_build_id,
    ld_args_bsymbolic,
    ld_args_gc_sections
  ],
  gnu_symbol_visibility : 'hidden',
  install : true,
)

if with_symbols_check
  test(
    'pvr symbols check',
    symbols_check,
    args : [
      '--lib', libvulkan_powervr_mesa,
      '--symbols-file', vulkan_icd_symbols,
      symbols_check_args,
    ],
    suite : ['imagination'],
  )
endif

icd_file_name = libname_prefix + 'vulkan_powervr_mesa.' + libname_suffix

powervr_mesa_icd = custom_target(
  'powervr_mesa_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : 'powervr_mesa_icd.' + vulkan_manifest_suffix,
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

_dev_icd = custom_target(
  'powervr_mesa_devenv_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : 'powervr_mesa_devenv_icd.@0@.json'.format(host_machine.cpu()),
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
