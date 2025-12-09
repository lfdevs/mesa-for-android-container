# Copyright © Microsoft Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

dxcore_lib = cpp.find_library('dxcore')
rc_compiler = find_program('rc')

mediafoundation_link_args = ['runtimeobject.lib','mfplat.lib','mfuuid.lib','d3d12.lib','dxgi.lib']
mediafoundation_link_depends = []

if with_ld_version_script
  mediafoundation_link_args += ['-Wl,--version-script', join_paths(meson.current_source_dir(), 'mediafoundation.sym')]
  mediafoundation_link_depends += files('mediafoundation.sym')
endif

link_with_libmediafoundation_gallium = [
  libgalliumvlwinsys, libgallium,
  libpipe_loader_static, libws_null, libwsw, libwsgdi
]

mediafoundation_def_input = configure_file(
  configuration: {
    'MFInit': 'MFInit' + '@4',
    'MFDestroy': 'MFDestroy' + '@4'
  },
  input: 'mediafoundation.def.in',
  output: 'mediafoundation.def.in',
)

mediafoundation_def = custom_target(
  'mediafoundation.def',
  input: mediafoundation_def_input,
  output : 'mediafoundation.def',
  command : gen_vs_module_defs_normal_command,
)

winmod = import('windows')
foreach codec : mft_codecs
  gallium_mf_name = 'ms' + codec + 'hmft'
  if get_option('mediafoundation-store-dll')
    gallium_mf_name += '_store'
  endif

  major_version = '1'
  minor_version = '0'
  patch_version = '8'
  build_version = '0'
  mft_dll_version = '@0@.@1@.@2@'.format(major_version, minor_version, patch_version)

  mediafoundation_rc_conf_data = configuration_data()
  mediafoundation_rc_conf_data.set('VER_MAJOR', major_version)
  mediafoundation_rc_conf_data.set('VER_MINOR', minor_version)
  mediafoundation_rc_conf_data.set('VER_PATCH', patch_version)
  mediafoundation_rc_conf_data.set('VER_BUILD', build_version)

  mediafoundation_rc_conf_data.set('CompanyName',       '"Microsoft Corporation"')
  mediafoundation_rc_conf_data.set('FileDescription',   '"Media Foundation DX12 Encoder MFT"')
  mediafoundation_rc_conf_data.set('FileVersion',       '@0@.@1@.@2@.@3@'.format(major_version, minor_version, patch_version, build_version))
  mediafoundation_rc_conf_data.set('InternalName',      '"@0@"'.format(gallium_mf_name))
  mediafoundation_rc_conf_data.set('OriginalFilename',  '"@0@"'.format(gallium_mf_name))
  mediafoundation_rc_conf_data.set('ProductName',       '"Media Foundation Encoder MFT"')
  mediafoundation_rc_conf_data.set('ProductVersion',    '@0@.@1@.@2@.@3@'.format(major_version, minor_version, patch_version, build_version))

  rc_file = configure_file(
    configuration: mediafoundation_rc_conf_data,
    input: 'mediafoundation.rc.in',
    output: 'mediafoundation_@0@.rc'.format(codec),
  )
  rc_target = winmod.compile_resources(rc_file)
  
  libmediafoundation_gallium = shared_library(
    gallium_mf_name,
    'target.c',
    'dllmain.cpp',
    link_args : [mediafoundation_link_args, ld_args_gc_sections, ld_args_build_id],
    vs_module_defs : 'dx12enchmft.def', #mediafoundation_def,
    cpp_args : mf_codecs_cpp_args[codec],
    override_options: ['cpp_std=c++20'],
    include_directories : [
      inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_util, inc_gallium_winsys, inc_gallium_drivers, inc_gallium_winsys_sw
    ],
    link_whole : [mediafoundation_st[codec]],
    link_with : link_with_libmediafoundation_gallium,
    dependencies : [driver_d3d12, idep_mesautil, dxcore_lib, dep_dxheaders],
    soversion : host_machine.system() == 'windows' ? '' : mft_dll_version,
    version : mft_dll_version,
    link_depends : mediafoundation_link_depends,
    install : true,
    name_suffix : 'dll',
    name_prefix : '',  # otherwise mingw will create libXXX.dll
    objects: [rc_target]
  )

  if with_gallium_mediafoundation_test
    gallium_mf_test_name = gallium_mf_name + '_test'
    test(
      gallium_mf_test_name,
      executable(
        gallium_mf_test_name,
        link_args : [mediafoundation_link_args, ld_args_gc_sections, ld_args_build_id, 'strmiids.lib'],
        link_whole: [mediafoundation_st[codec], mediafoundation_test_st[codec]],
        link_with : link_with_libmediafoundation_gallium,
        dependencies : [idep_gtest, driver_d3d12, idep_mesautil, dxcore_lib, dep_dxheaders],
      ),
      protocol: 'gtest'
    )
  endif
endforeach