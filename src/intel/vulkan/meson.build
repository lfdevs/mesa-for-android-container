# Copyright © 2017-2019 Intel Corporation

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

anv_extensions_py = files('anv_extensions.py')

anv_entrypoints = custom_target(
  'anv_entrypoints.[ch]',
  input : ['anv_entrypoints_gen.py', vk_api_xml],
  output : ['anv_entrypoints.h', 'anv_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--outdir', meson.current_build_dir(),
  ],
  depend_files : anv_extensions_py,
)

anv_extensions_c = custom_target(
  'anv_extensions.c',
  input : ['anv_extensions_gen.py', vk_api_xml],
  output : 'anv_extensions.c',
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--out-c', '@OUTPUT@',
  ],
  depend_files : anv_extensions_py,
)

anv_extensions_h = custom_target(
  'anv_extensions.h',
  input : ['anv_extensions_gen.py', vk_api_xml],
  output : 'anv_extensions.h',
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--out-h', '@OUTPUT@',
  ],
  depend_files : anv_extensions_py,
)

intel_icd = custom_target(
  'intel_icd',
  input : 'anv_icd.py',
  output : 'intel_icd.@0@.json'.format(host_machine.cpu()),
  command : [
    prog_python, '@INPUT@',
    '--lib-path', join_paths(get_option('prefix'), get_option('libdir')),
    '--out', '@OUTPUT@',
  ],
  depend_files : anv_extensions_py,
  build_by_default : true,
  install_dir : with_vulkan_icd_dir,
  install : true,
)

libanv_gen_libs = []
anv_gen_files = files(
  'genX_blorp_exec.c',
  'genX_cmd_buffer.c',
  'genX_gpu_memcpy.c',
  'genX_pipeline.c',
  'genX_query.c',
  'genX_state.c',
)
foreach g : [['70', ['gen7_cmd_buffer.c']], ['75', ['gen7_cmd_buffer.c']],
             ['80', ['gen8_cmd_buffer.c']], ['90', ['gen8_cmd_buffer.c']],
             ['110', ['gen8_cmd_buffer.c']], ['120', ['gen8_cmd_buffer.c']]]
  _gen = g[0]
  libanv_gen_libs += static_library(
    'anv_gen@0@'.format(_gen),
    [anv_gen_files, g[1], anv_entrypoints[0], anv_extensions_h],
    include_directories : [
      inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_compiler, inc_intel, inc_vulkan_wsi,
    ],
    c_args : [
      no_override_init_args, c_sse2_args,
      '-DGEN_VERSIONx10=@0@'.format(_gen),
    ],
    gnu_symbol_visibility : 'hidden',
    dependencies : [
      dep_libdrm, dep_valgrind, idep_nir_headers, idep_genxml, idep_vulkan_util_headers,
    ],
  )
endforeach

libanv_files = files(
  'anv_allocator.c',
  'anv_android.h',
  'anv_batch_chain.c',
  'anv_blorp.c',
  'anv_cmd_buffer.c',
  'anv_descriptor_set.c',
  'anv_device.c',
  'anv_dump.c',
  'anv_formats.c',
  'anv_genX.h',
  'anv_image.c',
  'anv_intel.c',
  'anv_nir.h',
  'anv_nir_add_base_work_group_id.c',
  'anv_nir_apply_pipeline_layout.c',
  'anv_nir_compute_push_layout.c',
  'anv_nir_lower_multiview.c',
  'anv_nir_lower_ycbcr_textures.c',
  'anv_pass.c',
  'anv_perf.c',
  'anv_pipeline.c',
  'anv_pipeline_cache.c',
  'anv_private.h',
  'anv_queue.c',
  'anv_util.c',
  'anv_wsi.c',
  'vk_format_info.h',
)

anv_deps = [
  dep_libdrm,
  dep_valgrind,
  idep_nir_headers,
  idep_vulkan_util_headers,
]
anv_flags = [
  no_override_init_args,
  c_sse2_args,
]

if with_platform_x11
  anv_deps += dep_xcb_dri3
  anv_flags += [
    '-DVK_USE_PLATFORM_XCB_KHR',
    '-DVK_USE_PLATFORM_XLIB_KHR',
  ]
  libanv_files += files('anv_wsi_x11.c')
endif

if with_platform_wayland
  anv_deps += dep_wayland_client
  anv_flags += '-DVK_USE_PLATFORM_WAYLAND_KHR'
  libanv_files += files('anv_wsi_wayland.c')
endif

if system_has_kms_drm and not with_platform_android
  anv_flags += '-DVK_USE_PLATFORM_DISPLAY_KHR'
  libanv_files += files('anv_wsi_display.c')
endif

if with_xlib_lease
  anv_deps += [dep_xcb_xrandr, dep_xlib_xrandr]
  anv_flags += '-DVK_USE_PLATFORM_XLIB_XRANDR_EXT'
endif

if with_platform_android
  anv_flags += '-DVK_USE_PLATFORM_ANDROID_KHR'
  libanv_files += files('anv_android.c')
else
  libanv_files += files('anv_android_stubs.c')
endif

libanv_common = static_library(
  'anv_common',
  [
    libanv_files, anv_entrypoints, anv_extensions_c, anv_extensions_h, sha1_h,
    gen_xml_pack,
  ],
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_intel, inc_compiler,
    inc_vulkan_wsi, inc_util,
  ],
  c_args : anv_flags,
  gnu_symbol_visibility : 'hidden',
  dependencies : anv_deps,
)

libvulkan_intel = shared_library(
  'vulkan_intel',
  [files('anv_gem.c'), anv_entrypoints[0], anv_extensions_h],
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_intel, inc_compiler, inc_vulkan_wsi,
  ],
  link_whole : [libanv_common, libanv_gen_libs],
  link_with : [
    libintel_compiler, libintel_dev, libisl, libblorp, libvulkan_wsi,
    libintel_perf,
  ],
  dependencies : [
    dep_thread, dep_dl, dep_m, anv_deps, idep_libintel_common,
    idep_nir, idep_genxml, idep_vulkan_util, idep_mesautil, idep_xmlconfig,
  ],
  c_args : anv_flags,
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_build_id, ld_args_bsymbolic, ld_args_gc_sections],
  install : true,
)

if with_symbols_check
  if get_option('buildtype') == 'debug'  # Same rule as `-DDEBUG` in /meson.build
    _extra_args = [
      '--ignore-symbol', 'anv_dump_start',
      '--ignore-symbol', 'anv_dump_finish',
    ]
  else
    _extra_args = []
  endif

  test(
    'anv symbols check',
    symbols_check,
    args : [
      '--lib', libvulkan_intel,
      '--symbols-file', vulkan_icd_symbols,
      _extra_args,
      symbols_check_args,
    ],
    suite : ['intel'],
  )
endif

if with_tests
  libvulkan_intel_test = static_library(
    'vulkan_intel_test',
    [files('anv_gem_stubs.c'), anv_entrypoints[0], anv_extensions_h],
    include_directories : [
      inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_intel, inc_compiler, inc_vulkan_wsi,
    ],
    link_whole : libanv_common,
    link_with : [
      libanv_gen_libs, libintel_compiler, libintel_common, libintel_dev,
      libisl, libblorp, libvulkan_wsi, libintel_perf,
    ],
    dependencies : [
      dep_thread, dep_dl, dep_m, anv_deps,
      idep_nir, idep_vulkan_util, idep_mesautil,
    ],
    c_args : anv_flags,
    gnu_symbol_visibility : 'hidden',
  )

  foreach t : ['block_pool_no_free', 'block_pool_grow_first',
               'state_pool_no_free', 'state_pool_free_list_only',
               'state_pool', 'state_pool_padding']
    test(
      'anv_@0@'.format(t),
      executable(
        t,
        ['tests/@0@.c'.format(t), anv_entrypoints[0], anv_extensions_h],
        c_args : [ c_sse2_args ],
        link_with : libvulkan_intel_test,
        dependencies : [dep_libdrm, dep_thread, dep_m, dep_valgrind, idep_vulkan_util, ],
        include_directories : [
          inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_intel, inc_compiler, inc_vulkan_wsi,
        ],
      ),
      suite : ['intel'],
    )
  endforeach
endif
