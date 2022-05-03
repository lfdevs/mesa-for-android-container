# Copyright © Microsoft Corporation

# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

dzn_entrypoints = custom_target(
  'dzn_entrypoints',
  input : [vk_entrypoints_gen, vk_api_xml],
  output : ['dzn_entrypoints.h', 'dzn_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--proto', '--weak',
    '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@', '--prefix', 'dzn'
  ],
  depend_files : vk_entrypoints_gen_depend_files,
)

libdzn_files = files(
  'dzn_cmd_buffer.cpp',
  'dzn_descriptor_set.cpp',
  'dzn_device.cpp',
  'dzn_image.cpp',
  'dzn_meta.cpp',
  'dzn_nir.c',
  'dzn_pass.cpp',
  'dzn_pipeline_cache.cpp',
  'dzn_pipeline.cpp',
  'dzn_query.cpp',
  'dzn_sync.cpp',
  'dzn_util.cpp',
  'dzn_util.c',
  'dzn_wsi.cpp',
)

dzn_deps = [
  idep_libdxil_compiler,
  idep_libspirv_to_dxil,
  idep_nir,
  idep_nir_headers,
  idep_vulkan_util,
  idep_vulkan_runtime,
  idep_vulkan_wsi,
  dep_dxheaders,
]

dzn_flags = [ ]

if with_platform_windows
  dzn_flags += '-DVK_USE_PLATFORM_WIN32_KHR'
endif

if meson.version().version_compare('>= 0.60')
  cpp_std = 'cpp_std=c++20'
elif cpp.get_id() == 'msvc'
  cpp_std = 'cpp_std=c++latest'
else
  cpp_std = 'cpp_std=c++2a'
endif

libvulkan_dzn = shared_library(
  'vulkan_dzn',
  [libdzn_files, dzn_entrypoints, sha1_h],
  vs_module_defs : 'vulkan_dzn.def',
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux,
    inc_compiler, inc_util
  ],
  dependencies : [dzn_deps, idep_vulkan_wsi],
  c_args : dzn_flags,
  cpp_args : dzn_flags,
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_bsymbolic, ld_args_gc_sections],
  name_prefix : host_machine.system() == 'windows' ? '' : 'lib',
  install : true,
  override_options: [cpp_std]
)

icd_file_name = 'libvulkan_dzn.so'
module_dir = join_paths(get_option('prefix'), get_option('libdir'))
if with_platform_windows
  module_dir = join_paths(get_option('prefix'), get_option('bindir'))
  icd_file_name = 'vulkan_dzn.dll'
endif

dzn_icd = custom_target(
  'dzn_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : 'dzn_icd.@0@.json'.format(host_machine.cpu()),
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.2', '--xml', '@INPUT1@',
    '--lib-path', join_paths(module_dir, icd_file_name),
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
  install_dir : with_vulkan_icd_dir,
  install : true,
)

if meson.version().version_compare('>= 0.58')
  _dev_icdname = 'dzn_devenv_icd.@0@.json'.format(host_machine.cpu())
  custom_target(
    'dzn_devenv_icd',
    input : [vk_icd_gen, vk_api_xml],
    output : _dev_icdname,
    command : [
      prog_python, '@INPUT0@',
      '--api-version', '1.1', '--xml', '@INPUT1@',
      '--lib-path', meson.current_build_dir() / icd_file_name,
      '--out', '@OUTPUT@',
    ],
    build_by_default : true,
  )

  devenv.append('VK_ICD_FILENAMES', meson.current_build_dir() / _dev_icdname)
endif
