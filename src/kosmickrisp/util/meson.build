# Copyright © 2025 LunarG, Inc
# SPDX-License-Identifier: MIT

kk_dispatch_trampolines_gen = files('kk_dispatch_trampolines_gen.py')

kk_dispatch_trampolines = custom_target(
  'kk_dispatch_trampolines',
  input : [kk_dispatch_trampolines_gen, vk_api_xml],
  output : ['kk_dispatch_trampolines.c', 'kk_dispatch_trampolines.h'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--out-c', '@OUTPUT0@', '--out-h', '@OUTPUT1@',
    '--beta', with_vulkan_beta.to_string()
  ],
  depend_files : vk_dispatch_trampolines_gen_depend_files,
)
