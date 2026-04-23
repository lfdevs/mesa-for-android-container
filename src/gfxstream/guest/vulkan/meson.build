# Copyright 2022 Android Open Source Project
# SPDX-License-Identifier: MIT

if with_platform_android
    dep_gfxstream_stubs = null_dep
else
    subdir('stubs')
endif

gfxstream_deps += dep_gfxstream_stubs

files_lib_vulkan_gfxstream = files(
    'gfxstream_vk_cmd.cpp',
    'gfxstream_vk_device.cpp',
    'gfxstream_vk_wsi.cpp',
)

gfxstream_vk_wsi_args = []
if with_platform_x11
    gfxstream_vk_wsi_args += '-DGFXSTREAM_VK_X11'
endif

if with_platform_wayland
    gfxstream_vk_wsi_args += '-DGFXSTREAM_VK_WAYLAND'
endif

files_vulkan_gfxstream = files_lib_vulkan_gfxstream + files_lib_vulkan_enc
files_vulkan_gfxstream += gfxstream_vk_entrypoints
files_vulkan_gfxstream += gfxstream_guest_vk_autogen
files_vulkan_gfxstream += vulkan_gfxstream_h

if with_platform_android or with_gfxstream_emulated_android
    files_vulkan_gfxstream += files_lib_vulkan_enc_android
endif

lib_vulkan_gfxstream = shared_library(
    'vulkan_gfxstream',
    files_vulkan_gfxstream,
    cpp_args: gfxstream_guest_args + gfxstream_vk_wsi_args,
    include_directories: [
        inc_vulkan_headers,
        inc_guest_iostream,
        inc_vulkan_enc,
        inc_goldfish_address_space,
        inc_include,
        inc_src,
        inc_aemu,
        inc_platform_virtgpu,
        inc_connection_manager,
    ],
    link_with: [
        libvulkan_wsi,
        libplatform_virtgpu,
        libconnection_manager,
        libaemu,
    ],
    link_args: [vulkan_icd_link_args, ld_args_bsymbolic, ld_args_gc_sections],
    link_depends: vulkan_icd_link_depends,
    dependencies: gfxstream_deps,
    install: true,
)

icd_file_name = libname_prefix + 'vulkan_gfxstream.' + libname_suffix

gfxstream_icd = custom_target(
    'gfxstream_vk_icd',
    input: [vk_icd_gen, vk_api_xml],
    output: 'gfxstream_vk_icd.' + vulkan_manifest_suffix,
    command: [
        prog_python,
        '@INPUT0@',
        '--api-version',
        '1.1',
        '--xml',
        '@INPUT1@',
        '--sizeof-pointer',
        sizeof_pointer,
        '--icd-lib-path',
        vulkan_icd_lib_path,
        '--icd-filename',
        icd_file_name,
        '--out',
        '@OUTPUT@',
    ],
    build_by_default: true,
    install_dir: with_vulkan_icd_dir,
    install: true,
)

_dev_icdname = 'gfxstream_vk_devenv_icd.@0@.json'.format(host_machine.cpu())
_dev_icd = custom_target(
    'gfxstream_vk_devenv_icd',
    input: [vk_icd_gen, vk_api_xml],
    output: _dev_icdname,
    command: [
        prog_python,
        '@INPUT0@',
        '--api-version',
        '1.3',
        '--xml',
        '@INPUT1@',
        '--sizeof-pointer',
        sizeof_pointer,
        '--icd-lib-path',
        meson.current_build_dir(),
        '--icd-filename',
        icd_file_name,
        '--out',
        '@OUTPUT@',
    ],
    build_by_default: true,
)

devenv.append('VK_DRIVER_FILES', _dev_icd.full_path())
