# Copyright © 2018 Rob Clark
# Copyright © 2019 Collabora
# SPDX-License-Identifier: MIT

inc_panfrost = include_directories([
   '.', 'shared', 'compiler', 'lib', 'libpan', 'model',
])

compile_args_panfrost = [
  '-Wno-pointer-arith'
]

if with_panfrost_rust
  panfrost_rust_args = [
    rust_global_args,
    rust_2024_lint_args,
    '-Aclippy::identity_op',
    '-Aclippy::len_zero',
    '-Aclippy::manual_range_contains',
    # normally this is a good one, but we use it where the "better" code is
    # worse
    '-Aclippy::needless_range_loop',
    '-Aclippy::redundant_field_names',
    '-Aclippy::upper_case_acronyms',
    '-Aclippy::vec_box',
    '-Aclippy::write_with_newline',
    # warns about public function might dereference a raw pointer, but nothing
    # is actually public here
    '-Aclippy::not_unsafe_ptr_arg_deref',
    '-Anon_snake_case',
  ]
endif

subdir('shared')
subdir('model')

if with_gallium_panfrost or with_panfrost_vk or with_tools.contains('panfrost')
   subdir('genxml')
   subdir('lib')
endif

subdir('compiler')

if with_gallium_panfrost or with_panfrost_vk or with_tools.contains('panfrost')
  subdir('clc')
endif

if with_gallium_panfrost or with_panfrost_vk
   subdir('libpan')
   subdir('perf')
endif

files_bifrost = files(
  'compiler/bifrost/cmdline.c',
)

bifrost_compiler = executable(
  'bifrost_compiler',
  [files_bifrost],
  include_directories : [
    inc_include,
    inc_src,
    inc_panfrost,
 ],
  dependencies : [
    idep_nir,
    idep_mesautil,
    idep_bi_opcodes_h,
    dep_libdrm,
  ],
  link_with : [
    libpanfrost_compiler,
  ],
  build_by_default : with_tools.contains('panfrost')
)

if with_panfrost_vk
  subdir('vulkan')
endif

if with_perfetto and (with_datasources.contains('panfrost') or with_datasources.contains('auto'))
  subdir('ds')
endif

if with_tools.contains('drm-shim')
  subdir('drm-shim')
endif

if with_tools.contains('panfrost')
   subdir('tools')
endif
